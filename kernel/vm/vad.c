#include "kdk/vm.h"
#include "vmp.h"

int vmp_vad_cmp(vm_vad_t *x, vm_vad_t *y);

RB_GENERATE(vm_vad_rbtree, vm_vad, rbtree_entry, vmp_vad_cmp);

int
vmp_vad_cmp(vm_vad_t *x, vm_vad_t *y)
{
	/*
	 * what this actually does is determine whether x's start address is
	 * lower than, greater than, or within the bounds of Y. it works because
	 * we allocate virtual address space with vmem, which already ensures
	 * there are no overlaps.
	 */

	if (x->start < y->start)
		return -1;
	else if (x->start >= y->end)
		return 1;
	else
		/* x->start is within VAD y */
		return 0;
}

vm_vad_t *
vmp_ps_vad_find(vmp_procstate_t *ps, vaddr_t vaddr)
{
	vm_vad_t key;
	key.start = vaddr;
	return RB_FIND(vm_vad_rbtree, &ps->vad_queue, &key);
}

int
vm_ps_allocate(vmp_procstate_t *vmps, vaddr_t *vaddrp, size_t size, bool exact)
{
	return vm_ps_map_section_view(vmps, NULL, vaddrp, size, 0, kVMAll,
	    kVMAll, false, false, exact);
}

int
vm_ps_map_section_view(vmp_procstate_t *vmps, void *section, vaddr_t *vaddrp,
    size_t size, off_t offset, vm_protection_t initial_protection,
    vm_protection_t max_protection, bool inherit_shared, bool cow, bool exact)
{
	int	      r;
	kwaitstatus_t w;
	vm_vad_t     *vad;
	vaddr_t	      addr = exact ? *vaddrp : 0;

	kassert(section == NULL);
	kassert(exact);

	ke_wait(&vmps->mutex, "map_section_view:vmps->mutex", false, false, -1);

	vad = kmem_alloc(sizeof(vm_vad_t));
	vad->start = (vaddr_t)addr;
	vad->end = addr + size;
	vad->flags.cow = cow;
	vad->flags.offset = offset;
	vad->flags.inherit_shared = inherit_shared;
	vad->flags.protection = initial_protection;
	vad->flags.max_protection = max_protection;
	vad->section = section;

	RB_INSERT(vm_vad_rbtree, &vmps->vad_queue, vad);

	ke_mutex_release(&vmps->mutex);

	*vaddrp = addr;

	return 0;
}

void
deallocate_page_callback(void *context, pte_t *saved_pte)
{
	vm_vad_t  *vad = context;
	vm_page_t *page = vmp_md_pte_page(saved_pte);

	/*
	 * note: we don't need to do a TLB shootdown here on a one-by-one
	 * basis because vmp_md_unmap_range_and_do operates with the PFNDB lock
	 * held and returns with it held. as such any pages we free are not able
	 * to be reused until we release that lock, and we keep it held for long
	 * enough to issue the TLB shootdown.
	 */

	if (vmp_md_pte_is_valid(saved_pte)) {
		switch (page->use) {
		case kPageUseAnonPrivate:
			vmp_page_delete_locked(page, NULL, true);
			break;
		default:
			kfatal("Can't handle this\n");
		}
	} else {
		switch (page->use) {
		case kPageUseAnonPrivate:
			vmp_page_delete_locked(page, NULL, false);
			break;
		default:
			kfatal("Can't handle this\n");
		}
	}
}

int
vm_ps_deallocate(vmp_procstate_t *vmps, vaddr_t start, size_t size)
{
	vm_vad_t     *entry, *tmp;
	vaddr_t	      end = start + size;
	kwaitstatus_t w;

	w = ke_wait(&vmps->mutex, "vm_ps_deallocate:vmps->mutex", false, false,
	    -1);
	kassert(w == kKernWaitStatusOK);

	RB_FOREACH_SAFE (entry, vm_vad_rbtree, &vmps->vad_queue, tmp) {
		// kprintf("Want 0x%zx@0x%zx, got 0x%zx@0x%zx", start, start +
		// size, entry->start, entry->end);
		if ((entry->start < start && entry->end <= start) ||
		    (entry->start >= end))
			continue;
		else if (entry->start >= start && entry->end <= end) {
			// int r;
			// ipl_t ipl;

			RB_REMOVE(vm_vad_rbtree, &vmps->vad_queue, entry);
			vmp_md_unmap_range_and_do(vmps, entry->start,
			    entry->end, deallocate_page_callback, NULL);

			kmem_free(entry, sizeof(vm_vad_t));
		} else if (entry->start >= start && entry->end <= end) {
			kfatal("unimplemented deallocate right of vadt\n");
		} else if (entry->start < start && entry->end < end) {
			kfatal("unimplemented other sort of deallocate\n");
		}
	}

	ke_mutex_release(&vmps->mutex);

	return 0;
}

int
vm_ps_dump_vadtree(vmp_procstate_t *vmps)
{
	vm_vad_t *vad;
	RB_FOREACH (vad, vm_vad_rbtree, &vmps->vad_queue) {
		printf("0x%zx-0x%zx\n", vad->start, vad->end);
	}
}

int
vm_ps_init(vmp_procstate_t *vmps)
{
	vmps->mutex = (kmutex_t)KMUTEX_INITIALISER;
	RB_INIT(&vmps->vad_queue);
	TAILQ_INIT(&vmps->ws_queue);
	RB_INIT(&vmps->ws_tree);
	vmp_md_ps_init(vmps);
}

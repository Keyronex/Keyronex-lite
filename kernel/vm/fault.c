#include "kdk/vm.h"
#include "vmp.h"

#if 0
int
vm_do_write_fault(vm_vad_t *vad, struct vmp_md_fault_state *state,
    vaddr_t vaddr, vm_page_t **out)
{
	/* either it's COW, or it's immediately possible to make it writeable */
	if (vad->flags.cow) {
		kfatal("implement cow\n");
	} else {
		/* ...AND MARK PAGE DIRTY */
		state->pte->hw.writeable = 1;
		if (out) {
			vm_page_t *page = vmp_md_pte_page(state->pte);
			*out = page;
			page->refcnt++;
		}
		return kVMFaultRetOK;
	}
}

int
vm_do_fault(struct vmp_md_fault_state *state, vaddr_t vaddr, bool write,
    bool *made_writeable, vm_page_t **out)
{
	vmp_procstate_t *vmps = SIM_vmps;
	vm_fault_return_t r;
	vm_vad_t *vad;
	ipl_t ipl;

	printf("vm_fault(0x%zx, %d)\n", vaddr, write);

	kassert(splget() < kIPLDPC);

	ke_wait(&vmps->mutex, "vm_fault:vmps->mutex", false, false, -1);
	vad = vmp_ps_vad_find(vmps, vaddr);

	if (!vad)
		kfatal("VM fault at 0x%zx doesn't have a vad\n", vaddr);

	/*
	 * a VAD exists. Check if it is nonwriteable and this is a write fault.
	 * If so, signal error.
	 */
	if (write && !(vad->flags.protection & kVMWrite))
		kfatal("Write fault at 0x%zx in nonwriteable vad\n", vaddr);

	ipl = vmp_acquire_pfn_lock();
	r = vmp_md_wire_pte(vaddr, state);
	switch (r) {
	case kVMFaultRetOK:
		break;

	default:
		kfatal("Handle %d return value from wire pte\n", r);
	}

	if (vmp_md_pte_is_valid(state->pte) &&
	    (!write || vmp_md_pte_is_writeable(state->pte))) {
		/* this fault must have already been handled */
		kfatal("fault arleady handled\n");
	} else if (vmp_md_pte_is_valid(state->pte)) {
		/* it must be valid but nonwriteable and this must be a write */
		kassert(write && !vmp_md_pte_is_writeable(state->pte));
		r = vm_do_write_fault(vad, state, vaddr, out);
		switch (r) {
		case kVMFaultRetOK:
			break;

		default:
			kfatal("Handle %d return value from "
			       "vmp_do_write_fault\n",
			    r);
		}
		*made_writeable = true;
	} else if (vmp_md_pte_is_trans(state->pte)) {
		vm_page_t *page = vmp_md_pte_page(state->pte);

		kassert(page != NULL);
		vmp_page_retain_locked(page);

		if (out != NULL) {
			page->reference_count++;
			*out = page;
		}

		vmp_md_pte_make_hw(state->pte, page->pfn, false);
		vmp_wsl_insert(vmps, vaddr);
	} else {
		vm_page_t *new_page;
		int r;

		/* it must be empty */
		kassert(vmp_md_pte_is_empty(state->pte));

		if (vad->section == NULL) {
			/* install demand-zeroed page */

			r = vmp_page_alloc_locked(&new_page,
			    kPageUseAnonPrivate, false);
			kassert(r == 0);

			if (out != NULL) {
				new_page->reference_count++;
				*out = new_page;
			}

			vmp_wsl_insert(vmps, vaddr);
			state->pte->hw.pfn = new_page->pfn;
			state->pte->hw.valid = 1;
			state->pte->hw.writeable = 0;
			state->bot_page->reference_count++;
			state->bot_page->used_ptes++;
		} else {
			kfatal("Section page\n");
		}
	}

	vmp_release_pfn_lock(ipl);
	ke_mutex_release(&vmps->mutex);

	// kfatal("Implement the logic...\n");

	return 0;
}
#endif

int
vmp_fault(vaddr_t vaddr, bool write, vm_page_t **out)
{
	struct vmp_md_fault_state state;
	bool made_writeable = false;

	kfatal("VM_FAULT at vaddr 0x%zx\n", vaddr);

#if 0

	memset(&state, 0x0, sizeof(state));

retry:
	made_writeable = false;
	switch (vm_do_fault(&state, vaddr, write, &made_writeable, out)) {
	case kVMFaultRetOK: {
		ipl_t ipl;

		if (write && !made_writeable) {
			/* unlock the out page, we need to go again */
			if (out != NULL) {
				ipl = vmp_acquire_pfn_lock();
				vmp_page_release_locked(*out);
				vmp_release_pfn_lock(ipl);
			}
			goto retry;
		}

		ipl = vmp_acquire_pfn_lock();
		vmp_md_fault_state_release(&state);
		vmp_release_pfn_lock(ipl);

		return kVMFaultRetOK;
	}
	default:
		kfatal("Unexpected vm_do_fault() return value\n");
	}
#endif
}

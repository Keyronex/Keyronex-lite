#include "../vmp.h"
#include "kdk/vm.h"
#include "vm/soft/vmp_soft.h"

extern uint8_t page_contents[PGSIZE * 128];

vaddr_t
P2V(paddr_t paddr)
{
	return paddr + (vaddr_t)page_contents;
}

paddr_t
V2P(vaddr_t vaddr)
{
	return vaddr - (vaddr_t)page_contents;
}

int
vmp_md_ps_init(vmp_procstate_t *vmps)
{
	vm_page_t *page;
	int	   r;

	r = vmp_page_alloc_locked(&page, &vmps->account, kPageUsePML3, false);
	if (r != 0)
		return r;

	vmps->md.top = page;

	return 0;
}

vm_fault_return_t
vmp_md_wire_pte(vmp_procstate_t *vmps, vaddr_t vaddr,
    struct vmp_md_fault_state *state)
{
	union soft_addr addr;
	vm_page_t      *pml2_page, *pml1_page;

	if (state->pte != NULL)
		return kVMFaultRetOK;

	addr.addr = vaddr;

	pte_t *top_phys = (void *)SIM_cr3,
	      *pml3_virt = (void *)P2V((paddr_t)top_phys);

	if (state->mid_page != NULL)
		goto fetch_pml1;
	else if (vmp_md_pte_is_empty(&pml3_virt[addr.top])) {
		int	  r = vmp_page_alloc_locked(&pml2_page, &vmps->account,
			  kPageUsePML2, false);
		uintptr_t pml2_phys;

		if (r != 0)
			return r;

		pml2_page->used_ptes = 0;
		pml2_page->referent_pte = (paddr_t)&top_phys[addr.top];

		pml2_phys = vm_page_paddr(pml2_page);
		pml3_virt[addr.top].hw.pfn = PADDR_TO_PFN(pml2_phys);
		pml3_virt[addr.top].hw.valid = 1;
		pml3_virt[addr.top].hw.writeable = 1;

		state->mid_page = pml2_page;
	} else if (vmp_md_pte_is_valid(&pml3_virt[addr.top])) {
		paddr_t *pml2_phys = (void *)(PFN_TO_PADDR(
		    pml3_virt[addr.top].hw.pfn));
		pml2_page = vm_paddr_to_page((paddr_t)pml2_phys);
		/* direct manipulation legal - page validly mapped */
		pml2_page->refcnt++;
		state->mid_page = pml2_page;
	} else {
		kfatal("Unhandled\n");
	}

fetch_pml1:
	pml2_page = state->mid_page;
	pte_t *pml2_phys = (void *)vm_page_paddr(pml2_page),
	      *pml2_virt = (void *)P2V((paddr_t)pml2_phys);

	if (state->bot_page != NULL)
		goto fetch_pte;
	else if (vmp_md_pte_is_empty(&pml2_virt[addr.mid])) {
		int	  r = vmp_page_alloc_locked(&pml1_page, &vmps->account,
			  kPageUsePML1, false);
		uintptr_t pml1_phys;

		if (r != 0)
			return r;

		/* direct manipulation legal - page is validly mapped */
		pml2_page->refcnt++;
		pml2_page->used_ptes += 1;
		pml1_page->referent_pte = (paddr_t)&pml2_phys[addr.mid];

		pml1_phys = vm_page_paddr(pml1_page);
		pml2_virt[addr.mid].hw.pfn = PADDR_TO_PFN(pml1_phys);
		pml2_virt[addr.mid].hw.valid = 1;
		pml2_virt[addr.mid].hw.writeable = 1;
		state->bot_page = pml1_page;
	} else if (vmp_md_pte_is_valid(&pml2_virt[addr.mid])) {
		/* assuming it's valid... */
		pte_hw_t *pml1_phys = (void *)(PFN_TO_PADDR(
		    pml2_virt[addr.mid].hw.pfn));
		pml1_page = vm_paddr_to_page((paddr_t)pml1_phys);
		/* direct manipulation legal -page is validly mapped */
		pml1_page->refcnt++;
		state->bot_page = pml1_page;
	} else {
		kfatal("Unhandled\n");
	}

fetch_pte:
	pml1_page = state->bot_page;
	pte_t *pml1 = (void *)vm_page_paddr(pml1_page);
	state->pte = (pte_t *)P2V((paddr_t)&pml1[addr.bot]);
	pml1_page->used_ptes += 1;

	return kVMFaultRetOK;
}

static void
free_pagetable(vmp_procstate_t *vmps, vm_page_t *page)
{
#if VM_DEBUG_PAGETABLES
	pac_printf("Pagetable page 0x%zx to be freed. Its referent PTE is %p. "
		   "Its parent pagetable is 0x%zx\n",
	    VM_PAGE_PADDR(page), page->referent_pte,
	    VM_PAGE_PADDR(parent_page));
#endif

	if (page->use == kPageUsePML1 || page->use == kPageUsePML2) {
		vm_page_t *parent_page;
		pte_t	  *referent_pte_phys = (void *)page->referent_pte,
		      *referent_pte_virt = (pte_t *)P2V(
			  (paddr_t)referent_pte_phys);
		parent_page = vm_paddr_to_page((paddr_t)referent_pte_phys);

		vmp_md_pte_make_empty(referent_pte_virt);
		if (page->use != kPageUsePML2) {
			parent_page->used_ptes--;
			if (parent_page->used_ptes == 0)
				free_pagetable(vmps, parent_page);
		}
		vmp_page_delete_locked(page, &vmps->account, true);
	} else if (page->use == kPageUsePML3) {
		kfatal("Free PML3\n");
	} else {
		kfatal("Unexpected paeg use\n");
	}
}

static void
vmp_pagetable_page_pte_became_zero(vmp_procstate_t *vmps, vm_page_t *page)
{
	page->used_ptes--;

	if (page->used_ptes == 0)
		free_pagetable(vmps, page);
	else
		vmp_page_release_locked(page, &vmps->account);
}

/*
 * fetch PTE (and containing page) for a given virtual address
 * (note: hope the optimiser is smart enough to inline this in its uses)
 * \pre vmps working set lock held
 * \pre PFN lock held
 */
int
vmp_mp_fetch_pte(vmp_procstate_t *vmps, vaddr_t vaddr, pte_t **pppte,
    vm_page_t **ptablepage)
{
	union soft_addr addr;
	vm_page_t      *pml2_page, *pml1_page;
	addr.addr = vaddr;

	pte_t *top_phys = (void *)SIM_cr3,
	      *pml3_virt = (void *)P2V((paddr_t)top_phys);

	if (vmp_md_pte_is_empty(&pml3_virt[addr.top])) {
		*pppte = NULL;
		return -1;
	} else if (vmp_md_pte_is_valid(&pml3_virt[addr.top])) {
		paddr_t *pml2_phys = (void *)(PFN_TO_PADDR(
		    pml3_virt[addr.top].hw.pfn));
		pml2_page = vm_paddr_to_page((paddr_t)pml2_phys);
	} else {
		kfatal("Unhandled\n");
	}

fetch_pml1:;
	pte_t *pml2_phys = (void *)vm_page_paddr(pml2_page),
	      *pml2_virt = (void *)P2V((paddr_t)pml2_phys);

	if (vmp_md_pte_is_empty(&pml2_virt[addr.mid])) {
		*pppte = NULL;
		return -1;
	} else if (vmp_md_pte_is_valid(&pml2_virt[addr.mid])) {
		/* assuming it's valid... */
		pte_hw_t *pml1_phys = (void *)(PFN_TO_PADDR(
		    pml2_virt[addr.mid].hw.pfn));
		pml1_page = vm_paddr_to_page((paddr_t)pml1_phys);
	} else {
		kfatal("Unhandled\n");
	}

fetch_pte:;
	pte_t *pml1 = (void *)vm_page_paddr(pml1_page);
	*pppte = (pte_t *)P2V((paddr_t)&pml1[addr.bot]);
	if (ptablepage)
		*ptablepage = pml1_page;

	return 0;
}

void
vmp_md_unmap_range_and_do(vmp_procstate_t *vmps, vaddr_t vstart, vaddr_t vend,
    void (*callback)(void *context, pte_t *saved_pte), void *context)
{
	union soft_addr start, end;
	ipl_t		ipl;
	pte_t	       *top_phys = (void *)SIM_cr3,
	      *pml3_virt = (void *)P2V((paddr_t)top_phys);

	start.addr = vstart;
	end.addr = vend;

	ipl = vmp_acquire_pfn_lock();
	for (vaddr_t i = vstart; i <= vend; i += PGSIZE) {
		pte_t	  *pte, saved_pte;
		vm_page_t *table_page;

		if (vmp_mp_fetch_pte(vmps, i, &pte, &table_page) != 0)
			continue;

		saved_pte = *pte;
		vmp_md_pte_make_empty(pte);

		if (callback)
			callback(context, &saved_pte);

		vmp_pagetable_page_pte_became_zero(vmps, table_page);
	}
	vmp_release_pfn_lock(ipl);
}

void
vmp_md_fault_state_release(vmp_procstate_t *vmps,
    struct vmp_md_fault_state		   *state)
{
	vmp_page_release_locked(state->mid_page, &vmps->account);
	vmp_pagetable_page_pte_became_zero(vmps, state->bot_page);
}

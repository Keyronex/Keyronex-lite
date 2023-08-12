#include "kdk/vm.h"
#include "vm/vmp.h"

typedef uint8_t pagecontents_t[128];
vm_page_t	mypages[128];
uint8_t		page_contents[PGSIZE * 128];

__thread paddr_t SIM_cr3;
__thread ipl_t	 SIM_ipl = kIPL0;
__thread void	*SIM_vmps = NULL;
vmp_procstate_t	 kernel_ps;

void
access(paddr_t addr, bool for_write)
{
	pte_hw_t       *top, *mid, *bot, pte;
	paddr_t		final_addr;
	union soft_addr unpacked;
	ipl_t		ipl;

	unpacked.addr = addr;

retry:
	ipl = vmp_acquire_pfn_lock();
	top = (pte_hw_t *)P2V(SIM_cr3);
	if (!top[unpacked.top].valid) {
		vmp_release_pfn_lock(ipl);
		printf("mmu: invalid entry in pml3\n");
		vmp_fault(addr, for_write, NULL);
		goto retry;
	}

	mid = (pte_hw_t *)P2V(PFN_TO_PADDR(top[unpacked.top].pfn));
	if (!mid[unpacked.mid].valid) {
		vmp_release_pfn_lock(ipl);
		printf("mmu: invalid entry in pml2\n");
		vmp_fault(addr, for_write, NULL);
		goto retry;
	}

	bot = (pte_hw_t *)P2V(PFN_TO_PADDR(mid[unpacked.mid].pfn));
	if (!bot[unpacked.bot].valid) {
		vmp_release_pfn_lock(ipl);
		printf("mmu: invalid entry in pml1\n");
		vmp_fault(addr, for_write, NULL);
		goto retry;
	} else if (for_write && !bot[unpacked.bot].writeable) {
		vmp_release_pfn_lock(ipl);
		printf("mmu: write protected\n");
		vmp_fault(addr, for_write, NULL);
		goto retry;
	}

	final_addr = PFN_TO_PADDR(bot[unpacked.bot].pfn);
	vmp_release_pfn_lock(ipl);

	printf("mmu: %s 0x%zx => 0x%zx\n", for_write ? "write" : "read ", addr,
	    final_addr + unpacked.pgi);
}

int
main(int argc, char *argv[])
{
	vaddr_t vaddr = PGSIZE;

	vm_region_add(V2P((paddr_t)page_contents), sizeof(page_contents));

	vm_ps_init(&kernel_ps);
	SIM_vmps = &kernel_ps;
	SIM_cr3 = vm_page_paddr(kernel_ps.md.top);

	printf("Allocating anonymous memory\n");
	vm_ps_allocate(&kernel_ps, &vaddr, PGSIZE * 4, true);
	vm_ps_dump_vadtree(&kernel_ps);

	printf("\n\nMaking accesses\n");
	access(PGSIZE, true);
	access(PGSIZE * 2, true);
	access(PGSIZE * 3, true);
}

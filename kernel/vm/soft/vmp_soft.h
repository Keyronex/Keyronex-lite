#ifndef KRX_SOFT_VMP_SOFT_H
#define KRX_SOFT_VMP_SOFT_H

#include "kdk/vm.h"

#define PADDR_TO_PFN(PADDR) ((uintptr_t)PADDR >> 7)
#define PFN_TO_PADDR(PFN) ((uintptr_t)PFN << 7)

union __attribute__((packed)) soft_addr {
	struct __attribute__((packed)) {
		uint64_t pgi : 7, bot : 4, mid : 4, top : 4, unused : 45;
	};
	uint64_t addr;
};

enum pte_sw_type {
	kPTENotSoftware = 0x0,
	kPTETransition,
	kPTETransitionFork,
	kPTEOutpaged,
};

typedef struct pte_hw {
	uint64_t pfn : 62;
	bool writeable : 1, valid : 1;
} pte_hw_t;

typedef struct pte_sw {
	enum pte_sw_type type : 2;
	/*! or drumslot */
	uint64_t pfn : 60;
	bool valid : 1;
} pte_sw_t;

typedef union pte {
	pte_sw_t sw;
	pte_hw_t hw;
} pte_t;

struct vmp_md_procstate {
	vm_page_t *top;
};

struct vmp_md_fault_state {
	/*! pinned pages of the page table. */
	vm_page_t *mid_page, *bot_page;
	pte_t *pte;
};

static inline bool
vmp_md_pte_is_valid(void *pte)
{
	return ((pte_hw_t *)pte)->valid == 1;
}

static inline bool
vmp_md_pte_is_writeable(void *pte)
{
	return ((pte_hw_t *)pte)->writeable == 1;
}

static inline bool
vmp_md_pte_is_trans(void *pte)
{
	pte_sw_t *sw = pte;
	return sw->valid == 0 && sw->type == kPTETransition;
}

static inline bool
vmp_md_pte_is_outpaged(void *pte)
{
	pte_sw_t *sw = pte;
	return sw->valid == 0 && sw->type == kPTEOutpaged;
}

static inline bool
vmp_md_pte_is_empty(void *pte)
{
	return *(uint64_t *)pte == 0;
}

static inline vm_page_t *
vmp_md_pte_page(pte_t *pte)
{
	if (vmp_md_pte_is_valid(pte))
		return vm_paddr_to_page(PFN_TO_PADDR(pte->hw.pfn));
	else if (vmp_md_pte_is_trans(pte))
		return vm_paddr_to_page(PFN_TO_PADDR(pte->sw.pfn));
	else
		kfatal("PTE has no page\n");
}

static inline void
vmp_md_pte_make_empty(pte_t *pte)
{
	pte->hw.pfn = 0;
	pte->hw.valid = 0;
	pte->hw.writeable = 0;
}

static inline void
vmp_md_pte_make_trans(pte_t *pte, pfn_t pfn)
{
	pte->sw.type = kPTETransition;
	pte->sw.pfn = pfn;
	pte->sw.valid = 0;
}

static inline void
vmp_md_pte_make_hw(pte_t *pte, pfn_t pfn, bool writeable)
{
	pte->hw.writeable = writeable;
	pte->hw.pfn = pfn;
	pte->hw.valid = 1;
}

#endif /* KRX_SOFT_VMP_SOFT_H */

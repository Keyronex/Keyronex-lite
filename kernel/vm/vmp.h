#ifndef KRX_VM_VMP_H
#define KRX_VM_VMP_H

#include "kdk/tree.h"
#include "kdk/vm.h"

struct vmp_forkpage {
	void	*pte;
	uint32_t refcount;
};

struct vmp_filepage {
	RB_ENTRY(vmp_filepage) rb_entry;
	void		      *pte;
};

/*! @brief Acquire the PFN database lock. */
#define vmp_acquire_pfn_lock() ke_spinlock_acquire(&vmp_pfn_lock)

/*! @brief Release the PFN database lock. */
#define vmp_release_pfn_lock(IPL) ke_spinlock_release(&vmp_pfn_lock, IPL)

int	   vmp_page_alloc_locked(vm_page_t **out, vm_account_t *account,
	   enum vm_page_use use, bool must);
void	   vmp_page_free_locked(vm_page_t *page);
void	   vmp_page_delete_locked(vm_page_t *page, vm_account_t *account,
	  bool release);
vm_page_t *vmp_page_retain_locked(vm_page_t *page, vm_account_t *account);
void	   vmp_page_release_locked(vm_page_t *page, vm_account_t *account);

extern kspinlock_t vmp_pfn_lock;

#endif /* KRX_VM_VMP_H */

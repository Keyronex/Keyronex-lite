#ifndef KRX_VM_VMP_H
#define KRX_VM_VMP_H

#include "kdk/tree.h"
#include "kdk/vm.h"

#ifdef KRX_SOFT
#include "soft/vmp_soft.h"
#else
#error Unknown port
#endif

/*!
 *  Page fault return values.
 */
typedef enum vm_fault_return {
	kVMFaultRetOK = 0,
	kVMFaultRetFailure = -1,
	kVMFaultRetPageShortage = -2,
	kVMFaultRetRetry = -3,
} vm_fault_return_t;

/*!
 * Virtual Address Descriptor - a mapping of a section object. Note that
 * copy-on-write is done at the section object level, not here.
 */
typedef struct vm_vad {
	struct vm_vad_flags {
		/*! current protection, and maximum legal protection */
		vm_protection_t protection : 3, max_protection : 3;
		/*! whether shared on fork (if false, copied) */
		bool inherit_shared : 1;
		/*! whether mapping is private anonymous memory. */
		bool private : 1;
		/*! (!private only) whether the mapping is copy-on-write */
		bool cow : 1;
		/*! if !private, page offset into section object (max 256tib) */
		off_t offset : 36;
	} flags;
	/*! Entry in vm_procstate::vad_rbtree */
	RB_ENTRY(vm_vad) rbtree_entry;
	/*! Start and end vitrual address. */
	vaddr_t start, end;
	/*! Section object; if flags.anonymous = false */
	void *section;
} vm_vad_t;

/*!
 * Per-process state.
 */
typedef struct vmp_procstate {
	/*! VAD queue + working set list lock. */
	kmutex_t mutex;
	/*! Working set entry queue - tail most recently added, head least. */
	TAILQ_HEAD(, vmp_wsle) ws_queue;
	/*! Working set entry tree. */
	RB_HEAD(vmp_wsle_tree, vmp_wsle) ws_tree;
	/*! VAD tree. */
	RB_HEAD(vm_vad_rbtree, vm_vad) vad_queue;
	/*! Count of pages in working set list. */
	size_t ws_current_count;
	/*! Per-arch stuff. */
	struct vmp_md_procstate md;
} vmp_procstate_t;

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

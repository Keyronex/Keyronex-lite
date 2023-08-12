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
	/*! Account. */
	vm_account_t account;
	/*! Per-arch stuff. */
	struct vmp_md_procstate md;
} vmp_procstate_t;

/*! roto-level shared anonymous table */
struct vmp_amap_l3 {
	struct vmp_amap_l2 *entries[512];
};

/*! mid-level shared anonymous table */
struct vmp_amap_l2 {
	struct vmp_amap_l1 *entries[512];
};

/*! leaf-level shared anonymous table */
struct vmp_amap_l1 {
	pte_t entries[512];
};

struct vmp_forkpage {
	pte_t	 pte;
	uint32_t refcount;
};

struct vmp_filepage {
	vm_page_t	      *page;
	RB_ENTRY(vmp_filepage) rb_entry;
};

struct vm_section {
	/*! What kind of section is this? */
	enum vm_section_kind {
		kFile,
		kAnon,
	} kind;
	union {
		struct {
			RB_HEAD(vmp_file_page_tree, vm_page) page_tree;
		} file;
		struct {
			struct vmp_amap_l3 *l3;
		} anon;
	};
};

/*! @brief Acquire the PFN database lock. */
#define vmp_acquire_pfn_lock() ke_spinlock_acquire(&vmp_pfn_lock)

/*! @brief Release the PFN database lock. */
#define vmp_release_pfn_lock(IPL) ke_spinlock_release(&vmp_pfn_lock, IPL)

/*!
 * @post If kVMFaultRetOK, reference held to each pagetable level and used PTE
 * count incremented on leaf table.
 "*/
vm_fault_return_t vmp_md_wire_pte(vmp_procstate_t *vmps, vaddr_t vaddr,
    struct vmp_md_fault_state *state);
void		  vmp_md_fault_state_release(vmp_procstate_t *vmps,
		 struct vmp_md_fault_state		     *state);

int	   vmp_page_alloc_locked(vm_page_t **out, vm_account_t *account,
	   enum vm_page_use use, bool must);
void	   vmp_page_free_locked(vm_page_t *page);
void	   vmp_page_delete_locked(vm_page_t *page, vm_account_t *account,
	  bool release);
vm_page_t *vmp_page_retain_locked(vm_page_t *page, vm_account_t *account);
void	   vmp_page_release_locked(vm_page_t *page, vm_account_t *account);

int vmp_fault(vaddr_t vaddr, bool write, vm_account_t *out_account,
    vm_page_t **out);

vm_vad_t *vmp_ps_vad_find(vmp_procstate_t *ps, vaddr_t vaddr);

void vmp_wsl_insert(vmp_procstate_t *ps, vaddr_t vaddr);
void vmp_wsl_remove(vmp_procstate_t *ps, vaddr_t vaddr);

extern kspinlock_t vmp_pfn_lock;

#endif /* KRX_VM_VMP_H */

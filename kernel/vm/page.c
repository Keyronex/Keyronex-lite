/*
 * Copyright (c) 2023 NetaScale Object Solutions.
 * Created on Fri Aug 11 2023.
 */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*!
 * @file page.c
 * @brief Resident page management - allocation, deallocation, etc.
 */

#include "kdk/libkern.h"
#include "kdk/soft_compat.h"
#include "kdk/vm.h"
#include "vmp.h"

#define KRX_VM_SANITY_CHECKING

#if KRX_PORT_BITS == 64
#define BAD_INT 0xdeadbeefdeafbeef
#else
#define BAD_INT 0xdeadbeef
#endif
#define BAD_PTR ((void *)BAD_INT)

#define DEFINE_PAGEQUEUE(NAME) \
	static page_queue_t NAME = TAILQ_HEAD_INITIALIZER(NAME)

typedef TAILQ_HEAD(, vm_page) page_queue_t;

DEFINE_PAGEQUEUE(vm_pagequeue_free);
DEFINE_PAGEQUEUE(vm_pagequeue_modified);
DEFINE_PAGEQUEUE(vm_pagequeue_standby);
struct vm_stat vmstat;
kspinlock_t    vmp_pfn_lock = KSPINLOCK_INITIALISER;
vm_account_t   deleted_account;

static inline void
update_page_use_stats(enum vm_page_use use, int value)
{
	kassert(ke_spinlock_held(&vmp_pfn_lock));

	switch (use) {
	case kPageUseDeleted:
		vmstat.ndeleted += value;
		break;

	case kPageUseAnonPrivate:
		vmstat.nanonprivate += value;
		break;

	default:
		kfatal("Handle");
	}
}

int
vmp_page_alloc_locked(vm_page_t **out, vm_account_t *account,
    enum vm_page_use use, bool must)
{
	vm_page_t *page;

	kassert(ke_spinlock_held(&vmp_pfn_lock));

	page = TAILQ_FIRST(&vm_pagequeue_free);
	kassert(page != NULL);
	TAILQ_REMOVE(&vm_pagequeue_free, page, queue_link);

#ifdef KRX_VM_SANITY_CHECKING
	kassert(page->refcnt == 0);
	kassert(page->used_ptes == 0);
	kassert(page->referent_pte == 0);
#endif

	page->refcnt = 1;
	page->use = use;
	page->busy = 0;
	page->dirty = 0;
	page->offset = 0;
	page->referent_pte = 0;
	page->owner = 0;
	page->swap_descriptor = 0;
	page->used_ptes = 0;

	vmstat.nfree--;
	vmstat.nactive++;
	account->nalloced++;
	account->nwires++;
	update_page_use_stats(use, 1);

	*out = page;

	memset((void *)vm_page_direct_map_addr(page), 0x0, PGSIZE);

	return 0;
}

void
vmp_page_free_locked(vm_page_t *page)
{
	kassert(ke_spinlock_held(&vmp_pfn_lock));
	kassert(page->use == kPageUseDeleted);
	kassert(page->refcnt == 0);

	page->dirty = false;
	page->referent_pte = 0;
	page->use = kPageUseDeleted;
	page->used_ptes = 0;
	deleted_account.nalloced--;
	vmstat.nfree++;
	vmstat.ndeleted--;
}

void
vmp_page_delete_locked(vm_page_t *page, vm_account_t *account, bool release)
{
	kassert(ke_spinlock_held(&vmp_pfn_lock));
	kassert(page->use != kPageUseDeleted);
	kassert(!page->busy);

	update_page_use_stats(page->use, -1);
	vmstat.ndeleted++;
	page->use = kPageUseDeleted;

	account->nalloced--;
	deleted_account.nalloced++;

	if (release) {
		kassert(page->refcnt > 0);
		vmp_page_release_locked(page, account);
	} else {
		if (page->refcnt == 0 && page->dirty) {
			TAILQ_REMOVE(&vm_pagequeue_modified, page, queue_link);
			vmstat.nmodified--;
			vmp_page_free_locked(page);
		} else if (page->refcnt == 0) {
			TAILQ_REMOVE(&vm_pagequeue_standby, page, queue_link);
			vmstat.nstandby--;
			vmp_page_free_locked(page);
		}
	}
}

vm_page_t *
vmp_page_retain_locked(vm_page_t *page, vm_account_t *account)
{
	kassert(ke_spinlock_held(&vmp_pfn_lock));

	account->nwires++;

	if (page->refcnt++ == 0) {
		/* going from inactive to active state */
		kassert(page->use != kPageUseDeleted);
		if (page->dirty) {
			TAILQ_REMOVE(&vm_pagequeue_modified, page, queue_link);
			vmstat.nmodified--;
			vmstat.nactive++;
		} else {
			TAILQ_REMOVE(&vm_pagequeue_standby, page, queue_link);
			vmstat.nstandby--;
			vmstat.nactive++;
		}
	}

	return page;
}

void
vmp_page_release_locked(vm_page_t *page, vm_account_t *account)
{
	kassert(ke_spinlock_held(&vmp_pfn_lock));
	kassert(page->refcnt > 0);

	account->nwires--;

	if (page->refcnt-- == 1) {
		/* going from active to inactive state */
		vmstat.nactive--;
		if (page->use == kPageUseDeleted) {
			vmp_page_free_locked(page);
		} else if (page->dirty) {
			TAILQ_REMOVE(&vm_pagequeue_modified, page, queue_link);
			vmstat.nmodified++;
		} else {
			TAILQ_REMOVE(&vm_pagequeue_standby, page, queue_link);
			vmstat.nstandby++;
		}
	}
}
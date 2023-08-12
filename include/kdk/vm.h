/*
 * Copyright (c) 2023 NetaScale Object Solutions.
 * Created on Fri Aug 11 2023.
 */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef KRX_KDK_VM_H
#define KRX_KDK_VM_H

#include <stdbool.h>
#include <stdint.h>

#include "kdk/port.h"
#include "kdk/queue.h"

typedef uintptr_t vaddr_t, paddr_t, pfn_t;

#if KRX_PORT_BITS == 64
#define PFN_BITS 52
#else
#define PFN_BITS 20
#endif

struct vm_stat {
	/*! memory by state */
	size_t nactive, nfree, nmodified, nstandby;

	/*! memory by use; nfree still counts free. */
	size_t ndeleted, nanonprivate, nanonfork, nfile, nanonshare,
	    nprocpgtable, nprotopgtable, nkwired;
};

enum vm_page_use {
	kPageUseInvalid,
	kPageUseFree,
	kPageUseDeleted,
	kPageUseAnonPrivate,
};

/*!
 * PFN database element. Mainly for private use by the VMM, but published here
 * publicly for efficiency.
 */
typedef struct __attribute__((packed)) vm_page {
	/* first word */
	uintptr_t	 pfn : PFN_BITS;
	enum vm_page_use use : 4;
	bool		 dirty : 1;
	bool		 busy : 1;
	uintptr_t	 padding : 6;

	/* second word */
	union __attribute__((packed)) {
		uint16_t used_ptes : 16;
		size_t	 offset : 48;
	};
	uint16_t refcnt;

	/* third word */
	paddr_t referent_pte;

	/* 4th, 5th words */
	union {
		TAILQ_ENTRY(vm_page)	  queue_link;
		struct vmp_pager_request *pager_request;
	};

	/* 6th word */
	void *owner;

	/* 7th word */
	uintptr_t swap_descriptor;
} vm_page_t;

typedef struct vm_account {
	size_t nalloced;
	size_t nwires;
} vm_account_t;

/*!
 * @brief Allocate a physical page frame.
 *
 * @param account An optional account to debit for this page. If an account is
 * specified, then vm_page_delete should be called when the allocator has no
 * further need of the page; this will credit the account for its page
 * allocation. The account is also charged for a wiring.
 *
 * @pre PFNDB lock must not be held.
 */
int vm_page_alloc(vm_page_t **out, vm_account_t *account, enum vm_page_use use,
    bool must);

/*!
 * @brief Release and mark a page for deletion.
 *
 * This changes the page's use to kPageUseDeleted and then calls
 * vm_page_release() on the page if its refcount is greater than 0. Otherwise,
 * it immediately frees the page.
 *
 * @param account Account to credit for deallocating a page.
 * @param release Whether account holds a reference which is being released. If
 * so, its wire quota is also credited.
 *
 * @pre PFNDB lock must not be held.
 */
void vm_page_delete(vm_page_t *page, vm_account_t *account, bool release);

/*!
 * @brief Retain a reference to a page.
 *
 * This preserves the page from being freed by wiring it in-memory. It will
 * remain possible to access it until vm_page_release() is called. Access must
 * be via a private mapping or the direct map; any other mappings of the page
 * are not necessarily preserved.
 *
 * @param account An optional account to debit for wiring the page.
 *
 * @pre PFNDB lock must not be held.
 */
vm_page_t *vm_page_retain(vm_page_t *page, vm_account_t *account);

/*!
 * @brief Release a reference to a page.
 *
 * @param account An optional account to credit for unwiring the page.
 *
 * @pre PFNDB lock must not be held.
 */
void vm_page_release(vm_page_t *page, vm_account_t *account);

/*!
 * @brief Get the page frame structure for a given physical address.
 */
vm_page_t *vm_paddr_to_page(paddr_t paddr);

static inline paddr_t
vm_page_paddr(vm_page_t *page)
{
	return PFN_TO_PADDR(page->pfn);
}

static inline vaddr_t
vm_page_direct_map_addr(vm_page_t *page)
{
	return P2V(vm_page_paddr(page));
}

extern vm_account_t general_account, deleted_account;

#endif /* KRX_KDK_VM_H */

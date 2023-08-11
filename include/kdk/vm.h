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

enum vm_page_use {
	kPageUseInvalid,
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
	/*! if refcnt == 0, determines if on modified or standby list */
	bool dirty : 1;
	/*! only legitimately 1 if refcnt > 0, whether paging request ongoing */
	bool	  busy : 1;
	uintptr_t padding : 6;

	/* second word */
	union __attribute__((packed)) {
		/*! count of used PTEs if pagetable */
		uint16_t used_ptes : 16;
		/*! offset within file/anonshared if file/anonshared */
		size_t offset : 48;
	};
	uint16_t refcnt;

	/* third word */
	/*! PTE or prototype PTE mapping this */
	paddr_t referent_pte;

	/* 4th, 5th words */
	union {
		TAILQ_ENTRY(vm_pfn)	  entry;
		struct vmp_pager_request *pager_request;
	};

	/* 6th word */
	void *owner;
} vm_page_t;

#endif /* KRX_KDK_VM_H */

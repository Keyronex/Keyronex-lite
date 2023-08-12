#ifndef KRX_KDK_SOFT_H
#define KRX_KDK_SOFT_H

#include <stdint.h>

#include "soft_compat.h"

#define KRX_PORT_BITS 64
#define PGSIZE 128

uintptr_t P2V(uintptr_t paddr);
uintptr_t V2P(uintptr_t vaddr);

#define PADDR_TO_PFN(PADDR) ((uintptr_t)PADDR >> 7)
#define PFN_TO_PADDR(PFN) ((uintptr_t)PFN << 7)

#endif /* KRX_KDK_SOFT_H */

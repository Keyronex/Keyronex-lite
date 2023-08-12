#include "kdk/vm.h"
#include "vmp.h"

struct vmp_wsle {
	TAILQ_ENTRY(vmp_wsle) queue_entry;
	RB_ENTRY(vmp_wsle) rb_entry;
	vaddr_t vaddr;
};

static inline intptr_t
wsle_cmp(struct vmp_wsle *x, struct vmp_wsle *y)
{
	return x->vaddr - y->vaddr;
}

RB_GENERATE(vmp_wsle_tree, vmp_wsle, rb_entry, wsle_cmp);

static struct vmp_wsle *
vmp_wsl_find(vmp_procstate_t *ps, vaddr_t vaddr)
{
	struct vmp_wsle key;
	key.vaddr = vaddr;
	return RB_FIND(vmp_wsle_tree, &ps->ws_tree, &key);
}

static void
vm_page_evict(vmp_procstate_t *ps, pte_t *pte)
{
	bool dirty = vmp_md_pte_is_writeable(pte);
	vm_page_t *page = vmp_md_pte_page(pte);

	page->dirty |= dirty;

	switch (page->use) {
	case kPageUseAnonPrivate: {
		/*
		 * we need to replace this with a transition PTE then.
		 * used_ptes count is as such unchanged.
		 */
		page->referent_pte = V2P((vaddr_t)pte);
		vmp_md_pte_make_trans(pte, page->pfn);
		vmp_page_release_locked(page, &ps->account);
		break;
	}

	default:
		kfatal("Unhandled page use in working set eviction\n");
	}
}

static void
wsl_evict_one(vmp_procstate_t *ps)
{
	struct vmp_wsle *wsle = TAILQ_FIRST(&ps->ws_queue);
	pte_t *pte;
	int r;

	kassert(wsle != NULL);
	TAILQ_REMOVE(&ps->ws_queue, wsle, queue_entry);
	RB_REMOVE(vmp_wsle_tree, &ps->ws_tree , wsle);

	kprintf("Evicting 0x%zx\n", wsle->vaddr);
	r = vmp_mp_fetch_pte(ps, wsle->vaddr, &pte, NULL);
	kassert(r == 0);
	kassert(vmp_md_pte_is_valid(pte));

	kmem_free(wsle, sizeof(*wsle));
	vm_page_evict(ps, pte);
}

void
vmp_wsl_insert(vmp_procstate_t *ps, vaddr_t vaddr)
{
	struct vmp_wsle *wsle;

	kassert(vmp_wsl_find(ps, vaddr) == NULL);

	if ((ps->ws_current_count + 1) > 2)
		wsl_evict_one(ps);
	else
		ps->ws_current_count++;

	wsle = kmem_alloc(sizeof(*wsle));
	wsle->vaddr = vaddr;
	TAILQ_INSERT_TAIL(&ps->ws_queue, wsle, queue_entry);
	RB_INSERT(vmp_wsle_tree, &ps->ws_tree, wsle);
}

void vmp_wsl_remove(vmp_procstate_t*ps, vaddr_t vaddr)
{
	struct vmp_wsle *wsle;

	kassert(vmp_wsl_find(ps, vaddr) == NULL);

	kfatal("Implement this function\n");

}

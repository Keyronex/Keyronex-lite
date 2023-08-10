Virtual Memory Manager
======================

Overview
--------

Virtual memory is the centrepiece of Keyronex and so is the subject of a lot of
attention in the design. The goal of the design is to provide the following
features:

 - Inter-process protection
 - Memory mapped files
 - Shareable anonymous memory
 - Posix fork() support
 - Copy-on-write optimisation for mapped files, shared memory, and Posix fork()
 - Paging dynamics in line with Denning's Working Set Model
 - (some) kernel as well as userland data to be pageable

The design of the virtual memory manager fits most closely within the VMS
tradition; it is mostly modelled after that of OpenVMS, Windows NT, and MINTIA,
as described in VMS Internals, Windows Internals, and by the author of MINTIA.
The handling of Posix fork() on the other hand is borrowed from NetBSD's UVM,
which got the same from SunOS VM.

Compromises
-----------

Keyronex VM assumes native support in the architecture for a traditional system
of tree-based page tables, which is true of the 68k, amd64, aarch64, and risc64
ports. It could run on systems based on software-refilled TLBs by constructing
page tables of its own format and walking these in the TLB refill trap handler.

..
    Some compromises are made in this initial design for simplicity. The main
    compromise is that, unlike VMS, NT, and MINTIA, page tables and VM support
    structures are not themselves pageable. A direct map of all physical memory is
    also relied upon. In the future, when the VMM is proven to be reliable in
    operation, it might become a goal to do away with these restrictions. Some
    inconsistent efforts are made to leave the door open for this.


Principle of Operation
----------------------

Denning's Working Set Model proposes that a working set is the set of pages that
a process must have resident in memory to run efficiently (without excessive
page faults) at any given time. To this the concept of balance set can be added:
the set of processes being permitted to be resident in memory at any given time.
The aim is that the balance set be sized suitably to allow all the processes
currently resident to have their working set be wholly resident.

Squaring this model with multi-threaded processes adds difficulty and as such a
proper implementation of balance set management is not within scope yet, but the
working set model remains the basis of Keyronex VM.

This determines the page replacement approach that the VM takes. The algorithm
is called Segmented FIFO. It is so-called because the page cache is Segmented
into two entities:

Primary Page Cache (aka Working Set, Resident Set)
    This is composed of per-process queues of pages; these pages are those which
    are currently mapped with valid PTEs in that process . Replacement is by
    FIFO - when the working set queue of a process reaches its size limit, the
    least recently mapped page in a process is locally replaced when a new page
    is mapped in that process.

Secondary Page Cache
    This is composed of two queues: the Modified Page Queue and the Standby Page
    Queue. When a page is no longer mapped in any Working Set, it is placed onto
    either the Modified or Standby page queue according to whether it was
    modified while mapped into a working set, giving the page a second chance.
    It is from these queues that pages actually get replaced on the global
    level, by being written to disk if dirty and made available for reuse for
    new data when clean.

One of the main jobs of the VMM is to achieve a balance between the sizes of
the primary page cache (and between the working sets comprising it) and the
secondary page cache for optimal performance.

Page Frame Number Database (aka PFN Database, PFNDB)
-----------------------------------------------------

The PFN database is a set of databases (one for every contiguous region of main
memory) describing the current state of each page of main memory. It is
organised as an array at the start of each contiguous region of main memory,
each linked into a queue of regions so that the PFNDB entry corresponding to a
physical address can be quickly determined

.. note::
    as an efficiency to eliminate the need to iterate the queue of regions, in
    the future this could become a fixed region of the virtual address space
    which is mapped such that e.g. `pfndb[pfn]` is all it takes to access the
    PFNDB entry for a given page frame number.


The PFNDB stores differing data for different sorts of pages. The format for
32-bit ports is:

.. code-block:: c

    /* first word */
    uintptr_t   pfn:    20;
    enum        use:    4;
    enum        state:  3;
    bool        dirty:  1;
    uintptr_t   padding: 4;

    /* second word */
    uint16_t    refcnt;
    uint16_t used_ptes;

    /* third word */
    paddr_t referent_pte;

    /* 4th, 5th words */
    union {
        TAILQ_ENTRY(vm_pfn) entry;
        struct vmp_pager_request *pager_request;
    };

    /* 6th, 7th, 8th words */
    RB_ENTRY(vm_pfn) file_tree_entry;


.. todo::
    Anonymous should carry a swap descriptor somewhere in this so that they can
    be written to disk but remain on the standby list.

The total size thus amounts to 32 bytes.

For 64-bit ports, the same format is used, except `pfn` is 52 bits, and padding
between `used_ptes` and `referent_ptes` yields a structure totalling 64 bytes.

The fields provide information about pages. The first field is the actual page
frame number of a page.

What a page is being used for is tracked by `use`. The uses are Free, Deleted,
Anonymous Private; Anonymous Forked; Anonymous Shared; File Cache; Amap Levels
3, 2, or 1; or hardware-specific uses for native page tables.

Pages can be in several states. The states are tied to the reference count; for
a reference count of 1 or above, the state must be Active, while for 0, it must
be Modified, Standby, or Free.

Active
    The page is mapped in at least one working set or has been wired, e.g.
    by an MDL.

Modified
    The page is not validly mapped anywhere, but it is dirty and must be
    flushed to disk.

Standby
    The page is not validly mapped anywhere and has already been flushed to
    disk (or was never dirtied), so it is free to be reused.

Free
    The page is available for immediate reuse.

.. todo::
    How about for a page which is currently being written out to disk? It gets
    promoted to Active until pageout is complete, presumably.
    And what about for a page being paged in? We must want a flag or new state
    for that case so that people know to wait on its pager request.

The `dirty` field notes whether the page is explicitly known to be dirty. It is
OR'd into the PFNDB entry at the time of a page's removal from a working set, or
may be done explicitly. (This field is extraneous?)

The `refcnt` field is the number of wires on a page and determines whether the
page can be evicted or freed. The refcount dropping to zero will place a page
on either the Modified or Standby list depending on whether it's dirty, or onto
the Free list if the page use has been set to Deleted.

Pages which contain page tables (either Amaps, described later, or hardware page
tables) make use of the `used_ptes` field to indicate how many non-zero PTEs are
in that page. The `used_ptes` field is incremented and decremented together with
the reference count; if it drops to 0, the page use is set to Deleted so that
when the reference count is dropped to 0, the page is freed.

PFNDB entries also carry a pointer to the PTE which maps a given page. The
definition of this varies depending on the use:

Private anonymous, hardware page tables:
    In this case, it is the actual hardware PTE that maps either this page (in a
    leaf page table) or which maps this page table in the next level of the
    tree.
Shared anonymous, Amap tables:
    As above, except it's the prototype PTE in the Amap L3 leaf table, or the
    element of the Amap L2 or L1 array mapping this level of the Amap tables.
Anonymous forked:
    Points to the `pte` field within the `vmp_anon` that this page belongs to.
File cache:
    referent_pte is instead the page index within the file
    
    .. todo::
        we don't have a way of getting the file object from this, which we need
        to update the prototype PTE!
        maybe file cache should also use 3-level tables like anonymous does?
        then we can drop 3 words from a PFNDB entry and have space for an owning
        file/anonymous section pointer. Trouble is that mappable files may be
        > 512GiB, so 4 levels may be necessary.

Page Table Entries
------------------

The VMM by relying on the existence of traditional multi-level page tables can
store metadata more optimally. In contrast to Mach-style VMMs, Keyronex VM
uses the native page tables of the architecture to store metadata and does not
treat them as purely caches of more abstract datastructures.

For consistency, the PTE format is also used by abstract datastructures of the
Keyronex VM - when PTEs are used in this way, in locations where they will never
be interpreted by the MMU itself, they are called prototype PTEs. Prototype
PTEs are used to implement shared anonymous, file cache, and forked anonymous
memory.

Page table entries can then be either software or hardware PTEs. A hardware PTE
has the valid bit set, while a software PTE does not. The general format of
software PTEs varies depending on the architecture, but looks roughly like this
on a 32-bit platform:

.. code-block:: c

    enum soft_pte_kind kind: 2;
    uintptr_t   data:   29;
    bool        valid:   1;

On 64-bit platforms, the `data` field is instead around 61 bits in length.

There are several kinds of software PTEs:

Transition PTEs
    These are created when a private anonymous page is evicted from a process'
    working set. The `data` field is the PFN number of the anonymous page.

Swap Descriptor PTEs
    These are created when a private anonymous page is paged out at the global
    level, i.e. written to disk and removed from the standby page queue. The
    `data` field is a unique number by which the swapped-out page can be
    retrieved from the pagefile.

Fork PTEs:
    These are created when the Posix fork() operation is carried out. The `data`
    field is a pointer to the `vmp_anon` structure (described later) which holds
    the prototype PTE (again described later). The pointer can fit here because
    `vmp_anon`\ s are always 8-byte aligned, meaning the 3 low bits are always
    zero and can accordingly be shifted away. (If it were necessary to shrink
    the number of bits used for the `data` field even further, we could do so
    by storing the vmp_anon as an offset from the kernel heap base instead; this
    would save yet more bits).


Amaps
-----

.. todo::
    describe shared anonymous memory

Forked Anonymous and `vmp_anon`\ s
----------------------------------

.. code-block:: c

    pte_t       pte;
    uint32_t    refcnt;

On 32-bit platforms this makes 8 bytes, while on 64-bit platforms padding is
added to extend it from 12 to 16 bytes.

.. todo::
    describe support for fork()
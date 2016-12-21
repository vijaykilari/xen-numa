/*
 * Common NUMA handling functions for x86 and arm.
 * Original code extracted from arch/x86/numa.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */


#include <xen/mm.h>
#include <xen/string.h>
#include <xen/init.h>
#include <xen/ctype.h>
#include <xen/nodemask.h>
#include <xen/numa.h>
#include <xen/keyhandler.h>
#include <xen/time.h>
#include <xen/smp.h>
#include <xen/pfn.h>
#include <xen/sched.h>
#include <xen/errno.h>
#include <xen/softirq.h>
#include <asm/setup.h>

static int numa_setup(char *s);
custom_param("numa", numa_setup);

bool_t numa_off = 0;
struct node_data node_data[MAX_NUMNODES];

/* Mapping from pdx to node id */
int memnode_shift;
unsigned long memnodemapsize;
u8 *memnodemap;
typeof(*memnodemap) _memnodemap[64];

nodeid_t cpu_to_node[NR_CPUS] __read_mostly = {
    [0 ... NR_CPUS-1] = NUMA_NO_NODE
};

cpumask_t node_to_cpumask[MAX_NUMNODES] __read_mostly;

int num_node_memblks;
struct node node_memblk_range[NR_NODE_MEMBLKS];
nodeid_t memblk_nodeid[NR_NODE_MEMBLKS];
struct node nodes[MAX_NUMNODES] __initdata;

int valid_numa_range(u64 start, u64 end, nodeid_t node)
{
#ifdef CONFIG_NUMA
    int i;

    for (i = 0; i < num_node_memblks; i++) {
        struct node *nd = &node_memblk_range[i];

        if (nd->start <= start && nd->end > end &&
            memblk_nodeid[i] == node )
            return 1;
    }

    return 0;
#else
    return 1;
#endif
}

__init int conflicting_memblks(u64 start, u64 end)
{
    int i;

    for (i = 0; i < num_node_memblks; i++) {
        struct node *nd = &node_memblk_range[i];
        if (nd->start == nd->end)
            continue;
        if (nd->end > start && nd->start < end)
            return i;
        if (nd->end == end && nd->start == start)
            return i;
    }
    return -1;
}

__init void cutoff_node(int i, u64 start, u64 end)
{
    struct node *nd = &nodes[i];
    if (nd->start < start) {
        nd->start = start;
        if (nd->end < nd->start)
            nd->start = nd->end;
    }
    if (nd->end > end) {
        nd->end = end;
        if (nd->start > nd->end)
            nd->start = nd->end;
    }
}

/*
 * Given a shift value, try to populate memnodemap[]
 * Returns :
 * 1 if OK
 * 0 if memnodmap[] too small (of shift too small)
 * -1 if node overlap or lost ram (shift too big)
 */
static int __init populate_memnodemap(const struct node *nodes,
                                      int numnodes, int shift,
                                      nodeid_t *nodeids)
{
    unsigned long spdx, epdx;
    int i, res = -1;

    memset(memnodemap, NUMA_NO_NODE, memnodemapsize * sizeof(*memnodemap));
    for ( i = 0; i < numnodes; i++ )
    {
        spdx = paddr_to_pdx(nodes[i].start);
        epdx = paddr_to_pdx(nodes[i].end - 1) + 1;
        if ( spdx >= epdx )
            continue;
        if ( (epdx >> shift) >= memnodemapsize )
            return 0;
        do {
            if ( memnodemap[spdx >> shift] != NUMA_NO_NODE )
                return -1;

            if ( !nodeids )
                memnodemap[spdx >> shift] = i;
            else
                memnodemap[spdx >> shift] = nodeids[i];

            spdx += (1UL << shift);
        } while ( spdx < epdx );
        res = 1;
    }

    return res;
}

static int __init allocate_cachealigned_memnodemap(void)
{
    unsigned long size = PFN_UP(memnodemapsize * sizeof(*memnodemap));
    unsigned long mfn;

    mfn = alloc_boot_pages(size, 1);
    if ( !mfn )
    {
        printk(KERN_ERR
               "NUMA: Unable to allocate Memory to Node hash map\n");
        memnodemapsize = 0;
        return -1;
    }

    memnodemap = mfn_to_virt(mfn);
    mfn <<= PAGE_SHIFT;
    size <<= PAGE_SHIFT;
    printk(KERN_DEBUG "NUMA: Allocated memnodemap from %lx - %lx\n",
           mfn, mfn + size);
    memnodemapsize = size / sizeof(*memnodemap);

    return 0;
}

/*
 * The LSB of all start and end addresses in the node map is the value of the
 * maximum possible shift.
 */
static int __init extract_lsb_from_nodes(const struct node *nodes,
                                         int numnodes)
{
    int i, nodes_used = 0;
    unsigned long spdx, epdx;
    unsigned long bitfield = 0, memtop = 0;

    for ( i = 0; i < numnodes; i++ )
    {
        spdx = paddr_to_pdx(nodes[i].start);
        epdx = paddr_to_pdx(nodes[i].end - 1) + 1;
        if ( spdx >= epdx )
            continue;
        bitfield |= spdx;
        nodes_used++;
        if ( epdx > memtop )
            memtop = epdx;
    }
    if ( nodes_used <= 1 )
        i = BITS_PER_LONG - 1;
    else
        i = find_first_bit(&bitfield, sizeof(unsigned long)*8);

    memnodemapsize = (memtop >> i) + 1;

    return i;
}

int __init compute_hash_shift(struct node *nodes, int numnodes,
                              nodeid_t *nodeids)
{
    int shift;

    shift = extract_lsb_from_nodes(nodes, numnodes);
    if ( memnodemapsize <= ARRAY_SIZE(_memnodemap) )
        memnodemap = _memnodemap;
    else if ( allocate_cachealigned_memnodemap() )
        return -1;
    printk(KERN_DEBUG "NUMA: Using %d for the hash shift.\n", shift);

    if ( populate_memnodemap(nodes, numnodes, shift, nodeids) != 1 )
    {
        printk(KERN_INFO "Your memory is not aligned you need to "
               "rebuild your hypervisor with a bigger NODEMAPSIZE "
               "shift=%d\n", shift);
        return -1;
    }

    return shift;
}

/* initialize NODE_DATA given nodeid and start/end */
void __init setup_node_bootmem(nodeid_t nodeid, u64 start, u64 end)
{
    unsigned long start_pfn, end_pfn;

    start_pfn = start >> PAGE_SHIFT;
    end_pfn = end >> PAGE_SHIFT;

    NODE_DATA(nodeid)->node_id = nodeid;
    NODE_DATA(nodeid)->node_start_pfn = start_pfn;
    NODE_DATA(nodeid)->node_spanned_pages = end_pfn - start_pfn;

    node_set_online(nodeid);
}

void numa_add_cpu(int cpu)
{
    cpumask_set_cpu(cpu, &node_to_cpumask[cpu_to_node(cpu)]);
}

void numa_set_node(int cpu, nodeid_t node)
{
    cpu_to_node[cpu] = node;
}

EXPORT_SYMBOL(node_to_cpumask);
EXPORT_SYMBOL(memnode_shift);
EXPORT_SYMBOL(memnodemap);
EXPORT_SYMBOL(node_data);

static __init int numa_setup(char *opt)
{
    if ( !strncmp(opt,"off",3) )
        numa_off = 1;
    if ( !strncmp(opt,"on",2) )
        numa_off = 0;

    return arch_numa_setup(opt);
}

static void dump_numa(unsigned char key)
{
    s_time_t now = NOW();
    unsigned int i, j, n;
    int err;
    struct domain *d;
    struct page_info *page;
    unsigned int page_num_node[MAX_NUMNODES] = {0};
    const struct vnuma_info *vnuma;

    printk("'%c' pressed -> dumping numa info (now-0x%X:%08X)\n", key,
           (u32)(now>>32), (u32)now);

    for_each_online_node ( i )
    {
        paddr_t pa = (paddr_t)(NODE_DATA(i)->node_start_pfn + 1)<< PAGE_SHIFT;
        printk("idx%d -> NODE%d start->%lu size->%lu free->%lu\n",
               i, NODE_DATA(i)->node_id,
               NODE_DATA(i)->node_start_pfn,
               NODE_DATA(i)->node_spanned_pages,
               avail_node_heap_pages(i));
        /* sanity check phys_to_nid() */
        printk("phys_to_nid(%"PRIpaddr") -> %d should be %d\n", pa,
               phys_to_nid(pa),
               NODE_DATA(i)->node_id);
    }

    j = cpumask_first(&cpu_online_map);
    n = 0;
    for_each_online_cpu ( i )
    {
        if ( i != j + n || cpu_to_node[j] != cpu_to_node[i] )
        {
            if ( n > 1 )
                printk("CPU%u...%u -> NODE%d\n", j, j + n - 1, cpu_to_node[j]);
            else
                printk("CPU%u -> NODE%d\n", j, cpu_to_node[j]);
            j = i;
            n = 1;
        }
        else
            ++n;
    }
    if ( n > 1 )
        printk("CPU%u...%u -> NODE%d\n", j, j + n - 1, cpu_to_node[j]);
    else
        printk("CPU%u -> NODE%d\n", j, cpu_to_node[j]);

    rcu_read_lock(&domlist_read_lock);

    printk("Memory location of each domain:\n");
    for_each_domain ( d )
    {
        process_pending_softirqs();
        printk("Domain %u (total: %u):\n", d->domain_id, d->tot_pages);

        for_each_online_node ( i )
            page_num_node[i] = 0;

        spin_lock(&d->page_alloc_lock);
        page_list_for_each(page, &d->page_list)
        {
            i = phys_to_nid((paddr_t)page_to_mfn(page) << PAGE_SHIFT);
            page_num_node[i]++;
        }
        spin_unlock(&d->page_alloc_lock);

        for_each_online_node ( i )
            printk("    Node %u: %u\n", i, page_num_node[i]);

        if ( !read_trylock(&d->vnuma_rwlock) )
            continue;

        if ( !d->vnuma )
        {
            read_unlock(&d->vnuma_rwlock);
            continue;
        }

        vnuma = d->vnuma;
        printk("     %u vnodes, %u vcpus, guest physical layout:\n",
               vnuma->nr_vnodes, d->max_vcpus);
        for ( i = 0; i < vnuma->nr_vnodes; i++ )
        {
            unsigned int start_cpu = ~0U;

            err = snprintf(keyhandler_scratch, 12, "%3u",
                    vnuma->vnode_to_pnode[i]);
            if ( err < 0 || vnuma->vnode_to_pnode[i] == NUMA_NO_NODE )
                strlcpy(keyhandler_scratch, "???", sizeof(keyhandler_scratch));

            printk("       %3u: pnode %s,", i, keyhandler_scratch);

            printk(" vcpus ");

            for ( j = 0; j < d->max_vcpus; j++ )
            {
                if ( !(j & 0x3f) )
                    process_pending_softirqs();

                if ( vnuma->vcpu_to_vnode[j] == i )
                {
                    if ( start_cpu == ~0U )
                    {
                        printk("%d", j);
                        start_cpu = j;
                    }
                }
                else if ( start_cpu != ~0U )
                {
                    if ( j - 1 != start_cpu )
                        printk("-%d ", j - 1);
                    else
                        printk(" ");
                    start_cpu = ~0U;
                }
            }

            if ( start_cpu != ~0U  && start_cpu != j - 1 )
                printk("-%d", j - 1);

            printk("\n");

            for ( j = 0; j < vnuma->nr_vmemranges; j++ )
            {
                if ( vnuma->vmemrange[j].nid == i )
                    printk("           %016"PRIx64" - %016"PRIx64"\n",
                           vnuma->vmemrange[j].start,
                           vnuma->vmemrange[j].end);
            }
        }

        read_unlock(&d->vnuma_rwlock);
    }

    rcu_read_unlock(&domlist_read_lock);
}

static __init int register_numa_trigger(void)
{
    register_keyhandler('u', dump_numa, "dump NUMA info", 1);
    return 0;
}
__initcall(register_numa_trigger);


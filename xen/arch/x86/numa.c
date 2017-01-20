/* 
 * Generic VM initialization for x86-64 NUMA setups.
 * Copyright 2002,2003 Andi Kleen, SuSE Labs.
 * Adapted for Xen: Ryan Harper <ryanh@us.ibm.com>
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
#include <xen/srat.h>
#include <asm/acpi.h>
#include <xen/sched.h>
#include <xen/softirq.h>

#ifndef Dprintk
#define Dprintk(x...)
#endif

/*
 * Keep BIOS's CPU2node information, should not be used for memory allocaion
 */
nodeid_t apicid_to_node[MAX_LOCAL_APIC] = {
    [0 ... MAX_LOCAL_APIC-1] = NUMA_NO_NODE
};

nodemask_t __read_mostly node_online_map = { { [0] = 1UL } };

s8 acpi_numa = 0;

int srat_disabled(void)
{
    return numa_off || acpi_numa < 0;
}

void __init numa_init_array(void)
{
    int rr, i;

    /* There are unfortunately some poorly designed mainboards around
       that only connect memory to a single CPU. This breaks the 1:1 cpu->node
       mapping. To avoid this fill in the mapping for all possible
       CPUs, as the number of CPUs is not known yet.
       We round robin the existing nodes. */
    rr = first_node(node_online_map);
    for ( i = 0; i < nr_cpu_ids; i++ )
    {
        if ( cpu_to_node[i] != NUMA_NO_NODE )
            continue;
        numa_set_node(i, rr);
        rr = next_node(rr, node_online_map);
        if ( rr == MAX_NUMNODES )
            rr = first_node(node_online_map);
    }
}

#ifdef CONFIG_NUMA_EMU
static int numa_fake __initdata = 0;

/* Numa emulation */
static int __init numa_emulation(u64 start_pfn, u64 end_pfn)
{
    int i;
    struct node nodes[MAX_NUMNODES];
    u64 sz = ((end_pfn - start_pfn)<<PAGE_SHIFT) / numa_fake;

    /* Kludge needed for the hash function */
    if ( hweight64(sz) > 1 )
    {
        u64 x = 1;
        while ( (x << 1) < sz )
            x <<= 1;
        if ( x < sz/2 )
            printk(KERN_ERR "Numa emulation unbalanced. Complain to maintainer\n");
        sz = x;
    }

    memset(&nodes,0,sizeof(nodes));
    for ( i = 0; i < numa_fake; i++ )
    {
        nodes[i].start = (start_pfn<<PAGE_SHIFT) + i*sz;
        if ( i == numa_fake - 1 )
            sz = (end_pfn<<PAGE_SHIFT) - nodes[i].start;
        nodes[i].end = nodes[i].start + sz;
        printk(KERN_INFO "Faking node %d at %"PRIx64"-%"PRIx64" (%"PRIu64"MB)\n",
               i,
               nodes[i].start, nodes[i].end,
               (nodes[i].end - nodes[i].start) >> 20);
        node_set_online(i);
    }
    memnode_shift = compute_hash_shift(nodes, numa_fake, NULL);
    if ( memnode_shift < 0 )
    {
        memnode_shift = 0;
        printk(KERN_ERR "No NUMA hash function found. Emulation disabled.\n");
        return -1;
    }
    for_each_online_node ( i )
        setup_node_bootmem(i, nodes[i].start, nodes[i].end);
    numa_init_array();

    return 0;
}
#endif

void __init numa_initmem_init(unsigned long start_pfn, unsigned long end_pfn)
{ 
    int i;

#ifdef CONFIG_NUMA_EMU
    if ( numa_fake && !numa_emulation(start_pfn, end_pfn) )
        return;
#endif

#ifdef CONFIG_ACPI_NUMA
    if ( !numa_off && !acpi_scan_nodes((u64)start_pfn << PAGE_SHIFT,
         (u64)end_pfn << PAGE_SHIFT) )
        return;
#endif

    printk(KERN_INFO "%s\n",
           numa_off ? "NUMA turned off" : "No NUMA configuration found");

    printk(KERN_INFO "Faking a node at %016"PRIx64"-%016"PRIx64"\n",
           (u64)start_pfn << PAGE_SHIFT,
           (u64)end_pfn << PAGE_SHIFT);
    /* setup dummy node covering all memory */
    memnode_shift = BITS_PER_LONG - 1;
    memnodemap = _memnodemap;
    nodes_clear(node_online_map);
    node_set_online(0);
    for ( i = 0; i < nr_cpu_ids; i++ )
        numa_set_node(i, 0);
    cpumask_copy(&node_to_cpumask[0], cpumask_of(0));
    setup_node_bootmem(0, (u64)start_pfn << PAGE_SHIFT,
                    (u64)end_pfn << PAGE_SHIFT);
}

int __init arch_numa_setup(char *opt)
{ 
#ifdef CONFIG_NUMA_EMU
    if ( !strncmp(opt, "fake=", 5) )
    {
        numa_off = 0;
        numa_fake = simple_strtoul(opt+5,NULL,0);
        if ( numa_fake >= MAX_NUMNODES )
            numa_fake = MAX_NUMNODES;
    }
#endif
#ifdef CONFIG_ACPI_NUMA
    if ( !strncmp(opt,"noacpi",6) )
    {
        numa_off = 0;
        acpi_numa = -1;
    }
#endif

    return 1;
} 

/*
 * Setup early cpu_to_node.
 *
 * Populate cpu_to_node[] only if x86_cpu_to_apicid[],
 * and apicid_to_node[] tables have valid entries for a CPU.
 * This means we skip cpu_to_node[] initialisation for NUMA
 * emulation and faking node case (when running a kernel compiled
 * for NUMA on a non NUMA box), which is OK as cpu_to_node[]
 * is already initialized in a round robin manner at numa_init_array,
 * prior to this call, and this initialization is good enough
 * for the fake NUMA cases.
 */
void __init init_cpu_to_node(void)
{
    unsigned int i;
    nodeid_t node;

    for ( i = 0; i < nr_cpu_ids; i++ )
    {
        u32 apicid = x86_cpu_to_apicid[i];
        if ( apicid == BAD_APICID )
            continue;
        node = apicid < MAX_LOCAL_APIC ? apicid_to_node[apicid] : NUMA_NO_NODE;
        if ( node == NUMA_NO_NODE || !node_online(node) )
            node = 0;
        numa_set_node(i, node);
    }
}

unsigned int __init arch_get_dma_bitsize(void)
{
    unsigned int node;

    for_each_online_node(node)
        if ( node_spanned_pages(node) &&
             !(node_start_pfn(node) >> (32 - PAGE_SHIFT)) )
            break;
    if ( node >= MAX_NUMNODES )
        panic("No node with memory below 4Gb");

    /*
     * Try to not reserve the whole node's memory for DMA, but dividing
     * its spanned pages by (arbitrarily chosen) 4.
     */
    return min_t(unsigned int,
                 flsl(node_start_pfn(node) + node_spanned_pages(node) / 4 - 1)
                 + PAGE_SHIFT, 32);
}

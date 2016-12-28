/*
 * ARM NUMA Implementation
 *
 * Copyright (C) 2016 - Cavium Inc.
 * Vijaya Kumar K <vijaya.kumar@cavium.com>
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
 */

#include <xen/init.h>
#include <xen/ctype.h>
#include <xen/mm.h>
#include <xen/nodemask.h>
#include <xen/pfn.h>
#include <asm/mm.h>
#include <xen/numa.h>
#include <asm/acpi.h>
#include <xen/errno.h>
#include <xen/cpumask.h>
#include <asm/setup.h>

int _node_distance[MAX_NUMNODES * 2];
int *node_distance;
extern nodemask_t numa_nodes_parsed;
extern struct node nodes[MAX_NUMNODES] __initdata;
extern int num_node_memblks;
extern struct node node_memblk_range[NR_NODE_MEMBLKS];
extern nodeid_t memblk_nodeid[NR_NODE_MEMBLKS];

void __init numa_set_cpu_node(int cpu, unsigned long hwid)
{
    unsigned node;

    node = hwid >> 16 & 0xf;
    if ( !node_isset(node, numa_nodes_parsed) || node == MAX_NUMNODES )
        node = 0;

    numa_set_node(cpu, node);
    numa_add_cpu(cpu);
}

u8 __node_distance(nodeid_t a, nodeid_t b)
{
    if ( !node_distance )
        return a == b ? 10 : 20;

    return _node_distance[a * MAX_NUMNODES + b];
}

EXPORT_SYMBOL(__node_distance);

static int __init numa_mem_init(void)
{
    nodemask_t memory_nodes_parsed;
    int bank, nodeid;
    struct node *nd;
    paddr_t start, size, end;

    nodes_clear(memory_nodes_parsed);
    for ( bank = 0 ; bank < bootinfo.mem.nr_banks; bank++ )
    {
        start = bootinfo.mem.bank[bank].start;
        size = bootinfo.mem.bank[bank].size;
        end = start + size;

        nodeid = get_numa_node(start, end);
        if ( nodeid == -EINVAL || nodeid > MAX_NUMNODES )
        {
            printk(XENLOG_WARNING
                   "NUMA: node for mem bank start 0x%lx - 0x%lx not found\n",
                   start, end);

            return -EINVAL;
        }

        nd = &nodes[nodeid];
        if ( !node_test_and_set(nodeid, memory_nodes_parsed) )
        {
            nd->start = start;
            nd->end = end;
        }
        else
        {
            if ( start < nd->start )
                nd->start = start;
            if ( nd->end < end )
                nd->end = end;
        }
    }

    return 0;
}

/* Use the information discovered above to actually set up the nodes. */
static int __init numa_scan_mem_nodes(void)
{
    int i;

    memnode_shift = compute_hash_shift(node_memblk_range, num_node_memblks,
                                       memblk_nodeid);
    if ( memnode_shift < 0 )
    {
        printk(XENLOG_WARNING "No NUMA hash found.\n");
        memnode_shift = 0;
    }

    for_each_node_mask(i, numa_nodes_parsed)
    {
        u64 size = node_memblk_range[i].end - node_memblk_range[i].start;

        if ( size == 0 )
            printk(XENLOG_WARNING "NUMA: Node %u has no memory. \n", i);

        printk(XENLOG_INFO
               "NUMA: NODE[%d]: Start 0x%lx End 0x%lx\n",
               i, nodes[i].start, nodes[i].end);
        setup_node_bootmem(i, nodes[i].start, nodes[i].end);
    }

    return 0;
}

static void __init numa_dummy_init(unsigned long start_pfn,
                                   unsigned long end_pfn)
{
    int i;

    nodes_clear(numa_nodes_parsed);
    memnode_shift = BITS_PER_LONG - 1;
    memnodemap = _memnodemap;
    nodes_clear(node_online_map);
    node_set_online(0);

    for ( i = 0; i < NR_CPUS; i++ )
        numa_set_node(i, 0);

    node_distance = NULL;
    for ( i = 0; i < MAX_NUMNODES * 2; i++ )
        _node_distance[i] = 0;

    cpumask_copy(&node_to_cpumask[0], cpumask_of(0));
    setup_node_bootmem(0, (u64)start_pfn << PAGE_SHIFT,
                       (u64)end_pfn << PAGE_SHIFT);
}

static int __init numa_initmem_init(void)
{
    if ( !numa_mem_init() )
    {
        if ( !numa_scan_mem_nodes() )
            return 0;
    }

    return -EINVAL;
}

/*
 * Setup early cpu_to_node.
 */
void __init init_cpu_to_node(void)
{
    int i;

    for ( i = 0; i < nr_cpu_ids; i++ )
        numa_set_node(i, 0);
}

int __init numa_init(void)
{
    int i, bank, ret = 0;
    paddr_t ram_start = ~0;
    paddr_t ram_end = 0;

    if ( numa_off )
        goto no_numa;

    for ( i = 0; i < MAX_NUMNODES * 2; i++ )
        _node_distance[i] = 0;

    ret = dt_numa_init();

    if ( !ret )
        ret = numa_initmem_init();

    if ( !ret )
        return 0;

no_numa:
    for ( bank = 0 ; bank < bootinfo.mem.nr_banks; bank++ )
    {
        paddr_t bank_start = bootinfo.mem.bank[bank].start;
        paddr_t bank_end = bank_start + bootinfo.mem.bank[bank].size;

        ram_start = min(ram_start, bank_start);
        ram_end = max(ram_end, bank_end);
    }

    printk(XENLOG_INFO "%s\n",
           numa_off ? "NUMA turned off" : "No NUMA configuration found");

    printk(XENLOG_INFO "Faking a node at %016"PRIx64"-%016"PRIx64"\n",
           (u64)ram_start, (u64)ram_end);

    numa_dummy_init(PFN_UP(ram_start),PFN_DOWN(ram_end));

    return 0;
}

int __init arch_numa_setup(char *opt)
{
    return 1;
}

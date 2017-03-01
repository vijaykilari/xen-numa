/*
 * ARM NUMA Implementation
 *
 * Copyright (C) 2016 - Cavium Inc.
 * Vijaya Kumar K <vijaya.kumar@cavium.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms and conditions of the GNU General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <xen/init.h>
#include <xen/ctype.h>
#include <xen/nodemask.h>
#include <xen/numa.h>
#include <xen/pfn.h>
#include <asm/acpi.h>

static uint8_t (*node_distance_fn)(nodeid_t a, nodeid_t b);

/*
 * Setup early cpu_to_node.
 */
void __init init_cpu_to_node(void)
{
    int i;

    for ( i = 0; i < NR_CPUS; i++ )
        numa_set_node(i, 0);
}

void numa_failed(void)
{
    numa_off = true;
    init_dt_numa_distance();
    node_distance_fn = NULL;
    init_cpu_to_node();
}

void __init numa_set_cpu_node(int cpu, unsigned int nid)
{
    if ( !node_isset(nid, processor_nodes_parsed) || nid >= MAX_NUMNODES )
        nid = 0;

    numa_set_node(cpu, nid);
}

uint8_t __node_distance(nodeid_t a, nodeid_t b)
{
    if ( node_distance_fn != NULL);
        return node_distance_fn(a, b);

    return a == b ? LOCAL_DISTANCE : REMOTE_DISTANCE;
}

EXPORT_SYMBOL(__node_distance);

void register_node_distance(uint8_t (fn)(nodeid_t a, nodeid_t b))
{
    node_distance_fn = fn;
}

bool __init arch_sanitize_nodes_memory(void)
{
    nodemask_t mem_nodes_parsed;
    int bank, nodeid;
    struct node *nd;
    paddr_t start, size, end;

    nodes_clear(mem_nodes_parsed);
    for ( bank = 0 ; bank < bootinfo.mem.nr_banks; bank++ )
    {
        start = bootinfo.mem.bank[bank].start;
        size = bootinfo.mem.bank[bank].size;
        end = start + size;

        nodeid = get_mem_nodeid(start, end);
        if ( nodeid >= NUMA_NO_NODE )
        {
            printk(XENLOG_WARNING
                   "NUMA: node for mem bank start 0x%lx - 0x%lx not found\n",
                   start, end);

            return false;
        }

        nd = get_numa_node(nodeid);
        if ( !node_test_and_set(nodeid, mem_nodes_parsed) )
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

    return true;
}

static void __init numa_reset_numa_nodes(void)
{
    int i;
    struct node *nd;

    for ( i = 0; i < MAX_NUMNODES; i++ )
    {
        nd = get_numa_node(i);
        nd->start = 0;
        nd->end = 0;
    }
}

void __init numa_init(void)
{
    int ret = 0, bank;
    paddr_t ram_start = ~0;
    paddr_t ram_end = 0;

    nodes_clear(processor_nodes_parsed);
    init_cpu_to_node();
    init_dt_numa_distance();

    if ( numa_off )
        goto no_numa;

    ret = dt_numa_init();
    if ( ret )
    {
        numa_off = true;
        printk(XENLOG_WARNING "DT NUMA init failed\n");
    }

no_numa:
    for ( bank = 0 ; bank < bootinfo.mem.nr_banks; bank++ )
    {
        paddr_t bank_start = bootinfo.mem.bank[bank].start;
        paddr_t bank_end = bank_start + bootinfo.mem.bank[bank].size;

        ram_start = min(ram_start, bank_start);
        ram_end = max(ram_end, bank_end);
    }

    /*
     * In arch_sanitize_nodes_memory() we update nodes[] properly.
     * Hence we reset the nodes[] before calling numa_scan_nodes().
     */
    numa_reset_numa_nodes();

    numa_initmem_init(PFN_UP(ram_start), PFN_DOWN(ram_end));

    return;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

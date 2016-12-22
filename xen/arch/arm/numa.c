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
#include <asm/mm.h>
#include <xen/numa.h>
#include <asm/acpi.h>
#include <xen/errno.h>
#include <xen/cpumask.h>

int _node_distance[MAX_NUMNODES * 2];
int *node_distance;
extern nodemask_t numa_nodes_parsed;

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
    int i, ret = 0;

    if ( numa_off )
        goto no_numa;

    for ( i = 0; i < MAX_NUMNODES * 2; i++ )
        _node_distance[i] = 0;

    ret = dt_numa_init();

no_numa:
    return ret;
}

int __init arch_numa_setup(char *opt)
{
    return 1;
}

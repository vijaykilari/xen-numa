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
#include <asm/acpi.h>

static uint8_t (*node_distance_fn)(nodeid_t a, nodeid_t b);

void numa_failed(void)
{
    numa_off = true;
    init_dt_numa_distance();
    node_distance_fn = NULL;
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

void __init numa_init(void)
{
    int ret = 0;

    nodes_clear(processor_nodes_parsed);
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

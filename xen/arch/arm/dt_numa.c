/*
 * OF NUMA Parsing support.
 *
 * Copyright (C) 2015 - 2016 Cavium Inc.
 *
 * Some code extracts are taken from linux drivers/of/of_numa.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <xen/config.h>
#include <xen/device_tree.h>
#include <xen/libfdt/libfdt.h>
#include <xen/mm.h>
#include <xen/nodemask.h>
#include <asm/mm.h>
#include <xen/numa.h>

nodemask_t numa_nodes_parsed;
extern struct node node_memblk_range[NR_NODE_MEMBLKS];

/*
 * Even though we connect cpus to numa domains later in SMP
 * init, we need to know the node ids now for all cpus.
*/
static int __init dt_numa_process_cpu_node(const void *fdt, int node,
                                           const char *name,
                                           u32 address_cells, u32 size_cells)
{
    u32 nid;

    nid = device_tree_get_u32(fdt, node, "numa-node-id", MAX_NUMNODES);

    if ( nid >= MAX_NUMNODES )
        printk(XENLOG_WARNING "NUMA: Node id %u exceeds maximum value\n", nid);
    else
        node_set(nid, numa_nodes_parsed);

    return 0;
}

static int __init dt_numa_process_memory_node(const void *fdt, int node,
                                              const char *name,
                                              u32 address_cells,
                                              u32 size_cells)
{
    const struct fdt_property *prop;
    int i, ret, banks;
    const __be32 *cell;
    paddr_t start, size;
    u32 reg_cells = address_cells + size_cells;
    u32 nid;

    if ( address_cells < 1 || size_cells < 1 )
    {
        printk(XENLOG_WARNING
               "fdt: node `%s': invalid #address-cells or #size-cells", name);
        return -EINVAL;
    }

    nid = device_tree_get_u32(fdt, node, "numa-node-id", MAX_NUMNODES);
    if ( nid >= MAX_NUMNODES) {
        /*
         * No node id found. Skip this memory node.
         */
        return 0;
    }

    prop = fdt_get_property(fdt, node, "reg", NULL);
    if ( !prop )
    {
        printk(XENLOG_WARNING "fdt: node `%s': missing `reg' property\n",
               name);
        return -EINVAL;
    }

    cell = (const __be32 *)prop->data;
    banks = fdt32_to_cpu(prop->len) / (reg_cells * sizeof (u32));

    for ( i = 0; i < banks; i++ )
    {
        device_tree_get_reg(&cell, address_cells, size_cells, &start, &size);
        if ( !size )
            continue;

        /* It is fine to add this area to the nodes data it will be used later*/
        ret = conflicting_memblks(start, start + size);
        if (ret < 0)
             numa_add_memblk(nid, start, size);
        else
        {
             printk(XENLOG_ERR
                    "NUMA DT: node %u (%"PRIx64"-%"PRIx64") overlaps with ret %d (%"PRIx64"-%"PRIx64")\n",
                    nid, start, start + size, ret,
                    node_memblk_range[i].start, node_memblk_range[i].end);
             return -EINVAL;
        }
    }

    node_set(nid, numa_nodes_parsed);

    return 0;
}

static int __init dt_numa_scan_cpu_node(const void *fdt, int node,
                                        const char *name, int depth,
                                        u32 address_cells, u32 size_cells,
                                        void *data)
{
    if ( device_tree_node_matches(fdt, node, "cpu") )
        return dt_numa_process_cpu_node(fdt, node, name, address_cells,
                                        size_cells);

    return 0;
}

static int __init dt_numa_scan_memory_node(const void *fdt, int node,
                                           const char *name, int depth,
                                           u32 address_cells, u32 size_cells,
                                           void *data)
{
    if ( device_tree_node_matches(fdt, node, "memory") )
        return dt_numa_process_memory_node(fdt, node, name, address_cells,
                                           size_cells);

    return 0;
}

int __init dt_numa_init(void)
{
    int ret;

    nodes_clear(numa_nodes_parsed);
    ret = device_tree_for_each_node((void *)device_tree_flattened,
                                    dt_numa_scan_cpu_node, NULL);

    if ( ret )
        return ret;

    ret = device_tree_for_each_node((void *)device_tree_flattened,
                                    dt_numa_scan_memory_node, NULL);

    return ret;
}

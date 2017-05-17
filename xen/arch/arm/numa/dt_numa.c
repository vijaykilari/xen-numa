/*
 * OF NUMA Parsing support.
 *
 * Copyright (C) 2015 - 2016 Cavium Inc.
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

#include <xen/mm.h>
#include <xen/nodemask.h>
#include <xen/libfdt/libfdt.h>
#include <xen/device_tree.h>
#include <xen/numa.h>
#include <asm/setup.h>

/*
 * Even though we connect cpus to numa domains later in SMP
 * init, we need to know the node ids now for all cpus.
 */
static int __init dt_numa_process_cpu_node(const void *fdt)
{
    int node, offset;
    uint32_t nid;

    offset = fdt_path_offset(fdt, "/cpus");
    if ( offset < 0 )
        return -EINVAL;

    node = fdt_first_subnode(fdt, offset);
    if ( node == -FDT_ERR_NOTFOUND )
        return -EINVAL;

    do {
        if ( device_tree_type_matches(fdt, node, "cpu") )
        {
            nid = device_tree_get_u32(fdt, node, "numa-node-id", MAX_NUMNODES);
            if ( nid >= MAX_NUMNODES )
                printk(XENLOG_WARNING
                       "NUMA: Node id %u exceeds maximum value\n", nid);
            else
                node_set(nid, processor_nodes_parsed);
        }

        offset = node;
        node = fdt_next_subnode(fdt, offset);
    } while (node != -FDT_ERR_NOTFOUND);

    return 0;
}

int __init dt_numa_init(void)
{
    int ret;

    ret = dt_numa_process_cpu_node((void *)device_tree_flattened);

    return ret;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

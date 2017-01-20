/*
 * ACPI based NUMA setup
 *
 * Copyright (C) 2016 - Cavium Inc.
 * Vijaya Kumar K <Vijaya.Kumar@cavium.com>
 *
 * Reads the ACPI MADT and SRAT table to setup NUMA information.
 *
 * Contains Excerpts from x86 implementation
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
#include <xen/mm.h>
#include <xen/inttypes.h>
#include <xen/nodemask.h>
#include <xen/acpi.h>
#include <xen/numa.h>
#include <xen/pfn.h>
#include <xen/srat.h>
#include <asm/page.h>
#include <asm/acpi.h>

extern int num_node_memblks;
extern struct node node_memblk_range[NR_NODE_MEMBLKS];
extern nodeid_t memblk_nodeid[NR_NODE_MEMBLKS];
extern nodemask_t numa_nodes_parsed;
struct uid_to_mpidr {
    u32 uid;
    u64 mpidr;
};

/* Holds mapping of CPU id to MPIDR read from MADT */
static struct uid_to_mpidr cpu_uid_to_hwid[NR_CPUS] __read_mostly;
static __initdata DECLARE_BITMAP(memblk_hotplug, NR_NODE_MEMBLKS);

static __init void bad_srat(void)
{
    int i;

    printk(KERN_ERR "SRAT: SRAT not used.\n");
    acpi_numa = -1;
    for (i = 0; i < ARRAY_SIZE(pxm2node); i++)
        pxm2node[i].node = NUMA_NO_NODE;
}

static u64 acpi_get_cpu_mpidr(int uid)
{
    int i;

    if ( uid < ARRAY_SIZE(cpu_uid_to_hwid) && cpu_uid_to_hwid[uid].uid == uid &&
         cpu_uid_to_hwid[uid].mpidr != MPIDR_INVALID )
        return cpu_uid_to_hwid[uid].mpidr;

    for ( i = 0; i < NR_CPUS; i++ )
    {
        if ( cpu_uid_to_hwid[i].uid == uid )
            return cpu_uid_to_hwid[i].mpidr;
    }

    return MPIDR_INVALID;
}

static void __init
acpi_map_cpu_to_mpidr(struct acpi_madt_generic_interrupt *processor)
{
    static int cpus = 0;

    u64 mpidr = processor->arm_mpidr & MPIDR_HWID_MASK;

    if ( mpidr == MPIDR_INVALID )
    {
        printk("Skip MADT cpu entry with invalid MPIDR\n");
        bad_srat();
        return;
    }

    cpu_uid_to_hwid[cpus].mpidr = mpidr;
    cpu_uid_to_hwid[cpus].uid = processor->uid;

    cpus++;
}

static int __init acpi_parse_madt_handler(struct acpi_subtable_header *header,
                                          const unsigned long end)
{
    struct acpi_madt_generic_interrupt *p =
               container_of(header, struct acpi_madt_generic_interrupt, header);

    if ( BAD_MADT_ENTRY(p, end) )
    {
        /* Though MADT is invalid, we disable NUMA by calling bad_srat() */
        bad_srat();
        return -EINVAL;
    }

    acpi_table_print_madt_entry(header);
    acpi_map_cpu_to_mpidr(p);

    return 0;
}

/* Callback for Proximity Domain -> ACPI processor UID mapping */
void __init acpi_numa_gicc_affinity_init(const struct acpi_srat_gicc_affinity *pa)
{
    int pxm, node;
    u64 mpidr = 0;
    static u32 cpus_in_srat;

    if ( srat_disabled() )
        return;

    if ( pa->header.length < sizeof(struct acpi_srat_gicc_affinity) )
    {
        printk(XENLOG_WARNING "SRAT: Invalid SRAT header length: %d\n",
               pa->header.length);
        bad_srat();
        return;
    }

    if ( !(pa->flags & ACPI_SRAT_GICC_ENABLED) )
        return;

    if ( cpus_in_srat >= NR_CPUS )
    {
        printk(XENLOG_WARNING
               "SRAT: cpu_to_node_map[%d] is too small to fit all cpus\n",
               NR_CPUS);
        return;
    }

    pxm = pa->proximity_domain;
    node = setup_node(pxm);
    if ( node == NUMA_NO_NODE || node >= MAX_NUMNODES )
    {
        printk(XENLOG_WARNING "SRAT: Too many proximity domains %d\n", pxm);
        bad_srat();
        return;
    }

    mpidr = acpi_get_cpu_mpidr(pa->acpi_processor_uid);
    if ( mpidr == MPIDR_INVALID )
    {
        printk(XENLOG_WARNING
               "SRAT: PXM %d with ACPI ID %d has no valid MPIDR in MADT\n",
               pxm, pa->acpi_processor_uid);
        bad_srat();
        return;
    }

    node_set(node, numa_nodes_parsed);
    cpus_in_srat++;
    acpi_numa = 1;
    printk(XENLOG_INFO "SRAT: PXM %d -> MPIDR 0x%lx -> Node %d\n",
           pxm, mpidr, node);
}

/* Callback for parsing of the Proximity Domain <-> Memory Area mappings */
void __init
acpi_numa_memory_affinity_init(const struct acpi_srat_mem_affinity *ma)
{
    u64 start, end;
    unsigned pxm;
    nodeid_t node;
    int i;

    if ( srat_disabled() )
        return;

    if ( ma->header.length != sizeof(struct acpi_srat_mem_affinity) )
    {
        bad_srat();
        return;
    }
    if ( !(ma->flags & ACPI_SRAT_MEM_ENABLED) )
        return;

    if ( num_node_memblks >= NR_NODE_MEMBLKS )
    {
        printk(XENLOG_WARNING
               "Too many numa entry, try bigger NR_NODE_MEMBLKS \n");
        bad_srat();
        return;
    }

    start = ma->base_address;
    end = start + ma->length;
    pxm = ma->proximity_domain;
    node = setup_node(pxm);
    if ( node == NUMA_NO_NODE )
    {
        bad_srat();
        return;
    }
    /* It is fine to add this area to the nodes data it will be used later*/
    i = conflicting_memblks(start, end);
    if ( i < 0 )
        /* everything fine */;
    else if ( memblk_nodeid[i] == node )
    {
        bool_t mismatch = !(ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE) !=
                           !test_bit(i, memblk_hotplug);

        printk(XENLOG_WARNING
               "%sSRAT: PXM %u (%"PRIx64"-%"PRIx64") overlaps with itself (%"PRIx64"-%"PRIx64")\n",
               mismatch ? KERN_ERR : KERN_WARNING, pxm, start, end,
               node_memblk_range[i].start, node_memblk_range[i].end);
        if ( mismatch )
        {
            bad_srat();
            return;
        }
    }
    else
    {
         printk(XENLOG_WARNING
                "SRAT: PXM %u (%"PRIx64"-%"PRIx64") overlaps with PXM %u (%"PRIx64"-%"PRIx64")\n",
                pxm, start, end, node_to_pxm(memblk_nodeid[i]),
                node_memblk_range[i].start, node_memblk_range[i].end);
        bad_srat();
        return;
    }

    printk(XENLOG_INFO "SRAT: Node %u PXM %u %"PRIx64"-%"PRIx64"%s\n",
           node, pxm, start, end,
           ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE ? " (hotplug)" : "");

    numa_add_memblk(node, start, ma->length);
    node_set(node, numa_nodes_parsed);
    if (ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE)
        __set_bit(num_node_memblks, memblk_hotplug);
}

void __init acpi_map_uid_to_mpidr(void)
{
    int i;

    for ( i  = 0; i < NR_CPUS; i++ )
    {
        cpu_uid_to_hwid[i].mpidr = MPIDR_INVALID;
        cpu_uid_to_hwid[i].uid = -1;
    }

    acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
                    acpi_parse_madt_handler, 0);
}

void __init acpi_numa_arch_fixup(void) {}

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

extern nodemask_t numa_nodes_parsed;
struct uid_to_mpidr {
    u32 uid;
    u64 mpidr;
};

/* Holds mapping of CPU id to MPIDR read from MADT */
static struct uid_to_mpidr cpu_uid_to_hwid[NR_CPUS] __read_mostly;

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

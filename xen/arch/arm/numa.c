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

int __init numa_init(void)
{
    int ret = 0;

    if ( numa_off )
        goto no_numa;

    ret = dt_numa_init();

no_numa:
    return ret;
}

int __init arch_numa_setup(char *opt)
{
    return 1;
}

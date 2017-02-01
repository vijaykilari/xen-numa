/*
 * ACPI 3.0 based NUMA setup
 * Copyright 2004 Andi Kleen, SuSE Labs.
 *
 * Reads the ACPI SRAT table to figure out what memory belongs to which CPUs.
 *
 * Called from acpi_numa_init while reading the SRAT and SLIT tables.
 * Assumes all memory regions belonging to a single proximity domain
 * are in one chunk. Holes between them will be included in the node.
 *
 * Adapted for Xen: Ryan Harper <ryanh@us.ibm.com>
 *
 * Moved this generic code from xen/arch/x86/srat.c for other arch usage
 * by Vijaya Kumar K <Vijaya.Kumar@cavium.com>
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

struct acpi_table_slit *__read_mostly acpi_slit;
extern struct node nodes[MAX_NUMNODES] __initdata;

struct pxm2node __read_mostly pxm2node[MAX_NUMNODES] =
    { [0 ... MAX_NUMNODES - 1] = {.node = NUMA_NO_NODE} };

s8 acpi_numa = 0;

int srat_disabled(void)
{
    return numa_off || acpi_numa < 0;
}

static inline bool_t node_found(unsigned idx, unsigned pxm)
{
    return ((pxm2node[idx].pxm == pxm) &&
        (pxm2node[idx].node != NUMA_NO_NODE));
}

nodeid_t pxm_to_node(unsigned pxm)
{
    unsigned i;

    if ( (pxm < ARRAY_SIZE(pxm2node)) && node_found(pxm, pxm) )
        return pxm2node[pxm].node;

    for ( i = 0; i < ARRAY_SIZE(pxm2node); i++ )
        if ( node_found(i, pxm) )
            return pxm2node[i].node;

    return NUMA_NO_NODE;
}

nodeid_t setup_node(unsigned pxm)
{
    nodeid_t node;
    unsigned idx;
    static bool_t warned;
    static unsigned nodes_found;

    BUILD_BUG_ON(MAX_NUMNODES >= NUMA_NO_NODE);

    if ( pxm < ARRAY_SIZE(pxm2node) ) {
        if (node_found(pxm, pxm))
            return pxm2node[pxm].node;

        /* Try to maintain indexing of pxm2node by pxm */
        if ( pxm2node[pxm].node == NUMA_NO_NODE ) {
            idx = pxm;
            goto finish;
        }
    }

    for ( idx = 0; idx < ARRAY_SIZE(pxm2node); idx++ )
        if ( pxm2node[idx].node == NUMA_NO_NODE )
            goto finish;

    if ( !warned ) {
        printk(KERN_WARNING "SRAT: Too many proximity domains (%#x)\n",
               pxm);
        warned = 1;
    }

    return NUMA_NO_NODE;

 finish:
    node = nodes_found++;
    if (node >= MAX_NUMNODES)
        return NUMA_NO_NODE;
    pxm2node[idx].pxm = pxm;
    pxm2node[idx].node = node;

    return node;
}

/*
 * A lot of BIOS fill in 10 (= no distance) everywhere. This messes
 * up the NUMA heuristics which wants the local node to have a smaller
 * distance than the others.
 * Do some quick checks here and only use the SLIT if it passes.
 */
static __init int slit_valid(struct acpi_table_slit *slit)
{
    int i, j;
    int d = slit->locality_count;

    for ( i = 0; i < d; i++ ) {
        for ( j = 0; j < d; j++ )  {
            u8 val = slit->entry[d*i + j];
            if ( i == j ) {
                if (val != 10)
                    return 0;
            } else if ( val <= 10 )
                return 0;
        }
    }

    return 1;
}

/* Callback for SLIT parsing */
void __init acpi_numa_slit_init(struct acpi_table_slit *slit)
{
    unsigned long mfn;

    if ( !slit_valid(slit) ) {
        printk(KERN_INFO "ACPI: SLIT table looks invalid. "
               "Not used.\n");
        return;
    }
    mfn = alloc_boot_pages(PFN_UP(slit->header.length), 1);
    if ( !mfn ) {
        printk(KERN_ERR "ACPI: Unable to allocate memory for "
               "saving ACPI SLIT numa information.\n");
        return;
    }
    acpi_slit = mfn_to_virt(mfn);
    memcpy(acpi_slit, slit, slit->header.length);
}

unsigned node_to_pxm(nodeid_t n)
{
    unsigned i;

    if ( (n < ARRAY_SIZE(pxm2node)) && (pxm2node[n].node == n) )
        return pxm2node[n].pxm;
    for ( i = 0; i < ARRAY_SIZE(pxm2node); i++ )
        if ( pxm2node[i].node == n )
            return pxm2node[i].pxm;
    return 0;
}

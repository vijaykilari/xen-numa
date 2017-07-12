#ifndef _ASM_X8664_NUMA_H
#define _ASM_X8664_NUMA_H 1

#include <xen/cpumask.h>

typedef uint8_t nodeid_t;

#define ZONE_ALIGN (1UL << (MAX_ORDER+PAGE_SHIFT))

extern void srat_detect_node(int cpu);

extern nodeid_t apicid_to_node[];

void srat_parse_regions(paddr_t addr);
unsigned int arch_get_dma_bitsize(void);

#endif

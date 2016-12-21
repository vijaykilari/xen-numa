#ifndef _ASM_X8664_NUMA_H 
#define _ASM_X8664_NUMA_H 1

#include <xen/cpumask.h>

#define NODES_SHIFT 6

typedef u8 nodeid_t;

extern int srat_rev;

extern nodeid_t      cpu_to_node[NR_CPUS];
extern cpumask_t     node_to_cpumask[];

#define cpu_to_node(cpu)		(cpu_to_node[cpu])
#define parent_node(node)		(node)
#define node_to_first_cpu(node)  (__ffs(node_to_cpumask[node]))
#define node_to_cpumask(node)    (node_to_cpumask[node])

extern nodeid_t pxm_to_node(unsigned int pxm);

#define ZONE_ALIGN (1UL << (MAX_ORDER+PAGE_SHIFT))

extern void numa_init_array(void);

extern int srat_disabled(void);
extern void srat_detect_node(int cpu);

extern nodeid_t apicid_to_node[];

void srat_parse_regions(u64 addr);
extern u8 __node_distance(nodeid_t a, nodeid_t b);
unsigned int arch_get_dma_bitsize(void);
int arch_numa_setup(char *opt);

#endif

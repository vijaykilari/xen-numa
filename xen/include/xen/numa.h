#ifndef _XEN_NUMA_H
#define _XEN_NUMA_H

#include <xen/cpumask.h>
#include <xen/mm.h>
#include <asm/numa.h>

#define NUMA_NO_NODE     0xFF
#define NUMA_NO_DISTANCE 0xFF

#define MAX_NUMNODES    NR_NODES
#define NR_NODE_MEMBLKS (MAX_NUMNODES * 2)

struct node {
    paddr_t start;
    paddr_t end;
};

extern nodeid_t      cpu_to_node[NR_CPUS];
extern cpumask_t     node_to_cpumask[];
/* Simple perfect hash to map pdx to node numbers */
extern unsigned int memnode_shift;
extern unsigned long memnodemapsize;
extern uint8_t *memnodemap;
extern bool numa_off;
extern s8 acpi_numa;

void numa_initmem_init(unsigned long start_pfn, unsigned long end_pfn);
int srat_disabled(void);
int valid_numa_range(paddr_t start, paddr_t end, nodeid_t node);

#ifdef CONFIG_NUMA
#define cpu_to_node(cpu)         (cpu_to_node[cpu])
#define parent_node(node)        (node)
#define node_to_first_cpu(node)  (__ffs(node_to_cpumask[node]))
#define node_to_cpumask(node)    (node_to_cpumask[node])

struct node_data {
    unsigned long node_start_pfn;
    unsigned long node_spanned_pages;
};

extern struct node_data node_data[];

static inline __attribute_pure__ nodeid_t phys_to_nid(paddr_t addr)
{
   nodeid_t nid;

   ASSERT((paddr_to_pdx(addr) >> memnode_shift) < memnodemapsize);
   nid = memnodemap[paddr_to_pdx(addr) >> memnode_shift];
   ASSERT(nid <= MAX_NUMNODES || !node_data[nid].node_start_pfn);

   return nid;
}

#define NODE_DATA(nid)          (&(node_data[nid]))

#define node_start_pfn(nid)     NODE_DATA(nid)->node_start_pfn
#define node_spanned_pages(nid) NODE_DATA(nid)->node_spanned_pages
#define node_end_pfn(nid)       NODE_DATA(nid)->node_start_pfn + \
                                 NODE_DATA(nid)->node_spanned_pages

void numa_add_cpu(int cpu);
void numa_set_node(int cpu, nodeid_t node);
int conflicting_memblks(paddr_t start, paddr_t end);
struct node *get_numa_node(unsigned int id);
nodeid_t get_memblk_nodeid(unsigned int memblk);
struct node *get_node_memblk_range(unsigned int memblk);
int numa_add_memblk(nodeid_t nodeid, paddr_t start, uint64_t size);
int get_num_node_memblks(void);
bool arch_sanitize_nodes_memory(void);
void numa_failed(void);
#else
static inline void numa_add_cpu(int cpu) { }
static inline void numa_set_node(int cpu, nodeid_t node) { }
#endif

#define vcpu_to_node(v) (cpu_to_node((v)->processor))

#define domain_to_node(d) \
  (((d)->vcpu != NULL && (d)->vcpu[0] != NULL) \
   ? vcpu_to_node((d)->vcpu[0]) : NUMA_NO_NODE)

#endif /* _XEN_NUMA_H */

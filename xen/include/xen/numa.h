#ifndef _XEN_NUMA_H
#define _XEN_NUMA_H

#include <asm/numa.h>

#ifndef NODES_SHIFT
#define NODES_SHIFT     0
#endif

#define NUMA_NO_NODE     0xFF
#define NUMA_NO_DISTANCE 0xFF

#define MAX_NUMNODES    (1 << NODES_SHIFT)
#define NR_NODE_MEMBLKS (MAX_NUMNODES*2)
#define vcpu_to_node(v) (cpu_to_node((v)->processor))

#define domain_to_node(d) \
  (((d)->vcpu != NULL && (d)->vcpu[0] != NULL) \
   ? vcpu_to_node((d)->vcpu[0]) : NUMA_NO_NODE)

extern bool_t numa_off;
struct node {
	u64 start,end;
};

struct node_data {
    unsigned long node_start_pfn;
    unsigned long node_spanned_pages;
    nodeid_t      node_id;
};

#define NODE_DATA(nid)		(&(node_data[nid]))
#define VIRTUAL_BUG_ON(x)

#ifdef CONFIG_NUMA
extern void init_cpu_to_node(void);

static inline void clear_node_cpumask(int cpu)
{
	cpumask_clear_cpu(cpu, &node_to_cpumask[cpu_to_node(cpu)]);
}

/* Simple perfect hash to map pdx to node numbers */
extern int memnode_shift;
extern unsigned long memnodemapsize;
extern u8 *memnodemap;
extern typeof(*memnodemap) _memnodemap[];

extern struct node_data node_data[];

static inline __attribute__((pure)) nodeid_t phys_to_nid(paddr_t addr)
{
	nodeid_t nid;
	VIRTUAL_BUG_ON((paddr_to_pdx(addr) >> memnode_shift) >= memnodemapsize);
	nid = memnodemap[paddr_to_pdx(addr) >> memnode_shift];
	VIRTUAL_BUG_ON(nid >= MAX_NUMNODES || !node_data[nid]);
	return nid;
}

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_spanned_pages(nid)	(NODE_DATA(nid)->node_spanned_pages)
#define node_end_pfn(nid)       (NODE_DATA(nid)->node_start_pfn + \
				 NODE_DATA(nid)->node_spanned_pages)

#else
#define init_cpu_to_node() do {} while (0)
#define clear_node_cpumask(cpu) do {} while (0)
#endif /* CONFIG_NUMA */

extern void numa_add_memblk(nodeid_t nodeid, u64 start, u64 size);
extern int get_numa_node(u64 start, u64 end);
extern int valid_numa_range(u64 start, u64 end, nodeid_t node);
extern int conflicting_memblks(u64 start, u64 end);
extern void cutoff_node(int i, u64 start, u64 end);
extern void numa_add_cpu(int cpu);
extern void numa_set_node(int cpu, nodeid_t node);
extern void setup_node_bootmem(nodeid_t nodeid, u64 start, u64 end);
extern int compute_hash_shift(struct node *nodes, int numnodes,
			      nodeid_t *nodeids);
#endif /* _XEN_NUMA_H */

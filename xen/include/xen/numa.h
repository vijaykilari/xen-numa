#ifndef _XEN_NUMA_H
#define _XEN_NUMA_H

#include <asm/numa.h>

#define NUMA_NO_NODE     0xFF
#define NUMA_NO_DISTANCE 0xFF

#define MAX_NUMNODES    NR_NODES
#define NR_NODE_MEMBLKS (MAX_NUMNODES * 2)

#define vcpu_to_node(v) (cpu_to_node((v)->processor))

#define domain_to_node(d) \
  (((d)->vcpu != NULL && (d)->vcpu[0] != NULL) \
   ? vcpu_to_node((d)->vcpu[0]) : NUMA_NO_NODE)

#endif /* _XEN_NUMA_H */

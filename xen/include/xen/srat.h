#ifndef __XEN_SRAT_H__
#define __XEN_SRAT_H__

struct pxm2node {
    unsigned pxm;
    nodeid_t node;
};

extern struct pxm2node __read_mostly pxm2node[MAX_NUMNODES];
nodeid_t pxm_to_node(unsigned pxm);
nodeid_t setup_node(unsigned pxm);
unsigned node_to_pxm(nodeid_t n);
#endif /* __XEN_SRAT_H__ */

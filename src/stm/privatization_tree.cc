/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#include "privatization_tree.h"

// this is const, but should be aligned and occupy whole cache lines
CACHE_LINE_ALIGNED unsigned tlstm::PrivatizationTree::map_count_to_root_idx[MAX_THREADS];

CACHE_LINE_ALIGNED tlstm::PaddedWord tlstm::PrivatizationTree::tree_nodes[PRIVATIZATION_TREE_NODE_COUNT];

CACHE_LINE_ALIGNED tlstm::PaddedWord tlstm::PrivatizationTree::proxy_nodes[PRIVATIZATION_TREE_PROXY_COUNT];

// for debug
CACHE_LINE_ALIGNED tlstm::PaddedSpinTryLock tlstm::PrivatizationTree::tree_node_locks[PRIVATIZATION_TREE_NODE_COUNT];
CACHE_LINE_ALIGNED tlstm::PaddedSpinTryLock tlstm::PrivatizationTree::proxy_node_locks[PRIVATIZATION_TREE_PROXY_COUNT];

Word tlstm::PrivatizationTree::last_sibling_ts[MAX_THREADS];
Word tlstm::PrivatizationTree::last_my_ts[MAX_THREADS];
Word tlstm::PrivatizationTree::last_parent_ts[MAX_THREADS];

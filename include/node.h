#ifndef NODE_H
#define NODE_H

#include "pager.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include "main.h"

// Node Types

enum class NodeType: uint8_t{
  INTERNAL = 0,
  LEAF = 1,
};

// Common Node Header Layout

constexpr uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
constexpr uint32_t NODE_TYPE_OFFSET = 0;

constexpr uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
constexpr uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;

constexpr uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
constexpr uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;

constexpr uint32_t COMMON_NODE_HEADER_SIZE =
    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

// node related operations

inline NodeType get_node_type(void *node){
  uint8_t value = *(reinterpret_cast<uint8_t *>(reinterpret_cast<char *>(node) +
                                                NODE_TYPE_OFFSET));
  return (NodeType)value;
}

inline void set_node_type(void *node, NodeType type){
  uint8_t value = static_cast<uint8_t>(type);
  *(reinterpret_cast<uint8_t *>(reinterpret_cast<char *>(node) +
                                NODE_TYPE_OFFSET)) = value;
}

inline bool is_node_root(void *node){
  uint8_t value = *(reinterpret_cast<uint8_t *>(reinterpret_cast<char *>(node) +
                                                IS_ROOT_OFFSET));
  return static_cast<bool>(value);
}

inline void set_node_root(void *node, bool is_root){
  uint8_t value = static_cast<uint8_t>(is_root);
  *(reinterpret_cast<uint8_t *>(reinterpret_cast<char *>(node) +
                                IS_ROOT_OFFSET)) = value;
}

inline uint32_t *node_parent(void *node){
  return reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(node) +
                                      PARENT_POINTER_OFFSET);
}


// internal node

#define INVALID_PAGE_NUM UINT32_MAX

// Internal Node Header Layout

constexpr uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
constexpr uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
constexpr uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
constexpr uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET =
    INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
constexpr uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                               INTERNAL_NODE_NUM_KEYS_SIZE +
                                               INTERNAL_NODE_RIGHT_CHILD_SIZE;

// Internal Node Body Layout
// NOTE: internal nodes never store row data (only keys + child page
// pointers), so their layout does NOT depend on the table's row size and
// stays fixed-size/compile-time, unlike leaf nodes below.

constexpr uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
constexpr uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
constexpr uint32_t INTERNAL_NODE_CELL_SIZE =
    INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;
constexpr uint32_t INTERNAL_NODE_MAX_CELLS =
    (PAGE_SIZE - INTERNAL_NODE_HEADER_SIZE) / INTERNAL_NODE_CELL_SIZE;

// functions

inline uint32_t *internal_node_num_keys(void *node){
  return reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(node) +
                                      INTERNAL_NODE_NUM_KEYS_OFFSET);
}

inline uint32_t *internal_node_right_child(void *node){
  return reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(node) +
                                      INTERNAL_NODE_RIGHT_CHILD_OFFSET);
}

inline uint32_t *internal_node_cell(void *node, uint32_t cell_num){
  return reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(node) +
                                      INTERNAL_NODE_HEADER_SIZE +
                                      cell_num * INTERNAL_NODE_CELL_SIZE);
}

inline uint32_t *internal_node_child(void *node, uint32_t child_num){
  uint32_t num_keys = *internal_node_num_keys(node);

  if(child_num > num_keys){
    throw std::runtime_error("Tried to access child_num " + std::to_string(child_num) + 
                                " > num_keys " + std::to_string(num_keys) + ".\n");
  } 
  else if(child_num == num_keys){
    uint32_t *right_child = internal_node_right_child(node);
    if(*right_child == INVALID_PAGE_NUM){
      throw std::runtime_error("Tried to access right child of node, but was invalid page.\n");
    }
    return right_child;
  }
  else{
    uint32_t *child = reinterpret_cast<uint32_t *>(
        reinterpret_cast<char *>(node) + INTERNAL_NODE_HEADER_SIZE +
        child_num * INTERNAL_NODE_CELL_SIZE);
    if(*child == INVALID_PAGE_NUM){
      throw std::runtime_error("Tried to access child " + std::to_string(child_num) + "of node, but was invalid page.\n");
    }
    return child;
  }
}

inline uint32_t *internal_node_key(void *node, uint32_t key_num){
  return reinterpret_cast<uint32_t *>(
      reinterpret_cast<char *>(internal_node_cell(node, key_num)) +
      INTERNAL_NODE_CHILD_SIZE);
}

// Initialization

inline void initialize_internal_node(void *node){
  set_node_type(node, NodeType::INTERNAL);
  set_node_root(node, false);
  *internal_node_num_keys(node) = 0;
  *internal_node_right_child(node) = INVALID_PAGE_NUM;
}


// leaf node

// Leaf Node Header Layout
 
constexpr uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
constexpr uint32_t LEAF_NODE_NUM_CELLS_OFFSET =
    COMMON_NODE_HEADER_SIZE;

constexpr uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
constexpr uint32_t LEAF_NODE_NEXT_LEAF_OFFSET =
    LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
constexpr uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                       LEAF_NODE_NUM_CELLS_SIZE +
                                       LEAF_NODE_NEXT_LEAF_SIZE;


constexpr uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
constexpr uint32_t LEAF_NODE_KEY_OFFSET = 0;

constexpr uint32_t LEAF_NODE_VALUE_SIZE_MAX =
    (PAGE_SIZE - LEAF_NODE_HEADER_SIZE) / 2 - LEAF_NODE_KEY_SIZE;

constexpr uint32_t LEAF_NODE_SPACE_FOR_CELLS =
    PAGE_SIZE - LEAF_NODE_HEADER_SIZE;

// Accessors - all take rowSize explicitly

inline uint32_t leaf_node_cell_size(uint32_t rowSize){
    return LEAF_NODE_KEY_SIZE + rowSize;
}

inline uint32_t leaf_node_max_cells(uint32_t rowSize){
    return LEAF_NODE_SPACE_FOR_CELLS / leaf_node_cell_size(rowSize);
}

inline uint32_t leaf_node_right_split_count(uint32_t rowSize){
    return (leaf_node_max_cells(rowSize) + 1) / 2;
}

inline uint32_t leaf_node_left_split_count(uint32_t rowSize){
    return (leaf_node_max_cells(rowSize) + 1) - leaf_node_right_split_count(rowSize);
}

inline uint32_t* leafNodeNumCells(void* node){
    return reinterpret_cast<uint32_t*>(
        reinterpret_cast<char*>(node)  + LEAF_NODE_NUM_CELLS_OFFSET
    );
}

inline void* leafNodeCell(void* node, uint32_t cellNum, uint32_t rowSize){
    return reinterpret_cast<void*>(
        reinterpret_cast<char*>(node)  +
        LEAF_NODE_HEADER_SIZE  +
        cellNum * leaf_node_cell_size(rowSize)
    );
}

inline uint32_t* leafNodeKey(void* node, uint32_t cellNum, uint32_t rowSize){
    return reinterpret_cast<uint32_t*>(
        leafNodeCell(node, cellNum, rowSize)
    );
}

inline void* leafNodeValue(void* node, uint32_t cellNum, uint32_t rowSize){
    return reinterpret_cast<void*>(
        reinterpret_cast<char*>(leafNodeCell(node, cellNum, rowSize))  +
        LEAF_NODE_KEY_SIZE
    );
}

inline uint32_t* leaf_node_next_leaf(void* node){
  return reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(node)+ LEAF_NODE_NEXT_LEAF_OFFSET);
}

// Initialization 

inline void initializeLeafNode(void* node) {
     set_node_type(node, NodeType::LEAF);
     set_node_root(node, false);
    *leafNodeNumCells(node) = 0;   // initially node is empty
    *leaf_node_next_leaf(node) = INVALID_PAGE_NUM;
}

// utilities

inline uint32_t get_node_max_key(Pager *pager, void *node, uint32_t rowSize){
  if (get_node_type(node) == NodeType::LEAF) {
    return *leafNodeKey(node, *leafNodeNumCells(node) - 1, rowSize);
  }
  void *right_child = pager->getPage(*internal_node_right_child(node));
  uint32_t result = get_node_max_key(pager, right_child, rowSize);
  std::free(right_child);
  return result;
}

// Minimum occupancy (underflow threshold)
constexpr uint32_t INTERNAL_NODE_MIN_KEYS = INTERNAL_NODE_MAX_CELLS / 2;

#endif
#include "../include/btree.h"
#include "../include/node.h"
#include <cstdlib>
#include <cstring>

// Internal (file-local) helpers

static uint32_t internal_node_find_child(void* node, uint32_t key);
static void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key);
static uint32_t get_unused_page_num(Table* table);
static void create_new_root(Table* table, uint32_t right_child_page_num);
static void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num);
static void split_internal_node(Table* table, uint32_t old_page_num, uint32_t child_page_num);
static void split_leaf_node(Cursor* cursor, uint32_t key, const void* rowData);

// Binary search over an internal node's keys
static uint32_t internal_node_find_child(void* node, uint32_t key){
    uint32_t num_keys = *internal_node_num_keys(node);
    uint32_t min_index = 0;
    uint32_t max_index = num_keys;

    while(min_index != max_index){
        uint32_t index = (min_index + max_index) / 2;
        uint32_t key_at_index = *internal_node_key(node, index);
        if(key_at_index >= key){
            max_index = index;
        }
        else{
            min_index = index + 1;
        }
    }
    return min_index;
}

static void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key){
    uint32_t index = internal_node_find_child(node, old_key);
    if(index < *internal_node_num_keys(node)){
        *internal_node_key(node, index) = new_key;
    }
}

static uint32_t get_unused_page_num(Table* table){
    return table->pager->getFileLength() / PAGE_SIZE;
}

// Root splitting
static void create_new_root(Table* table, uint32_t right_child_page_num){
    void* root = table->pager->getPage(table->rootPageNum);
    void* right_child = table->pager->getPage(right_child_page_num);

    uint32_t left_child_page_num = get_unused_page_num(table);
    void* left_child = table->pager->getPage(left_child_page_num); // zero-filled, unused page

    std::memcpy(left_child, root, PAGE_SIZE);
    set_node_root(left_child, false);

    if(get_node_type(left_child) == NodeType::INTERNAL){
        uint32_t numKeys = *internal_node_num_keys(left_child);
        for(uint32_t i = 0; i < numKeys; i++){
            uint32_t childPageNum = *internal_node_child(left_child, i);
            void* child = table->pager->getPage(childPageNum);
            *node_parent(child) = left_child_page_num;
            table->pager->setPage(childPageNum, child);
            std::free(child);
        }
        uint32_t rightChildPageNum = *internal_node_right_child(left_child);
        void* child = table->pager->getPage(rightChildPageNum);
        *node_parent(child) = left_child_page_num;
        table->pager->setPage(rightChildPageNum, child);
        std::free(child);
    }
    table->pager->setPage(left_child_page_num, left_child);

    initialize_internal_node(root);
    set_node_root(root, true);
    *internal_node_num_keys(root) = 1;
    *internal_node_cell(root, 0) = left_child_page_num;
    *internal_node_key(root, 0) = get_node_max_key(table->pager, left_child, table->rowSize);
    *internal_node_right_child(root) = right_child_page_num;

    *node_parent(left_child) = table->rootPageNum;
    *node_parent(right_child) = table->rootPageNum;

    table->pager->setPage(table->rootPageNum, root);
    table->pager->setPage(right_child_page_num, right_child);

    std::free(root);
    std::free(right_child);
    std::free(left_child);
}

// Internal node insert / split

static void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num){
    void* parent = table->pager->getPage(parent_page_num);
    void* child = table->pager->getPage(child_page_num);
    uint32_t child_max_key = get_node_max_key(table->pager, child, table->rowSize);
    uint32_t index = internal_node_find_child(parent, child_max_key);
    uint32_t original_num_keys = *internal_node_num_keys(parent);

    if(original_num_keys >= INTERNAL_NODE_MAX_CELLS){
        std::free(parent);
        std::free(child);
        split_internal_node(table, parent_page_num, child_page_num);
        return;
    }

    uint32_t right_child_page_num = *internal_node_right_child(parent);

    if(right_child_page_num == INVALID_PAGE_NUM){
        *internal_node_right_child(parent) = child_page_num;
        *node_parent(child) = parent_page_num;
        table->pager->setPage(parent_page_num, parent);
        table->pager->setPage(child_page_num, child);
        std::free(parent);
        std::free(child);
        return;
    }

    void* right_child = table->pager->getPage(right_child_page_num);
    uint32_t right_child_max_key = get_node_max_key(table->pager, right_child, table->rowSize);

    *internal_node_num_keys(parent) = original_num_keys + 1;

    if(child_max_key > right_child_max_key){
        *internal_node_cell(parent, original_num_keys) = right_child_page_num;
        *internal_node_key(parent, original_num_keys) = right_child_max_key;
        *internal_node_right_child(parent) = child_page_num;
    }
    else{
        for(uint32_t i = original_num_keys; i > index; i--){
            std::memcpy(internal_node_cell(parent, i), internal_node_cell(parent, i - 1), INTERNAL_NODE_CELL_SIZE);
        }
        *internal_node_cell(parent, index) = child_page_num;
        *internal_node_key(parent, index) = child_max_key;
    }

    *node_parent(child) = parent_page_num;

    table->pager->setPage(parent_page_num, parent);
    table->pager->setPage(child_page_num, child);

    std::free(parent);
    std::free(child);
    std::free(right_child);
}

static void split_internal_node(Table* table, uint32_t old_page_num, uint32_t child_page_num){
    void* old_node = table->pager->getPage(old_page_num);
    uint32_t old_max = get_node_max_key(table->pager, old_node, table->rowSize);
    bool splitting_root = is_node_root(old_node);

    void* child_probe = table->pager->getPage(child_page_num);
    uint32_t child_max = get_node_max_key(table->pager, child_probe, table->rowSize);
    std::free(child_probe);

    uint32_t new_page_num = get_unused_page_num(table);
    uint32_t parent_page_num;

    if(splitting_root){
        void* new_sibling = table->pager->getPage(new_page_num);
        initialize_internal_node(new_sibling);
        table->pager->setPage(new_page_num, new_sibling);
        std::free(new_sibling);

        table->pager->setPage(old_page_num, old_node);
        std::free(old_node);

        create_new_root(table, new_page_num);

        void* newRoot = table->pager->getPage(table->rootPageNum);
        old_page_num = *internal_node_child(newRoot, 0);
        std::free(newRoot);

        old_node = table->pager->getPage(old_page_num);
        parent_page_num = table->rootPageNum;
    }
    else{
        parent_page_num = *node_parent(old_node);

        void* new_node = table->pager->getPage(new_page_num);
        initialize_internal_node(new_node);
        table->pager->setPage(new_page_num, new_node);
        std::free(new_node);
    }

    uint32_t num_keys = *internal_node_num_keys(old_node);

    // Move the old right child to the new sibling first.
    uint32_t cur_page_num = *internal_node_right_child(old_node);
    void* cur = table->pager->getPage(cur_page_num);
    *node_parent(cur) = new_page_num;
    table->pager->setPage(cur_page_num, cur);
    std::free(cur);
    internal_node_insert(table, new_page_num, cur_page_num);
    *internal_node_right_child(old_node) = INVALID_PAGE_NUM;

    // Move the upper half of old_node's keyed children to the new sibling.
    for(int32_t i = static_cast<int32_t>(INTERNAL_NODE_MAX_CELLS) - 1;
        i > static_cast<int32_t>(INTERNAL_NODE_MAX_CELLS / 2); i--){
        cur_page_num = *internal_node_child(old_node, static_cast<uint32_t>(i));
        cur = table->pager->getPage(cur_page_num);
        *node_parent(cur) = new_page_num;
        table->pager->setPage(cur_page_num, cur);
        std::free(cur);

        internal_node_insert(table, new_page_num, cur_page_num);

        num_keys--;
        *internal_node_num_keys(old_node) = num_keys;
    }

    // The last remaining keyed child of old_node becomes its new right child.
    *internal_node_right_child(old_node) = *internal_node_cell(old_node, num_keys - 1);
    num_keys--;
    *internal_node_num_keys(old_node) = num_keys;

    table->pager->setPage(old_page_num, old_node);
    uint32_t max_after_split = get_node_max_key(table->pager, old_node, table->rowSize);
    std::free(old_node);

    uint32_t destination_page_num = (child_max < max_after_split) ? old_page_num : new_page_num;
    internal_node_insert(table, destination_page_num, child_page_num);

    void* child = table->pager->getPage(child_page_num);
    *node_parent(child) = destination_page_num;
    table->pager->setPage(child_page_num, child);
    std::free(child);

    void* parent = table->pager->getPage(parent_page_num);
    void* refreshedOld = table->pager->getPage(old_page_num);
    uint32_t refreshedOldMax = get_node_max_key(table->pager, refreshedOld, table->rowSize);
    std::free(refreshedOld);
    update_internal_node_key(parent, old_max, refreshedOldMax);
    table->pager->setPage(parent_page_num, parent);
    std::free(parent);

    if(!splitting_root){
        internal_node_insert(table, parent_page_num, new_page_num);
    }
}

// Leaf lookup / insert / split

static uint32_t leaf_node_find_cell(void* node, uint32_t key, uint32_t rowSize){
    uint32_t num_cells = *leafNodeNumCells(node);
    uint32_t min_index = 0;
    uint32_t max_index = num_cells;

    while(min_index != max_index){
        uint32_t index = (min_index + max_index) / 2;
        uint32_t key_at_index = *leafNodeKey(node, index, rowSize);
        if(key == key_at_index){
            return index;
        }
        if(key < key_at_index){
            max_index = index;
        }
        else{
            min_index = index + 1;
        }
    }
    return min_index;
}

Cursor tableFind(Table* table, uint32_t key){
    uint32_t pageNum = table->rootPageNum;
    void* node = table->pager->getPage(pageNum);

    while(get_node_type(node) == NodeType::INTERNAL){
        uint32_t childIndex = internal_node_find_child(node, key);
        uint32_t childPageNum = *internal_node_child(node, childIndex);
        std::free(node);
        pageNum = childPageNum;
        node = table->pager->getPage(pageNum);
    }

    uint32_t cellNum = leaf_node_find_cell(node, key, table->rowSize);
    std::free(node);

    Cursor cursor;
    cursor.table = table;
    cursor.pageNum = pageNum;
    cursor.cellNum = cellNum;
    cursor.EOT = false;
    return cursor;
}

static void split_leaf_node(Cursor* cursor, uint32_t key, const void* rowData){
    Table* table = cursor->table;
    uint32_t rowSize = table->rowSize;

    uint32_t old_page_num = cursor->pageNum;
    void* old_node = table->pager->getPage(old_page_num);
    bool old_node_was_root = is_node_root(old_node);
    uint32_t old_max = old_node_was_root ? 0 : get_node_max_key(table->pager, old_node, rowSize);

    uint32_t new_page_num = get_unused_page_num(table);
    void* new_node = table->pager->getPage(new_page_num); // zero-filled, unused page
    initializeLeafNode(new_node);
    *node_parent(new_node) = *node_parent(old_node);
    *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
    *leaf_node_next_leaf(old_node) = new_page_num;

    uint32_t max_cells = leaf_node_max_cells(rowSize);
    uint32_t left_count = leaf_node_left_split_count(rowSize);
    uint32_t cell_size = leaf_node_cell_size(rowSize);

    for(int32_t i = static_cast<int32_t>(max_cells); i >= 0; i--){
        uint32_t idx = static_cast<uint32_t>(i);
        void* destination_node = (idx >= left_count) ? new_node : old_node;
        uint32_t index_within_node = idx % left_count;
        void* destination = leafNodeCell(destination_node, index_within_node, rowSize);

        if(idx == cursor->cellNum){
            *reinterpret_cast<uint32_t*>(destination) = key;
            std::memcpy(leafNodeValue(destination_node, index_within_node, rowSize), rowData, rowSize);
        }
        else if(idx > cursor->cellNum){
            std::memcpy(destination, leafNodeCell(old_node, idx - 1, rowSize), cell_size);
        }
        else{
            std::memcpy(destination, leafNodeCell(old_node, idx, rowSize), cell_size);
        }
    }

    *leafNodeNumCells(old_node) = left_count;
    *leafNodeNumCells(new_node) = leaf_node_right_split_count(rowSize);

    table->pager->setPage(old_page_num, old_node);
    table->pager->setPage(new_page_num, new_node);

    if(old_node_was_root){
        std::free(old_node);
        std::free(new_node);
        create_new_root(table, new_page_num);
    }
    else{
        uint32_t parent_page_num = *node_parent(old_node);
        uint32_t new_max = get_node_max_key(table->pager, old_node, rowSize);
        std::free(old_node);
        std::free(new_node);

        void* parent = table->pager->getPage(parent_page_num);
        update_internal_node_key(parent, old_max, new_max);
        table->pager->setPage(parent_page_num, parent);
        std::free(parent);

        internal_node_insert(table, parent_page_num, new_page_num);
    }
}

void insert_leaf_node(Cursor* cursor, uint32_t key, const void* rowData){
    Table* table = cursor->table;
    void* node = table->pager->getPage(cursor->pageNum);
    uint32_t num_cells = *leafNodeNumCells(node);
    uint32_t max_cells = leaf_node_max_cells(table->rowSize);

    if(num_cells >= max_cells){
        std::free(node);
        split_leaf_node(cursor, key, rowData);
        return;
    }

    if(cursor->cellNum < num_cells){
        for(uint32_t i = num_cells; i > cursor->cellNum; i--){
            std::memcpy(leafNodeCell(node, i, table->rowSize),
                        leafNodeCell(node, i - 1, table->rowSize),
                        leaf_node_cell_size(table->rowSize));
        }
    }

    *leafNodeNumCells(node) += 1;
    *leafNodeKey(node, cursor->cellNum, table->rowSize) = key;
    std::memcpy(leafNodeValue(node, cursor->cellNum, table->rowSize), rowData, table->rowSize);

    table->pager->setPage(cursor->pageNum, node);
    std::free(node);
}
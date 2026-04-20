#ifndef CURSOR_H
#define CURSOR_H

#include "table.h"
#include "node.h"
#include <cstdlib>

struct Cursor{
    Table* table;
    uint32_t pageNum;
    uint32_t cellNum;
    bool EOT;
};

// Traverse from root down to the leftmost leaf
inline Cursor tableStart(Table* table){
    Cursor cursor;
    cursor.table = table;
    cursor.EOT = (table->numRows == 0);

    uint32_t pageNum = table->rootPageNum;
    void* node = table->pager->getPage(pageNum);

    while(get_node_type(node) == NodeType::INTERNAL){
        uint32_t childPageNum = *internal_node_child(node, 0);
        table->pager->unpinPage(pageNum, false);
        pageNum = childPageNum;
        node = table->pager->getPage(pageNum);
    }

    cursor.pageNum = pageNum;
    cursor.cellNum = 0;
    table->pager->unpinPage(pageNum, false);
    return cursor;
}

// Walk to the rightmost leaf, then past its last cell
inline Cursor getTableEnd(Table* table){
    Cursor cursor;
    cursor.table = table;
    cursor.EOT = true;

    uint32_t pageNum = table->rootPageNum;
    void* node = table->pager->getPage(pageNum);

    while(get_node_type(node) == NodeType::INTERNAL){
        uint32_t childPageNum = *internal_node_right_child(node);
        table->pager->unpinPage(pageNum, false);
        pageNum = childPageNum;
        node = table->pager->getPage(pageNum);
    }

    cursor.pageNum = pageNum;
    cursor.cellNum = *leafNodeNumCells(node);  // one past last cell
    table->pager->unpinPage(pageNum, false);
    return cursor;
}

// Follow next_leaf pointer at end of each leaf
inline void cursorNext(Cursor* cursor){
    uint32_t currentPageNum = cursor->pageNum;
    void* node = cursor->table->pager->getPage(currentPageNum);
    cursor->cellNum++;

    if(cursor->cellNum >= *leafNodeNumCells(node)){
        uint32_t next = *leaf_node_next_leaf(node);
        if(next == INVALID_PAGE_NUM){
            cursor->EOT = true;
        }
        else{
            cursor->pageNum = next;
            cursor->cellNum = 0;
        }
    }
    cursor->table->pager->unpinPage(currentPageNum, false);
}

inline void* getCursorValue(Cursor* cursor, void** outPage){
    void* page = cursor->table->pager->getPage(cursor->pageNum);
    *outPage = page;
    return leafNodeValue(page, cursor->cellNum, cursor->table->rowSize);
}

#endif
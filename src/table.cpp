#include "../include/table.h"
#include "../include/node.h"
#include <stdexcept>

Table::Table(const std::string& tableName, const std::string& dbFileName, const std::string& dir){
    name = tableName;
    rowSize = 0;
    numRows = 0;
    rootPageNum = 0;
    pager = new Pager(dbFileName, dir);

    if(pager->getFileLength() == 0){
        pager->beginTransaction();

        void* rootNode = pager->getPage(rootPageNum); // zero-filled page from pool
        initializeLeafNode(rootNode);
        set_node_root(rootNode, true);
        pager->unpinPage(rootPageNum, true);

        pager->commitTransaction(); // Push to DB
    }
}

Table::~Table(){
    delete pager;
    for(auto& pair : lookup){
        delete pair.second;
    }
}

void Table::calculateRowLayout(){
    uint32_t currentOffset = VALID_BIT_SIZE;

    for(const std::string& colName : orderedCol){
        colInfo* col = lookup[colName];
        col->offset = currentOffset;

        switch(col->type){
            case INT:
                col->size = SIZE_INT;
                break;
            case FLOAT:
                col->size = SIZE_FLOAT;
                break;
            case CHAR:
                col->size = SIZE_CHAR;
                break;
            case STRING:
                col->size = SIZE_STRING;
                break;
            default:
                col->size = SIZE_INT;
                break;
        }
        currentOffset += col->size;
    }
    rowSize = currentOffset;

    if(rowSize == VALID_BIT_SIZE){
        throw std::runtime_error("Table '" + name + "' has zero-width rows (no columns).\n");
    }
    if(rowSize > LEAF_NODE_VALUE_SIZE_MAX){
        throw std::runtime_error(
            "Table '" + name + "' row size (" + std::to_string(rowSize) +
            " bytes) exceeds the maximum supported row size (" +
            std::to_string(LEAF_NODE_VALUE_SIZE_MAX) + " bytes).\n");
    }
}
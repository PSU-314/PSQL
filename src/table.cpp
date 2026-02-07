#include "../include/table.h"

Table::Table(const std::string& tableName, const std::string& dbFileName, const std::string& dir){
    name = tableName;
    rowSize = 0;
    pager = new Pager(dbFileName, dir);
}

Table::~Table(){
    delete pager;
    for(auto& pair : lookup){
        delete pair.second;
    }
}

void Table::calculateRowLayout(){
    uint32_t currentOffset = 0;

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
}
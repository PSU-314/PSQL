#ifndef TABLE_H
#define TABLE_H

#include "pager.h"
#include "parser.h"
#include <string>
#include <vector>
#include <unordered_map>

const uint32_t SIZE_INT = 4;
const uint32_t SIZE_FLOAT = 4;
const uint32_t SIZE_CHAR = 1;
const uint32_t SIZE_STRING = 255;

struct colInfo{
    Type type;
    uint32_t size;
    uint32_t offset;
    bool isPrimary;
    bool canHoldNull;
};

class Table{
public:
    std::string name;
    Pager* pager;
    uint32_t rowSize;
    uint32_t numRows;
    std::vector<std::string> orderedCol;
    std::unordered_map<std::string, colInfo*> lookup;

    Table(const std::string& tableName, const std::string& dbFileName);
    ~Table();
    
    void calculateRowLayout();
};

#endif
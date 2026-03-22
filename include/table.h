#ifndef TABLE_H
#define TABLE_H

#include "pager.h"
#include "parser.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

const uint32_t SIZE_INT = 4;
const uint32_t SIZE_FLOAT = 4;
const uint32_t SIZE_CHAR = 2;
const uint32_t SIZE_STRING = 255;
const uint32_t VALID_BIT_SIZE = 1;
const uint32_t VALID_BIT_OFFSET = 0;

inline bool isRowValid(const void* rowSlot){
    return *(reinterpret_cast<const uint8_t*>(rowSlot) + VALID_BIT_OFFSET) != 0;
}

inline void setRowValid(void* rowSlot, bool valid){
    *(reinterpret_cast<uint8_t*>(rowSlot) + VALID_BIT_OFFSET) = valid ? 1 : 0;
}

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
    uint32_t rootPageNum;
    
    std::vector<std::string> orderedCol;
    std::unordered_map<std::string, colInfo*> lookup;

    Table(const std::string& tableName, const std::string& dbFileName, const std::string& dir);
    ~Table();
    
    void calculateRowLayout();
};

#endif
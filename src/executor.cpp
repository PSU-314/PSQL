#include "../include/executor.h"
#include "../include/main.h"
#include "../include/pager.h"
#include "../include/catalog.h"
#include "../include/btree.h"
#include "../include/cursor.h"
#include "../include/node.h"
#include <stdexcept>
#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>

static Catalog catalog("catalog.meta", "data");

Executor::Executor(){
    catalog.loadCatalog(tableList);
}

Executor::~Executor(){
    shutdown();
}

void Executor::execute(psqlStatement& stmt){
    if(stmt.type == C_CREATE){
        executeCreate(static_cast<createStatement*>(stmt.args.get()));
    }
    else if(stmt.type == C_INSERT){
        executeInsert(static_cast<insertStatement*>(stmt.args.get()));
    }
    else if(stmt.type == C_SELECT){
        executeSelect(static_cast<selectStatement*>(stmt.args.get()));
    }
}

void Executor::executeCreate(createStatement* args){
    if(!args) return;

    if(tableList.find(args->tableName) != tableList.end()){
        throw std::runtime_error("Table '" + args->tableName + "' already exists.\n");
    }

    std::string dbFileName = args->tableName + ".db";
    Table* newTable = new Table(args->tableName, dbFileName, "data");

    try{
        for(const auto& attr : args->attributes){
            colInfo* col = new colInfo();
            col->type = attr.type;
            col->isPrimary = attr.isPrimary;
            col->canHoldNull = attr.canHoldNull;

            newTable->orderedCol.push_back(attr.ColName);
            newTable->lookup[attr.ColName] = col;
        }

        newTable->calculateRowLayout();
    }
    catch(...){
        delete newTable;
        throw;
    }

    tableList[args->tableName] = newTable;

    catalog.saveCatalog(tableList);

    print("Table '" + args->tableName + "' created.\n", 0);
}

void Executor::executeInsert(insertStatement* args){
    if(!args) return;

    if(tableList.find(args->tableName) == tableList.end()){
        throw std::runtime_error("Table '" + args->tableName + "' does not exist.\n");
    }

    Table* table = tableList[args->tableName];

    if(args->values.size() != table->orderedCol.size()){
        throw std::runtime_error("Column/Value count mismatch. Expected " + std::to_string(table->orderedCol.size()) +
                                 " values, got " + std::to_string(args->values.size()) + ".\n");
    }

    std::vector<char> rowBuf(table->rowSize, 0);
    char* rowSlot = rowBuf.data();

    for(size_t i = 0; i < table->orderedCol.size(); i++){
        const std::string& colName = table->orderedCol[i];
        colInfo* col = table->lookup[colName];
        const std::string& rawVal = args->values[i];

        char* writeDest = rowSlot + col->offset;

        if(col->type == INT){
            try{
                int val = std::stoi(rawVal);
                std::memcpy(writeDest, &val, col->size);
            }
            catch(const std::invalid_argument&){
                throw std::runtime_error("Expected integer for column '" + colName + "'.\n");
            }
            catch(const std::out_of_range&){
                throw std::runtime_error("Integer out of range for column '" + colName + "'.\n");
            }
        } 
        else if(col->type == FLOAT){
            try{
                float val = std::stof(rawVal);
                std::memcpy(writeDest, &val, col->size);
            }
            catch(const std::invalid_argument&){
                throw std::runtime_error("Expected float for column '" + colName + "'.\n");
            }
            catch(const std::out_of_range&){
                throw std::runtime_error("Float out of range for column '" + colName + "'.\n");
            }
        } 
        else if(col->type == CHAR || col->type == STRING){
            std::memset(writeDest, 0, col->size);
            if(col->size > 0){
                size_t copyLen = std::min(rawVal.size(), static_cast<size_t>(col->size - 1));
                std::memcpy(writeDest, rawVal.data(), copyLen);
                writeDest[copyLen] = '\0';
            }
        }
    }

    uint32_t key = table->numRows;
    Cursor cursor = tableFind(table, key);

    if(!cursor.EOT){
        void* existingPage = table->pager->getPage(cursor.pageNum);
        bool duplicate = (cursor.cellNum < *leafNodeNumCells(existingPage)) &&
                          (*leafNodeKey(existingPage, cursor.cellNum, table->rowSize) == key);
        std::free(existingPage);
        if(duplicate){
            throw std::runtime_error("Internal error: duplicate row key " + std::to_string(key) + ".\n");
        }
    }

    insert_leaf_node(&cursor, key, rowSlot);

    table->numRows++;

    catalog.saveCatalog(tableList);

    print("Inserted a row.\n", 0);
}

void Executor::executeSelect(selectStatement* args){
    if(!args) return;

    if(tableList.find(args->tableName) == tableList.end()){
        throw std::runtime_error("Table does not exist.\n");
    }

    Table* table = tableList[args->tableName];

    if(table->numRows == 0){
        print("No rows selected.\n", 0);
        return;
    }

    for(size_t i = 0; i < table->orderedCol.size(); i++){
        std::cout << table->orderedCol[i];
        if(i < table->orderedCol.size() - 1) std::cout << " | ";
    }
    print("\n" + std::string(40, '-') + "\n", 0);

    for(Cursor cursor = tableStart(table); !cursor.EOT; cursorNext(&cursor)){
        void* page = nullptr;
        void* rowSlot = getCursorValue(&cursor, &page);

        for(size_t i = 0; i < table->orderedCol.size(); i++){
            const std::string& colName = table->orderedCol[i];
            colInfo* col = table->lookup.at(colName);
            void* fieldAddr = static_cast<char*>(rowSlot) + col->offset;

            if(col->type == INT){
                int32_t v;
                std::memcpy(&v, fieldAddr, col->size);
                print(std::to_string(v), 0);
            }
            else if(col->type == FLOAT){
                float v;
                std::memcpy(&v, fieldAddr, col->size);
                print(std::to_string(v), 0);
            } 
            else{
                print(static_cast<char*>(fieldAddr), 0);
            }

            if(i < table->orderedCol.size() - 1) print(" | ", 0);
        }
        print("\n", 0);

        std::free(page);
    }
}

void Executor::shutdown(){
    for(auto& pair : tableList){
        delete pair.second;
    }
    tableList.clear();
}
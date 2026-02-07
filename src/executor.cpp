#include "../include/executor.h"
#include "../include/main.h"
#include "../include/pager.h"
#include "../include/catalog.h"
#include <stdexcept>
#include <string>
#include <cstring>

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


    for(const auto& attr : args->attributes){
        colInfo* col = new colInfo();
        col->type = attr.type;
        col->isPrimary = attr.isPrimary;
        col->canHoldNull = attr.canHoldNull;
        
        newTable->orderedCol.push_back(attr.ColName);
        newTable->lookup[attr.ColName] = col;
    }

    newTable->calculateRowLayout();

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

    uint32_t currentOffset = table->numRows * table->rowSize;
    if(currentOffset + table->rowSize > PAGE_SIZE){
        throw std::runtime_error("Single-page storage capacity limit reached.\n");
    }

    void* page = table->pager->getPage(0);
    if(!page){
        throw std::runtime_error("Pager allocation failure.\n");
    }

    char* rowSlot = static_cast<char*>(page) + currentOffset;

    for(size_t i = 0; i < table->orderedCol.size(); i++){
        std::string colName = table->orderedCol[i];
        colInfo* col = table->lookup[colName];
        std::string rawVal = args->values[i];

        char* writeDest = rowSlot + col->offset;

        if(col->type == INT){
            try{
                int val = std::stoi(rawVal);
                std::memcpy(writeDest, &val, col->size);
            }
            catch(std::invalid_argument){
                throw std::runtime_error("Expected integer.\n");
            }
        } 
        else if(col->type == FLOAT){
            try{
                float val = std::stof(rawVal);
                std::memcpy(writeDest, &val, col->size);
            }
            catch(std::invalid_argument){
                throw std::runtime_error("Expected float.\n");
            }
        } 
        else if(col->type == CHAR || col->type == STRING){
            try{
                std::memset(writeDest, 0, col->size);
                std::strncpy(writeDest, rawVal.c_str(), col->size - 1);
            }
            catch(std::invalid_argument){
                throw std::runtime_error("Expected char/string");
            }
        }
    }

    table->pager->setPage(0, page);
    table->numRows++;

    catalog.saveCatalog(tableList);

    std::free(page);

    print("Inserted a row.\n", 0);
}

void Executor::executeSelect(selectStatement* args){
    if(!args) return;

    if(tableList.find(args->tableName) == tableList.end()){
        throw std::runtime_error("Table does not exist.\n");
        return;
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

    void* page = table->pager->getPage(0);
    if(!page){
        throw std::runtime_error("Failed to read page from database disk memory.\n");
    }

    char* pageBytes = static_cast<char*>(page);

    for(uint32_t r = 0; r < table->numRows; r++){
        char* rowSlot = pageBytes + (r * table->rowSize);

        for(size_t i = 0; i < table->orderedCol.size(); i++){
            std::string colName = table->orderedCol[i];
            colInfo* col = table->lookup.at(colName);
            void* fieldAddr = rowSlot + col->offset;

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
    }

    std::free(page);
}

void Executor::shutdown(){
    for(auto& pair : tableList){
        delete pair.second;
    }
    tableList.clear();
}
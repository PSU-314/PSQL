#include "../include/executor.h"
#include "../include/main.h"
#include "../include/pager.h"
#include <stdexcept>
#include <string>
#include <cstring>

Executor::Executor(){}

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
}

void Executor::executeCreate(createStatement* args){
    if(!args) return;

    if(tableList.find(args->tableName) != tableList.end()){
        throw std::runtime_error("Table '" + args->tableName + "' already exists.\n");
    }

    std::string dbFileName = args->tableName + ".db";
    Table* newTable = new Table(args->tableName, dbFileName);


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
            int val = std::stoi(rawVal);
            std::memcpy(writeDest, &val, col->size);
        } 
        else if(col->type == FLOAT){
            float val = std::stof(rawVal);
            std::memcpy(writeDest, &val, col->size);
        } 
        else if(col->type == CHAR || col->type == STRING){
            std::memset(writeDest, 0, col->size);
            std::strncpy(writeDest, rawVal.c_str(), col->size - 1);
        }
    }

    table->pager->setPage(0, page);
    table->numRows++;

    std::free(page);

    std::cout << "Inserted a row.\n";
}

void Executor::shutdown(){
    for(auto& pair : tableList){
        delete pair.second;
    }
    tableList.clear();
}
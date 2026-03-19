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
#include <filesystem>

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
    else if(stmt.type == C_DROP){
        executeDrop(static_cast<dropStatement*>(stmt.args.get()));
    }
    else if(stmt.type == C_UPDATE){
        executeUpdate(static_cast<updateStatement*>(stmt.args.get())); // Add this route
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
    
    uint32_t btreeKey = table->numRows;
    bool hasIntPK = false;

    // Data Processing and NULL validation
    for(size_t i = 0; i < table->orderedCol.size(); i++){
        const std::string& colName = table->orderedCol[i];
        colInfo* col = table->lookup[colName];
        
        Token token = args->values[i];
        const std::string& rawVal = token.value;

        char* writeDest = rowSlot + col->offset;
        
        // Null parsing & enforcement
        if(token.type == KEYWORD && rawVal == "null"){
            if(!col->canHoldNull){
                throw std::runtime_error("Constraint Violated: Column '" + colName + "' cannot be null.\n");
            }
            if(col->isPrimary){
                throw std::runtime_error("Constraint Violated: Primary key column '" + colName + "' cannot be null.\n");
            }
            std::memset(writeDest, 0, col->size);
            continue;
        }

        if(col->type == INT){
            try{
                int val = std::stoi(rawVal);
                std::memcpy(writeDest, &val, col->size);
                
                // If it is an INT PK, use it as the B-Tree Key
                if (col->isPrimary) {
                    hasIntPK = true;
                    btreeKey = static_cast<uint32_t>(val);
                }
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
    
    // Enforce Primary Key Uniqueness
    int pkIndex = -1;
    for(size_t i = 0; i < table->orderedCol.size(); i++){
        if(table->lookup[table->orderedCol[i]]->isPrimary){
            pkIndex = static_cast<int>(i);
            break;
        }
    }

    if(hasIntPK){
        Cursor cursor = tableFind(table, btreeKey);
        if(!cursor.EOT){
            void* existingPage = table->pager->getPage(cursor.pageNum);
            bool duplicate = (cursor.cellNum < *leafNodeNumCells(existingPage)) &&
                             (*leafNodeKey(existingPage, cursor.cellNum, table->rowSize) == btreeKey);
            std::free(existingPage);
            if(duplicate){
                throw std::runtime_error("Constraint Violated: Duplicate primary key value: " + std::to_string(btreeKey) + ".\n");
            }
        }
    } 
    else if(pkIndex != -1){
        colInfo* pkCol = table->lookup[table->orderedCol[pkIndex]];
        for(Cursor scan = tableStart(table); !scan.EOT; cursorNext(&scan)){
            void* page = nullptr;
            void* existingRow = getCursorValue(&scan, &page);
            
            if(std::memcmp(static_cast<char*>(existingRow) + pkCol->offset, 
                            rowSlot + pkCol->offset, pkCol->size) == 0){
                std::free(page);
                throw std::runtime_error("Constraint Violated: Duplicate primary key value.\n");
            }
            std::free(page);
        }
    }

    // Final B-Tree leaf insertion
    Cursor cursor = tableFind(table, btreeKey);

    if(!cursor.EOT){
        void* existingPage = table->pager->getPage(cursor.pageNum);
        bool duplicate = (cursor.cellNum < *leafNodeNumCells(existingPage)) &&
                          (*leafNodeKey(existingPage, cursor.cellNum, table->rowSize) == btreeKey);
        std::free(existingPage);
        if(duplicate){
            throw std::runtime_error("Internal error: duplicate row key " + std::to_string(btreeKey) + ".\n");
        }
    }

    insert_leaf_node(&cursor, btreeKey, rowSlot);

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

void Executor::executeDrop(dropStatement* args){
    if(!args) return;

    auto it = tableList.find(args->tableName);
    if(it == tableList.end()){
        throw std::runtime_error("Table '" + args->tableName + "' does not exist.\n");
    }

    Table* table = it->second;
    
    std::string dbFilePath = "data/" + table->name + ".db";

    tableList.erase(it);
    delete table;
    std::filesystem::remove(dbFilePath);
    catalog.saveCatalog(tableList);

    print("Table '" + args->tableName + "' dropped successfully.\n", 0);
}

void Executor::executeUpdate(updateStatement* args){
    if(!args) return;

    if(tableList.find(args->tableName) == tableList.end()){
        throw std::runtime_error("Table '" + args->tableName + "' does not exist.\n");
    }

    Table* table = tableList[args->tableName];

    for(const auto& u : args->updates){
        if(table->lookup.find(u.colName) == table->lookup.end()){
            throw std::runtime_error("Column not found: " + u.colName + "\n");
        }
        if(table->lookup[u.colName]->isPrimary){
            throw std::runtime_error("Constraint Violated: Updating primary key column '" + u.colName + "' is not supported.\n");
        }
    }
    
    if(args->hasWhere && table->lookup.find(args->whereCol) == table->lookup.end()){
        throw std::runtime_error("Where column not found: " + args->whereCol + "\n");
    }

    int updatedRows = 0;
    
    for(Cursor scan = tableStart(table); !scan.EOT; cursorNext(&scan)){
        void* page = nullptr;
        void* rowSlot = getCursorValue(&scan, &page);
        bool match = true;

        if(args->hasWhere){
            colInfo* wCol = table->lookup[args->whereCol];
            void* fieldAddr = static_cast<char*>(rowSlot) + wCol->offset;
            const std::string& rawVal = args->whereVal.value;

            if(wCol->type == INT){
                int32_t v;
                std::memcpy(&v, fieldAddr, wCol->size);
                if(v != std::stoi(rawVal)) match = false;
            } 
            else if(wCol->type == FLOAT){
                float v;
                std::memcpy(&v, fieldAddr, wCol->size);
                if(v != std::stof(rawVal)) match = false;
            } 
            else {
                std::string vStr(static_cast<char*>(fieldAddr));
                if(vStr != rawVal) match = false;
            }
        }

        if(match){
            for(const auto& u : args->updates){
                colInfo* sCol = table->lookup[u.colName];
                void* writeDest = static_cast<char*>(rowSlot) + sCol->offset;
                const std::string& rawVal = u.value.value;

                if(u.value.type == KEYWORD && rawVal == "null"){
                    if(!sCol->canHoldNull) {
                        std::free(page);
                        throw std::runtime_error("Constraint Violated: Column '" + u.colName + "' cannot be null.\n");
                    }
                    std::memset(writeDest, 0, sCol->size);
                } 
                else if(sCol->type == INT){
                    int val = std::stoi(rawVal);
                    std::memcpy(writeDest, &val, sCol->size);
                } 
                else if(sCol->type == FLOAT){
                    float val = std::stof(rawVal);
                    std::memcpy(writeDest, &val, sCol->size);
                } 
                else {
                    std::memset(writeDest, 0, sCol->size);
                    if(sCol->size > 0){
                        size_t copyLen = std::min(rawVal.size(), static_cast<size_t>(sCol->size - 1));
                        std::memcpy(writeDest, rawVal.data(), copyLen);
                    }
                }
            }
            
            table->pager->setPage(scan.pageNum, page);
            updatedRows++;
        }
        std::free(page);
    }
    
    print("Updated " + std::to_string(updatedRows) + " row(s).\n", 0);
}

void Executor::shutdown(){
    for(auto& pair : tableList){
        delete pair.second;
    }
    tableList.clear();
}
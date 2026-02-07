#include "../include/catalog.h"
#include "../include/main.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>

Catalog::Catalog(const std::string& fileName, const std::string& dir):
    fullPathCatalog(dir + '/' + fileName){
        if(!std::filesystem::exists(dir)){
            std::filesystem::create_directory(dir);
        }
    }

void Catalog::saveCatalog(const std::unordered_map<std::string, Table*>& tableList){
    std::ofstream out(fullPathCatalog, std::ios::out | std::ios::trunc);
    if(!out.is_open()){
        throw std::runtime_error("Could not open catalog file.\n");
    }

    out << tableList.size() << "\n";
    
    for(const auto& pair : tableList){
        Table* table = pair.second;
        out << table->name << " " << table->numRows << " " << table->orderedCol.size() << "\n";
        
        for(const std::string& colName : table->orderedCol){
            colInfo* col = table->lookup.at(colName);
            out << colName << " " 
                << static_cast<int>(col->type) << " " 
                << (col->isPrimary ? 1 : 0) << " " 
                << (col->canHoldNull ? 1 : 0) << "\n";
        }
    }
    out.close();
}

void Catalog::loadCatalog(std::unordered_map<std::string, Table*>& tableList){
    std::ifstream in(fullPathCatalog);
    if (!in.is_open()){
        return;
    }

    size_t numTables = 0;
    if(!(in >> numTables)) return;

    for(size_t t = 0; t < numTables; t++){
        std::string tableName;
        uint32_t savedRows = 0;
        size_t numCols = 0;
        if(!(in >> tableName >> savedRows >> numCols)) break;

        std::string dbFileName = tableName + ".db";
        Table* table = new Table(tableName, dbFileName, "data");
        table->numRows = savedRows;
        for(size_t c = 0; c < numCols; c++){
            std::string colName;
            int typeInt;
            int isPrimaryInt;
            int canHoldNullInt;
            
            if(in >> colName >> typeInt >> isPrimaryInt >> canHoldNullInt){
                colInfo* col = new colInfo();
                col->type = static_cast<Type>(typeInt);
                col->isPrimary = (isPrimaryInt == 1);
                col->canHoldNull = (canHoldNullInt == 1);

                table->orderedCol.push_back(colName);
                table->lookup[colName] = col;
            }
        }

        table->calculateRowLayout();
        tableList[tableName] = table;
    }
    in.close();
    print("Database metadata loaded.\n", 0);
}
#include "../include/executor.h"
#include "../include/main.h"

Executor::Executor(){}

Executor::~Executor(){
    shutdown();
}

void Executor::execute(psqlStatement& stmt){
    if(stmt.type == C_CREATE){
        executeCreate(static_cast<createStatement*>(stmt.args.get()));
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

void Executor::shutdown(){
    for(auto& pair : tableList){
        delete pair.second;
    }
    tableList.clear();
}
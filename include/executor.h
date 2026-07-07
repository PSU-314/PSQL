#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"
#include "psql.h"
#include "table.h"
#include <unordered_map>
#include <string>
#include <cstdint>

class Executor{
private:
    std::unordered_map<std::string, Table*> tableList;
    ResultSet executeCreate(createStatement* args);
    ResultSet executeInsert(insertStatement* args);
    ResultSet executeSelect(selectStatement* args);
    ResultSet executeDrop(dropStatement* args);
    ResultSet executeUpdate(updateStatement* args);
    ResultSet executeDelete(deleteStatement* args);

public:
    Executor();
    ~Executor();

    ResultSet execute(psqlStatement& stmt);
    void shutdown();
};

#endif
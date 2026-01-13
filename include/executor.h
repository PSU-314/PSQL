#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"
#include "table.h"
#include <unordered_map>
#include <string>

class Executor{
private:
    std::unordered_map<std::string, Table*> tableList;
    void executeCreate(createStatement* args);

public:
    Executor();
    ~Executor();

    void execute(psqlStatement& stmt);
    void shutdown();
};

#endif
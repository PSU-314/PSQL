#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include <memory>
#include <string>
#include <vector>

enum Type{
    INT,
    STRING,
    FLOAT,
    CHAR,
    NONE
};

enum Commands{C_CREATE, C_INSERT, C_SELECT};

struct Item{
    Type type;
    std::string value;
};

struct Attribute{
    std::string ColName;
    Type type;
    bool isPrimary;
    bool canHoldNull;
    Attribute(){
        isPrimary = false;
        canHoldNull = true;
    }
};

struct statement{
    virtual ~statement(){};
};

struct createStatement : public statement{
    std::string tableName;
    std::vector<Attribute> attributes;
};

struct insertStatement : public statement{
    std::string tableName;
    std::vector<Token> values;
};

struct selectStatement : public statement{
    std::string tableName;
};

struct psqlStatement{
    Commands type;
    std::unique_ptr<statement> args;
};


class Parser{
private:
    int current;
    Token cursor(const std::vector<Token>&);
    Token move(const std::vector<Token>&);

    void handleCreate(const std::vector<Token>&, psqlStatement& );
    void handleInsert(const std::vector<Token>&, psqlStatement& );
    void handleSelect(const std::vector<Token>&, psqlStatement& );

public:
    Parser();
    void parse(std::vector<Token> , psqlStatement& );
};

#endif
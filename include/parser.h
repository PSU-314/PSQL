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

enum Commands{C_CREATE, C_INSERT, C_SELECT, C_DROP, C_UPDATE, C_DELETE};

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
    bool selectAll = true;
    std::vector<std::string> columns;
    bool hasWhere = false;
    std::string whereCol;
    Token whereVal;
};

struct dropStatement : public statement{
    std::string tableName;
};

struct setClause{
    std::string colName;
    Token value;
};

struct updateStatement : public statement{
    std::string tableName;
    std::vector<setClause> updates;
    bool hasWhere = false;
    std::string whereCol;
    Token whereVal;
};

struct deleteStatement : public statement{
    std::string tableName;
    bool hasWhere = false;
    std::string whereCol;
    Token whereVal;
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
    void handleDrop(const std::vector<Token>&, psqlStatement& );
    void handleUpdate(const std::vector<Token>&, psqlStatement& );
    void handleDelete(const std::vector<Token>&, psqlStatement& );

public:
    Parser();
    void parse(std::vector<Token> , psqlStatement& );
};

#endif
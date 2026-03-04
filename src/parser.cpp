#include "../include/parser.h"
#include <stdexcept>

Token Parser::cursor(const std::vector<Token>& tokens){
    if(static_cast<size_t>(current) >= tokens.size()){
        return {SYMBOL, ""};
    }
    else{
        return tokens[current];
    }
}

Token Parser::move(const std::vector<Token>& tokens){
    if(static_cast<size_t>(current) >= tokens.size()){
        return {SYMBOL, ""};
    }
    else{
        return tokens[current++];
    }
}

void Parser::handleCreate(const std::vector<Token>& tokens, psqlStatement& stmt){
    move(tokens);

    auto args = std::make_unique<createStatement>();
    
    if(cursor(tokens).type != KEYWORD || cursor(tokens).value != "table"){
        throw std::runtime_error("Missing 'table' keyword.\n");
    }
    move(tokens);
    
    if(cursor(tokens).type != IDENTIFIER){
        throw std::runtime_error("Missing table name.\n");
    }

    args->tableName = cursor(tokens).value;
    move(tokens);

    if(cursor(tokens).type != LPARA && cursor(tokens).type != SEMIC){
        throw std::runtime_error("Wrong create statement syntax.\n");
    }

    while(cursor(tokens).type != SEMIC){
        if(cursor(tokens).type == RPARA){
            move(tokens);
            if(cursor(tokens).type == SEMIC) break;
            else throw std::runtime_error("Expected ; right after ).\n");
        }
        move(tokens);
        if(cursor(tokens).type != IDENTIFIER){
            throw std::runtime_error("Missing column name.\n");
        }

        Attribute A;
        A.ColName = cursor(tokens).value;
        move(tokens);
        bool f1 = true, f2 = true, f3 = true;

        while(cursor(tokens).type != COMMA && cursor(tokens).type != RPARA){

            if(cursor(tokens).type == SEMIC){
                throw std::runtime_error("Missing right parenthesis.\n");
            }
            if(cursor(tokens).type != KEYWORD){
                throw std::runtime_error(cursor(tokens).value + " is not a keyword.\n");
            }
            if(cursor(tokens).value == "primary_key"){
                if(f2){
                    f2 = false;
                    A.isPrimary = true;
                }
                else{
                    throw std::runtime_error("Keyword 'primary_key' is mentioned more than once for a column.\n");
                }
            }
            else if(cursor(tokens).value == "not_null"){
                if(f3){
                    f3 = false;
                    A.canHoldNull = false;
                }
                else{
                    throw std::runtime_error("Keyword 'not_null' is mentioned more than once for a column.\n");
                }
            }
            else{
                std::string t = cursor(tokens).value;
                if(!f1){
                    throw std::runtime_error("A column cannot hold more than one data type.\n");
                }
                if(t == "int"){
                    f1 = false;
                    A.type = INT;  
                }
                if(t == "float"){
                    f1 = false;
                    A.type = FLOAT;   
                }
                if(t == "char"){
                    f1 = false;
                    A.type = CHAR;   
                }
                if(t == "string"){
                    f1 = false;
                    A.type = STRING;   
                }
            }
            move(tokens);
        }
        
        if(f1){
            throw std::runtime_error("Column data type is missing.\n");
        }

        args->attributes.push_back(A);
    }

    stmt.type = C_CREATE;
    stmt.args = std::move(args);
}

void Parser::handleInsert(const std::vector<Token>& tokens, psqlStatement& stmt){
    move(tokens);

    if(cursor(tokens).type != KEYWORD || cursor(tokens).value != "into"){
        throw std::runtime_error("Missing 'into' keyword.\n");
    }
    move(tokens);

    if(cursor(tokens).type != IDENTIFIER){
        throw std::runtime_error("Missing table name.\n");
    }
    
    auto args = std::make_unique<insertStatement>();
    args->tableName = cursor(tokens).value;
    move(tokens);

    if(cursor(tokens).type != KEYWORD || cursor(tokens).value != "values"){
        throw std::runtime_error("Missing 'values' keyword.\n");
    }
    move(tokens);

    if(cursor(tokens).type != LPARA){
        throw std::runtime_error("Expected '(' opening values list.\n");
    }
    move(tokens);

    while(cursor(tokens).type != RPARA && cursor(tokens).type != SEMIC){
        TOKEN_ID t = cursor(tokens).type;
        
        // Enforce basic literal types
        if(t != INT_LIT && t != FLOAT_LIT && t != STRING_LIT && t != CHAR_LIT && t != IDENTIFIER){
            if (!(t == KEYWORD && cursor(tokens).value == "null")) {
                throw std::runtime_error("Invalid literal value in insert list.\n");
            }
        }
        
        args->values.push_back(cursor(tokens));
        move(tokens);

        if(cursor(tokens).type == COMMA){
            move(tokens);
        } 
        else if(cursor(tokens).type != RPARA){
            throw std::runtime_error("Expected ',' or ')' in values list.\n");
        }
    }

    if(cursor(tokens).type != RPARA){
        throw std::runtime_error("Missing closing parenthesis ')' for values.\n");
    }
    move(tokens);

    if(cursor(tokens).type != SEMIC){
        throw std::runtime_error("Missing trailing semicolon ';'.\n");
    }
    move(tokens);

    stmt.type = C_INSERT;
    stmt.args = std::move(args);
}

void Parser::handleSelect(const std::vector<Token>& tokens, psqlStatement& stmt){
    move(tokens);

    if(cursor(tokens).type != SYMBOL || cursor(tokens).value != "*"){
        throw std::runtime_error("Expected '*' after select.\n");
    }
    move(tokens);

    if(cursor(tokens).type != KEYWORD || cursor(tokens).value != "from"){
        throw std::runtime_error("Expected 'from' keyword.\n");
    }
    move(tokens);

    if(cursor(tokens).type != IDENTIFIER){
        throw std::runtime_error("Missing table name.\n");
    }

    auto args = std::make_unique<selectStatement>();
    args->tableName = cursor(tokens).value;
    move(tokens);

    if(cursor(tokens).type != SEMIC){
        throw std::runtime_error("Missing trailing semicolon ';'.\n");
    }

    stmt.type = C_SELECT;
    stmt.args = std::move(args);
}

Parser::Parser(){
    current = 0;
}

void Parser::parse(std::vector<Token> tokens, psqlStatement& stmt){
    if(cursor(tokens).type == KEYWORD){
        if(cursor(tokens).value == "create") handleCreate(tokens, stmt);
        else if(cursor(tokens).value == "insert") handleInsert(tokens, stmt);
        else if(cursor(tokens).value == "select") handleSelect(tokens, stmt);
    }
    else{
        throw std::runtime_error(cursor(tokens).value + " is not an operation.\n");
    }
}
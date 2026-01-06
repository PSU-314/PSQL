#include "../include/parser.h"
#include <stdexcept>

Token Parser::cursor(const std::vector<Token>& tokens){
    if(current >= tokens.size()){
        return {SYMBOL, ""};
    }
    else{
        return tokens[current];
    }
}

Token Parser::move(const std::vector<Token>& tokens){
    if(current >= tokens.size()){
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
        move(tokens);
        if(cursor(tokens).type == SEMIC){
            throw std::runtime_error("Missing right parenthesis.\n");
        }
        if(cursor(tokens).type != IDENTIFIER){
            throw std::runtime_error("Missing column name.\n");
        }

        Attribute A;
        A.ColName = cursor(tokens).value;
        move(tokens);
        bool f1 = true, f2 = true, f3 = false;

        while(cursor(tokens).type != COMMA && cursor(tokens).type != RPARA){
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
                    A.type = CHAR;   
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

Parser::Parser(){
    current = 0;
}

void Parser::parse(std::vector<Token> tokens, psqlStatement& stmt){
    if(cursor(tokens).type == KEYWORD){
        if(cursor(tokens).value == "create") handleCreate(tokens, stmt);
    }
    else{
        throw std::runtime_error(cursor(tokens).value + " is not an operation.\n");
    }
}
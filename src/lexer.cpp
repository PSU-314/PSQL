#include "../include/lexer.h"
#include <stdexcept>
#include <vector>

const std::unordered_set<std::string> psql_keywords =
        {"create", "table", "int", "float",
            "char", "string", "insert","into", 
            "values", "select", "from",
            "primary_key", "not_null", "null", "drop",
            "update", "set", "where"}; 

void tokenize(const std::string& cmd, std::vector<Token>& tokens){
    int curr = 0, length = cmd.size();

    while(curr < length){
        char c = cmd[curr];

        if(c == ' '){
            curr++;
            continue;
        }
        if(c == '('){
            tokens.push_back({LPARA, "("});
            curr++;
            continue;
        }
        if(c == ')'){
            tokens.push_back({RPARA, ")"});
            curr++;
            continue;
        }
        if(c == ','){
            tokens.push_back({COMMA, ","});
            curr++;
            continue;
        }
        if(c == ';'){
            tokens.push_back({SEMIC, ";"});
            curr++;
            return;
        }
        if(c == '\''){
            std::string word;
            bool isEnclosed = false;
            curr++;
            while(curr < length){
                if(cmd[curr] == '\''){
                    isEnclosed = true;
                    if(word.size() == 1) tokens.push_back({CHAR_LIT, word});
                    else tokens.push_back({STRING_LIT, word});
                    curr++;
                    break;
                }
                else{
                    word += cmd[curr];
                    curr++;
                }
            }
            if(isEnclosed) continue;
            else throw std::runtime_error("Missing (') enclosing character.\n");            
        }

        if(isalpha(c)){
            std::string word;
            while(curr < length && (isalnum(cmd[curr]) || (cmd[curr] == '_'))){
                word += cmd[curr];
                curr++;
            }

            if(psql_keywords.find(word) != psql_keywords.end()){
                tokens.push_back({KEYWORD, word});
            }
            else{
                tokens.push_back({IDENTIFIER, word});
            }
            continue;
        }

        if(isdigit(c) || (c == '-' && curr < length-1 && isdigit(cmd[curr+1]))){
            std::string word;
            bool isFloat = false;

            if(c == '-') word += cmd[curr++];
            while(curr < length && (isdigit(cmd[curr]) || cmd[curr] == '.')){
                if(cmd[curr] == '.'){
                    if(isFloat) break;
                    isFloat = true;
                }
                word += cmd[curr];
                curr++;
            }
            
            if(isFloat) tokens.push_back({FLOAT_LIT, word});
            else tokens.push_back({INT_LIT, word});
            continue;
        }

        tokens.push_back({SYMBOL, std::string(1, c)});
        curr++;
    }

    throw std::runtime_error("Missing semicolon.\n");
}
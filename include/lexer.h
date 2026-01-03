#ifndef LEXER_H
#define LEXER_H

#include <string>
#include <vector>
#include <unordered_set>

extern const std::unordered_set<std::string> psql_keywords;

enum TOKEN_ID{
    KEYWORD,
    IDENTIFIER,
    SYMBOL,
    INT_LIT,
    FLOAT_LIT,
    STRING_LIT,
    CHAR_LIT,
    RPARA,
    LPARA,
    COMMA,
};

struct Token{
    TOKEN_ID type;
    std::string value;
};

void tokenize(const std::string& , std::vector<Token>& );

#endif
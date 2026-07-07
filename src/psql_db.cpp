#include "../include/psql.h"
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/executor.h"
#include <algorithm>

struct PSQLDatabase::EngineImpl{
    Executor E;

    std::string trimString(const std::string& s) const{
        std::string::size_type a = s.find_first_not_of(" \t\r\n");
        if(a == std::string::npos) return "";
        std::string::size_type b = s.find_last_not_of(" \t\r\n");
        std::string r = s.substr(a, b - a + 1);
        std::transform(r.begin(), r.end(), r.begin(), ::tolower);
        return r;
    }
};

PSQLDatabase::PSQLDatabase() : pimpl(std::make_unique<EngineImpl>()){}

PSQLDatabase::~PSQLDatabase() = default;

ResultSet PSQLDatabase::executeQuery(const std::string& buffer){
    std::string cmd = pimpl->trimString(buffer);
    ResultSet emptyResult;
    
    if(cmd.empty()){
        emptyResult.success = false;
        return emptyResult;
    }

    std::vector<Token> tokens;
    psqlStatement stmt;
    Parser P;
    
    try{
        tokenize(cmd, tokens);
        P.parse(tokens, stmt);
        
        return pimpl->E.execute(stmt); 
    }
    catch(const std::runtime_error& e){
        ResultSet r;
        r.success = false;
        r.errorMessage = std::string(e.what());
        return r;
    }
}
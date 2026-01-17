#include "../include/main.h"
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/executor.h"
#include <stdexcept>

static Executor E;

static std::string trimString(const std::string& s){
    int a = s.find_first_not_of(" \t\r\n");
    if(a == std::string::npos) return "";
    int b = s.find_last_not_of(" \t\r\n");
    std::string r = s.substr(a, b-a+1);
    std::transform(r.begin(), r.end(), r.begin(), tolower);
    return r;
}

void handleMetaCommand(const std::string& cmd){
    if(cmd == ".exit") exit(0);
    else if(cmd == ".help"){

    }
    else{
        print("Unknown Metacommand.\n", 1);
    }
}

void input(std::string& buffer){
    std::getline(std::cin, buffer);
}

void processInput(std::string& buffer){
    std::string cmd = trimString(buffer);

    if(cmd[0] == '.'){
        handleMetaCommand(cmd);
        return;
    }

    std::vector<Token> tokens;
    psqlStatement stmt;
    Parser P;
    try{
        tokenize(cmd, tokens);
        P.parse(tokens, stmt);
        E.execute(stmt);
    }
    catch(const std::runtime_error& e){
        print(e.what(), 1);
    }
}

void print(const std::string& output, const int type){
    if(type == 0){
        std::cout << output << std::flush;
    }
    else if(type == 1){
        std::cerr << "Error: " << output << std::flush;
    }
}

int main(){
    std::string buffer;
    while(true){
        print("psql >> ", 0);
        input(buffer);
        if(buffer.empty()) continue;
        processInput(buffer);
    }

    return 0;
}
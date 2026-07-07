#include "../include/psql.h"
#include "../include/cli.h" 
#include <iostream>
#include <cstdlib>
#include <string>

bool input(std::string& buffer){
    return bool(std::getline(std::cin, buffer));
}

void print(const std::string& output, const int type){
    if(type == 0){
        std::cout << output << std::flush;
    }
    else if(type == 1){
        std::cerr << "Error: " << output << std::flush;
    }
}

void handleMetaCommand(const std::string& cmd){
    if(cmd == ".exit") exit(0);
    else if(cmd == ".help"){
        print("Help menu...\n", 0);
    }
    else{
        print("Unknown Metacommand.\n", 1);
    }
}

void printResultSet(const ResultSet& result){
    if(!result.success){
        if(!result.errorMessage.empty()){
            print(result.errorMessage + "\n", 1);
        }
        return;
    }

    if(result.rows.empty() && result.rowsAffected > 0){
        print("Query OK, " + std::to_string(result.rowsAffected) + " rows affected.\n", 0);
        return;
    }

    if(!result.headers.empty()){
        for(const auto& header : result.headers){
            print(header + "\t| ", 0);
        }
        print("\n------------------------\n", 0);
        
        for(const auto& row : result.rows){
            for(const auto& val : row.values){
                print(val + "\t| ", 0);
            }
            print("\n", 0);
        }
    }
}

int main(){
    std::string buffer;
    PSQLDatabase db;

    while(true){
        print("psql >> ", 0); 
        if(!input(buffer)) break; 
        if(buffer.empty()) continue; 

        if(buffer[0] == '.'){
            handleMetaCommand(buffer); 
            continue;
        }

        try{
            ResultSet result = db.executeQuery(buffer);
            printResultSet(result);
        }
        catch(const std::exception& e) {
            print(e.what() + std::string("\n"), 1);
        }
    }

    return 0; 
}
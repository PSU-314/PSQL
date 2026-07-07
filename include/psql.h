#ifndef PSQL_H
#define PSQL_H

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

// Data Structures

struct Row {
    std::vector<std::string> values;
};

struct ResultSet {
    bool success = false;
    std::string errorMessage;
    
    std::vector<std::string> headers;
    std::vector<Row> rows;
    int rowsAffected = 0;
};

// Database Interface

class PSQLDatabase {
public:
    PSQLDatabase();
    ~PSQLDatabase();

    // Takes a raw SQL string, executes it, and returns the result
    ResultSet executeQuery(const std::string& query);

private:
    struct EngineImpl;
    std::unique_ptr<EngineImpl> pimpl;
};

#endif 
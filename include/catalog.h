#ifndef CATALOG_H
#define CATALOG_H

#include <string>
#include <unordered_map>
#include "table.h"

class Catalog{
private:
    std::string fullPathCatalog;

public:
    Catalog(const std::string& fileName, const std::string& dir);

    void saveCatalog(const std::unordered_map<std::string, Table*>& tableList);
    void loadCatalog(std::unordered_map<std::string, Table*>& tableList);
};

#endif
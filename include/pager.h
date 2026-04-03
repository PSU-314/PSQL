#ifndef PAGER_H
#define PAGER_H

#include <string>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <unordered_map>

const uint32_t PAGE_SIZE = 4096;

class Pager{
private:
    std::fstream fileStream;
    std::string filePath;
    uint32_t fileLength;
    uint32_t logicalFileLength;

    std::fstream walStream;
    std::string walPath;

    std::unordered_map<uint32_t, void*> dirtyPages;
    bool inTransaction;

    void replayWal();
    void writeToWal(uint32_t pageNum, const void* pageData);

public:
    Pager(const std::string& filename, const std::string& dir);
    ~Pager();

    void* getPage(uint32_t pageNum);
    void setPage(uint32_t pageNum, const void* pageData);
    
    uint32_t getFileLength() const;

    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();
};

#endif
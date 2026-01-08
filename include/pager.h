#ifndef PAGER_H
#define PAGER_H

#include <string>
#include <fstream>
#include <iostream>
#include <cstdint>

const uint32_t PAGE_SIZE = 4096;

class Pager{
private:
    std::fstream fileStream;
    std::string filePath;
    uint32_t fileLength;

public:
    Pager(const std::string& filename);
    ~Pager();

    void* getPage(uint32_t pageNum);
    void setPage(uint32_t pageNum, const void* pageData);
    
    uint32_t getFileLength() const;
};

#endif
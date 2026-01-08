#include "../include/pager.h"
#include <cstdlib>
#include <cstring>

Pager::Pager(const std::string& filename) : filePath(filename), fileLength(0){
    
    fileStream.open(filename, std::ios::in | std::ios::out | std::ios::binary);

    if(!fileStream.is_open()){
        fileStream.clear();
        fileStream.open(filename, std::ios::out | std::ios::binary);
        fileStream.close();
        fileStream.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }

    if(!fileStream.is_open()){
        throw std::runtime_error("Unable to open or create file " + filename);
    }

    fileStream.seekg(0, std::ios::end);
    fileLength = static_cast<uint32_t>(fileStream.tellg());
}

Pager::~Pager(){
    if(fileStream.is_open()){
        fileStream.close();
    }
}

void* Pager::getPage(uint32_t pageNum){
    void* page = std::malloc(PAGE_SIZE);
    if(!page) return nullptr;

    uint32_t offset = pageNum * PAGE_SIZE;

    if(offset < fileLength){
        fileStream.seekg(offset, std::ios::beg);
        fileStream.read(static_cast<char*>(page), PAGE_SIZE);
    }
    else{
        std::memset(page, 0, PAGE_SIZE);
    }

    return page;
}

void Pager::setPage(uint32_t pageNum, const void* pageData){
    uint32_t offset = pageNum * PAGE_SIZE;
    
    fileStream.seekp(offset, std::ios::beg);
    fileStream.write(static_cast<const char*>(pageData), PAGE_SIZE);
    fileStream.flush();

    fileStream.seekg(0, std::ios::end);
    fileLength = static_cast<uint32_t>(fileStream.tellg());
}

uint32_t Pager::getFileLength() const{ 
    return fileLength;
}
#include "../include/pager.h"
#include <cstdlib>
#include <cstring>
#include <vector>

Pager::Pager(const std::string& filename, const std::string& dir) 
    : filePath(dir + '/' + filename), walPath(dir + '/' + filename + ".wal"), 
      fileLength(0), logicalFileLength(0), inTransaction(false) {
    
    // Replay any complete transactions from WAL before opening the main DB stream
    replayWal();

    fileStream.open(filePath, std::ios::in | std::ios::out | std::ios::binary);

    if(!fileStream.is_open()){
        fileStream.clear();
        fileStream.open(filePath, std::ios::out | std::ios::binary);
        fileStream.close();
        fileStream.open(filePath, std::ios::in | std::ios::out | std::ios::binary);
    }

    if(!fileStream.is_open()){
        throw std::runtime_error("Unable to open or create file " + filename);
    }

    fileStream.seekg(0, std::ios::end);
    fileLength = static_cast<uint32_t>(fileStream.tellg());
    logicalFileLength = fileLength;
}

Pager::~Pager(){
    if (inTransaction) {
        rollbackTransaction();
    }
    if(fileStream.is_open()){
        fileStream.close();
    }
}

void Pager::replayWal() {
    std::fstream wal(walPath, std::ios::in | std::ios::binary);
    if (!wal.is_open()) return;

    bool needsReplay = false;
    wal.seekg(0, std::ios::end);
    if (wal.tellg() > 0) needsReplay = true;
    wal.seekg(0, std::ios::beg);

    if (!needsReplay) {
        wal.close();
        return;
    }

    std::fstream db(filePath, std::ios::in | std::ios::out | std::ios::binary);
    if (!db.is_open()) {
        db.clear();
        db.open(filePath, std::ios::out | std::ios::binary);
        db.close();
        db.open(filePath, std::ios::in | std::ios::out | std::ios::binary);
    }

    std::unordered_map<uint32_t, std::vector<char>> uncommittedPages;
    
    while (true) {
        char type;
        if (!wal.read(&type, 1)) break; // EOF or partial write

        if (type == 1) { // PAGE Record
            uint32_t pageNum;
            if (!wal.read(reinterpret_cast<char*>(&pageNum), sizeof(pageNum))) break;
            std::vector<char> pageData(PAGE_SIZE);
            if (!wal.read(pageData.data(), PAGE_SIZE)) break;
            uncommittedPages[pageNum] = std::move(pageData);
        } 
        else if (type == 2) { // COMMIT Record
            for (const auto& pair : uncommittedPages) {
                db.seekp(pair.first * PAGE_SIZE, std::ios::beg);
                db.write(pair.second.data(), PAGE_SIZE);
            }
            db.flush();
            uncommittedPages.clear();
        } 
        else {
            break; // Corrupt WAL
        }
    }
    wal.close();
    db.close();

    // Clear WAL once replayed
    std::fstream clearWal(walPath, std::ios::out | std::ios::trunc | std::ios::binary);
}

void Pager::beginTransaction() {
    if (inTransaction) return;
    walStream.open(walPath, std::ios::out | std::ios::app | std::ios::binary);
    inTransaction = true;
}

void Pager::commitTransaction() {
    if (!inTransaction) return;

    // Write COMMIT marker to WAL
    char type = 2;
    walStream.write(&type, 1);
    walStream.flush();
    walStream.close();

    // Flush dirty pages to the main DB
    for (const auto& pair : dirtyPages) {
        uint32_t offset = pair.first * PAGE_SIZE;
        fileStream.seekp(offset, std::ios::beg);
        fileStream.write(static_cast<const char*>(pair.second), PAGE_SIZE);
    }
    fileStream.flush();
    
    fileLength = logicalFileLength;

    // Clear dirty pages from memory
    for (auto& pair : dirtyPages) {
        std::free(pair.second);
    }
    dirtyPages.clear();

    // Truncate WAL 
    std::fstream clearWal(walPath, std::ios::out | std::ios::trunc | std::ios::binary);
    inTransaction = false;
}

void Pager::rollbackTransaction() {
    if (!inTransaction) return;
    walStream.close();
    
    for (auto& pair : dirtyPages) {
        std::free(pair.second);
    }
    dirtyPages.clear();
    logicalFileLength = fileLength;
    
    // Truncate the uncommitted data
    std::fstream clearWal(walPath, std::ios::out | std::ios::trunc | std::ios::binary);
    inTransaction = false;
}

void Pager::writeToWal(uint32_t pageNum, const void* pageData) {
    char type = 1;
    walStream.write(&type, 1);
    walStream.write(reinterpret_cast<const char*>(&pageNum), sizeof(pageNum));
    walStream.write(static_cast<const char*>(pageData), PAGE_SIZE);
    walStream.flush();
}

void* Pager::getPage(uint32_t pageNum){
    // Must return cached copy if dirtied in current transaction
    if (inTransaction && dirtyPages.find(pageNum) != dirtyPages.end()) {
        void* page = std::malloc(PAGE_SIZE);
        std::memcpy(page, dirtyPages[pageNum], PAGE_SIZE);
        return page;
    }

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
    if (!inTransaction) {
        // Auto-commit safety wrapper for non-transactional calls
        beginTransaction();
        writeToWal(pageNum, pageData);
        commitTransaction();
        return;
    }

    writeToWal(pageNum, pageData);

    // Deep copy into the transaction cache
    void* cachePage = std::malloc(PAGE_SIZE);
    std::memcpy(cachePage, pageData, PAGE_SIZE);
    
    if (dirtyPages.find(pageNum) != dirtyPages.end()) {
        std::free(dirtyPages[pageNum]);
    }
    dirtyPages[pageNum] = cachePage;

    uint32_t offset = (pageNum + 1) * PAGE_SIZE;
    if (offset > logicalFileLength) {
        logicalFileLength = offset;
    }
}

uint32_t Pager::getFileLength() const{ 
    return logicalFileLength; 
}
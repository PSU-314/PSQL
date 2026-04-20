#include "../include/pager.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

const uint32_t INVALID_FRAME_PAGE = UINT32_MAX;

Pager::Pager(const std::string& filename, const std::string& dir)
    : filePath(dir + '/' + filename), fileLength(0), logicalFileLength(0),
      walPath(dir + '/' + filename + ".wal"), inTransaction(false),
      clockHand(0) {

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

    // Allocate the fixed pool once
    pool = static_cast<char*>(std::malloc(static_cast<size_t>(NUM_FRAMES) * PAGE_SIZE));
    if(!pool){
        throw std::runtime_error("Failed to allocate buffer pool.\n");
    }
    frames.resize(NUM_FRAMES);
    for(uint32_t i = 0; i < NUM_FRAMES; i++){
        frames[i].pageNum = INVALID_FRAME_PAGE;
        frames[i].valid = false;
        frames[i].dirty = false;
        frames[i].refBit = false;
        frames[i].pinCount = 0;
    }
}

Pager::~Pager(){
    if(inTransaction){
        rollbackTransaction();
    }
    // Flush any dirty frames still resident so nothing is lost.
    for(uint32_t i = 0; i < NUM_FRAMES; i++){
        if(frames[i].valid && frames[i].dirty){
            flushFrame(i);
        }
    }
    if(fileStream.is_open()){
        fileStream.flush();
        fileStream.close();
    }
    std::free(pool);
}

// WAL replay

void Pager::replayWal(){
    std::fstream wal(walPath, std::ios::in | std::ios::binary);
    if(!wal.is_open()) return;

    bool needsReplay = false;
    wal.seekg(0, std::ios::end);
    if(wal.tellg() > 0) needsReplay = true;
    wal.seekg(0, std::ios::beg);

    if(!needsReplay){
        wal.close();
        return;
    }

    std::fstream db(filePath, std::ios::in | std::ios::out | std::ios::binary);
    if(!db.is_open()){
        db.clear();
        db.open(filePath, std::ios::out | std::ios::binary);
        db.close();
        db.open(filePath, std::ios::in | std::ios::out | std::ios::binary);
    }

    std::unordered_map<uint32_t, std::vector<char>> uncommittedPages;

    while(true){
        char type;
        if(!wal.read(&type, 1)) break;

        if(type == 1){
            uint32_t pageNum;
            if(!wal.read(reinterpret_cast<char*>(&pageNum), sizeof(pageNum))) break;
            std::vector<char> pageData(PAGE_SIZE);
            if(!wal.read(pageData.data(), PAGE_SIZE)) break;
            uncommittedPages[pageNum] = std::move(pageData);
        }
        else if(type == 2){
            for(const auto& pair : uncommittedPages){
                db.seekp(pair.first * PAGE_SIZE, std::ios::beg);
                db.write(pair.second.data(), PAGE_SIZE);
            }
            db.flush();
            uncommittedPages.clear();
        }
        else{
            break;
        }
    }
    wal.close();
    db.close();

    std::fstream clearWal(walPath, std::ios::out | std::ios::trunc | std::ios::binary);
}

void Pager::writeToWal(uint32_t pageNum, const void* pageData){
    char type = 1;
    walStream.write(&type, 1);
    walStream.write(reinterpret_cast<const char*>(&pageNum), sizeof(pageNum));
    walStream.write(static_cast<const char*>(pageData), PAGE_SIZE);
    walStream.flush();
}

// Buffer pool

void Pager::flushFrame(uint32_t frameIndex){
    Frame& f = frames[frameIndex];
    if(!f.valid || !f.dirty) return;

    char* pageData = pool + static_cast<size_t>(frameIndex) * PAGE_SIZE;
    uint32_t offset = f.pageNum * PAGE_SIZE;

    fileStream.seekp(offset, std::ios::beg);
    fileStream.write(pageData, PAGE_SIZE);
    fileStream.flush();

    if(offset + PAGE_SIZE > fileLength){
        fileLength = offset + PAGE_SIZE;
    }
    f.dirty = false;
}

uint32_t Pager::evictFrame(){
    uint32_t scanned = 0;
    while(true){
        Frame& f = frames[clockHand];

        if(!f.valid){
            uint32_t chosen = clockHand;
            clockHand = (clockHand + 1) % NUM_FRAMES;
            return chosen;
        }

        if(f.pinCount == 0){
            if(f.refBit){
                f.refBit = false;
            }
            else{
                // Evict this frame.
                if(f.dirty) flushFrame(clockHand);
                pageTable.erase(f.pageNum);
                uint32_t chosen = clockHand;
                f.valid = false;
                f.pageNum = INVALID_FRAME_PAGE;
                clockHand = (clockHand + 1) % NUM_FRAMES;
                return chosen;
            }
        }

        clockHand = (clockHand + 1) % NUM_FRAMES;
        scanned++;
        if(scanned > 2 * NUM_FRAMES){
            throw std::runtime_error(
                "Buffer pool exhausted: all frames pinned. Increase "
                "BUFFER_POOL_BYTES or reduce concurrently pinned pages.\n");
        }
    }
}

void Pager::loadPageIntoFrame(uint32_t pageNum, uint32_t frameIndex){
    char* dest = pool + static_cast<size_t>(frameIndex) * PAGE_SIZE;
    uint32_t offset = pageNum * PAGE_SIZE;

    if(offset < fileLength){
        fileStream.seekg(offset, std::ios::beg);
        fileStream.read(dest, PAGE_SIZE);
    }
    else{
        std::memset(dest, 0, PAGE_SIZE);
    }

    Frame& f = frames[frameIndex];
    f.pageNum = pageNum;
    f.valid = true;
    f.dirty = false;
    f.refBit = false;
    f.pinCount = 0;

    pageTable[pageNum] = frameIndex;
}

uint32_t Pager::findFrameForPage(uint32_t pageNum){
    auto it = pageTable.find(pageNum);
    if(it != pageTable.end()){
        return it->second;
    }
    uint32_t frameIndex = evictFrame();
    loadPageIntoFrame(pageNum, frameIndex);
    return frameIndex;
}


void* Pager::getPage(uint32_t pageNum){
    uint32_t frameIndex = findFrameForPage(pageNum);
    Frame& f = frames[frameIndex];
    f.pinCount++;
    f.refBit = true;
    return pool + static_cast<size_t>(frameIndex) * PAGE_SIZE;
}

void Pager::unpinPage(uint32_t pageNum, bool markDirty){
    auto it = pageTable.find(pageNum);
    if(it == pageTable.end()) return;

    Frame& f = frames[it->second];
    if(markDirty){
        f.dirty = true;
        if(inTransaction){
            writeToWal(pageNum, pool + static_cast<size_t>(it->second) * PAGE_SIZE);
            txnDirtyFrames[it->second] = true;

            uint32_t offset = (pageNum + 1) * PAGE_SIZE;
            if(offset > logicalFileLength) logicalFileLength = offset;
        }
    }
    if(f.pinCount > 0) f.pinCount--;
}

void Pager::setPage(uint32_t pageNum, const void* pageData){
    uint32_t frameIndex = findFrameForPage(pageNum);
    Frame& f = frames[frameIndex];
    f.pinCount++;
    char* dest = pool + static_cast<size_t>(frameIndex) * PAGE_SIZE;
    std::memcpy(dest, pageData, PAGE_SIZE);
    f.refBit = true;
    unpinPage(pageNum, true);
}

uint32_t Pager::getFileLength() const{
    return logicalFileLength;
}

// Transactions

void Pager::beginTransaction(){
    if(inTransaction) return;
    walStream.open(walPath, std::ios::out | std::ios::app | std::ios::binary);
    txnDirtyFrames.clear();
    inTransaction = true;
}

void Pager::commitTransaction(){
    if(!inTransaction) return;

    char type = 2;
    walStream.write(&type, 1);
    walStream.flush();
    walStream.close();

    // Flush every frame touched during this transaction to the main DB file.
    for(const auto& pair : txnDirtyFrames){
        uint32_t frameIndex = pair.first;
        if(frames[frameIndex].valid && frames[frameIndex].dirty){
            flushFrame(frameIndex);
        }
    }
    fileLength = logicalFileLength;
    txnDirtyFrames.clear();

    std::fstream clearWal(walPath, std::ios::out | std::ios::trunc | std::ios::binary);
    inTransaction = false;
}

void Pager::rollbackTransaction(){
    if(!inTransaction) return;
    walStream.close();

    for(const auto& pair : txnDirtyFrames){
        uint32_t frameIndex = pair.first;
        Frame& f = frames[frameIndex];
        if(f.valid){
            loadPageIntoFrame(f.pageNum, frameIndex); // re-read from disk, resets dirty/pin
        }
    }
    txnDirtyFrames.clear();
    logicalFileLength = fileLength;

    std::fstream clearWal(walPath, std::ios::out | std::ios::trunc | std::ios::binary);
    inTransaction = false;
}
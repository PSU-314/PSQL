#ifndef PAGER_H
#define PAGER_H

#include <string>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <unordered_map>
#include <vector>

const uint32_t PAGE_SIZE = 4096;

// Total pool size in bytes. 64MB / 4KB = 16384 frames.
const uint32_t BUFFER_POOL_BYTES = 64u * 1024u * 1024u;
const uint32_t NUM_FRAMES = BUFFER_POOL_BYTES / PAGE_SIZE;

struct Frame{
    uint32_t pageNum;   // logical page number currently held; INVALID if unused
    bool valid;         // frame currently holds a page
    bool dirty;         // frame has been modified since load
    bool refBit;        // clock-sweep reference bit
    uint32_t pinCount;  // number of active pins (callers holding the pointer)
};

class Pager{
private:
    std::fstream fileStream;
    std::string filePath;
    uint32_t fileLength;
    uint32_t logicalFileLength;

    std::fstream walStream;
    std::string walPath;
    bool inTransaction;

    // Fixed buffer pool
    char* pool;                                   // NUM_FRAMES * PAGE_SIZE contiguous bytes
    std::vector<Frame> frames;                    // metadata per frame
    std::unordered_map<uint32_t, uint32_t> pageTable; // pageNum -> frameIndex
    uint32_t clockHand;

    // Transaction-local write buffer
    std::unordered_map<uint32_t, bool> txnDirtyFrames; // frameIndex -> true

    void replayWal();
    void writeToWal(uint32_t pageNum, const void* pageData);

    uint32_t findFrameForPage(uint32_t pageNum); // returns frame index, loading/evicting as needed
    uint32_t evictFrame();                        // clock sweep; returns a frame index ready for reuse
    void flushFrame(uint32_t frameIndex);          // write frame's page to disk if dirty
    void loadPageIntoFrame(uint32_t pageNum, uint32_t frameIndex);

public:
    Pager(const std::string& filename, const std::string& dir);
    ~Pager();

    void* getPage(uint32_t pageNum);

    void unpinPage(uint32_t pageNum, bool markDirty);

    void setPage(uint32_t pageNum, const void* pageData);

    uint32_t getFileLength() const;

    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();
};

#endif
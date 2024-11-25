#pragma once
#include <utility>
#include <map>
#include <vector>
#include <functional>

int bestFit(int sizeInWords, void* list);
int worstFit(int sizeInWords, void* list);

class MemoryManager {

    unsigned wordSize;
    std::function<int(int, void*)> allocator;

    size_t sizeInWords;
    uint8_t* memo;
    uint8_t* bitmap;

    std::map<int, std::pair<size_t, int>> layout;

    public:

        MemoryManager(unsigned wordSize, std::function<int(int, void*)> allocator);
        ~MemoryManager();
        void initialize(size_t sizeInWords);
        void shutdown();
        void* allocate(size_t sizeInBytes);
        void free(void* address);
        void setAllocator(std::function<int(int, void*)> allocator);
        int dumpMemoryMap(char* filename);
        void* getList();
        void* getBitmap();
        unsigned getWordSize();
        void* getMemoryStart();
        unsigned getMemoryLimit();
};

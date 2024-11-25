#include "MemoryManager.h"
#include <sys/mman.h>
#include <stdio.h>
#include <iostream>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <sstream>
#include <bitset>
using namespace std;

int bestFit(int sizeInWords, void* list) {
    uint16_t* myList = static_cast<uint16_t*>(list);
    if (myList == nullptr) {
        return 0;
    }
    int offset = -1;
    uint16_t minLength = UINT16_MAX;
    uint16_t size = myList[0] * 2 + 1;
    for (uint16_t i = 1; i < size; i += 2) {
        if (myList[i+1] >= sizeInWords) {
            if (myList[i+1] < minLength) {
                minLength = myList[i+1];
                offset = myList[i];
            }
        }
    }
    myList = nullptr;
    return offset;
}

int worstFit(int sizeInWords, void* list) {
    uint16_t* myList = static_cast<uint16_t*>(list);
    if (myList == nullptr) {
        return 0;
    }
    int offset = -1;
    uint16_t maxLength = 0;
    uint16_t size = myList[0] * 2 + 1;
    for (uint16_t i = 1; i < size; i += 2) {
        if (myList[i+1] >= sizeInWords) {
            if (myList[i+1] > maxLength) {
                maxLength = myList[i+1];
                offset = myList[i];
            }
        }
    }
    myList = nullptr;
    return offset;
}

MemoryManager::MemoryManager(unsigned wordSize, function<int(int, void*)> allocator) {
    this->wordSize = wordSize;
    this->allocator = allocator;
    memo = nullptr;
}

MemoryManager::~MemoryManager() {
    shutdown();
}

void MemoryManager::initialize(size_t sizeInWords) {
    if (sizeInWords > 65536) {
        //printf("Greedy, are we\n");
        return;
    }
    if (memo != nullptr) {
        shutdown();
    }
    this->memo = static_cast<uint8_t*>(mmap(NULL, sizeInWords * this->wordSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    this-> sizeInWords = sizeInWords;
    int bitmapSize = ceil(sizeInWords / 8.0) * 8;
    this->bitmap = new uint8_t[bitmapSize];
    for (int i = 0; i < bitmapSize; i++) {
        bitmap[i] = 0;
    }
    layout.emplace(0, make_pair(sizeInWords, 0));
}

void MemoryManager::shutdown() {
    if (memo == nullptr) {
        return;
    }
    munmap((void*) memo, sizeInWords * wordSize);
    delete [] bitmap;
    bitmap = nullptr;
    layout.clear();
}

void* MemoryManager::allocate(size_t sizeInBytes) {
    size_t wordCount = ceil(sizeInBytes / wordSize);
    void* myList = getList();
    int myOffset = allocator(wordCount, myList);
    uint16_t* delList = static_cast<uint16_t*>(myList);
    delete [] delList;
    myList = nullptr;
    delList = nullptr;
    if (myOffset == -1) {
        return nullptr;
    }
    for (int i = 0; i < wordCount; i++) {
        bitmap[myOffset + i] = 1;
    }
    auto nodeA = layout.find(myOffset);
    if (wordCount < (nodeA->second).first) {
        layout.emplace(myOffset + wordCount, make_pair((nodeA->second).first - wordCount, 0));
    }
    (nodeA->second).first = wordCount;
    (nodeA->second).second = 1;
    void* ptr = memo + (uint8_t)myOffset * (uint8_t)wordSize;
    return ptr;
}

void MemoryManager::free(void* address) {
    int offset = (static_cast<uint8_t*>(address) - memo) / (uint8_t)wordSize;
    auto nodeA = layout.find(offset);
    for (int i = 0; i < (nodeA->second).first; i++) {
        bitmap[offset + i] = 0;
    }
    (nodeA->second).second = 0;
    if (next(nodeA) != layout.cend()) {
	auto nodeB = next(nodeA);
	if ((nodeB->second).second == 0) {
            (nodeA->second).first += (nodeB->second).first;
            layout.erase(nodeB);
        }
    }
    if (nodeA != layout.cbegin()) {
	auto nodeZ = prev(nodeA);
	if ((nodeZ->second).second == 0) {
            (nodeZ->second).first += (nodeA->second).first;
	    layout.erase(nodeA);
        }
    }
}

void MemoryManager::setAllocator(function<int(int, void*)> allocator) {
    this->allocator = allocator;
}

int MemoryManager::dumpMemoryMap(char* filename) {
    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0777);
    if (fd == -1) {
        return -1;
    }
    uint16_t* myList = (uint16_t*)getList();
    stringstream ss;
    if (myList[0] != 0) {
        ss << "[";
    }
    for (int i = 1; i < 2 * myList[0] - 1; i += 2) {
        ss << myList[i] << ", " << myList[i+1] << "] - [";
    }
    int lastInd = 2 * myList[0] - 1;
    ss << myList[lastInd] << ", " << myList[lastInd+1] << "]";
    string toWrite = ss.str();
    if (write(fd, toWrite.c_str(), toWrite.size()) != (ssize_t) toWrite.size()) {
        return -1;
    }
    close (fd);
    delete [] myList;
    myList = nullptr;
    return 0;
}

void* MemoryManager::getList() {
    vector<uint16_t> temp;
    temp.push_back(0);
    for (auto i = layout.begin(); i != layout.end(); i++) {
        if ((i->second).second == 0) {
            temp[0]++;
            temp.push_back(i->first);
            temp.push_back((i->second).first);
        }
    }
    int size = temp.size();
    uint16_t* arr = new uint16_t[size];
    arr[0] = temp[0];
    for (int i = 1; i < temp.size(); i++) {
        arr[i] = temp[i];
    }
    return arr;
}

void* MemoryManager::getBitmap() {
    int numBytes = ceil(sizeInWords / 8.0);
    uint8_t* byteMap = new uint8_t[numBytes + 2];
    byteMap[0] = (uint8_t)numBytes & 0xFF;
    byteMap[1] = (uint8_t)((numBytes >> 8 ) & 0xFF);
    int byteMapIter = 2;
    for (int i = 0; i < numBytes * 8; i += 8) {
        string byte = "00000000";
        for (int j = 0; j < 8; j++) {
            if (bitmap[i + 7 - j] == 1) {
                byte[j] = '1';
            }
        }
        bitset<8> toAdd(byte);
        byteMap[byteMapIter] = static_cast<uint8_t>(toAdd.to_ulong());
        byteMapIter++;
    }
    return byteMap;
}

unsigned MemoryManager::getWordSize() {
    return wordSize;
}

void* MemoryManager::getMemoryStart() {
    return memo;
}

unsigned MemoryManager::getMemoryLimit() {
    return (unsigned)sizeInWords * wordSize;
}

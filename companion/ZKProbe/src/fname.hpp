// fname.hpp — tim FNamePool (GNames) under an experimental layout trong runtime, khong can offset tinh
#pragma once

#include "dyld_util.hpp"
#include <string>
#include <vector>
#include <functional>

namespace zk {

const uint32_t kBlockSize = 0x10000;   // FNamePool: 1 block = 64 KB (BlockBits = 16)
const uint32_t kMaxBlocks = 512;       // explicit scan cap; not inferred block count

struct FNamePool {
    uintptr_t blocksAddr = 0;   // dia chi mang Blocks[0] (uint8*[])
    uintptr_t poolAddr = 0;     // dia chi struct FNamePool (0 until the actual pool header is verified)
    uintptr_t rvaBlocks = 0;    // blocksAddr - mainBase
    uintptr_t rvaPool = 0;      // poolAddr  - mainBase
    bool found() const { return blocksAddr != 0; }
};

// Doc 1 FName theo id (id = (blockIndex << 16) | byteOffset)
std::string fnameById(uintptr_t blocks, uint32_t id);

// Kiem tra 1 dia chi co phai mang Blocks[] hop le khong.
// Tra ve % entry dau tien parse duoc (0..100), -1 = khong hop le.
int poolScore(uintptr_t blocksAddr);

// Quet toan bo vung RW de tim FNamePool.
FNamePool findFNamePool();

// Tim id cua ten (so sanh chinh xac). Tra ve 0 neu khong thay.
uint32_t findNameId(uintptr_t blocks, const std::string &name);

// Liet ke toan bo ten (dung cho dump)
void forEachName(uintptr_t blocks, const std::function<bool(uint32_t, const std::string &)> &fn);

} // namespace zk

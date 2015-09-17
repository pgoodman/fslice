/* Copyright 2015 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <utility>
#include <set>
#include <vector>

struct Taint {
  uint32_t id;
  uint32_t offset;
} __attribute__((packed));

static Taint gArgs[16] = {{0,0}};
static Taint gReturn = {0,0};
static std::unordered_map<uint64_t,Taint> gShadow;
static std::unordered_map<uint64_t,Taint> gValues;
static std::unordered_map<uint64_t,Taint> gObjects;
static std::set<uint64_t> gPrintedBlocks;
static std::unordered_map<uint64_t,Taint> gBlocks;
static std::unordered_map<uint64_t,uint64_t> gPrevBlock;
static std::unordered_map<uint64_t,std::vector<Taint>> gPrevBlockData;
static std::unordered_map<const char *,std::unordered_map<uint64_t,Taint>> gBinaryOps;
static unsigned gId = 1;

extern "C" Taint __fslice_value(uint64_t);

// Load a taint from the shadow memory.
static Taint Load(uint64_t addr, uint64_t size) {

  // So this isn't super great, but it's sufficient for now. Hopefully there
  // are no collisions!
  uint64_t obj_hash = 0;
  for (auto i = 0U; i < size; ++i) {
    const auto mt = gShadow[addr + i];
    obj_hash = ((obj_hash ^ mt.id) << 27) | ((obj_hash >> 19) ^ mt.offset);
  }

  auto &t = gObjects[obj_hash];
  if (t.id) return t;

  t = {gId++, 0};
  auto sep = "";
  std::cerr << "t" << t.id << "=O(";
  for (auto i = 0U; i < size; ++i) {
    const auto mt = gShadow[addr + i];
    std::cerr << sep << "t" << mt.id << "[" << mt.offset << "]";
    sep = ",";
  }
  std::cerr << ")" << std::endl;
  return t;
}

// Store a taint to the shadow memory.
static void Store(uint64_t addr, uint64_t size, Taint taint) {
  for (auto i = 0U; i < size; ++i) {
    gShadow[addr + i] = {taint.id, taint.offset + i};  // should be `taint.offset + i`?
  }
}

#define LOAD_STORE(size) \
  extern "C" Taint __fslice_load ## size (uint64_t addr) { \
    return Load(addr, size); \
  } \
  extern "C" void __fslice_store ## size (uint64_t addr, Taint taint) { \
    Store(addr, size, taint); \
  }

LOAD_STORE(1)
LOAD_STORE(2)
LOAD_STORE(4)
LOAD_STORE(8)
LOAD_STORE(16)
LOAD_STORE(32)
LOAD_STORE(64)

extern "C" Taint __fslice_load_ret(void) {
  memset(gArgs, 0, sizeof gArgs);
  const auto t = gReturn;
  gReturn = {0,0};
  return t;
}

extern "C" void __fslice_store_ret(Taint taint) {
  memset(gArgs, 0, sizeof gArgs);
  gReturn = taint;
}

extern "C" Taint __fslice_load_arg(uint64_t i) {
  const auto t = gArgs[i];
  gArgs[i] = {0,0};
  return t;
}

extern "C" void __fslice_store_arg(uint64_t i, Taint taint) {
  gArgs[i] = taint;
}

extern "C" void *__fslice_memset(void *dst, int val, uint64_t size) {
  const auto t = __fslice_load_arg(1);
  const auto daddr = reinterpret_cast<uint64_t>(dst);
  for (auto i = 0U; i < size; ++i) {
    gShadow[daddr + i] = t;
  }
  __fslice_store_ret({0,0});
  return memset(dst, val, size);
}

extern "C" void *__fslice_memmove(void *dst, const void *src, uint64_t size) {
  const auto daddr = reinterpret_cast<uint64_t>(dst);
  const auto saddr = reinterpret_cast<uint64_t>(src);
  for (auto i = 0U; i < size; ++i) {
    gShadow[daddr + i] = gShadow[saddr + i];  // Not right but whatever.
  }
  __fslice_store_ret({0,0});
  return memmove(dst, src, size);
}

extern "C" void *__fslice_memcpy(void *dst, const void *src, uint64_t size) {
  return __fslice_memmove(dst, src, size);
}

extern "C" char *__fslice_strcpy(char *dst, const char *src) {
  return reinterpret_cast<char *>(__fslice_memmove(dst, src, strlen(src)));
}

extern "C" Taint __fslice_value(uint64_t val) {
  auto &t = gValues[val];
  if (val && !t.id) {
    t = {gId++, 0};
    std::cerr << "t" << t.id << "=V(" << val << ")" << std::endl;
  }
  return t;
}

extern "C" Taint __fslice_op2(const char *op, Taint t1, Taint t2) {
  const auto id = t1.id | (static_cast<uint64_t>(t2.id) << 32);
  auto &t = gBinaryOps[op][id];
  if (!t.id) {
    t = {gId++, 0};
    std::cerr << "t" << t.id << "=A(\"" << op << "\",t" << t1.id
              << ",t" << t2.id << ")" << std::endl;
  }
  return t;
}

static uint64_t BlockId(Taint s, Taint n) {
  return s.id | (static_cast<uint64_t>(n.id) << 32);
}

static void PrintBlock(Taint bt, uint64_t size, uint64_t nr,
                       Taint st, Taint nt) {
  auto id = BlockId(st, nt);
  if (!gPrintedBlocks.count(id)) {
    gPrintedBlocks.insert(id);
    std::cerr << "t" << bt.id << "=B(" << size << "," << nr << ",t"
              << st.id << ",t" << nt.id << ")" << std::endl;
  }
  __fslice_store_ret({0,0});
}

extern "C" void __fslice_read_block(uint64_t addr, uint64_t size, uint64_t nr) {
  const auto st = __fslice_load_arg(1);  // Taint for the size :-)
  const auto nt = __fslice_load_arg(2);  // Taint for the block number :-)
  auto id = BlockId(st, nt);
  auto &t = gBlocks[id];
  if (!t.id) {
    t = {gId++,0};
  }

  PrintBlock(t, size, nr, st, nt);

  // Get the most recent version of this block.
  auto &taints = gPrevBlockData[nr];
  for (auto i = 0UL; i < std::min(taints.size(), size); ++i) {
    const auto ot = taints[i];
    if (ot.id && !(ot.id == t.id && ot.offset == i)) {
      std::cerr << "t" << t.id << "[" << i << "]=t" << ot.id
                << "[" << ot.offset << "]" << std::endl;
    }
  }
  taints.clear();

  for (auto i = 0U; i < size; ++i) {
    gShadow[addr + i] = {t.id, i};
  }
}

// Mark some memory as a block.
extern "C" void __fslice_write_block(uint64_t addr, uint64_t size, uint64_t nr) {
  auto &taints = gPrevBlockData[nr];
  taints.clear();
  taints.reserve(size);
  for (auto i = 0U; i < size; ++i) {
    taints.push_back(gShadow[addr + i]);
  }
}

// Mark some memory as a name.
extern "C" void __fslice_name(uint64_t addr, uint64_t len) {

}

// Mark some memory as data.
extern "C" void __fslice_data(uint64_t addr, uint64_t len) {

}

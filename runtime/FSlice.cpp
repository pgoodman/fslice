/* Copyright 2015 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <utility>
#include <set>
#include <vector>
#include <cerrno>

struct Taint {
  uint64_t id:32;
  uint64_t offset:31;
  bool is_obj:1;
} __attribute__((packed));

struct SaveErrno {
  int no;
  SaveErrno(void)
      : no(errno) {}
  ~SaveErrno(void) {
    errno = no;
  }
};

// Try to merge things like binary operators, constants, and memory loads
// into single taint nodes.
#define CACHE 1

// Treat heap-allocated memory objects as special intermediate objects.
#define MEM false

static Taint gArgs[16] = {{0,0}};
static Taint gReturn = {0,0};
static std::unordered_map<uint64_t,Taint> gShadow;
static std::unordered_map<uint64_t,Taint> gValues;
static std::unordered_map<uint64_t,Taint> gObjects;
static std::set<uint64_t> gPrintedBlocks;
static std::unordered_map<uint64_t,Taint> gBlocks;
static std::unordered_map<uint64_t,uint64_t> gPrevBlock;
static std::unordered_map<const char *,std::unordered_map<uint64_t,Taint>> gBinaryOps;
static unsigned gId = 1;

extern "C" Taint __fslice_value(uint64_t);

// Load a taint from the shadow memory.
static Taint Load(uint64_t addr, uint64_t size) {
  SaveErrno save_errno;
#if CACHE
  // So this isn't super great, but it's sufficient for now. Hopefully there
  // are no collisions!
  uint64_t obj_hash = 0;
  for (auto i = 0U; i < size; ++i) {
    const auto mt = gShadow[addr + i];
    obj_hash = ((obj_hash ^ mt.id) << 27) | ((obj_hash >> 19) ^ mt.offset);
  }

  auto &t = gObjects[obj_hash];
  if (t.id) return t;
  t = {gId++, 0, false};
#else
  Taint t = {gId++, 0, false};
#endif
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
static void Store(uint64_t addr, uint64_t size, Taint t) {
  SaveErrno save_errno;
  for (auto i = 0U; i < size; ++i) {
    auto &et = gShadow[addr + i];
    if (et.is_obj) {
      std::cerr << "t" << et.id << "[" << et.offset << "]=t" << t.id << "["
                << (t.offset + i) << "]" << std::endl;
    } else {
      et = {t.id, t.offset + i, false};  // should be `taint.offset + i`?
    }
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
  gReturn = {0,0,false};
  return t;
}

extern "C" void __fslice_store_ret(Taint taint) {
  memset(gArgs, 0, sizeof gArgs);
  gReturn = {taint.id, taint.offset, false};
}

extern "C" Taint __fslice_load_arg(uint64_t i) {
  const auto t = gArgs[i];
  gArgs[i] = {0,0,false};
  return t;
}

extern "C" void __fslice_store_arg(uint64_t i, Taint taint) {
  gArgs[i] = {taint.id, taint.offset, false};
}

extern "C" void *__fslice_memset(void *dst, int val, uint64_t size) {
  SaveErrno save_errno;
  const auto t = __fslice_load_arg(1);
  const auto daddr = reinterpret_cast<uint64_t>(dst);
  for (auto i = 0U; i < size; ++i) {
    gShadow[daddr + i] = t;
  }
  __fslice_store_ret({0,0,false});
  return memset(dst, val, size);
}

extern "C" void *__fslice_memmove(void *dst, const void *src, uint64_t size) {
  SaveErrno save_errno;
  const auto daddr = reinterpret_cast<uint64_t>(dst);
  const auto saddr = reinterpret_cast<uint64_t>(src);
  for (auto i = 0U; i < size; ++i) {
    const auto bt = gShadow[saddr + i];
    gShadow[daddr + i] = {bt.id, bt.offset, false};
  }
  __fslice_store_ret({0,0,false});
  return memmove(dst, src, size);
}

extern "C" void *__fslice_memcpy(void *dst, const void *src, uint64_t size) {
  return __fslice_memmove(dst, src, size);
}

extern "C" char *__fslice_strcpy(char *dst, const char *src) {
  return reinterpret_cast<char *>(__fslice_memmove(dst, src, strlen(src) + 1));
}

extern "C" void __fslice_bzero(void *dst, uint64_t size) {
  const auto daddr = reinterpret_cast<uint64_t>(dst);
  for (auto i = 0U; i < size; ++i) {
    gShadow[daddr + i] = {0,0,false};
  }
  __fslice_store_ret({0,0,false});
  memset(dst, 0, size);
}

extern "C" void *__fslice_malloc(uint64_t size) {
  auto ptr = calloc(1, size);
  const auto addr = reinterpret_cast<uint64_t>(ptr);
  Taint t = {gId++, 0};
  std::cerr << "t" << t.id << "=M(" << size << ",t"
            << __fslice_load_arg(0).id << ")" << std::endl;
  for (auto i = 0U; i < size; ++i) {
    gShadow[addr + i] = {t.id, i, MEM};
  }
  __fslice_store_ret({0,0,false});
  return ptr;
}

extern "C" void *__fslice_calloc(uint64_t num, uint64_t size) {
  auto ptr = calloc(num, size);
  const auto addr = reinterpret_cast<uint64_t>(ptr);
  Taint t = {gId++, 0};
  std::cerr << "t" << t.id << "=M(" << size << ",t"
            << __fslice_load_arg(0).id << ",t"
            << __fslice_load_arg(0).id << ")" << std::endl;
  for (auto i = 0U; i < num * size; ++i) {
    gShadow[addr + i] = {t.id, i, MEM};
  }
  __fslice_store_ret({0,0,false});
  return ptr;
}

extern "C" Taint __fslice_value(uint64_t val) {
  SaveErrno save_errno;
#if CACHE
  auto &t = gValues[val];
  if (val && !t.id) {
    Taint t = {gId++, 0, false};
    std::cerr << "t" << t.id << "=V(" << val << ")" << std::endl;
  }
  return t;
#else
  if (val) {
    Taint t = {gId++, 0, false};
    std::cerr << "t" << t.id << "=V(" << val << ")" << std::endl;
    return t;
  } else {
    return {0, 0, false};
  }
#endif
}

extern "C" Taint __fslice_op2(const char *op, Taint t1, Taint t2) {
  SaveErrno save_errno;
#if CACHE
  const auto id = t1.id | (static_cast<uint64_t>(t2.id) << 32);
  auto &t = gBinaryOps[op][id];
  if (!t.id) {
    t = {gId++, 0, false};
    std::cerr << "t" << t.id << "=A(\"" << op << "\",t" << t1.id
              << ",t" << t2.id << ")" << std::endl;
  }
#else
  Taint t = {gId++, 0, false};
  std::cerr << "t" << t.id << "=A(\"" << op << "\",t" << t1.id
            << ",t" << t2.id << ")" << std::endl;
#endif
  return t;
}

static Taint GetBlock(uint64_t size, uint64_t nr) {
  auto &t = gBlocks[nr];
  if (!t.id) {
    t = {gId++,0, false};
    const auto st = __fslice_load_arg(1);  // Taint for the size :-)
    const auto nt = __fslice_load_arg(2);  // Taint for the block number :-)
    std::cerr << "t" << t.id << "=B(" << size << "," << nr << ",t"
              << st.id << ",t" << nt.id << ")" << std::endl;
    __fslice_store_ret({0,0,false});
  }
  return t;
}

extern "C" void __fslice_read_block(uint64_t addr, uint64_t size, uint64_t nr) {
  SaveErrno save_errno;
  auto t = GetBlock(size, nr);
  for (auto i = 0U; i < size; ++i) {
    gShadow[addr + i] = {t.id, i, false};
  }
}

// Mark some memory as a block.
extern "C" void __fslice_write_block(uint64_t addr, uint64_t size,
                                     uint64_t nr) {
  SaveErrno save_errno;
  auto t = GetBlock(size, nr);
  for (auto i = 0UL; i < size; ++i) {
    const auto bt = gShadow[addr + i];
    if (!bt.id || (t.id == bt.id && i == bt.offset)) continue;
    std::cerr << "t" << t.id << "[" << i << "]=t" << bt.id
              << "[" << bt.offset << "]" << std::endl;
  }
}

// Mark some memory as a name.
extern "C" void __fslice_name(uint64_t addr, uint64_t len) {
  SaveErrno save_errno;
  Taint t = {gId++, 0};
  std::cerr << "t" << t.id << "=N(" << len << ")" << std::endl;
  for (auto i = 0U; i < len; ++i) {
    gShadow[addr + i] = {t.id, i, false};
  }
}

// Mark some memory as data.
extern "C" void __fslice_data(uint64_t addr, uint64_t len) {
  SaveErrno save_errno;
  Taint t = {gId++, 0};
  std::cerr << "t" << t.id << "=D(" << len << ")" << std::endl;
  for (auto i = 0U; i < len; ++i) {
    auto &bt = gShadow[addr + i];
    if (bt.id) {
      std::cerr << "t" << t.id << "[" << i << "]=t" << bt.id
                << "[" << bt.offset << "]" << std::endl;
    }
    bt = {t.id, i, false};
  }
}

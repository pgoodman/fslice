/* Copyright 2015 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <utility>

inline namespace __fslice {
namespace {

struct Taint {
  uint32_t id;
  uint32_t offset;
} __attribute__((packed));

static Taint gArgs[16] = {{0,0}};
static Taint gReturn = {0,0};
static std::unordered_map<uintptr_t,Taint> gShadow;
static std::unordered_map<uintptr_t,Taint> gValues;
static std::unordered_map<const char *,std::unordered_map<uint64_t,Taint>> gBinaryOps;
static unsigned gId = 1;

extern "C" Taint __fslice_value(uintptr_t);

// Load a taint from the shadow memory.
static Taint Load(uintptr_t addr, size_t size) {
  const Taint t = {gId++, 0};

  auto sep = "";
  std::cerr << "t" << t.id << " = Obj(";
  for (auto i = 0U; i < size; ++i) {
    auto mt = gShadow[addr + i];
    std::cerr << sep << "t" << mt.id << "[" << mt.offset << "]";
    sep = ",";
  }
  std::cerr << ")" << std::endl;
  return t;
}

// Store a taint to the shadow memory.
static void Store(uintptr_t addr, size_t size, Taint taint) {
  for (auto i = 0U; i < size; ++i) {
    gShadow[addr + i] = {taint.id, taint.offset + i};  // should be `taint.offset + i`?
  }
}

}
}  // namespace __fslice

extern "C" {

#define LOAD_STORE(size) \
  Taint __fslice_load ## size (uintptr_t addr) { \
    return Load(addr, size); \
  } \
  void __fslice_store ## size (uintptr_t addr, Taint taint) { \
    Store(addr, size, taint); \
  }

LOAD_STORE(1)
LOAD_STORE(2)
LOAD_STORE(4)
LOAD_STORE(8)
LOAD_STORE(16)
LOAD_STORE(32)
LOAD_STORE(64)

Taint __fslice_load_ret(void) {
  memset(gArgs, 0, sizeof gArgs);
  const auto t = gReturn;
  gReturn = {0,0};
  return t;
}

void __fslice_store_ret(Taint taint) {
  memset(gArgs, 0, sizeof gArgs);
  gReturn = taint;
}

Taint __fslice_load_arg(uintptr_t i) {
  const auto t = gArgs[i];
  gArgs[i] = {0,0};
  return t;
}

void __fslice_store_arg(uintptr_t i, Taint taint) {
  gArgs[i] = taint;
}

void *__fslice_memset(void *dst, int val, size_t size) {
  const auto t = __fslice_load_arg(1);
  const auto daddr = reinterpret_cast<uintptr_t>(dst);
  for (auto i = 0U; i < size; ++i) {
    gShadow[daddr + i] = t;
  }
  __fslice_store_ret({0,0});
  return memset(dst, val, size);
}

void *__fslice_memmove(void *dst, const void *src, size_t size) {
  const auto daddr = reinterpret_cast<uintptr_t>(dst);
  const auto saddr = reinterpret_cast<uintptr_t>(src);
  for (auto i = 0U; i < size; ++i) {
    gShadow[daddr + i] = gShadow[saddr + i];  // Not right but whatever.
  }
  __fslice_store_ret({0,0});
  return memmove(dst, src, size);
}

void *__fslice_memcpy(void *dst, const void *src, size_t size) {
  return __fslice_memmove(dst, src, size);
}

char *__fslice_strcpy(char *dst, const char *src) {
  return reinterpret_cast<char *>(__fslice_memmove(dst, src, strlen(src)));
}

Taint __fslice_value(uintptr_t val) {
  auto &t = gValues[val];
  if (val && !t.id) {
    t = {gId++, 0};
    std::cerr << "t" << t.id << " = Val(" << val << ")" << std::endl;
  }
  return t;
}

#if 0
Taint __fslice_op1(const char *op, Taint t1) {
  Taint t;
  t.id = gId++;
  t.offset = 0;
  std::cerr << "t" << t.id << " = Op1(\"" << op << "\",t" << t1.id
            << ")" << std::endl;
  return t;
}
#endif

Taint __fslice_op2(const char *op, Taint t1, Taint t2) {
  const auto id = t1.id | (static_cast<uint64_t>(t2.id) << 32);
  auto &t = gBinaryOps[op][id];
  if (!t.id) {
    t = {gId++, 0};
    std::cerr << "t" << t.id << " = Bin(\"" << op << "\",t" << t1.id
              << ",t" << t2.id << ")" << std::endl;
  }
  return t;
}

void __fslice_read_block(uintptr_t addr, size_t size, size_t nr) {
  const auto size_t = __fslice_load_arg(1);  // Taint for the block number :-)
  const auto nr_t = __fslice_load_arg(2);  // Taint for the block number :-)
  const Taint t = {gId++, 0};

  std::cerr << "t" << t.id << " = Block(" << size << ",t" << size_t.id << ",t"
                << nr_t.id << ")" << std::endl;

  for (auto i = 0U; i < size; ++i) {
    gShadow[addr + i] = {t.id, i};
  }

  __fslice_store_ret({0,0});
}

}  // extern C

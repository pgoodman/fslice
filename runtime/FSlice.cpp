/* Copyright 2015 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <iostream>
#include <fstream>

inline namespace __fslice {
namespace {

struct Taint {
  uint64_t offset:16;
  uint64_t id:48;
} __attribute__((packed));

static Taint gArgs[16] = {{0}};
static Taint gReturn = {0};
static std::unordered_map<uintptr_t,Taint> gShadow;
static unsigned gId = 1;

static void Select(Taint t, unsigned offset) {
  if (t.id) {
    std::cerr << "t" << t.id << "[" << (t.offset + offset) << "]";
  } else {
    std::cerr << "0";
  }
}

// Load a taint from the shadow memory.
Taint Load(uintptr_t addr, size_t size) {

  // If we're only loading uninitialized memory, then return zero.
  auto initial_taint = gShadow[addr];
  if (!initial_taint.id) {
    for (auto i = 1U; i < size; ++i) {
      if (gShadow[addr + i].id) {
        goto use_existing_obj;
      }
    }
    return {0,0};
  }

  // If we're loading contiguous memory all from the same tainted object then
  // just return the tainted object.
use_existing_obj:
  if (!initial_taint.offset) {
    for (auto i = 1U; i < size; ++i) {
      auto curr_taint = gShadow[addr + i];
      if (curr_taint.id != initial_taint.id ||
          curr_taint.offset != (initial_taint.offset + 1)) {
        goto make_obj;
      }
    }
    return initial_taint;
  }

  // Make a new object out of a bunch of bytes.
make_obj:
  Taint t;
  t.id = gId++;
  t.offset = 0;
  auto sep = "";
  std::cerr << "t" << t.id << " = Obj(";
  for (auto i = 0U; i < size; ++i) {
    std::cerr << sep;
    Select(gShadow[addr + i], 0);
    sep = ",";
  }
  std::cerr << ")" << std::endl;
  return t;
}

// Store a taint to the shadow memory.
void Store(uintptr_t addr, size_t size, Taint taint) {
  if (!taint.id) return;
  for (auto i = 0U; i < size; ++i) {
    gShadow[addr + i] = {taint.id, taint.offset + i};
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
  return gReturn;
}

void __fslice_store_ret(Taint taint) {
  gReturn = taint;
}

Taint __fslice_load_arg(uintptr_t i) {
  return gArgs[i];
}

void __fslice_store_arg(uintptr_t i, Taint taint) {
  gArgs[i] = taint;
}

void *__fslice_memset(void *dst, int val, size_t size) {
  auto t = __fslice_load_arg(1);
  auto daddr = reinterpret_cast<uintptr_t>(dst);
  for (auto i = 0U; i < size; ++i) {
    gShadow[daddr + i] = {t.id, 0};
  }
  return memset(dst, val, size);
}

void *__fslice_memmove(void *dst, const void *src, size_t size) {
  auto daddr = reinterpret_cast<uintptr_t>(dst);
  auto saddr = reinterpret_cast<uintptr_t>(src);
  for (auto i = 0U; i < size; ++i) {
    gShadow[daddr + i] = gShadow[saddr + i];  // Not right but whatever.
  }
  return memmove(dst, src, size);
}

void *__fslice_memcpy(void *dst, const void *src, size_t size) {
  return __fslice_memmove(dst, src, size);
}

char *__fslice_strcpy(char *dst, const char *src) {
  return reinterpret_cast<char *>(__fslice_memmove(dst, src, strlen(src)));
}

Taint __fslice_combine(uintptr_t op, Taint t1,
                                 Taint t2) {
  if (!t1.id && !t2.id) return {0,0};

  Taint t;
  t.id = gId++;
  t.offset = 0;
  std::cerr << "t" << t.id << " = Combine(" << op << ",t" << t1.id
            << ",t" << t2.id << ")" << std::endl;
  return t;
}

}  // extern C

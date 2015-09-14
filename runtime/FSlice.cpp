/* Copyright 2015 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#include <cstdint>
#include <cstring>
#include <unordered_map>

typedef uintptr_t OpaqueTaint;

inline namespace __fslice {
namespace {

union Taint {
  OpaqueTaint opaque;
  struct {
    enum : uint8_t {
      kConst,
      kSelect,
      kCombine,
      kInput
    } kind:2;

    union {
      struct {
        uint8_t op;
        uint32_t arg0;
        uint32_t arg1;
      } combine;

      struct {
        uint32_t offset;
      } select;

      struct {

      } input;

  } __attribute__((packed)) t;
} __attribute__((packed));

static __thread Taint gArgs[16] = {{0}};
static __thread Taint gReturn = {0};
static std::unordered_map<uintptr_t, Taint> gShadow;

// Load a taint from the shadow memory.
OpaqueTaint Load(uintptr_t addr, size_t size) {
  return 0;
}

// Store a taint to the shadow memory.
void Store(uintptr_t addr, size_t size, OpaqueTaint taint) {
  if (1 == size) {
    gShadow[addr] = taint;
  } else {
    for (auto offset = 0U; offset < size; ++offset) {

      gShadow[addr + offset] =
    }
  }
}

}
}  // namespace __fslice

extern "C" {

#define LOAD_STORE(size) \
  OpaqueTaint __fslice_load ## size (uintptr_t addr) { \
    return Load(addr, size); \
  } \
  void __fslice_store ## size (uintptr_t addr, OpaqueTaint taint) { \
    return Store(addr, size, taint); \
  }

LOAD_STORE(1)
LOAD_STORE(2)
LOAD_STORE(4)
LOAD_STORE(8)
LOAD_STORE(16)
LOAD_STORE(32)
LOAD_STORE(64)

OpaqueTaint __fslice_load_ret(void) {
  return gReturn.opaque;
}

void __fslice_store_ret(OpaqueTaint taint) {
  gReturn.opaque = taint;
}

OpaqueTaint __fslice_load_arg(uintptr_t i) {
  return gArgs[i].opaque;
}

void __fslice_store_arg(uintptr_t i, OpaqueTaint taint) {
  gArgs[i].opaque = taint;
}

char *__fslice_strcpy(char *dst, const char *src) {
  return strcpy(dst, src);
}

void *__fslice_memset(void *dst, int val, size_t size) {
  return memset(dst, val, size);
}

void *__fslice_memcpy(void *dst, const void *src, size_t size) {
  return memcpy(dst, src, size);
}

void *__fslice_memmove(void *dst, const void *src, size_t size) {
  return memmove(dst, src, size);
}

OpaqueTaint __fslice_combine(uintptr_t op, OpaqueTaint t1, OpaqueTaint t2) {
  return 0;
}

}  // extern C

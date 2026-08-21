#ifndef MOSAIC_MINI_TEST_H
#define MOSAIC_MINI_TEST_H
#include <stdio.h>
#include <stdint.h>
static int mt_failures = 0;
#define MT_CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); mt_failures++; } } while (0)
#define MT_CHECK_EQ_U64(a, b) do { uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b); \
  if (_a != _b) { fprintf(stderr, "FAIL %s:%d: %s == %s (%llu != %llu)\n", __FILE__, __LINE__, #a, #b, \
                          (unsigned long long)_a, (unsigned long long)_b); mt_failures++; } } while (0)
#define MT_RUN(fn) do { int _pre = mt_failures; fn(); \
  fprintf(stderr, "  %-28s %s\n", #fn, mt_failures == _pre ? "ok" : "FAILED"); } while (0)
#define MT_RESULT() (mt_failures == 0)
#endif

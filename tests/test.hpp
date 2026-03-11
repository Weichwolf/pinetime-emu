#pragma once
#include <cstdio>
#include <cstdlib>

static int test_count_ = 0;
static int pass_count_ = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    auto _a_ = (unsigned)(a); auto _b_ = (unsigned)(b); \
    if (_a_ != _b_) { \
        fprintf(stderr, "  FAIL %s:%d: %s == 0x%X, expected %s == 0x%X\n", \
                __FILE__, __LINE__, #a, _a_, #b, _b_); \
        exit(1); \
    } \
} while(0)

#define RUN(fn) do { \
    test_count_++; \
    fn(); \
    pass_count_++; \
    printf("  PASS %s\n", #fn); \
} while(0)

#define TEST_REPORT() \
    printf("%d/%d passed\n", pass_count_, test_count_)

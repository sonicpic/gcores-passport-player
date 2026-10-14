/* Tiny host assertion adapter for the upstream Unity-shaped tests.
 * SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
inline unsigned test_count;
inline void check(bool pass, const char *expr, const char *file, int line) {
    if (!pass) { std::fprintf(stderr, "%s:%d FAIL: %s\n", file, line, expr); std::exit(1); }
}
#define CHECK(x) check((x), #x, __FILE__, __LINE__)
#define UNITY_BEGIN() (test_count = 0)
#define UNITY_END() (std::printf("%u upstream regression tests PASS\n", test_count), 0)
#define RUN_TEST(fn) do { fn(); ++test_count; } while (false)
#define TEST_ASSERT_TRUE(x) CHECK(static_cast<bool>(x))
#define TEST_ASSERT_FALSE(x) CHECK(!static_cast<bool>(x))
#define TEST_ASSERT_TRUE_MESSAGE(x,m) TEST_ASSERT_TRUE(x)
#define TEST_ASSERT_FALSE_MESSAGE(x,m) TEST_ASSERT_FALSE(x)
#define TEST_ASSERT_EQUAL_STRING(a,b) CHECK(std::strcmp((a),(b)) == 0)
#define TEST_ASSERT_EQUAL_UINT32(a,b) CHECK(static_cast<unsigned long long>(a) == static_cast<unsigned long long>(b))
#define TEST_ASSERT_EQUAL_UINT32_MESSAGE(a,b,m) TEST_ASSERT_EQUAL_UINT32(a,b)
#define TEST_ASSERT_EQUAL_UINT64(a,b) TEST_ASSERT_EQUAL_UINT32(a,b)
#define TEST_ASSERT_EQUAL_INT(a,b) CHECK(static_cast<long long>(a) == static_cast<long long>(b))
#define TEST_ASSERT_GREATER_THAN_UINT32(a,b) CHECK(static_cast<unsigned long long>(b) > static_cast<unsigned long long>(a))
#define TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(a,b,m) TEST_ASSERT_GREATER_THAN_UINT32(a,b)
#define TEST_ASSERT_LESS_OR_EQUAL_UINT32(a,b) CHECK(static_cast<unsigned long long>(b) <= static_cast<unsigned long long>(a))
#define TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(a,b,m) TEST_ASSERT_LESS_OR_EQUAL_UINT32(a,b)
#define TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(a,b,m) CHECK(static_cast<unsigned long long>(b) < static_cast<unsigned long long>(a))

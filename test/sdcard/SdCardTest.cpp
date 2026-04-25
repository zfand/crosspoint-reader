#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "src/util/BytesFormatter.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_STR_EQ(actual, expected)                                                                         \
  do {                                                                                                          \
    if (strcmp((actual), (expected)) != 0) {                                                                    \
      fprintf(stderr, "  FAIL: %s:%d: got \"%s\", expected \"%s\"\n", __FILE__, __LINE__, (actual), (expected)); \
      testsFailed++;                                                                                            \
      return;                                                                                                   \
    }                                                                                                           \
  } while (0)

#define PASS() testsPassed++

// ============================================================================
// BytesFormatter::format
// ============================================================================

static void test_bytes_below_one_kb() {
  char buf[16];
  BytesFormatter::format(0, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "0 KB");
  PASS();
}

static void test_bytes_exactly_one_kb() {
  char buf[16];
  BytesFormatter::format(1024, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "1 KB");
  PASS();
}

static void test_bytes_1023_kb() {
  char buf[16];
  BytesFormatter::format(1023ULL * 1024, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "1023 KB");
  PASS();
}

static void test_bytes_exactly_one_mb() {
  char buf[16];
  BytesFormatter::format(1024ULL * 1024, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "1 MB");
  PASS();
}

static void test_bytes_512_mb() {
  char buf[16];
  BytesFormatter::format(512ULL * 1024 * 1024, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "512 MB");
  PASS();
}

static void test_bytes_rounding_mb() {
  char buf[16];
  // 1 MB + 400 KB = 1.39 MB → "%.0f" rounds to "1 MB"
  BytesFormatter::format(1ULL * 1024 * 1024 + 400 * 1024, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "1 MB");
  // 1 MB + 900 KB = 1.88 MB → "%.0f" rounds to "2 MB"
  BytesFormatter::format(1ULL * 1024 * 1024 + 900 * 1024, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "2 MB");
  PASS();
}

static void test_bytes_exactly_one_gb() {
  char buf[16];
  BytesFormatter::format(1024ULL * 1024 * 1024, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "1.0 GB");
  PASS();
}

static void test_bytes_32_gb() {
  char buf[16];
  BytesFormatter::format(32ULL * 1024 * 1024 * 1024, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "32.0 GB");
  PASS();
}

static void test_bytes_7_5_gb() {
  char buf[16];
  // 7.5 GB
  BytesFormatter::format(static_cast<uint64_t>(7.5 * 1024 * 1024 * 1024), buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "7.5 GB");
  PASS();
}

static void test_buf_truncation() {
  // Buffer too small should not crash (snprintf guarantees null-termination)
  char buf[5];
  BytesFormatter::format(32ULL * 1024 * 1024 * 1024, buf, sizeof(buf));
  assert(buf[4] == '\0');
  PASS();
}

// ============================================================================
// Space used calculation (the arithmetic feeding the progress bar)
// ============================================================================

static void test_used_bytes_is_total_minus_free() {
  const uint64_t total = 32ULL * 1024 * 1024 * 1024;
  const uint64_t free_ = 28ULL * 1024 * 1024 * 1024;
  const uint64_t used = total - free_;

  char buf[16];
  BytesFormatter::format(used, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "4.0 GB");
  PASS();
}

static void test_full_card_zero_free() {
  const uint64_t total = 16ULL * 1024 * 1024 * 1024;
  const uint64_t free_ = 0;
  const uint64_t used = total - free_;

  char buf[16];
  BytesFormatter::format(used, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "16.0 GB");
  PASS();
}

static void test_empty_card_all_free() {
  const uint64_t total = 16ULL * 1024 * 1024 * 1024;
  const uint64_t used = total - total;  // 0

  char buf[16];
  BytesFormatter::format(used, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "0 KB");
  PASS();
}

// ============================================================================

int main() {
  printf("Running SdCard tests...\n");

  test_bytes_below_one_kb();
  test_bytes_exactly_one_kb();
  test_bytes_1023_kb();
  test_bytes_exactly_one_mb();
  test_bytes_512_mb();
  test_bytes_rounding_mb();
  test_bytes_exactly_one_gb();
  test_bytes_32_gb();
  test_bytes_7_5_gb();
  test_buf_truncation();
  test_used_bytes_is_total_minus_free();
  test_full_card_zero_free();
  test_empty_card_all_free();

  printf("\n%d passed, %d failed\n", testsPassed, testsFailed);
  return testsFailed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

#include <cstdio>
#include <cstdlib>

#include "lib/KOReaderSync/XPathUtils.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_EQ(actual, expected)                                                                    \
  do {                                                                                                 \
    if ((actual) != (expected)) {                                                                      \
      fprintf(stderr, "  FAIL %s:%d: %s == %d, expected %d\n", __FILE__, __LINE__, #actual,           \
              (actual), (expected));                                                                    \
      testsFailed++;                                                                                    \
      return;                                                                                           \
    }                                                                                                  \
  } while (0)

#define ASSERT_TRUE(cond)                                                                        \
  do {                                                                                           \
    if (!(cond)) {                                                                               \
      fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
      testsFailed++;                                                                              \
      return;                                                                                     \
    }                                                                                            \
  } while (0)

#define PASS() testsPassed++

// ── parseIndex ────────────────────────────────────────────────────────────────

static void test_parseIndex_DocFragment() {
  printf("test_parseIndex_DocFragment...\n");
  // Standard KOReader XPath — DocFragment is always the first interesting index.
  ASSERT_EQ(XPathUtils::parseIndex("/body/DocFragment[8]/body/p[4]", "/body/DocFragment["), 8);
  ASSERT_EQ(XPathUtils::parseIndex("/body/DocFragment[1]/body/p[1]", "/body/DocFragment["), 1);
  ASSERT_EQ(XPathUtils::parseIndex("/body/DocFragment[42]/body", "/body/DocFragment["), 42);
  PASS();
}

static void test_parseIndex_p_last() {
  printf("test_parseIndex_p_last...\n");
  // Using last=true picks the rightmost match — the deepest /p[N] in the path.
  ASSERT_EQ(XPathUtils::parseIndex("/body/DocFragment[8]/body/p[4]", "/p[", true), 4);
  ASSERT_EQ(XPathUtils::parseIndex("/body/DocFragment[1]/body/div[2]/section[1]/p[12]", "/p[", true), 12);
  PASS();
}

static void test_parseIndex_p_first_vs_last() {
  printf("test_parseIndex_p_first_vs_last...\n");
  // A path with two segments matching the same prefix: first picks the outer one.
  const std::string xpath = "/body/DocFragment[3]/body/div[1]/p[2]/span[1]/p[7]";
  ASSERT_EQ(XPathUtils::parseIndex(xpath, "/p[", false), 2);
  ASSERT_EQ(XPathUtils::parseIndex(xpath, "/p[", true), 7);
  PASS();
}

static void test_parseIndex_missing_prefix() {
  printf("test_parseIndex_missing_prefix...\n");
  ASSERT_EQ(XPathUtils::parseIndex("/body/DocFragment[8]/body/p[4]", "/section["), -1);
  ASSERT_EQ(XPathUtils::parseIndex("", "/body/DocFragment["), -1);
  PASS();
}

static void test_parseIndex_empty_brackets() {
  printf("test_parseIndex_empty_brackets...\n");
  // "[" immediately followed by "]" — no number to parse.
  ASSERT_EQ(XPathUtils::parseIndex("/body/DocFragment[]/body", "/body/DocFragment["), -1);
  PASS();
}

static void test_parseIndex_non_digit_in_brackets() {
  printf("test_parseIndex_non_digit_in_brackets...\n");
  // Malformed index — must return -1, not garbage.
  ASSERT_EQ(XPathUtils::parseIndex("/body/DocFragment[abc]/body", "/body/DocFragment["), -1);
  PASS();
}

static void test_parseIndex_multi_digit() {
  printf("test_parseIndex_multi_digit...\n");
  ASSERT_EQ(XPathUtils::parseIndex("/body/DocFragment[123]/body/p[456]", "/body/DocFragment["), 123);
  ASSERT_EQ(XPathUtils::parseIndex("/body/DocFragment[123]/body/p[456]", "/p[", true), 456);
  PASS();
}

// ── parseCharOffset ───────────────────────────────────────────────────────────

static void test_parseCharOffset_present() {
  printf("test_parseCharOffset_present...\n");
  ASSERT_EQ(XPathUtils::parseCharOffset("/body/DocFragment[8]/body/p[4]/text().96"), 96);
  ASSERT_EQ(XPathUtils::parseCharOffset("/body/DocFragment[1]/body/p[1]/text().0"), 0);
  ASSERT_EQ(XPathUtils::parseCharOffset("/body/DocFragment[3]/body/p[42]/text().17"), 17);
  PASS();
}

static void test_parseCharOffset_absent() {
  printf("test_parseCharOffset_absent...\n");
  // No text() node → 0.
  ASSERT_EQ(XPathUtils::parseCharOffset("/body/DocFragment[8]/body/p[4]"), 0);
  ASSERT_EQ(XPathUtils::parseCharOffset(""), 0);
  PASS();
}

static void test_parseCharOffset_text_no_dot() {
  printf("test_parseCharOffset_text_no_dot...\n");
  // "text()" present but no dot — offset is 0.
  ASSERT_EQ(XPathUtils::parseCharOffset("/body/DocFragment[8]/body/p[4]/text()"), 0);
  PASS();
}

static void test_parseCharOffset_large_offset() {
  printf("test_parseCharOffset_large_offset...\n");
  ASSERT_EQ(XPathUtils::parseCharOffset("/body/DocFragment[2]/body/p[1]/text().1234"), 1234);
  PASS();
}

// ── Round-trip: full KOReader XPath strings ───────────────────────────────────

static void test_full_xpath_typical() {
  printf("test_full_xpath_typical...\n");
  // Typical KOReader progress XPath as it arrives from the sync server.
  const std::string xpath = "/body/DocFragment[8]/body/div[2]/section[1]/p[4]/text().96";
  ASSERT_EQ(XPathUtils::parseIndex(xpath, "/body/DocFragment["), 8);
  ASSERT_EQ(XPathUtils::parseIndex(xpath, "/p[", true), 4);
  ASSERT_EQ(XPathUtils::parseCharOffset(xpath), 96);
  PASS();
}

static void test_full_xpath_chapter_start() {
  printf("test_full_xpath_chapter_start...\n");
  // XPath pointing to the very start of a chapter — no text() offset.
  const std::string xpath = "/body/DocFragment[3]/body";
  ASSERT_EQ(XPathUtils::parseIndex(xpath, "/body/DocFragment["), 3);
  ASSERT_EQ(XPathUtils::parseIndex(xpath, "/p[", true), -1);
  ASSERT_EQ(XPathUtils::parseCharOffset(xpath), 0);
  PASS();
}

static void test_full_xpath_first_chapter_first_para() {
  printf("test_full_xpath_first_chapter_first_para...\n");
  const std::string xpath = "/body/DocFragment[1]/body/p[1]/text().0";
  ASSERT_EQ(XPathUtils::parseIndex(xpath, "/body/DocFragment["), 1);
  ASSERT_EQ(XPathUtils::parseIndex(xpath, "/p[", true), 1);
  ASSERT_EQ(XPathUtils::parseCharOffset(xpath), 0);
  PASS();
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
  printf("=== XPathUtils Tests ===\n\n");

  test_parseIndex_DocFragment();
  test_parseIndex_p_last();
  test_parseIndex_p_first_vs_last();
  test_parseIndex_missing_prefix();
  test_parseIndex_empty_brackets();
  test_parseIndex_non_digit_in_brackets();
  test_parseIndex_multi_digit();

  test_parseCharOffset_present();
  test_parseCharOffset_absent();
  test_parseCharOffset_text_no_dot();
  test_parseCharOffset_large_offset();

  test_full_xpath_typical();
  test_full_xpath_chapter_start();
  test_full_xpath_first_chapter_first_para();

  printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);
  return testsFailed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

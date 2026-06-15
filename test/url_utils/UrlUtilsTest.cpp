#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "src/util/UrlUtils.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_EQ(actual, expected)                                                                              \
  do {                                                                                                           \
    if ((actual) != (expected)) {                                                                                 \
      fprintf(stderr, "  FAIL %s:%d: got \"%s\", expected \"%s\"\n", __FILE__, __LINE__,                        \
              (actual).c_str(), (expected).c_str());                                                             \
      testsFailed++;                                                                                              \
      return;                                                                                                     \
    }                                                                                                            \
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

// ── isHttpsUrl ────────────────────────────────────────────────────────────────

static void test_isHttpsUrl_http() {
  printf("test_isHttpsUrl_http...\n");
  ASSERT_TRUE(!UrlUtils::isHttpsUrl("http://example.com"));
  ASSERT_TRUE(!UrlUtils::isHttpsUrl("http://example.com/path"));
  ASSERT_TRUE(!UrlUtils::isHttpsUrl(""));
  ASSERT_TRUE(!UrlUtils::isHttpsUrl("ftp://example.com"));
  PASS();
}

static void test_isHttpsUrl_https() {
  printf("test_isHttpsUrl_https...\n");
  ASSERT_TRUE(UrlUtils::isHttpsUrl("https://example.com"));
  ASSERT_TRUE(UrlUtils::isHttpsUrl("https://example.com/path?q=1"));
  PASS();
}

// ── ensureProtocol ────────────────────────────────────────────────────────────

static void test_ensureProtocol_no_protocol() {
  printf("test_ensureProtocol_no_protocol...\n");
  ASSERT_EQ(UrlUtils::ensureProtocol("example.com"), std::string("http://example.com"));
  ASSERT_EQ(UrlUtils::ensureProtocol("192.168.1.1:8080"), std::string("http://192.168.1.1:8080"));
  // Regression: OPDS feed URL must not lose protocol when concatenating path
  ASSERT_EQ(UrlUtils::ensureProtocol("192.168.1.17:8080") + "/opds/new",
            std::string("http://192.168.1.17:8080/opds/new"));
  PASS();
}

static void test_ensureProtocol_already_has_protocol() {
  printf("test_ensureProtocol_already_has_protocol...\n");
  ASSERT_EQ(UrlUtils::ensureProtocol("http://example.com"), std::string("http://example.com"));
  ASSERT_EQ(UrlUtils::ensureProtocol("https://example.com"), std::string("https://example.com"));
  PASS();
}

// ── extractHost ───────────────────────────────────────────────────────────────

static void test_extractHost_with_path() {
  printf("test_extractHost_with_path...\n");
  ASSERT_EQ(UrlUtils::extractHost("http://example.com/path/to/file"), std::string("http://example.com"));
  ASSERT_EQ(UrlUtils::extractHost("https://calibre.local:8080/opds/new"), std::string("https://calibre.local:8080"));
  PASS();
}

static void test_extractHost_no_path() {
  printf("test_extractHost_no_path...\n");
  ASSERT_EQ(UrlUtils::extractHost("http://example.com"), std::string("http://example.com"));
  PASS();
}

static void test_extractHost_no_protocol() {
  printf("test_extractHost_no_protocol...\n");
  ASSERT_EQ(UrlUtils::extractHost("example.com/path"), std::string("example.com"));
  ASSERT_EQ(UrlUtils::extractHost("example.com"), std::string("example.com"));
  PASS();
}

// ── buildUrl ──────────────────────────────────────────────────────────────────

static void test_buildUrl_absolute_url_passthrough() {
  printf("test_buildUrl_absolute_url_passthrough...\n");
  // If the path is itself an absolute URL, return it unchanged.
  ASSERT_EQ(UrlUtils::buildUrl("http://server.com", "http://other.com/book.epub"),
            std::string("http://other.com/book.epub"));
  ASSERT_EQ(UrlUtils::buildUrl("http://server.com", "https://cdn.example.com/file"),
            std::string("https://cdn.example.com/file"));
  PASS();
}

static void test_buildUrl_absolute_path() {
  printf("test_buildUrl_absolute_path...\n");
  // A path starting with '/' is rooted at the host, not the full server URL.
  ASSERT_EQ(UrlUtils::buildUrl("http://example.com/some/base", "/opds/new"),
            std::string("http://example.com/opds/new"));
  ASSERT_EQ(UrlUtils::buildUrl("http://calibre.local:8080/ignore", "/get/epub/1"),
            std::string("http://calibre.local:8080/get/epub/1"));
  PASS();
}

static void test_buildUrl_relative_path_no_trailing_slash() {
  printf("test_buildUrl_relative_path_no_trailing_slash...\n");
  // A relative path with no trailing slash on the base gets a '/' separator.
  ASSERT_EQ(UrlUtils::buildUrl("http://example.com/base", "book.epub"),
            std::string("http://example.com/base/book.epub"));
  PASS();
}

static void test_buildUrl_relative_path_trailing_slash() {
  printf("test_buildUrl_relative_path_trailing_slash...\n");
  // A trailing slash on the base should not produce a double slash.
  ASSERT_EQ(UrlUtils::buildUrl("http://example.com/base/", "book.epub"),
            std::string("http://example.com/base/book.epub"));
  PASS();
}

static void test_buildUrl_query_string_stripped_before_relative_append() {
  printf("test_buildUrl_query_string_stripped_before_relative_append...\n");
  // Regression: when the server URL contains a query string, the query must
  // be stripped before appending a relative path so it does not appear in
  // the middle of the constructed URL.
  ASSERT_EQ(UrlUtils::buildUrl("http://copyparty.example.com/foo?k=secret", "book.epub"),
            std::string("http://copyparty.example.com/foo/book.epub"));
  ASSERT_EQ(UrlUtils::buildUrl("http://example.com/?token=abc", "file.epub"),
            std::string("http://example.com/file.epub"));
  PASS();
}

static void test_buildUrl_no_protocol_on_server() {
  printf("test_buildUrl_no_protocol_on_server...\n");
  // If the server URL has no protocol, http:// is added before building.
  ASSERT_EQ(UrlUtils::buildUrl("calibre.local:8080", "book.epub"),
            std::string("http://calibre.local:8080/book.epub"));
  PASS();
}

static void test_buildUrl_empty_path() {
  printf("test_buildUrl_empty_path...\n");
  ASSERT_EQ(UrlUtils::buildUrl("http://example.com/base", ""),
            std::string("http://example.com/base"));
  PASS();
}

// ── filenameFromUrl ───────────────────────────────────────────────────────────

static void test_filenameFromUrl_normal() {
  printf("test_filenameFromUrl_normal...\n");
  ASSERT_EQ(UrlUtils::filenameFromUrl("http://example.com/books/my-book.epub"),
            std::string("my-book.epub"));
  ASSERT_EQ(UrlUtils::filenameFromUrl("/books/another.epub"), std::string("another.epub"));
  PASS();
}

static void test_filenameFromUrl_no_slash() {
  printf("test_filenameFromUrl_no_slash...\n");
  // No slash at all — treat entire input as the filename.
  ASSERT_EQ(UrlUtils::filenameFromUrl("book.epub"), std::string("book.epub"));
  PASS();
}

static void test_filenameFromUrl_trailing_slash() {
  printf("test_filenameFromUrl_trailing_slash...\n");
  // Trailing slash means nothing after the last '/' — return the full input.
  ASSERT_EQ(UrlUtils::filenameFromUrl("http://example.com/books/"),
            std::string("http://example.com/books/"));
  PASS();
}

static void test_filenameFromUrl_deep_path() {
  printf("test_filenameFromUrl_deep_path...\n");
  ASSERT_EQ(UrlUtils::filenameFromUrl("http://calibre.local:8080/get/epub/1/Author%20-%20Title.epub"),
            std::string("Author%20-%20Title.epub"));
  PASS();
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
  printf("=== UrlUtils Tests ===\n\n");

  test_isHttpsUrl_http();
  test_isHttpsUrl_https();

  test_ensureProtocol_no_protocol();
  test_ensureProtocol_already_has_protocol();

  test_extractHost_with_path();
  test_extractHost_no_path();
  test_extractHost_no_protocol();

  test_buildUrl_absolute_url_passthrough();
  test_buildUrl_absolute_path();
  test_buildUrl_relative_path_no_trailing_slash();
  test_buildUrl_relative_path_trailing_slash();
  test_buildUrl_query_string_stripped_before_relative_append();
  test_buildUrl_no_protocol_on_server();
  test_buildUrl_empty_path();

  test_filenameFromUrl_normal();
  test_filenameFromUrl_no_slash();
  test_filenameFromUrl_trailing_slash();
  test_filenameFromUrl_deep_path();

  printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);
  return testsFailed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

// Tests that a settings.json file survives the format-and-resave cycle.
//
// This mirrors exactly what the firmware does when the user formats the SD card
// from the Settings → System → SD Card screen:
//
//   1. User confirms format → Storage.formatCard() runs → card is blank FAT32
//   2. SettingsActivity result handler calls SETTINGS.saveToFile()
//      → writes /.crosspoint/settings.json to the fresh card
//   3. On next boot, loadFromFile() reads it back
//
// Here we exercise the same file-layer operations using mkfs.fat + mtools
// (mcopy / mmd / mdir) so no firmware or Arduino headers are required.
//
// Tests:
//   1. Fresh FAT32 image has no settings directory
//   2. A settings JSON written to the card reads back with identical content
//   3. Format wipes the settings file
//   4. Re-writing settings after format produces a readable, identical file  ← key assertion

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_TRUE(cond, msg)                                                               \
  do {                                                                                       \
    if (!(cond)) {                                                                           \
      fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));                    \
      testsFailed++;                                                                          \
      return;                                                                                 \
    }                                                                                        \
  } while (0)

#define PASS() \
  do { testsPassed++; } while (0)

// ── constants ────────────────────────────────────────────────────────────────

// Representatve settings JSON — matches the structure CrossPointSettings writes.
static constexpr const char* SETTINGS_JSON =
    "{\"fontSize\":2,\"fontFamily\":1,\"theme\":0,\"sleepTimeout\":2,"
    "\"refreshFrequency\":3,\"orientation\":0,\"frontButtonBack\":0,"
    "\"frontButtonConfirm\":1,\"frontButtonLeft\":2,\"frontButtonRight\":3}";

static constexpr size_t IMAGE_BYTES = 32UL * 1024 * 1024;

// ── helpers ──────────────────────────────────────────────────────────────────

static void make_path(char* buf, size_t len, const char* tag, const char* ext) {
  snprintf(buf, len, "/tmp/cpspers_%s_%d%s", tag, static_cast<int>(getpid()), ext);
}

// Create a blank 32 MB file then format it as FAT32.
static bool create_fat32_image(const char* img) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "dd if=/dev/zero bs=1M count=32 of=%s 2>/dev/null && mkfs.fat -F 32 %s >/dev/null 2>&1", img, img);
  return system(cmd) == 0;  // NOLINT — intentional external tool invocation
}

// Format an existing image in-place (simulates Storage.formatCard()).
static bool reformat_image(const char* img) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "mkfs.fat -F 32 %s >/dev/null 2>&1", img);
  return system(cmd) == 0;  // NOLINT
}

// Write a local file into /.crosspoint/settings.json inside the FAT32 image.
// Mirrors CrossPointSettings::saveToFile() → JsonSettingsIO::saveSettings() →
// Storage.writeFile("/.crosspoint/settings.json").
static bool write_settings_to_image(const char* img, const char* local_json) {
  char cmd[512];
  // Create the directory; || true so system() always returns 0 (dir may already exist).
  snprintf(cmd, sizeof(cmd), "mmd -i %s ::.crosspoint >/dev/null 2>&1 || true", img);
  if (system(cmd) != 0) return false;  // NOLINT — only fails if the shell itself fails
  snprintf(cmd, sizeof(cmd), "mcopy -i %s %s ::.crosspoint/settings.json 2>/dev/null", img, local_json);
  return system(cmd) == 0;  // NOLINT
}

// Read /.crosspoint/settings.json from the image to a local file.
// Returns false if the file does not exist.
static bool read_settings_from_image(const char* img, const char* dest_path) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "mcopy -i %s ::.crosspoint/settings.json %s 2>/dev/null", img, dest_path);
  return system(cmd) == 0;  // NOLINT
}

// Return true if /.crosspoint/settings.json exists inside the image.
static bool settings_file_exists(const char* img) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "mdir -i %s ::.crosspoint/ 2>/dev/null | grep -qF settings.json", img);
  return system(cmd) == 0;  // NOLINT
}

// Read a file into a std::string.  Returns empty on error.
static std::string slurp(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return {};
  fseek(f, 0, SEEK_END);
  const long sz = ftell(f);
  rewind(f);
  if (sz <= 0) {
    fclose(f);
    return {};
  }
  std::string buf(static_cast<size_t>(sz), '\0');
  const size_t n = fread(buf.data(), 1, static_cast<size_t>(sz), f);
  if (n != static_cast<size_t>(sz)) buf.clear();
  fclose(f);
  return buf;
}

// ── tests ─────────────────────────────────────────────────────────────────────

// A freshly formatted card has no /.crosspoint directory or settings file.
static void test_fresh_format_has_no_settings() {
  char img[128];
  make_path(img, sizeof(img), "fresh", ".img");

  ASSERT_TRUE(create_fat32_image(img), "create FAT32 image");
  ASSERT_TRUE(!settings_file_exists(img), "no settings.json on fresh card");

  remove(img);
  PASS();
}

// Writing a settings file to the card and reading it back yields identical content.
static void test_settings_write_read_roundtrip() {
  char img[128], json_in[128], json_out[128];
  make_path(img, sizeof(img), "rtrip", ".img");
  make_path(json_in, sizeof(json_in), "rtrip_in", ".json");
  make_path(json_out, sizeof(json_out), "rtrip_out", ".json");

  // Write the JSON to a local temp file (what saveToFile does in memory).
  {
    FILE* f = fopen(json_in, "w");
    ASSERT_TRUE(f != nullptr, "create local json_in");
    fputs(SETTINGS_JSON, f);
    fclose(f);
  }

  ASSERT_TRUE(create_fat32_image(img), "create FAT32 image");
  ASSERT_TRUE(write_settings_to_image(img, json_in), "write settings to card");
  ASSERT_TRUE(read_settings_from_image(img, json_out), "read settings from card");

  const std::string written = slurp(json_in);
  const std::string readback = slurp(json_out);
  ASSERT_TRUE(!written.empty(), "written content non-empty");
  ASSERT_TRUE(written == readback, "read-back content matches written content");

  remove(img);
  remove(json_in);
  remove(json_out);
  PASS();
}

// Format wipes the settings file — the card is blank after format.
static void test_format_wipes_settings() {
  char img[128], json_in[128];
  make_path(img, sizeof(img), "wipe", ".img");
  make_path(json_in, sizeof(json_in), "wipe_in", ".json");

  {
    FILE* f = fopen(json_in, "w");
    ASSERT_TRUE(f != nullptr, "create local json_in");
    fputs(SETTINGS_JSON, f);
    fclose(f);
  }

  ASSERT_TRUE(create_fat32_image(img), "create FAT32 image");
  ASSERT_TRUE(write_settings_to_image(img, json_in), "write settings before format");
  ASSERT_TRUE(settings_file_exists(img), "settings present before format");

  ASSERT_TRUE(reformat_image(img), "reformat card");

  ASSERT_TRUE(!settings_file_exists(img), "settings gone after format");

  remove(img);
  remove(json_in);
  PASS();
}

// Full persistence cycle: write → format → re-write → read back matches original.
// This is the exact sequence that happens on the device:
//   format card → SettingsActivity result handler calls saveToFile() → boot reads it.
static void test_settings_persist_after_resave() {
  char img[128], json_in[128], json_out[128];
  make_path(img, sizeof(img), "persist", ".img");
  make_path(json_in, sizeof(json_in), "persist_in", ".json");
  make_path(json_out, sizeof(json_out), "persist_out", ".json");

  {
    FILE* f = fopen(json_in, "w");
    ASSERT_TRUE(f != nullptr, "create local json_in");
    fputs(SETTINGS_JSON, f);
    fclose(f);
  }

  ASSERT_TRUE(create_fat32_image(img), "create FAT32 image");

  // Simulate pre-format state: settings file exists on card.
  ASSERT_TRUE(write_settings_to_image(img, json_in), "write settings (pre-format)");
  ASSERT_TRUE(settings_file_exists(img), "settings present before format");

  // Format wipes the card.
  ASSERT_TRUE(reformat_image(img), "format card");
  ASSERT_TRUE(!settings_file_exists(img), "settings gone after format");

  // SettingsActivity result handler re-saves settings to the fresh card.
  ASSERT_TRUE(write_settings_to_image(img, json_in), "re-save settings (post-format)");
  ASSERT_TRUE(settings_file_exists(img), "settings present after re-save");

  // Boot reads them back — content must be identical to what was saved.
  ASSERT_TRUE(read_settings_from_image(img, json_out), "read back settings after re-save");
  const std::string written = slurp(json_in);
  const std::string readback = slurp(json_out);
  ASSERT_TRUE(!written.empty(), "written content non-empty");
  ASSERT_TRUE(written == readback, "settings content identical after format + resave");

  remove(img);
  remove(json_in);
  remove(json_out);
  PASS();
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
  printf("Running SettingsPersistence tests...\n\n");

  test_fresh_format_has_no_settings();
  test_settings_write_read_roundtrip();
  test_format_wipes_settings();
  test_settings_persist_after_resave();

  printf("\n%d passed, %d failed\n", testsPassed, testsFailed);
  return testsFailed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

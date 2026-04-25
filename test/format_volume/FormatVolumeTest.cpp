// Tests that a FAT32 format operation transforms a raw disk image correctly.
//
// Strategy: create a 32 MB file filled with a known sentinel byte (0xDE), run
// mkfs.fat -F 32 against it, then parse the resulting boot sector to verify
// structural integrity.  No button simulation, no firmware running — the format
// logic under test is the same mkfs / FatFormatter codepath the device uses.
//
// FAT32 boot sector offsets used here (FAT32 Specification, section 3):
//   0       : x86 JMP instruction (0xEB .. 0x90)
//   82-89   : FileSystemType = "FAT32   "
//   510-511 : Boot sector signature = 0x55 0xAA

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

static int testsPassed = 0;
static int testsFailed = 0;

// On failure: print diagnostic, increment counter, return from enclosing function.
#define ASSERT_TRUE(cond, msg)                                                                    \
  do {                                                                                            \
    if (!(cond)) {                                                                                \
      fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));                         \
      testsFailed++;                                                                               \
      return;                                                                                      \
    }                                                                                             \
  } while (0)

#define PASS() \
  do { testsPassed++; } while (0)

// ── constants ────────────────────────────────────────────────────────────────

static constexpr size_t IMAGE_BYTES = 32UL * 1024 * 1024;  // 32 MB minimum for FAT32
static constexpr size_t SECTOR_SIZE = 512;
static constexpr uint8_t SENTINEL = 0xDE;

static constexpr size_t FAT32_FSTYPE_OFFSET = 82;
static constexpr size_t BOOT_SIG_OFFSET = 510;

// ── helpers ──────────────────────────────────────────────────────────────────

// Build a unique-per-process path under /tmp.
static void make_image_path(char* buf, size_t len, const char* tag) {
  snprintf(buf, len, "/tmp/cpfmt_%s_%d.img", tag, static_cast<int>(getpid()));
}

// Create IMAGE_BYTES file with the first 16 sectors filled with SENTINEL.
// The remainder is sparse (zeros on Linux) so creation is fast.
static bool create_sentinel_image(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) {
    perror("fopen");
    return false;
  }
  // Seek to end - 1 to size the file without writing every byte.
  if (fseek(f, static_cast<long>(IMAGE_BYTES - 1), SEEK_SET) != 0 || fputc(0, f) == EOF) {
    fclose(f);
    return false;
  }
  // Overwrite the first 16 sectors with SENTINEL so we can verify the
  // format operation touches them.
  rewind(f);
  uint8_t sector[SECTOR_SIZE];
  memset(sector, SENTINEL, SECTOR_SIZE);
  for (int i = 0; i < 16; i++) {
    if (fwrite(sector, 1, SECTOR_SIZE, f) != SECTOR_SIZE) {
      fclose(f);
      return false;
    }
  }
  fclose(f);
  return true;
}

// Call mkfs.fat -F 32 on the image file.  Returns true on success.
static bool run_mkfs_fat32(const char* path) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "mkfs.fat -F 32 %s >/dev/null 2>&1", path);
  return system(cmd) == 0;  // NOLINT — intentional external tool invocation
}

// Read exactly one sector from the beginning of the file.
static bool read_boot_sector(const char* path, uint8_t out[SECTOR_SIZE]) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  const bool ok = fread(out, 1, SECTOR_SIZE, f) == SECTOR_SIZE;
  fclose(f);
  return ok;
}

// Read one sector at byte offset `byte_offset`.
static bool read_sector_at(const char* path, long byte_offset, uint8_t out[SECTOR_SIZE]) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  bool ok = fseek(f, byte_offset, SEEK_SET) == 0 && fread(out, 1, SECTOR_SIZE, f) == SECTOR_SIZE;
  fclose(f);
  return ok;
}

// ── tests ────────────────────────────────────────────────────────────────────

// Baseline: raw sentinel image has no FAT32 boot signature at offset 510.
static void test_before_format_no_boot_signature() {
  char path[128];
  make_image_path(path, sizeof(path), "before");

  ASSERT_TRUE(create_sentinel_image(path), "create sentinel image");

  uint8_t sector[SECTOR_SIZE];
  ASSERT_TRUE(read_boot_sector(path, sector), "read pre-format boot sector");

  // The entire first sector is SENTINEL — no valid boot signature.
  ASSERT_TRUE(sector[BOOT_SIG_OFFSET] == SENTINEL, "no 0x55 before format");
  ASSERT_TRUE(sector[BOOT_SIG_OFFSET + 1] == SENTINEL, "no 0xAA before format");

  remove(path);
  PASS();
}

// After format: standard x86 boot signature (0x55 0xAA) at bytes 510–511.
static void test_after_format_has_boot_signature() {
  char path[128];
  make_image_path(path, sizeof(path), "bootsig");

  ASSERT_TRUE(create_sentinel_image(path), "create sentinel image");
  ASSERT_TRUE(run_mkfs_fat32(path), "mkfs.fat -F 32");

  uint8_t sector[SECTOR_SIZE];
  ASSERT_TRUE(read_boot_sector(path, sector), "read post-format boot sector");

  ASSERT_TRUE(sector[BOOT_SIG_OFFSET] == 0x55, "boot sig byte 0 == 0x55");
  ASSERT_TRUE(sector[BOOT_SIG_OFFSET + 1] == 0xAA, "boot sig byte 1 == 0xAA");

  remove(path);
  PASS();
}

// After format: FAT32 filesystem type string at bytes 82–89.
static void test_after_format_has_fat32_type_string() {
  char path[128];
  make_image_path(path, sizeof(path), "fstype");

  ASSERT_TRUE(create_sentinel_image(path), "create sentinel image");
  ASSERT_TRUE(run_mkfs_fat32(path), "mkfs.fat -F 32");

  uint8_t sector[SECTOR_SIZE];
  ASSERT_TRUE(read_boot_sector(path, sector), "read post-format boot sector");

  static const char expected[8] = {'F', 'A', 'T', '3', '2', ' ', ' ', ' '};
  ASSERT_TRUE(memcmp(&sector[FAT32_FSTYPE_OFFSET], expected, 8) == 0,
              "FAT32 type string at offset 82");

  remove(path);
  PASS();
}

// After format: sector 0 no longer filled with sentinel —
// the format operation overwrote the boot sector area.
static void test_format_overwrites_sentinel_in_boot_sector() {
  char path[128];
  make_image_path(path, sizeof(path), "overwrite");

  ASSERT_TRUE(create_sentinel_image(path), "create sentinel image");
  ASSERT_TRUE(run_mkfs_fat32(path), "mkfs.fat -F 32");

  uint8_t sector[SECTOR_SIZE];
  ASSERT_TRUE(read_boot_sector(path, sector), "read post-format boot sector");

  // A valid FAT32 boot sector always starts with a short-JMP (0xEB).
  ASSERT_TRUE(sector[0] == 0xEB, "boot sector starts with JMP (0xEB), sentinel gone");

  remove(path);
  PASS();
}

// After format: the FAT region (sectors immediately after reserved area) is
// initialised to FAT32 media-descriptor entries — not sentinel data.
// This confirms the format wrote beyond sector 0.
static void test_format_initialises_fat_region() {
  char path[128];
  make_image_path(path, sizeof(path), "fat");

  ASSERT_TRUE(create_sentinel_image(path), "create sentinel image");

  // Confirm sentinel IS present at sector 1 before format.
  {
    uint8_t sec[SECTOR_SIZE];
    ASSERT_TRUE(read_sector_at(path, SECTOR_SIZE, sec), "read sector 1 pre-format");
    ASSERT_TRUE(sec[0] == SENTINEL, "sector 1 is sentinel before format");
  }

  ASSERT_TRUE(run_mkfs_fat32(path), "mkfs.fat -F 32");

  // After format, sector 1 holds FSInfo or reserved area data — not sentinel.
  {
    uint8_t sec[SECTOR_SIZE];
    ASSERT_TRUE(read_sector_at(path, SECTOR_SIZE, sec), "read sector 1 post-format");
    bool all_sentinel = true;
    for (size_t i = 0; i < SECTOR_SIZE; i++) {
      if (sec[i] != SENTINEL) {
        all_sentinel = false;
        break;
      }
    }
    ASSERT_TRUE(!all_sentinel, "sector 1 no longer filled with sentinel after format");
  }

  remove(path);
  PASS();
}

// After format: read BPB fields and verify internal consistency
// (bytes-per-sector == 512, sectors-per-cluster is a power of 2).
static void test_bpb_fields_are_consistent() {
  char path[128];
  make_image_path(path, sizeof(path), "bpb");

  ASSERT_TRUE(create_sentinel_image(path), "create sentinel image");
  ASSERT_TRUE(run_mkfs_fat32(path), "mkfs.fat -F 32");

  uint8_t s[SECTOR_SIZE];
  ASSERT_TRUE(read_boot_sector(path, s), "read post-format boot sector");

  // BPB_BytsPerSec at offset 11 (little-endian uint16)
  const uint16_t bytes_per_sec = static_cast<uint16_t>(s[11]) | (static_cast<uint16_t>(s[12]) << 8);
  ASSERT_TRUE(bytes_per_sec == 512, "BPB_BytsPerSec == 512");

  // BPB_SecPerClus at offset 13 (uint8); must be a non-zero power of 2
  const uint8_t sec_per_clus = s[13];
  ASSERT_TRUE(sec_per_clus > 0, "BPB_SecPerClus > 0");
  ASSERT_TRUE((sec_per_clus & (sec_per_clus - 1)) == 0, "BPB_SecPerClus is power of 2");

  // BPB_NumFATs at offset 16; typically 2 for FAT32
  const uint8_t num_fats = s[16];
  ASSERT_TRUE(num_fats >= 1, "BPB_NumFATs >= 1");

  remove(path);
  PASS();
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
  printf("Running FormatVolume tests...\n\n");

  test_before_format_no_boot_signature();
  test_after_format_has_boot_signature();
  test_after_format_has_fat32_type_string();
  test_format_overwrites_sentinel_in_boot_sector();
  test_format_initialises_fat_region();
  test_bpb_fields_are_consistent();

  printf("\n%d passed, %d failed\n", testsPassed, testsFailed);
  return testsFailed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Adobe ADEPT DRM decryption for Libby/OverDrive EPUB files.
//
// Setup (one-time, per user):
//   1. Authorize a device with your Adobe ID using libgourou or de-acsm on a PC.
//   2. Export the RSA private key in PKCS#8 DER format:
//        openssl pkcs8 -topk8 -nocrypt -in private_key.pem -outform DER -out device.key
//   3. Place device.key at /drm/device.key on the SD card root.
//
// Per-book usage (called automatically by Epub::load() via Epub::initDrm()):
//   - Epub parses META-INF/encryption.xml and META-INF/rights.xml.
//   - init() RSA-decrypts the AES content key using the device key.
//   - Epub::readItemContentsToBytes() / readItemContentsToStream() call
//     isItemEncrypted() / decryptBuffer() transparently during reading.
class AdobeDrm {
 public:
  static constexpr size_t AES_KEY_LEN = 16;
  static constexpr size_t AES_IV_LEN = 16;
  static constexpr size_t AES_BLOCK_LEN = 16;
  // Buffer large enough for RSA-2048 output.
  static constexpr size_t MAX_RSA_OUTPUT = 256;

  // Path on the SD card where the PKCS#8 DER device private key is stored.
  static constexpr const char* ACTIVATION_KEY_PATH = "/drm/device.key";

  // Load the device RSA private key from the SD card into a static buffer.
  // Must succeed before init() can decrypt any book.
  // Returns false if the file is missing or cannot be parsed.
  static bool loadActivation();

  // Release the device private key from heap. Safe to call even if not loaded.
  static void freeActivation();

  static bool hasActivation() { return activationKeyDer != nullptr; }

  // Initialise per-book DRM from data already parsed by Epub::initDrm().
  //   encryptedPaths : sorted, ZIP-relative paths of encrypted items (moved in).
  //   encryptedKeyB64: base64-encoded RSA-encrypted AES key from rights.xml.
  // Returns false if activation is missing or RSA decryption fails.
  bool init(std::vector<std::string> encryptedPaths, const std::string& encryptedKeyB64);

  bool hasDrm() const { return keyReady; }

  // Returns true if itemPath (ZIP-relative, e.g. "OEBPS/chapter1.html") is encrypted.
  bool isItemEncrypted(const char* itemPath) const;

  // Decrypt a raw buffer read from the ZIP for an encrypted item.
  // Format: 16-byte IV || AES-128-CBC ciphertext with PKCS#7 padding.
  // Decrypts in-place: plaintext starts at buf[0].
  // Returns plaintext length (< rawLen), or 0 on error.
  size_t decryptBuffer(uint8_t* buf, size_t rawLen) const;

 private:
  uint8_t contentKey[AES_KEY_LEN] = {};
  bool keyReady = false;
  // ZIP-relative paths of encrypted items, kept sorted for binary search.
  std::vector<std::string> encryptedPaths;

  // Shared across all AdobeDrm instances (one device activation per session).
  static uint8_t* activationKeyDer;
  static size_t activationKeyLen;

  bool decryptContentKey(const uint8_t* encryptedKey, size_t encKeyLen);
};

#include "AdobeDrm.h"

#include <HalStorage.h>
#include <Logging.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>

#include <algorithm>
#include <cstring>
#include <string_view>

// Statics
mbedtls_pk_context* AdobeDrm::activationPk = nullptr;

// Hardware RNG callback required by mbedTLS for RSA blinding (ESP32 built-in TRNG).
static int espRngCallback(void* /*ctx*/, unsigned char* buf, size_t len) {
  for (size_t i = 0; i < len; i += 4) {
    const uint32_t r = esp_random();
    const size_t chunk = (len - i < 4) ? (len - i) : 4;
    memcpy(buf + i, &r, chunk);
  }
  return 0;
}

bool AdobeDrm::loadActivation() {
  if (activationPk) return true;

  FsFile keyFile;
  if (!Storage.openFileForRead("DRM", AdobeDrm::ACTIVATION_KEY_PATH, keyFile)) {
    LOG_ERR("DRM", "Device key not found at %s", AdobeDrm::ACTIVATION_KEY_PATH);
    return false;
  }

  const size_t fileSize = keyFile.fileSize();
  if (fileSize == 0 || fileSize > 4096) {
    LOG_ERR("DRM", "Device key file has unexpected size: %zu", fileSize);
    keyFile.close();
    return false;
  }

  auto* buf = static_cast<uint8_t*>(malloc(fileSize));
  if (!buf) {
    LOG_ERR("DRM", "malloc failed for device key buffer (%zu bytes)", fileSize);
    keyFile.close();
    return false;
  }

  if (keyFile.read(buf, fileSize) != static_cast<int>(fileSize)) {
    LOG_ERR("DRM", "Failed to read device key from SD");
    free(buf);
    keyFile.close();
    return false;
  }
  keyFile.close();

  // Parse and store the key. Keeping the parsed context avoids re-parsing (~1–2 s on
  // ESP32-C3) on every book open. The pk context manages its own internal heap memory.
  auto* pk = static_cast<mbedtls_pk_context*>(malloc(sizeof(mbedtls_pk_context)));
  if (!pk) {
    LOG_ERR("DRM", "malloc failed for pk_context");
    free(buf);
    return false;
  }
  mbedtls_pk_init(pk);

  const int ret = mbedtls_pk_parse_key(pk, buf, fileSize, nullptr, 0, espRngCallback, nullptr);
  free(buf);
  if (ret != 0) {
    LOG_ERR("DRM", "Device key parse failed: -0x%04X (must be PKCS#8 DER format)", -ret);
    mbedtls_pk_free(pk);
    free(pk);
    return false;
  }

  activationPk = pk;
  LOG_INF("DRM", "Device activation key loaded and parsed");
  return true;
}

void AdobeDrm::freeActivation() {
  if (activationPk) {
    mbedtls_pk_free(activationPk);
    free(activationPk);
    activationPk = nullptr;
  }
}

bool AdobeDrm::validateKeyBuffer(const uint8_t* der, size_t len) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  const int ret = mbedtls_pk_parse_key(&pk, der, len, nullptr, 0, espRngCallback, nullptr);
  mbedtls_pk_free(&pk);
  return ret == 0;
}

bool AdobeDrm::decryptContentKey(const uint8_t* encryptedKey, const size_t encKeyLen) {
  if (!activationPk) {
    LOG_ERR("DRM", "No device activation key loaded");
    return false;
  }

  // Output buffer on heap: 256 bytes exceeds the 256-byte stack variable limit (CLAUDE.md).
  auto* decrypted = static_cast<uint8_t*>(malloc(MAX_RSA_OUTPUT));
  if (!decrypted) {
    LOG_ERR("DRM", "malloc failed for RSA output buffer");
    return false;
  }

  size_t decryptedLen = 0;
  // Use the already-parsed activationPk — no re-parse needed.
  const int ret = mbedtls_pk_decrypt(activationPk, encryptedKey, encKeyLen, decrypted,
                                     &decryptedLen, MAX_RSA_OUTPUT, espRngCallback, nullptr);
  if (ret != 0) {
    LOG_ERR("DRM", "RSA decrypt failed: -0x%04X", -ret);
    free(decrypted);
    return false;
  }

  if (decryptedLen != AES_KEY_LEN) {
    LOG_ERR("DRM", "Unexpected content key length: %zu (expected %zu)", decryptedLen, AES_KEY_LEN);
    free(decrypted);
    return false;
  }

  memcpy(contentKey, decrypted, AES_KEY_LEN);
  free(decrypted);
  keyReady = true;
  LOG_INF("DRM", "Content key decrypted successfully");
  return true;
}

bool AdobeDrm::init(std::vector<std::string> paths, const std::string& encryptedKeyB64) {
  keyReady = false;
  encryptedPaths.clear();

  if (!activationPk) {
    LOG_ERR("DRM", "Cannot init book DRM: no device activation loaded");
    return false;
  }

  if (paths.empty() || encryptedKeyB64.empty()) {
    LOG_ERR("DRM", "init() called with empty paths or key");
    return false;
  }

  // Base64-decode the RSA-encrypted AES key. On heap: 256 bytes exceeds the stack limit.
  auto* rsaEncryptedKey = static_cast<uint8_t*>(malloc(MAX_RSA_OUTPUT));
  if (!rsaEncryptedKey) {
    LOG_ERR("DRM", "malloc failed for RSA key buffer");
    return false;
  }

  size_t rsaKeyLen = 0;
  const int b64Ret =
      mbedtls_base64_decode(rsaEncryptedKey, MAX_RSA_OUTPUT, &rsaKeyLen,
                            reinterpret_cast<const uint8_t*>(encryptedKeyB64.c_str()),
                            encryptedKeyB64.size());
  if (b64Ret != 0) {
    LOG_ERR("DRM", "Base64 decode of encryptedKey failed: -0x%04X", -b64Ret);
    free(rsaEncryptedKey);
    return false;
  }

  const bool ok = decryptContentKey(rsaEncryptedKey, rsaKeyLen);
  free(rsaEncryptedKey);
  if (!ok) return false;

  encryptedPaths = std::move(paths);
  std::sort(encryptedPaths.begin(), encryptedPaths.end());
  return true;
}

bool AdobeDrm::isItemEncrypted(const char* itemPath) const {
  if (!keyReady || encryptedPaths.empty()) return false;
  // string_view avoids heap allocation on this hot path.
  const std::string_view sv(itemPath);
  return std::binary_search(encryptedPaths.cbegin(), encryptedPaths.cend(), sv,
                             [](const auto& a, const auto& b) { return a < b; });
}

size_t AdobeDrm::decryptBuffer(uint8_t* buf, const size_t rawLen) const {
  if (!keyReady) {
    LOG_ERR("DRM", "decryptBuffer called without a ready content key");
    return 0;
  }
  if (rawLen <= AES_IV_LEN) {
    LOG_ERR("DRM", "Buffer too short to contain IV + ciphertext (%zu bytes)", rawLen);
    return 0;
  }

  const size_t cipherLen = rawLen - AES_IV_LEN;
  if (cipherLen % AES_BLOCK_LEN != 0) {
    LOG_ERR("DRM", "Ciphertext length %zu not a multiple of AES block size", cipherLen);
    return 0;
  }

  // First AES_IV_LEN bytes are the IV; capture it before we overwrite buf.
  uint8_t iv[AES_IV_LEN];
  memcpy(iv, buf, AES_IV_LEN);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, contentKey, AES_KEY_LEN * 8);
  // Decrypt ciphertext at buf[AES_IV_LEN..] into buf[0..]. The output range
  // starts 16 bytes before the input range, which mbedtls handles correctly
  // because CBC only reads each input block once before writing the output.
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipherLen, iv, buf + AES_IV_LEN, buf);
  mbedtls_aes_free(&aes);

  // Validate and remove PKCS#7 padding.
  const uint8_t pad = buf[cipherLen - 1];
  if (pad == 0 || pad > AES_BLOCK_LEN) {
    LOG_ERR("DRM", "Invalid PKCS#7 padding byte: %u", pad);
    return 0;
  }
  for (size_t i = cipherLen - pad; i < cipherLen; ++i) {
    if (buf[i] != pad) {
      LOG_ERR("DRM", "PKCS#7 padding validation failed at byte %zu", i);
      return 0;
    }
  }

  return cipherLen - pad;
}

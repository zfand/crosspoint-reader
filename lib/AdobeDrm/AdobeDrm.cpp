#include "AdobeDrm.h"

#include <HalStorage.h>
#include <Logging.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>

#include <algorithm>
#include <cstring>

// Statics
uint8_t* AdobeDrm::activationKeyDer = nullptr;
size_t AdobeDrm::activationKeyLen = 0;

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
  if (activationKeyDer) return true;

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

  // Validate before storing: attempt to parse the key with mbedTLS.
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  const int ret =
      mbedtls_pk_parse_key(&pk, buf, fileSize, nullptr, 0, espRngCallback, nullptr);
  mbedtls_pk_free(&pk);
  if (ret != 0) {
    LOG_ERR("DRM", "Device key parse failed: -0x%04X (must be PKCS#8 DER format)", -ret);
    free(buf);
    return false;
  }

  activationKeyDer = buf;
  activationKeyLen = fileSize;
  LOG_INF("DRM", "Device activation key loaded (%zu bytes)", fileSize);
  return true;
}

void AdobeDrm::freeActivation() {
  free(activationKeyDer);
  activationKeyDer = nullptr;
  activationKeyLen = 0;
}

bool AdobeDrm::decryptContentKey(const uint8_t* encryptedKey, const size_t encKeyLen) {
  if (!activationKeyDer) {
    LOG_ERR("DRM", "No device activation key loaded");
    return false;
  }

  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);

  int ret = mbedtls_pk_parse_key(&pk, activationKeyDer, activationKeyLen, nullptr, 0,
                                  espRngCallback, nullptr);
  if (ret != 0) {
    LOG_ERR("DRM", "pk_parse_key failed: -0x%04X", -ret);
    mbedtls_pk_free(&pk);
    return false;
  }

  uint8_t decrypted[MAX_RSA_OUTPUT];
  size_t decryptedLen = 0;
  ret = mbedtls_pk_decrypt(&pk, encryptedKey, encKeyLen, decrypted, &decryptedLen,
                            sizeof(decrypted), espRngCallback, nullptr);
  mbedtls_pk_free(&pk);

  if (ret != 0) {
    LOG_ERR("DRM", "RSA decrypt failed: -0x%04X", -ret);
    return false;
  }

  if (decryptedLen != AES_KEY_LEN) {
    LOG_ERR("DRM", "Unexpected content key length: %zu (expected %zu)", decryptedLen, AES_KEY_LEN);
    return false;
  }

  memcpy(contentKey, decrypted, AES_KEY_LEN);
  keyReady = true;
  LOG_INF("DRM", "Content key decrypted successfully");
  return true;
}

bool AdobeDrm::init(std::vector<std::string> paths, const std::string& encryptedKeyB64) {
  keyReady = false;
  encryptedPaths.clear();

  if (!activationKeyDer) {
    LOG_ERR("DRM", "Cannot init book DRM: no device activation loaded");
    return false;
  }

  if (paths.empty() || encryptedKeyB64.empty()) {
    LOG_ERR("DRM", "init() called with empty paths or key");
    return false;
  }

  // Base64-decode the RSA-encrypted AES key.
  uint8_t rsaEncryptedKey[MAX_RSA_OUTPUT];
  size_t rsaKeyLen = 0;
  const int b64Ret =
      mbedtls_base64_decode(rsaEncryptedKey, sizeof(rsaEncryptedKey), &rsaKeyLen,
                            reinterpret_cast<const uint8_t*>(encryptedKeyB64.c_str()),
                            encryptedKeyB64.size());
  if (b64Ret != 0) {
    LOG_ERR("DRM", "Base64 decode of encryptedKey failed: -0x%04X", -b64Ret);
    return false;
  }

  if (!decryptContentKey(rsaEncryptedKey, rsaKeyLen)) {
    return false;
  }

  encryptedPaths = std::move(paths);
  std::sort(encryptedPaths.begin(), encryptedPaths.end());
  return true;
}

bool AdobeDrm::isItemEncrypted(const char* itemPath) const {
  if (!keyReady || encryptedPaths.empty()) return false;
  return std::binary_search(encryptedPaths.begin(), encryptedPaths.end(),
                             std::string(itemPath));
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

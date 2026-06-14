#pragma once
#include <Print.h>
#include <cstddef>
#include <string>
#include <vector>

#include "expat.h"

// Parses META-INF/encryption.xml from an Adobe ADEPT DRM EPUB.
// Collects the ZIP-relative paths of all encrypted content items
// (the URI attributes on CipherReference elements).
class EncryptionXmlParser final : public Print {
  enum State { START, IN_ENCRYPTION, IN_ENCRYPTED_DATA, IN_CIPHER_DATA };

  size_t remainingSize;
  XML_Parser parser = nullptr;
  State state = START;

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void endElement(void* userData, const XML_Char* name);

 public:
  std::vector<std::string> encryptedPaths;

  explicit EncryptionXmlParser(size_t xmlSize) : remainingSize(xmlSize) {}
  ~EncryptionXmlParser() override;

  bool setup();
  size_t write(uint8_t) override;
  size_t write(const uint8_t* buf, size_t size) override;
};

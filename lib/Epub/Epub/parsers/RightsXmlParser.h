#pragma once
#include <Print.h>
#include <cstddef>
#include <string>

#include "expat.h"

// Parses META-INF/rights.xml from an Adobe ADEPT DRM EPUB.
// Extracts the base64-encoded RSA-encrypted AES content key from
// the adept:encryptedKey element inside adept:licenseToken.
class RightsXmlParser final : public Print {
  enum State { START, IN_RIGHTS, IN_LICENSE_TOKEN, IN_ENCRYPTED_KEY };

  size_t remainingSize;
  XML_Parser parser = nullptr;
  State state = START;

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void endElement(void* userData, const XML_Char* name);
  static void charData(void* userData, const XML_Char* s, int len);

 public:
  // base64-encoded RSA-encrypted AES content key; non-empty on success.
  std::string encryptedKeyB64;

  explicit RightsXmlParser(size_t xmlSize) : remainingSize(xmlSize) {}
  ~RightsXmlParser() override;

  bool setup();
  size_t write(uint8_t) override;
  size_t write(const uint8_t* buf, size_t size) override;
};

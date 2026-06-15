#include "RightsXmlParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

bool RightsXmlParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_ERR("RGT", "Failed to allocate expat parser");
    return false;
  }
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, charData);
  return true;
}

RightsXmlParser::~RightsXmlParser() { destroyXmlParser(parser); }

size_t RightsXmlParser::write(const uint8_t data) { return write(&data, 1); }

size_t RightsXmlParser::write(const uint8_t* buffer, const size_t size) {
  if (!parser) return 0;

  const uint8_t* pos = buffer;
  size_t remaining = size;

  while (remaining > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);
    if (!buf) {
      destroyXmlParser(parser);
      return 0;
    }
    const size_t toRead = remaining < 1024 ? remaining : 1024;
    memcpy(buf, pos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), remainingSize == toRead) ==
        XML_STATUS_ERROR) {
      LOG_ERR("RGT", "Parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return 0;
    }

    pos += toRead;
    remaining -= toRead;
    remainingSize -= toRead;
  }
  return size;
}

void XMLCALL RightsXmlParser::startElement(void* userData, const XML_Char* name,
                                            const XML_Char** /*atts*/) {
  auto* self = static_cast<RightsXmlParser*>(userData);

  const char* local = strrchr(name, ':');
  local = local ? local + 1 : name;

  if (self->state == START && strcmp(local, "rights") == 0) {
    self->state = IN_RIGHTS;
  } else if (self->state == IN_RIGHTS && strcmp(local, "licenseToken") == 0) {
    self->state = IN_LICENSE_TOKEN;
  } else if (self->state == IN_LICENSE_TOKEN && strcmp(local, "encryptedKey") == 0) {
    self->encryptedKeyB64.clear();
    self->state = IN_ENCRYPTED_KEY;
  }
}

void XMLCALL RightsXmlParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<RightsXmlParser*>(userData);

  const char* local = strrchr(name, ':');
  local = local ? local + 1 : name;

  if (self->state == IN_ENCRYPTED_KEY && strcmp(local, "encryptedKey") == 0) {
    // Trim whitespace from the collected base64 content.
    while (!self->encryptedKeyB64.empty() && self->encryptedKeyB64.back() <= ' ') {
      self->encryptedKeyB64.pop_back();
    }
    size_t start = 0;
    while (start < self->encryptedKeyB64.size() && self->encryptedKeyB64[start] <= ' ') {
      ++start;
    }
    if (start > 0) self->encryptedKeyB64.erase(0, start);
    self->state = IN_LICENSE_TOKEN;
  } else if (self->state == IN_LICENSE_TOKEN && strcmp(local, "licenseToken") == 0) {
    self->state = IN_RIGHTS;
  } else if (self->state == IN_RIGHTS && strcmp(local, "rights") == 0) {
    self->state = START;
  }
}

void XMLCALL RightsXmlParser::charData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<RightsXmlParser*>(userData);
  if (self->state == IN_ENCRYPTED_KEY && len > 0) {
    // Reserve space for the full expected base64 string (~172 bytes for RSA-1024).
    if (self->encryptedKeyB64.empty()) {
      self->encryptedKeyB64.reserve(256);
    }
    self->encryptedKeyB64.append(s, static_cast<size_t>(len));
  }
}

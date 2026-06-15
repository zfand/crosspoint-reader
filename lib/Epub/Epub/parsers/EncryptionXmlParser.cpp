#include "EncryptionXmlParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

bool EncryptionXmlParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_ERR("ENC", "Failed to allocate expat parser");
    return false;
  }
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  return true;
}

EncryptionXmlParser::~EncryptionXmlParser() { destroyXmlParser(parser); }

size_t EncryptionXmlParser::write(const uint8_t data) { return write(&data, 1); }

size_t EncryptionXmlParser::write(const uint8_t* buffer, const size_t size) {
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
      LOG_ERR("ENC", "Parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return 0;
    }

    pos += toRead;
    remaining -= toRead;
    remainingSize -= toRead;
  }
  return size;
}

void XMLCALL EncryptionXmlParser::startElement(void* userData, const XML_Char* name,
                                               const XML_Char** atts) {
  auto* self = static_cast<EncryptionXmlParser*>(userData);

  // Match local names regardless of namespace prefix (expat in non-NS mode keeps prefix).
  const char* local = strrchr(name, ':');
  local = local ? local + 1 : name;

  if (self->state == START && strcmp(local, "encryption") == 0) {
    self->state = IN_ENCRYPTION;
    return;
  }
  if (self->state == IN_ENCRYPTION && strcmp(local, "EncryptedData") == 0) {
    self->state = IN_ENCRYPTED_DATA;
    return;
  }
  if (self->state == IN_ENCRYPTED_DATA && strcmp(local, "CipherData") == 0) {
    self->state = IN_CIPHER_DATA;
    return;
  }
  if (self->state == IN_CIPHER_DATA && strcmp(local, "CipherReference") == 0) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "URI") == 0 && atts[i + 1][0] != '\0') {
        self->encryptedPaths.emplace_back(atts[i + 1]);
        break;
      }
    }
  }
}

void XMLCALL EncryptionXmlParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<EncryptionXmlParser*>(userData);

  const char* local = strrchr(name, ':');
  local = local ? local + 1 : name;

  if (self->state == IN_CIPHER_DATA && strcmp(local, "CipherData") == 0) {
    self->state = IN_ENCRYPTED_DATA;
  } else if (self->state == IN_ENCRYPTED_DATA && strcmp(local, "EncryptedData") == 0) {
    self->state = IN_ENCRYPTION;
  } else if (self->state == IN_ENCRYPTION && strcmp(local, "encryption") == 0) {
    self->state = START;
  }
}

#pragma once

#include <cstring>
#include <string>

namespace XPathUtils {

// Find the integer inside the first (or last) bracket following `prefix`.
// Returns -1 if the prefix is absent, the bracket is empty, or a non-digit is found.
// Examples:
//   parseIndex("/body/DocFragment[8]/body/p[4]", "/body/DocFragment[") -> 8
//   parseIndex("/body/DocFragment[8]/body/p[4]", "/p[", /*last=*/true) -> 4
inline int parseIndex(const std::string& xpath, const char* prefix, bool last = false) {
  const size_t prefixLen = strlen(prefix);
  const size_t pos = last ? xpath.rfind(prefix) : xpath.find(prefix);
  if (pos == std::string::npos) return -1;
  const size_t numStart = pos + prefixLen;
  const size_t numEnd = xpath.find(']', numStart);
  if (numEnd == std::string::npos || numEnd == numStart) return -1;
  int val = 0;
  for (size_t i = numStart; i < numEnd; i++) {
    if (xpath[i] < '0' || xpath[i] > '9') return -1;
    val = val * 10 + (xpath[i] - '0');
  }
  return val;
}

// Extract the character offset from a KOReader XPath trailing "text().<N>".
// Returns 0 when no "text()" node is present or the number is absent/malformed.
// Example:
//   parseCharOffset("/body/DocFragment[8]/body/p[4]/text().96") -> 96
inline int parseCharOffset(const std::string& xpath) {
  const size_t textPos = xpath.rfind("text()");
  if (textPos == std::string::npos) return 0;
  const size_t dotPos = xpath.find('.', textPos);
  if (dotPos == std::string::npos || dotPos + 1 >= xpath.size()) return 0;
  int val = 0;
  for (size_t i = dotPos + 1; i < xpath.size(); i++) {
    if (xpath[i] < '0' || xpath[i] > '9') return 0;
    val = val * 10 + (xpath[i] - '0');
  }
  return val;
}

}  // namespace XPathUtils

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace BytesFormatter {

// Formats a byte count into a human-readable string (e.g. "3.7 GB", "512 MB", "200 KB").
// buf must be at least 12 bytes.
inline void format(uint64_t bytes, char* buf, size_t bufLen) {
  if (bytes >= 1024ULL * 1024 * 1024) {
    snprintf(buf, bufLen, "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
  } else if (bytes >= 1024ULL * 1024) {
    snprintf(buf, bufLen, "%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else {
    snprintf(buf, bufLen, "%llu KB", static_cast<unsigned long long>(bytes) / 1024ULL);
  }
}

}  // namespace BytesFormatter

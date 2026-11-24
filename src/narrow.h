// A name read from a folder as the kit's narrow text. On Windows a name outside the program's code page (any but
// UTF-8's, without the UTF-8 manifest), or one that isn't valid UTF-16, can't be: path::string() throws there. No
// recipe names such a file, so the walks pass it over. Elsewhere it is always text.
#pragma once

#include <exception>
#include <filesystem>
#include <string>

namespace rekit {

inline bool narrow(const std::filesystem::path& p, std::string* out) {
  try {
    *out = p.string();
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace rekit

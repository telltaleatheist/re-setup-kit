#pragma once
// Archives and disc images the tests write with libarchive's own writer
// (nothing committed). Only with REKIT_HAVE_LIBARCHIVE.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <archive.h>
#include <archive_entry.h>

#include "check.h"
#include "rekit/archive.h"

namespace {

using Files = std::map<std::string, std::string>;  // path -> bytes

// `files` written in `format` ("zip", "7zip", "iso9660") into memory.
inline std::string write_archive(const char* format, const Files& files) {
  archive* a = archive_write_new();
  archive_write_set_format_by_name(a, format);
  if (std::strcmp(format, "7zip") == 0) archive_write_set_format_option(a, "7zip", "compression", "lzma2");
  std::vector<char> buf(64 << 20);
  size_t used = 0;
  archive_write_open_memory(a, buf.data(), buf.size(), &used);
  for (const auto& [path, data] : files) {
    archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, path.c_str());
    archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_write_header(a, e);
    archive_write_data(a, data.data(), data.size());
    archive_entry_free(e);
  }
  archive_write_close(a);
  archive_write_free(a);
  return std::string(buf.data(), used);
}

// A ReadFn over bytes in memory.
inline rekit::ArchiveReader::ReadFn memory_stream(const std::string& bytes) {
  auto at = std::make_shared<size_t>(0);
  return [bytes, at](uint8_t* buf, size_t n) -> int64_t {
    const size_t k = std::min(n, bytes.size() - *at);
    std::memcpy(buf, bytes.data() + *at, k);
    *at += k;
    return static_cast<int64_t>(k);
  };
}

inline std::string temp_file(const char* name, const std::string& bytes) {
  const std::string p = std::string(REKIT_TEST_TMP) + "/" + name;
  std::FILE* f = std::fopen(p.c_str(), "wb");
  std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  return p;
}

// An ISO image's 2048-byte sectors as a raw MODE1/2352 track: sync, header,
// the data at 16, the EDC/ECC area left zero (the reader skips it).
inline std::string raw_track(const std::string& iso) {
  std::string out;
  for (size_t at = 0; at < iso.size(); at += 2048) {
    std::string s(2352, '\0');
    s[0] = 0;
    for (int i = 1; i < 11; ++i) s[static_cast<size_t>(i)] = static_cast<char>(0xff);
    s[15] = 1;  // MODE1
    std::memcpy(&s[16], iso.data() + at, std::min<size_t>(2048, iso.size() - at));
    out += s;
  }
  return out;
}

}  // namespace

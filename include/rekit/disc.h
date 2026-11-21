#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// The files of a disc (from mw3-re): a folder or drive, an ISO 9660 image
// (2048-byte sectors or raw 2352-byte ones), or a cue sheet's first data
// track (MODE1/2352, MODE2/2352 XA form 1, MODE1/2048). Paths are
// '/'-separated as the disc has them, without ";1".
namespace rekit {

class Disc {
 public:
  struct File {
    std::string path;
    uint64_t size = 0;
    uint64_t at = 0;  // an image's: its first sector's number
  };
  Disc() = default;
  Disc(const Disc&) = delete;
  Disc& operator=(const Disc&) = delete;
  ~Disc();
  bool open(const std::string& path, std::string* error);
  const std::vector<File>& files() const { return files_; }
  const File* find(const std::string& path) const;  // any case
  // `n` bytes of `f` from `off`.
  bool read(const File& f, uint64_t off, size_t n, uint8_t* out);
  std::string kind() const;     // "folder", "ISO image", "raw image (2352-byte sectors)"
  std::string cue;              // the cue sheet it was opened from, if any

 private:
  bool read_image_dir(uint64_t sector, uint64_t size, const std::string& prefix, int depth);
  bool read_sectors(uint64_t sector, uint32_t in, size_t n, uint8_t* out);
  std::string root_;            // a folder's
  std::FILE* image_ = nullptr;  // an image's
  int64_t start_ = 0;           // the data track's first byte in the image file
  uint32_t sector_ = 2048, user_ = 0;  // bytes a sector, and the 2048 user bytes' offset in it
  std::vector<File> files_;
};

}  // namespace rekit

#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// InstallShield 5 cabinets (DATA1.HDR + DATA1.CAB), read, never run: the
// setups of late-90s PC games, discs and patches alike (from mw3-re's
// patch_import; the layout as unshield reads it).
namespace rekit {
struct CabFile {
  std::string path;  // as the source names it, '/'-separated ("zbd/c1/reader.zbd")
  std::vector<uint8_t> data;
};

// Every file of an InstallShield 5 cabinet (data1.hdr's directory, data1.cab's
// data), each under its directory; false with `error` set when it is not one
// this reads (another version, split across volumes) or a file fails to inflate.
bool read_installshield(const std::vector<uint8_t>& hdr, const std::vector<uint8_t>& cab,
                        std::vector<CabFile>* out, std::string* error);

// The same, file by file, from a cabinet read where it lies (a disc's
// DATA1.CAB, 198 MB): `read` gives `n` bytes at `at`; `each` takes each file
// (false stops with an error). Every file is checked against its MD5.
// The files a cabinet's header lists (path, expanded size), without its data.
bool list_installshield(const std::vector<uint8_t>& hdr, std::vector<std::pair<std::string, uint64_t>>* out,
                        std::string* error);
using CabRead = std::function<bool(uint64_t at, size_t n, uint8_t* out)>;
bool each_installshield(const std::vector<uint8_t>& hdr, uint64_t cab_size, const CabRead& read,
                        const std::function<bool(CabFile&&)>& each, std::string* error);
}  // namespace rekit

#include "rekit/sources.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>

#include "rekit/archive.h"
#include "rekit/cue_audio.h"
#include "rekit/disc.h"
#include "narrow.h"

namespace fs = std::filesystem;

namespace rekit {
namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool ends(const std::string& s, const std::string& e) {
  return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}
std::string base_of(const std::string& p) {
  const size_t k = p.find_last_of('/');
  return k == std::string::npos ? p : p.substr(k + 1);
}
std::string dir_of(const std::string& p) {
  const size_t k = p.find_last_of('/');
  return k == std::string::npos ? "" : p.substr(0, k + 1);
}
// `l` (an entry's path, lower case) is the clue at some folder: the clue's whole path or "<folder>/<clue>".
bool is_clue(const std::string& l, const std::string& clue) {
  return l == clue || (ends(l, clue) && l[l.size() - clue.size() - 1] == '/');
}

// A forward reader of a disc image's 2048-byte sectors from a stream.
class SectorStream {
 public:
  explicit SectorStream(ArchiveReader::ReadFn read) : read_(std::move(read)) {}
  // Sector `n` (not before the last one read), into `out`.
  bool get(uint64_t n, uint8_t* out) {
    if (n < next_) return false;
    while (next_ <= n) {
      size_t got = 0;
      while (got < 2048) {
        const int64_t r = read_(out + got, 2048 - got);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
      }
      ++next_;
    }
    return true;
  }

 private:
  ArchiveReader::ReadFn read_;
  uint64_t next_ = 0;
};

// A ReadFn that gives `prefix` first, then the rest of `rest`.
ArchiveReader::ReadFn prefixed(std::vector<uint8_t> prefix, ArchiveReader::ReadFn rest) {
  auto st = std::make_shared<std::pair<std::vector<uint8_t>, size_t>>(std::move(prefix), 0);
  return [st, rest](uint8_t* buf, size_t n) -> int64_t {
    if (st->second < st->first.size()) {
      const size_t k = std::min(n, st->first.size() - st->second);
      std::memcpy(buf, st->first.data() + st->second, k);
      st->second += k;
      return static_cast<int64_t>(k);
    }
    return rest(buf, n);
  };
}

// A disc image read from a stream: its kind (raw or 2048-byte sectors), its
// volume label, and the first disc clue its top folder lists (read when the
// stream reaches it).
struct ImageInfo {
  bool image = false, raw = false;
  int offset = 0;
  std::string label;
  const Clue* clue = nullptr;
  std::vector<uint8_t> bytes;
};
ImageInfo read_image(ArchiveReader::ReadFn read, const std::vector<const Clue*>& clues) {
  ImageInfo info;
  std::vector<uint8_t> head;
  head.resize(17 * 2352);
  size_t got = 0;
  while (got < head.size()) {
    const int64_t r = read(head.data() + got, head.size() - got);
    if (r <= 0) break;
    got += static_cast<size_t>(r);
  }
  head.resize(got);
  static const uint8_t sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
  ArchiveReader::ReadFn cooked;
  if (head.size() >= 2352 && std::memcmp(head.data(), sync, 12) == 0) {
    info.raw = true;
    info.offset = head[15] == 2 ? 24 : 16;
    cooked = cooked_sectors(prefixed(head, read), info.offset);
  } else if (head.size() > 0x8006 && std::memcmp(head.data() + 0x8001, "CD001", 5) == 0) {
    cooked = prefixed(head, read);
  } else {
    return info;  // not a data track (an audio track, or not an image)
  }
  SectorStream s(cooked);
  uint8_t pvd[2048];
  if (!s.get(16, pvd) || std::memcmp(pvd + 1, "CD001", 5) != 0) return info;
  info.image = true;
  info.label.assign(reinterpret_cast<char*>(pvd + 40), 32);
  while (!info.label.empty() && info.label.back() == ' ') info.label.pop_back();
  uint32_t root, root_size;
  std::memcpy(&root, pvd + 156 + 2, 4);
  std::memcpy(&root_size, pvd + 156 + 10, 4);
  // The root directory: the clue's extent and size.
  uint32_t at = 0, size = 0;
  uint8_t sec[2048];
  for (uint32_t i = 0; i < (root_size + 2047) / 2048 && !at; ++i) {
    if (!s.get(root + i, sec)) return info;
    for (uint32_t o = 0; o < 2048 && sec[o];) {
      const uint8_t len = sec[o];
      const uint8_t nl = sec[o + 32];
      std::string name(reinterpret_cast<char*>(sec + o + 33), nl);
      name = lower(name.substr(0, name.find(';')));
      for (const Clue* c : clues)
        if (name == c->path) {
          std::memcpy(&at, sec + o + 2, 4);
          std::memcpy(&size, sec + o + 10, 4);
          info.clue = c;
          break;
        }
      if (at) break;
      o += len;
    }
  }
  if (!at || size > info.clue->limit) {
    info.clue = nullptr;
    return info;
  }
  info.bytes.resize(size);
  for (uint32_t i = 0; i * 2048 < size; ++i) {
    if (!s.get(at + i, sec)) {
      info.bytes.clear();
      info.clue = nullptr;
      return info;
    }
    std::memcpy(info.bytes.data() + i * 2048, sec, std::min<size_t>(2048, size - i * 2048));
  }
  return info;
}

// A cue sheet's tracks, from its text (one inside an archive has no path to read_cue).
struct CueTrackAt {
  std::string file;
  int number = 0;
  int64_t start = 0;  // INDEX 01, in bytes into its file
  bool audio = false;
};
std::vector<CueTrackAt> parse_cue(const std::string& text) {
  std::vector<CueTrackAt> out;
  std::istringstream in(text);
  std::string line, file;
  while (std::getline(in, line)) {
    std::istringstream w(line);
    std::string kw;
    w >> kw;
    kw = lower(kw);
    if (kw == "file") {
      const size_t a = line.find('"'), b = line.rfind('"');
      file = a != std::string::npos && b > a ? line.substr(a + 1, b - a - 1) : "";
    } else if (kw == "track") {
      std::string num, mode;
      w >> num >> mode;
      CueTrackAt t;
      t.file = file;
      t.number = std::atoi(num.c_str());
      t.audio = lower(mode) == "audio";
      out.push_back(t);
    } else if (kw == "index" && !out.empty()) {
      std::string num, msf;
      w >> num >> msf;
      int m = 0, sec = 0, fr = 0;
      if (std::atoi(num.c_str()) == 1 && std::sscanf(msf.c_str(), "%d:%d:%d", &m, &sec, &fr) == 3)
        out.back().start = (static_cast<int64_t>(m) * 60 * 75 + sec * 75 + fr) * 2352;
    }
  }
  return out;
}

struct Walk {
  const SourceRules* rules = nullptr;
  Found best;
  std::string detail;
  SourceProgress progress;
  ArchiveReader* top = nullptr;  // the picked file's reader: its place for the fraction
  uint64_t top_size = 1;
  bool stopped = false;
  bool go(const std::string& entry) {
    if (stopped) return false;
    if (progress && !progress(top ? std::min(1.0, static_cast<double>(top->position()) / top_size) : 0.0, entry))
      stopped = true;
    return !stopped;
  }
  // `read` asking go() every 4 MB: a stop ends the stream.
  ArchiveReader::ReadFn watched(ArchiveReader::ReadFn read, const std::string& entry) {
    auto since = std::make_shared<uint64_t>(0);
    return [this, read, entry, since](uint8_t* buf, size_t n) -> int64_t {
      if ((*since += n) >= (4u << 20)) {
        *since = 0;
        if (!go(entry)) return -1;
      }
      return read(buf, n);
    };
  }
  // The recipe's rank, an installed copy over a disc; the first of equals kept.
  void consider(const Found& f) {
    auto rank = [](const Found& x) { return x.kind == Found::kNothing ? 0 : x.rank + (x.kind == Found::kInstall ? 1 : 0); };
    if (rank(f) > rank(best)) best = f;
  }
  std::vector<const Clue*> disc_clues() const {
    std::vector<const Clue*> out;
    for (const Clue& c : rules->clues)
      if (c.kind == Clue::kDisc && c.path.find('/') == std::string::npos) out.push_back(&c);
    return out;
  }
};

void take(Found& f, const Identity& id) {
  f.game = id.game, f.version = id.version, f.rank = id.rank;
}

std::vector<uint8_t> read_whole(ArchiveReader& r, size_t limit) {
  std::vector<uint8_t> out;
  uint8_t buf[1 << 16];
  for (int64_t n; (n = r.read(buf, sizeof buf)) > 0;) {
    out.insert(out.end(), buf, buf + n);
    if (out.size() > limit) break;
  }
  return out;
}

void walk_archive(ArchiveReader& r, const std::string& path, const std::vector<Step>& steps, int depth, Walk& w) {
  std::vector<std::pair<std::string, std::vector<CueTrackAt>>> cues;  // their folder, their tracks
  std::vector<std::pair<std::string, Found>> images;                  // the data tracks found, by entry
  const std::vector<const Clue*> image_clues = w.disc_clues();
  ArchiveReader::Entry e;
  while (r.next(&e) && w.go(e.path)) {
    if (e.dir) continue;
    const std::string l = lower(e.path), base = base_of(l);
    const Clue* clue = nullptr;
    for (const Clue& c : w.rules->clues)
      if (is_clue(l, c.path)) {
        clue = &c;
        break;
      }
    if (clue) {
      const Identity id = clue->judge(read_whole(r, clue->limit));
      if (id.game.empty()) {
        if (!id.why.empty()) w.detail += "  " + e.path + ": " + id.why + "\n";
        continue;
      }
      Found f;
      take(f, id);
      f.kind = clue->kind == Clue::kInstall ? Found::kInstall : Found::kDisc;
      f.path = path, f.steps = steps;
      f.root = e.path.substr(0, e.path.size() - clue->path.size());
      w.detail += std::string(f.kind == Found::kInstall ? "  an installed copy at " : "  a disc's files at ") +
                  (f.root.empty() ? "the top" : f.root) + "\n";
      w.consider(f);
    } else if (ends(base, ".cue")) {
      const std::vector<uint8_t> b = read_whole(r, 1u << 20);
      cues.emplace_back(dir_of(e.path), parse_cue(std::string(b.begin(), b.end())));
    } else if (ends(base, ".iso") || ends(base, ".bin") || ends(base, ".img")) {
      const ImageInfo info = read_image(w.watched(r.entry_stream(), e.path), image_clues);
      if (!info.image) continue;
      Identity id;
      id.why = "it holds none of the game's files";
      if (info.clue) id = info.clue->judge(info.bytes);
      w.detail += "  " + e.path + ": a disc image labelled \"" + info.label + "\"" +
                  (id.game.empty() ? ", but " + id.why : "") + "\n";
      if (id.game.empty()) continue;
      Step s;
      s.kind = info.raw ? Step::kRawImage : Step::kImage, s.entry = e.path, s.raw_offset = info.offset;
      Found f;
      take(f, id);
      f.kind = Found::kDisc, f.path = path, f.steps = steps, f.label = info.label;
      f.steps.push_back(s);
      f.data_track = e.path, f.raw = info.raw, f.raw_offset = info.offset;
      images.emplace_back(e.path, f);
    } else if (archive_name(base) && depth < 4) {
      ArchiveReader inner;
      if (!inner.open_stream(w.watched(r.entry_stream(), e.path))) continue;
      std::vector<Step> next = steps;
      Step s;
      s.entry = e.path;
      next.push_back(s);
      w.detail += "  into " + e.path + "\n";
      walk_archive(inner, path, next, depth + 1, w);
    }
  }
  // The images with their cue's audio tracks (a cue may come after its .bins).
  for (auto& [entry, f] : images) {
    for (const auto& [dir, tracks] : cues)
      for (const CueTrackAt& t : tracks)
        if (lower(dir + t.file) == lower(entry))
          for (const CueTrackAt& a : tracks)
            if (a.audio) f.audio.push_back(dir + a.file), f.audio_track.push_back(a.number), f.audio_start.push_back(a.start);
    w.consider(f);
  }
}

bool read_file(const std::string& p, size_t limit, std::vector<uint8_t>* out) {
  std::FILE* fh = std::fopen(p.c_str(), "rb");
  if (!fh) return false;
  uint8_t buf[1 << 16];
  for (size_t n; out->size() <= limit && (n = std::fread(buf, 1, sizeof buf, fh)) > 0;) out->insert(out->end(), buf, buf + n);
  std::fclose(fh);
  return true;
}

}  // namespace

std::string any_case_path(const std::string& dir, const std::string& rel) {
  std::error_code ec;
  fs::path at(dir);
  for (size_t i = 0; i < rel.size();) {
    const size_t end = std::min(rel.find('/', i), rel.size());
    const std::string part = rel.substr(i, end - i);
    i = end + 1;
    if (part.empty()) continue;
    if (fs::exists(at / part, ec)) {
      at /= part;
      continue;
    }
    const std::string want = lower(part);
    fs::path hit;
    for (auto it = fs::directory_iterator(at, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      std::string name;
      if (narrow(it->path().filename(), &name) && lower(name) == want) {
        hit = it->path();
        break;
      }
    }
    if (hit.empty()) return {};
    at = hit;
  }
  return fs::exists(at, ec) ? at.string() : std::string{};
}

Found find_source(const std::string& path, const SourceRules& rules, const SourceProgress& progress) {
  Walk w;
  w.rules = &rules;
  w.progress = progress;
  std::error_code ec;
  fs::path p(path);
  if (fs::is_regular_file(p, ec)) {
    const std::string name = lower(p.filename().string());
    if (std::find(rules.programs.begin(), rules.programs.end(), name) != rules.programs.end()) p = p.parent_path();
  }
  const std::string ps = p.string();
  // An installed copy, on disk: its folder holds an install clue.
  bool install_clue = false;
  if (fs::is_directory(p, ec))
    for (const Clue& c : rules.clues) {
      if (c.kind != Clue::kInstall) continue;
      const std::string at = any_case_path(ps, c.path);
      if (at.empty() || !fs::is_regular_file(at, ec)) continue;
      install_clue = true;
      std::vector<uint8_t> b;
      read_file(at, c.limit, &b);
      const Identity id = c.judge(b);
      if (id.game.empty()) {
        if (!id.why.empty()) w.detail += "  " + c.path + ": " + id.why + "\n";
        continue;
      }
      Found f;
      take(f, id);
      f.kind = Found::kInstall, f.path = ps, f.on_disk = true;
      w.consider(f);
    }
  if (!install_clue) {
    // A disc, a drive, an .iso or a .cue on disk.
    Disc disc;
    std::string err;
    const std::string lp = lower(ps);
    const bool image_or_folder = fs::is_directory(p, ec) || ends(lp, ".iso") || ends(lp, ".cue") || ends(lp, ".bin") ||
                                 ends(lp, ".img");
    if (image_or_folder && disc.open(ps, &err)) {
      for (const Clue& c : rules.clues) {
        if (c.kind != Clue::kDisc) continue;
        const Disc::File* file = disc.find(c.path);
        if (!file || file->size > c.limit) continue;
        std::vector<uint8_t> b(static_cast<size_t>(file->size));
        Identity id;
        if (disc.read(*file, 0, b.size(), b.data())) id = c.judge(b);
        else id.why = c.path + " could not be read";
        if (id.game.empty()) {
          if (!id.why.empty()) w.detail += "  " + id.why + "\n";
          continue;
        }
        Found f;
        take(f, id);
        f.kind = Found::kDisc, f.path = ps, f.on_disk = true;
        std::vector<CueTrack> tracks;
        std::string cue_err;
        if (!disc.cue.empty() && read_cue(disc.cue, &tracks, &cue_err))
          for (const CueTrack& t : tracks)
            if (t.audio)
              f.audio.push_back(t.file), f.audio_track.push_back(t.number),
                  f.audio_start.push_back(std::max<int64_t>(0, t.index1) * 2352);
        w.consider(f);
      }
    } else if (fs::is_regular_file(p, ec)) {
      // Anything else: an archive, perhaps.
      ArchiveReader r;
      if (r.open_file(ps)) {
        w.top = &r, w.top_size = std::max<uint64_t>(1, fs::file_size(p, ec));
        walk_archive(r, ps, {}, 0, w);
        if (!r.error().empty()) w.detail += "  " + r.error() + "\n";
      } else {
        w.detail += "  not an archive this reads: " + r.error() + "\n";
      }
    }
  }
  if (w.stopped) {
    Found f;
    f.path = ps, f.cancelled = true, f.detail = w.detail;
    return f;
  }
  Found f = w.best;
  f.path = ps;
  f.detail = w.detail;
  return f;
}

}  // namespace rekit

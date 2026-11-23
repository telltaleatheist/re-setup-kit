#include "rekit/game_folder.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace rekit {
namespace {

std::string read_text(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
bool write_text(const fs::path& p, const std::string& text) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << text;
  return static_cast<bool>(f);
}
bool marked_for(const fs::path& dir, const std::string& key) {
  std::error_code ec;
  const fs::path mark = dir / kGameFolderMark;
  return fs::is_regular_file(mark, ec) && read_text(mark).find("game=" + key + "\n") != std::string::npos;
}

// The folder made (or found) a games folder: an empty or new folder gets the mark; one holding anything else without
// it is someone's own and is never written to.
bool own_root(const std::string& root, std::string* error) {
  std::error_code ec;
  if (root.empty()) return *error = "there is no folder", false;
  fs::create_directories(root, ec);
  if (fs::exists(fs::path(root) / kGamesFolderMark, ec)) return true;
  if (!fs::is_directory(root, ec)) return *error = "the folder " + root + " could not be made", false;
  if (!fs::is_empty(root, ec)) return *error = root + " holds files that are not the game's: move them and try again", false;
  if (!write_text(fs::path(root) / kGamesFolderMark, "# A rebuild's games. Each folder is one game it installed.\n"))
    return *error = "the folder " + root + " could not be written to", false;
  return true;
}

}  // namespace

std::string game_folder_in(const std::string& picked, const std::string& key, const std::string& folder_name) {
  std::error_code ec;
  const fs::path p(picked);
  if (!fs::exists(p, ec) || fs::is_empty(p, ec)) return picked;
  if (marked_for(p, key)) return picked;
  return (p / folder_name).string();
}

bool claim_game_folder(const std::string& dir, const std::string& key, std::string* error) {
  std::error_code ec;
  const fs::path mark = fs::path(dir) / kGameFolderMark;
  if (fs::is_regular_file(mark, ec)) return marked_for(dir, key) || (*error = dir + " is another game's folder: pick another", false);
  if (fs::exists(dir, ec) && !(fs::is_directory(dir, ec) && fs::is_empty(dir, ec)))
    return *error = dir + " holds files that are not this game's: pick an empty folder", false;
  if (!own_root(dir, error)) return false;  // new or empty: the games folder's mark first
  if (!write_text(mark, "# A game's folder, made by a rebuild's launcher or the game's setup: its program and its game files.\ngame=" +
                            key + "\n"))
    return *error = dir + " could not be written to", false;
  return true;
}

std::string media_dir(const std::string& install, const std::string& user_folder) {
  if (install.empty()) return user_folder;
  std::error_code ec;
  const fs::path up = fs::path(install).parent_path();
  return fs::is_regular_file(up / kGameFolderMark, ec) ? up.string() : user_folder;
}

}  // namespace rekit

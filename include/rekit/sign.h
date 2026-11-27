#pragma once
// Ed25519 signatures (RFC 8032), checked with Monocypher's (third_party/monocypher, CC0 or BSD 2-clause): a file
// published with a detached signature, `<file>.sig`, a few lines of 128 hex digits, each a signature of the whole
// file by one key. More than one line lets a file carry its old key's and its next key's signatures while launchers
// and games move from one to the other (DESIGN.md, Signed data). Keys are 64 hex digits (32 bytes). The kit never
// signs: tools/package/sign.py in mw3-re does, with a private key kept outside every repo.
#include <string>
#include <vector>

namespace rekit {

// 64 hex digits (either case).
bool is_public_key(const std::string& hex);

// The key among `keys` whose signature, one of `sig_text`'s lines, is good for `message`; "" when none is (no line
// of 128 hex digits, or none matching any key). Blank lines and spaces around a line are ignored; anything else in it
// is not a signature.
std::string signed_by(const std::string& message, const std::string& sig_text, const std::vector<std::string>& keys);

}  // namespace rekit

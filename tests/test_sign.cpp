// Signed data (rekit/sign.h): RFC 8032's Ed25519 test vectors, a .sig of several lines, and what is refused.
#include <string>
#include <vector>

#include "check.h"
#include "rekit/sign.h"
#include "test_keys.h"

namespace {
std::string bytes(const std::string& hex) {
  std::string s;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) s += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
  return s;
}
}  // namespace

TEST(sign_checks_rfc8032_vectors) {
  // RFC 8032 section 7.1, tests 1 and 2.
  const std::string pk1 = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
  const std::string sig1 = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";
  const std::string pk2 = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c";
  const std::string sig2 = "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00";
  EXPECT(rekit::signed_by("", sig1, {pk1}) == pk1);
  EXPECT(rekit::signed_by(bytes("72"), sig2, {pk1, pk2}) == pk2);
  EXPECT(rekit::signed_by(bytes("73"), sig2, {pk1, pk2}).empty());  // another message
  EXPECT(rekit::signed_by("", sig1, {pk2}).empty());                // another key
  EXPECT(rekit::is_public_key(pk1) && !rekit::is_public_key(pk1.substr(2)) && !rekit::is_public_key(std::string(64, 'g')));
}

TEST(sign_reads_a_sig_file_of_several_lines) {
  const std::string msg = "{\"revision\": 4}\n";
  const std::string old_key = test_keys::public_key(1), next_key = test_keys::public_key(2), other = test_keys::public_key(3);
  const std::string both = test_keys::sign(msg, 1) + "\r\n  " + test_keys::sign(msg, 2);
  EXPECT(rekit::signed_by(msg, both, {old_key}) == old_key);
  EXPECT(rekit::signed_by(msg, both, {next_key}) == next_key);
  EXPECT(rekit::signed_by(msg, both, {other}).empty());
  EXPECT(rekit::signed_by(msg + " ", both, {old_key, next_key}).empty());  // one byte more
  std::string flipped = test_keys::sign(msg, 1);
  flipped[10] = flipped[10] == '0' ? '1' : '0';
  EXPECT(rekit::signed_by(msg, flipped, {old_key}).empty());
  EXPECT(rekit::signed_by(msg, "", {old_key}).empty() && rekit::signed_by(msg, "sig: " + test_keys::sign(msg, 1), {old_key}).empty());
  EXPECT(rekit::signed_by(msg, test_keys::sign(msg, 1), {}).empty() && rekit::signed_by(msg, test_keys::sign(msg, 1), {"zz"}).empty());
  EXPECT(rekit::signed_by(msg, std::string(5000, '\n') + test_keys::sign(msg, 1), {old_key}).empty());  // not a .sig
}

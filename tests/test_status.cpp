// The --status JSON (rekit/status.h): written, read back, and refused when it is not ours.
#include <string>

#include "check.h"
#include "rekit/status.h"

TEST(status_round_trips_and_escapes) {
  rekit::Status s;
  s.game = "mech3", s.version = "1.2.2", s.state = rekit::Status::kIncomplete;
  s.where = "C:\\Games\\MechWarrior 3 \"Gold\"\n", s.data = "1.2";
  s.missing = {"zbd\\", "vidéo"}, s.pending = {"movies"}, s.needs = {"mech3-pm"};
  s.note = "tab\there";
  const std::string j = rekit::status_json(s);
  std::printf("  %s\n", j.c_str());
  EXPECT(j.find('\n') == std::string::npos && j.rfind("{\"kit\": 1,", 0) == 0);
  rekit::Status b;
  std::string err;
  EXPECT(rekit::parse_status(j, &b, &err));
  EXPECT(b.game == s.game && b.version == s.version && b.state == s.state && b.where == s.where && b.data == s.data);
  EXPECT(b.missing == s.missing && b.pending == s.pending && b.needs == s.needs && b.note == s.note);
  rekit::Status m;
  m.game = "mech3";
  EXPECT(rekit::parse_status(rekit::status_json(m), &b) && b.state == rekit::Status::kMissing && b.where.empty());
}

TEST(status_reads_other_spacing_and_unknown_fields) {
  rekit::Status b;
  EXPECT(rekit::parse_status(" {\"kit\":1,\"extra\":{\"a\":[1,{\"b\":null}]},\"game\":\"x\\u00e9\\ud83d\\ude00\","
                             "\"files\":{\"state\":\"ready\",\"where\":\"/g\"},\"needs\":[]} \n", &b));
  EXPECT(b.game == "x\xc3\xa9\xf0\x9f\x98\x80" && b.state == rekit::Status::kReady && b.where == "/g");
}

TEST(status_refuses_what_is_not_a_status) {
  rekit::Status b;
  std::string err;
  EXPECT(!rekit::parse_status("", &b, &err) && !err.empty());
  EXPECT(!rekit::parse_status("mw3: started\n{\"kit\": 1}", &b, &err));
  EXPECT(!rekit::parse_status("{\"game\": \"x\"}", &b, &err) && err.find("kit") != std::string::npos);
  EXPECT(!rekit::parse_status("{\"kit\": 2, \"game\": \"x\"}", &b, &err) && err.find("newer") != std::string::npos);
  EXPECT(!rekit::parse_status("{\"kit\": 1, \"game\": \"x\"", &b, &err));
  EXPECT(!rekit::parse_status("{\"kit\": 1} trailing", &b, &err));
}

// The fields a launcher that starts the game itself reads (2026-11-25): each left out when empty, read back when there.
TEST(status_notes_user_and_play) {
  rekit::Status s;
  s.game = "demo", s.notes = {"No music: \"add\" it.", "Second."}, s.user = "/u/Demo Game", s.play = rekit::kPlayOptions;
  const std::string j = rekit::status_json(s);
  std::printf("  %s\n", j.c_str());
  rekit::Status b;
  EXPECT(rekit::parse_status(j, &b) && b.notes == s.notes && b.user == s.user && b.play == rekit::kPlayOptions);
  rekit::Status plain;
  plain.game = "demo";
  const std::string p = rekit::status_json(plain);
  EXPECT(p.find("notes") == std::string::npos && p.find("user") == std::string::npos && p.find("play") == std::string::npos);
  EXPECT(rekit::parse_status(p, &b) && b.notes.empty() && b.user.empty() && b.play == 0);
}

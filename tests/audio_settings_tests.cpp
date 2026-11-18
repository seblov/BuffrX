// Settings > Audio's selection file (audio_routing.h): an output device is what a new selection holds, and it
// survives a restart. The deck mixer and the settings list edit the same array.
// Built into buffrx-tests: one line in tests/engine_tests.cpp's main() and this file in BUFFRX_TEST_SOURCES.
#include <cstdio>
#include <string>

#include "../src/engine/audio_routing.h"
#include "../src/engine/config.h"
#include "../src/engine/ui/audio_speaker.h"
#include "../src/engine/ui/ui_audio_math.h"
#include "../src/shared/json.h"

namespace {

void expect(int& failures, bool ok, const char* what) {
  if (ok) return;
  std::printf("  FAIL %s\n", what);
  ++failures;
}

}  // namespace

int test_audio_settings() {
  std::printf("audio settings\n");
  int failures = 0;

  // A new selection starts empty; the output the person adds is the route, and its device and name are kept.
  bx::Config config;
  config.audio_capture_mode = "outputs";
  expect(failures, bx::effective_audio_outputs(config).empty(), "a new Selected outputs list starts empty");

  bx::AudioOutput speakers;
  speakers.device = "speakers-endpoint";
  speakers.name = "Headphones";
  config.audio_outputs = {speakers};
  bx::Config reloaded;
  bx::config_from_json(bx::config_to_json(config), reloaded);
  const std::vector<bx::AudioOutput> saved = bx::effective_audio_outputs(reloaded);
  expect(failures, saved.size() == 1 && saved[0].device == "speakers-endpoint" && saved[0].name == "Headphones",
         "a selected output survives a restart as the same device");

  // An empty list is a choice and stays empty across a restart.
  bx::Config emptied;
  emptied.audio_capture_mode = "outputs";
  bx::Config emptied_back;
  bx::config_from_json(bx::config_to_json(emptied), emptied_back);
  expect(failures, bx::effective_audio_outputs(emptied_back).empty(),
         "an output list the person emptied is still empty after a restart");

  // The device list is compared by id, duplicates are dropped, and only four slots are kept.
  std::vector<bx::AudioOutput> raw{speakers, speakers};
  raw.push_back(bx::AudioOutput{"dock", "Dock", 1.0, false});
  raw.push_back(bx::AudioOutput{"", "No device", 1.0, false});
  const std::vector<bx::AudioOutput> normalized = bx::normalize_audio_outputs(raw);
  expect(failures, normalized.size() == 2 && normalized[0].device == "speakers-endpoint" &&
                       normalized[1].device == "dock",
         "a duplicate output and an entry without a device are dropped");
  std::vector<bx::AudioOutput> many_outputs;
  for (int i = 0; i < 8; ++i)
    many_outputs.push_back(bx::AudioOutput{"device" + std::to_string(i), "Output", 1.0, false});
  expect(failures, bx::normalize_audio_outputs(many_outputs).size() == static_cast<size_t>(bx::kAudioOutputMax),
         "a hand-edited file never keeps more than the four slots");

  // The wire shape: device, name, gain and mute; the same list the config file carries.
  const bx::Json wire = bx::audio_outputs_json(config.audio_outputs);
  expect(failures, wire.items().size() == 1 && wire.items()[0]["device"].as_string() == "speakers-endpoint" &&
                       wire.items()[0]["name"].as_string() == "Headphones",
         "an output is stored by device and label");

  // The settings list and the deck mixer edit the same array. An entry starts at full volume and unmuted, a
  // fader or mute changes one entry and carries every other key through, and a device is never added twice.
  bx::Json mix_outputs = bx::Json::array();
  mix_outputs.push(bx::Json::object());
  mix_outputs.items()[0].set("device", bx::Json("speakers-endpoint"));
  mix_outputs.items()[0].set("name", bx::Json("Headphones"));
  mix_outputs.items()[0].set("note", bx::Json("hand-edited"));
  bx::Json second = bx::Json::object();
  second.set("device", bx::Json("dock"));
  second.set("name", bx::Json("Dock"));
  mix_outputs.push(second);
  expect(failures, bx::ui::audio_output_volume(mix_outputs.items()[0]) == 1.0 &&
                       !bx::ui::audio_output_muted(mix_outputs.items()[0]),
         "an output entry without mixer fields reads as full volume and unmuted");

  const bx::Json louder = bx::ui::audio_outputs_with_field(mix_outputs, 0, "volume", bx::Json(0.25));
  const bx::Json muted = bx::ui::audio_outputs_with_field(louder, 0, "muted", bx::Json(true));
  expect(failures, louder.items().size() == 2 && muted.items().size() == 2 &&
                       bx::ui::audio_output_volume(muted.items()[0]) == 0.25 && bx::ui::audio_output_muted(muted.items()[0]) &&
                       muted.items()[0]["note"].as_string() == "hand-edited" && !bx::ui::audio_output_muted(muted.items()[1]),
         "a fader or mute changes one output and keeps every other key and entry");
  expect(failures, bx::ui::audio_output_volume(bx::Json()) == 1.0 && bx::ui::audio_output_volume(bx::Json(4.0)) == 1.0,
         "a missing or out-of-range volume falls back to full");

  const bx::Json third = bx::ui::audio_outputs_without(muted, 0);
  expect(failures, third.items().size() == 1 && third.items()[0]["device"].as_string() == "dock",
         "removing an output keeps the entries after it");

  bx::Json picked_device = bx::Json::object();
  picked_device.set("id", bx::Json("headset"));
  picked_device.set("name", bx::Json("Headset"));
  const bx::Json entry = bx::ui::audio_output_entry(picked_device);
  expect(failures, entry["volume"].as_double() == 1.0 && !entry["muted"].as_bool(true) &&
                       entry["device"].as_string() == "headset" && entry["name"].as_string() == "Headset",
         "a picked output starts at full volume and unmuted, with the device's own name");
  const bx::Json added = bx::ui::audio_outputs_with_device(mix_outputs, entry, 4);
  expect(failures, added.items().size() == 3 && added.items()[2]["device"].as_string() == "headset",
         "a new output is appended after the selection");
  const bx::Json again = bx::ui::audio_outputs_with_device(added, entry, 4);
  expect(failures, again.items().size() == 3, "the same output is never added twice");
  bx::Json four = bx::Json::array();
  for (int i = 0; i < 4; ++i) four.push(bx::Json::object());
  expect(failures, bx::ui::audio_outputs_with_device(four, entry, 4).items().size() == 4,
         "a fifth output does not pass the four-slot limit");

  // A strip's one-line state is short enough for a 76-unit channel and never says more than the engine
  // reported: a connected capture is "Recording" only while something records, and a muted or off channel
  // says that first. The full sentence stays in the hover card.
  const auto strip = [](bool enabled, bool muted, bool recorded, const char* state, bool now) {
    return bx::ui::audio_strip_state(enabled, muted, recorded, state, now);
  };
  expect(failures, bx::ui::audio_strip_state(true, false, true, "connected", true) == bx::ui::StripState::recording,
         "a connected output records while the buffer or a recording runs");
  expect(failures, bx::ui::audio_strip_state(true, false, true, "connected", false) == bx::ui::StripState::ready,
         "a connected output that is not recording is ready, not recording");
  expect(failures, strip(true, true, true, "connected", true) == bx::ui::StripState::muted,
         "a muted channel says muted before anything about capture");
  expect(failures, strip(false, false, true, "connected", true) == bx::ui::StripState::off,
         "a channel switched off says off");
  expect(failures, strip(true, false, false, "connected", true) == bx::ui::StripState::saved,
         "a saved output is not recorded while another mode records");
  expect(failures, strip(true, false, true, "failed", true) == bx::ui::StripState::failed,
         "a real capture failure says failed");
  expect(failures, strip(true, false, true, "waiting", true) == bx::ui::StripState::waiting,
         "an output whose source is not open yet says waiting");
  expect(failures, std::string(bx::ui::audio_strip_label(bx::ui::StripState::recording)) == "Recording" &&
                       std::string(bx::ui::audio_strip_label(bx::ui::StripState::muted)) == "Muted",
         "the strip's labels are the short words the mixer draws");

  // Reset volume writes that one field: the mute stays as it was and every other entry and key is carried
  // through, so a muted channel comes back at the gain it was reset to, still muted.
  const bx::Json reset = bx::ui::audio_outputs_with_volume_reset(muted, 0);
  expect(failures, reset.items().size() == 2 && bx::ui::audio_output_volume(reset.items()[0]) == 1.0 &&
                       bx::ui::audio_output_muted(reset.items()[0]) &&
                       reset.items()[0]["note"].as_string() == "hand-edited" && !bx::ui::audio_output_muted(reset.items()[1]),
         "a volume reset changes one gain and leaves the mute and every other entry alone");
  expect(failures, bx::ui::audio_volume_at_full(1.0) && !bx::ui::audio_volume_at_full(0.99) &&
                       !bx::ui::audio_volume_at_full(0.0),
         "reset is offered only to a channel that is not already at full gain");

  // ---- automatic app audio: one preference per executable, patched by key, merged without clobbering.
  bx::Json auto_config = bx::Json::object();
  auto_config.set("audio_capture_mode", bx::Json("auto"));
  expect(failures, bx::ui::audio_is_auto(auto_config), "the normal capture mode is the automatic one");
  bx::Json legacy_config = bx::Json::object();
  legacy_config.set("audio_capture_mode", bx::Json("outputs"));
  legacy_config.set("audio_outputs", mix_outputs);
  expect(failures, !bx::ui::audio_is_auto(legacy_config) && legacy_config["audio_outputs"].items().size() == 2,
         "a manual outputs file still reads as manual and keeps its saved outputs");
  expect(failures, bx::ui::audio_exe_key("Firefox.EXE") == "firefox.exe",
         "a program is keyed by its lowercased executable");

  // Every field default, and a value outside 0..1 is read the way the strip draws it.
  const bx::Json no_prefs;
  expect(failures, bx::ui::audio_pref_volume(no_prefs, "x.exe") == 1.0 &&
                       !bx::ui::audio_pref_muted(no_prefs, "x.exe") &&
                       !bx::ui::audio_pref_excluded(no_prefs, "x.exe") && !bx::ui::audio_pref_has(no_prefs, "x.exe"),
         "a program with no preference plays at full volume, unmuted and captured");

  // The write is one program and the fields that changed: the engine merges it, so nothing else moves.
  const bx::Json mute_patch = bx::ui::audio_pref_patch("Discord.EXE", bx::ui::audio_muted_field(true));
  expect(failures, mute_patch["audio_app_prefs"].fields().size() == 1 &&
                       mute_patch["audio_app_prefs"]["discord.exe"]["muted"].as_bool(true) &&
                       mute_patch["audio_app_prefs"]["discord.exe"].fields().size() == 1 &&
                       !mute_patch["audio_app_prefs"]["discord.exe"].has("volume"),
         "a mute is one field of one program, addressed by executable");

  // The merge: a second program's edit leaves the first program's volume and mute where they were.
  bx::Json prefs = bx::Json::object();
  prefs = bx::ui::audio_prefs_merged(
      prefs, "Firefox.EXE", bx::ui::audio_fields_with(bx::ui::audio_volume_field(0.4), bx::ui::audio_muted_field(true)));
  prefs = bx::ui::audio_prefs_merged(prefs, "discord.exe", bx::ui::audio_volume_field(0.8));
  const bx::Json quiet = bx::ui::audio_prefs_merged(prefs, "FIREFOX.exe", bx::ui::audio_muted_field(false));
  expect(failures, quiet.fields().size() == 2 && bx::ui::audio_pref_volume(quiet, "firefox.exe") == 0.4 &&
                       !bx::ui::audio_pref_muted(quiet, "firefox.exe") &&
                       bx::ui::audio_pref_volume(quiet, "discord.exe") == 0.8,
         "one program's mute keeps the other program and this one's own volume");

  // The Other group's level is its own setting, beside the members' preferences rather than over them.
  const bx::Json group_patch = bx::ui::audio_other_volume_patch(0.5);
  const bx::Json member_patch = bx::ui::audio_pref_patch("spotify.exe", bx::ui::audio_volume_field(0.3));
  bx::Json combined = group_patch;
  for (const auto& field : member_patch.fields()) combined.set(field.first, field.second);
  expect(failures, !group_patch.has("audio_app_prefs") && !member_patch.has("audio_other_volume") &&
                       combined["audio_other_volume"].as_double() == 0.5 &&
                       bx::ui::audio_pref_volume(combined["audio_app_prefs"], "spotify.exe") == 0.3,
         "the Other group's level and a member's level are separate settings");

  // A member's row reads its own gain and mute, never the effective level the engine reports (own × group):
  // a group fader at 31% must not move every member's fader, and a member with no preference is at full gain.
  const bx::Json group_only = bx::Json::object();
  expect(failures, bx::ui::audio_member_volume(group_only, "Spotify.exe") == 1.0 &&
                       !bx::ui::audio_member_muted(group_only, "Spotify.exe"),
         "a member with no preference of its own shows full gain and no mute, whatever the group's");
  bx::Json member_prefs = bx::Json::object();
  member_prefs = bx::ui::audio_prefs_merged(
      member_prefs, "Spotify.exe", bx::ui::audio_fields_with(bx::ui::audio_volume_field(0.4), bx::ui::audio_muted_field(true)));
  expect(failures, bx::ui::audio_member_volume(member_prefs, "SPOTIFY.exe") == 0.4 &&
                       bx::ui::audio_member_muted(member_prefs, "spotify.exe"),
         "a member with its own preference shows that gain and mute");

  // The lane states follow the capture, not the legacy "saved selection" flag: an automatic lane is written by
  // the buffer or a recording, so it says Recording/Ready/Failed/Waiting while one runs.
  expect(failures, !bx::ui::audio_lanes_recorded(true, false, false) && bx::ui::audio_lanes_recorded(true, false, true) &&
                       bx::ui::audio_lanes_recorded(false, true, false) && !bx::ui::audio_lanes_recorded(false, false, true),
         "automatic lanes are recorded while a capture runs; a legacy route keeps the engine's own flag");

  // Programs the engine refused are named with the reason it gave, and the block they are drawn in is bounded:
  // a mixer that showed only its lanes would read as if every program were being recorded.
  const bx::Json quiet_status;
  expect(failures, bx::ui::audio_uncaptured_warnings(quiet_status, 3).empty() &&
                       bx::ui::audio_uncaptured_total(quiet_status) == 0 && bx::ui::audio_uncaptured_height(0, 0) == 0.0f,
         "a capture that refused nothing says nothing and takes no room");
  bx::Json blocked = bx::Json::object();
  blocked.set("name", bx::Json("Firefox"));
  blocked.set("exe", bx::Json("firefox.exe"));
  blocked.set("state", bx::Json("blocked"));
  blocked.set("error", bx::Json("Not captured: Steam is excluded and runs inside its process tree"));
  bx::Json failed_entry = bx::Json::object();
  failed_entry.set("name", bx::Json("Discord"));
  failed_entry.set("exe", bx::Json("discord.exe"));
  failed_entry.set("state", bx::Json("failed"));
  bx::Json unsupported = bx::Json::object();
  unsupported.set("exe", bx::Json("game.exe"));
  unsupported.set("state", bx::Json("unsupported"));
  bx::Json uncaptured_list = bx::Json::array();
  uncaptured_list.push(blocked);
  uncaptured_list.push(failed_entry);
  uncaptured_list.push(unsupported);
  bx::Json uncaptured_auto = bx::Json::object();
  uncaptured_auto.set("uncaptured", uncaptured_list);
  bx::Json uncaptured_status = bx::Json::object();
  uncaptured_status.set("auto", uncaptured_auto);
  const std::vector<bx::ui::AutoWarning> uncaptured_shown = bx::ui::audio_uncaptured_warnings(uncaptured_status, 3);
  expect(failures, uncaptured_shown.size() == 3 && uncaptured_shown[0].name == "Firefox" && uncaptured_shown[0].blocked &&
                       uncaptured_shown[0].reason == "Steam is excluded and runs inside its process tree" &&
                       !uncaptured_shown[1].blocked && uncaptured_shown[1].reason == "Audio capture failed" &&
                       uncaptured_shown[2].name == "game.exe" &&
                       uncaptured_shown[2].reason == "Process audio is not supported on this PC",
         "each refused program is named with the engine's own reason, and one sentence when it gave none");
  expect(failures, bx::ui::audio_uncaptured_total(uncaptured_status) == 3 &&
                       bx::ui::audio_uncaptured_height(3, 3) == bx::ui::kAutoWarningLine * 4 + 4.0f &&
                       bx::ui::audio_uncaptured_height(3, 9) == bx::ui::kAutoWarningLine * 5 + 4.0f,
         "the warning block is a head line, a line each, and one line for the rest");
  bx::Json many = bx::Json::array();
  for (int i = 0; i < 6; ++i) {
    bx::Json one = bx::Json::object();
    one.set("name", bx::Json(i == 5 ? "App1" : "App" + std::to_string(i + 1)));
    one.set("state", bx::Json("blocked"));
    many.push(one);
  }
  bx::Json many_auto = bx::Json::object();
  many_auto.set("uncaptured", many);
  bx::Json many_status = bx::Json::object();
  many_status.set("auto", many_auto);
  const std::vector<bx::ui::AutoWarning> many_shown = bx::ui::audio_uncaptured_warnings(many_status, 4);
  expect(failures, many_shown.size() == 4 && bx::ui::audio_uncaptured_total(many_status) == 6 &&
                       bx::ui::audio_uncaptured_height(many_shown.size(), bx::ui::audio_uncaptured_total(many_status)) ==
                           bx::ui::kAutoWarningLine * 6 + 4.0f,
         "a long list is cut to the limit, a name is never listed twice, and the block stays bounded");

  // Excluded programs are remembered, listed in a stable order, and restored without losing their volumes.
  bx::Json excluded_prefs = bx::Json::object();
  excluded_prefs = bx::ui::audio_prefs_merged(
      excluded_prefs, "Firefox.exe", bx::ui::audio_fields_with(bx::ui::audio_excluded_field(true), bx::ui::audio_volume_field(0.25)));
  excluded_prefs = bx::ui::audio_prefs_merged(excluded_prefs, "Discord.exe", bx::ui::audio_excluded_field(true));
  excluded_prefs = bx::ui::audio_prefs_merged(excluded_prefs, "spotify.exe", bx::ui::audio_volume_field(0.5));
  const std::vector<std::string> excluded = bx::ui::audio_excluded(excluded_prefs);
  expect(failures, excluded.size() == 2 && excluded[0] == "discord.exe" && excluded[1] == "firefox.exe",
         "every excluded program is listed, in one order");
  const bx::Json restore = bx::ui::audio_restore_excluded_patch(excluded_prefs);
  bx::Json restored = excluded_prefs;
  for (const auto& entry : restore["audio_app_prefs"].fields()) restored = bx::ui::audio_prefs_merged(restored, entry.first, entry.second);
  expect(failures, restore["audio_app_prefs"].fields().size() == 2 && bx::ui::audio_excluded(restored).empty() &&
                       bx::ui::audio_pref_volume(restored, "firefox.exe") == 0.25 &&
                       bx::ui::audio_pref_volume(restored, "spotify.exe") == 0.5,
         "restoring excluded apps clears one flag each and keeps every volume");

  // The draw order: frozen while a hand is in the mixer, and a lane that left is never drawn again.
  const std::vector<std::string> frozen_order = bx::ui::audio_stable_order({"a", "b"}, {"b", "c", "a"}, true);
  const std::vector<std::string> live_order = bx::ui::audio_stable_order({"a", "b"}, {"b", "c"}, false);
  const std::vector<std::string> gone_order = bx::ui::audio_stable_order({"a", "b", "c"}, {"b", "c"}, true);
  expect(failures, frozen_order == std::vector<std::string>({"a", "b", "c"}) &&
                       live_order == std::vector<std::string>({"b", "c"}) &&
                       gone_order == std::vector<std::string>({"b", "c"}),
         "a frozen lane order keeps its rows and appends a new one; a departed lane is dropped");

  // A lane's identity and the two short words the row uses.
  bx::Json firefox_lane = bx::Json::object();
  firefox_lane.set("exe", bx::Json("Firefox.EXE"));
  firefox_lane.set("name", bx::Json("Firefox"));
  firefox_lane.set("state", bx::Json("connected"));
  bx::Json other_lane = bx::Json::object();
  other_lane.set("other", bx::Json(true));
  other_lane.set("name", bx::Json("Other audio"));
  other_lane.set("exe", bx::Json(""));
  expect(failures, bx::ui::audio_lane_key(firefox_lane) == "firefox.exe" &&
                       bx::ui::audio_lane_title(firefox_lane) == "Firefox" && !bx::ui::audio_lane_other(firefox_lane) &&
                       bx::ui::audio_lane_key(other_lane) == "other" &&
                       bx::ui::audio_lane_title(other_lane) == "Other audio" && bx::ui::audio_lane_other(other_lane),
         "the Other lane is one lane and a program is known by its executable");

  // The empty states: nothing is looked for while nothing captures, and an actual refusal is not called quiet.
  const bx::Json no_audio;
  expect(failures, bx::ui::audio_auto_state(no_audio, false) == bx::ui::AutoAudioState::idle &&
                       bx::ui::audio_auto_state(no_audio, true) == bx::ui::AutoAudioState::waiting,
         "with no capture running nothing is looked for, and a running capture with no stream waits calmly");
  const std::string idle_line = bx::ui::audio_auto_line(bx::ui::AutoAudioState::idle);
  const std::string waiting_line = bx::ui::audio_auto_line(bx::ui::AutoAudioState::waiting);
  expect(failures, idle_line.find("buffer") != std::string::npos && waiting_line == "Nothing playing" &&
                       idle_line != waiting_line,
         "the two empty states say different, calm things");
  bx::Json unsupported_status = bx::Json::object();
  bx::Json unsupported_info = bx::Json::object();
  unsupported_info.set("unsupported", bx::Json(true));
  unsupported_status.set("auto", unsupported_info);
  expect(failures, bx::ui::audio_auto_state(unsupported_status, true) == bx::ui::AutoAudioState::unsupported,
         "an unsupported capture is said plainly, never as waiting");
  bx::Json refused_status = bx::Json::object();
  bx::Json refused_info = bx::Json::object();
  refused_info.set("error", bx::Json("0x80070005: access denied"));
  refused_status.set("auto", refused_info);
  expect(failures, bx::ui::audio_auto_state(refused_status, true) == bx::ui::AutoAudioState::error,
         "an activation failure keeps its own state");
  bx::Json active_status = bx::Json::object();
  bx::Json active_info = bx::Json::object();
  active_info.set("on", bx::Json(true));
  active_status.set("auto", active_info);
  bx::Json active_lanes = bx::Json::array();
  active_lanes.push(firefox_lane);
  active_status.set("lanes", active_lanes);
  expect(failures, bx::ui::audio_auto_state(active_status, true) == bx::ui::AutoAudioState::active &&
                       std::string(bx::ui::audio_auto_line(bx::ui::AutoAudioState::active)).empty(),
         "a lane on screen is the active state, and it needs no line of its own");

  // The engine's own status puts the same array inside `auto`; both shapes read the same.
  bx::Json nested_status = bx::Json::object();
  bx::Json nested_info = bx::Json::object();
  nested_info.set("on", bx::Json(true));
  nested_info.set("lanes", active_lanes);
  nested_status.set("auto", nested_info);
  expect(failures, bx::ui::audio_auto_state(nested_status, true) == bx::ui::AutoAudioState::active &&
                       bx::ui::audio_lanes_of(nested_status).items().size() == 1,
         "the lanes read from status.audio.lanes and from status.audio.auto.lanes alike");

  return failures;
}

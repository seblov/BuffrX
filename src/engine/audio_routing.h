// Source-to-track routing for the mixer, and the recorded-time track labels a file carries.
//
// Track 1 is always the audible mix, and the layout is fixed: track 1 mix, track 2 microphone, tracks
// 3..6 the four selected outputs. A slot's track belongs to its position in the selection, so adding or
// removing an output moves only the slots after it, and a slot never lands on another's track.
//
// Each selected output is a loopback capture of that render endpoint: the sound the person hears on that
// device, whichever apps produce it. Every selected output is captured at once, so an app that plays to
// two outputs appears on both tracks. The one desktop loopback of the all-sound-together mode is not used
// while outputs record: the selected outputs are the recording.
//
// Whether the saved outputs record is `audio_capture_mode`: "outputs" records them, "desktop" (the
// default of a directly built config) records the output loopback and the microphone together, and
// Performance mode's effective config forces "desktop" without touching the stored mode or the outputs.
//
// The container does not carry stream titles through OBS's muxer, so a multitrack file gets one
// sidecar beside it (`<file>.buffrx-audio.json`) naming each track and its role. It describes the
// routing at recording time, not the live config.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../shared/json.h"
#include "config.h"

namespace bx {

inline constexpr int kAudioOutputMax = 4;          // the four selected slots, tracks 3..6
inline constexpr int kAudioOutputFirstTrack = 3;   // slot 0's track; slot i is 3 + i
inline constexpr const char* kAudioSidecarSuffix = ".buffrx-audio.json";
inline constexpr const char* kAudioOutputSourcePrefix = "output:";

// A stored gain, kept finite and inside 0..1: a hand-edited file or another client cannot hand libobs a NaN.
double audio_output_volume(double value);
// The name a slot is shown and labelled under: the saved device name, else its position ("Output 1").
std::string audio_output_name(const AudioOutput& output, size_t slot);
// The identity a selection is compared by: the device id. "default" is the system default.
std::string audio_output_key(const AudioOutput& output);

// Wins over duplicates and malformed entries in a hand-edited file. Never keeps more than the four slots
// this build records; a file written by this build never holds more.
std::vector<AudioOutput> normalize_audio_outputs(const std::vector<AudioOutput>& outputs);
// Routing identity only: the same device on the same track. A gain or mute change is not a route
// change, so this keeps a fader from rebuilding the encoders (config_change_bits).
bool audio_outputs_equal(const std::vector<AudioOutput>& left, const std::vector<AudioOutput>& right);
// Whether two selections need no live control update either: identity, gain and mute all agree. Used to
// decide whether a saved gain change has to be applied to the running sources in place.
bool audio_output_controls_equal(const std::vector<AudioOutput>& left, const std::vector<AudioOutput>& right);

// The saved outputs that actually record: the normalized list while the mode is "outputs", none while it
// is "desktop" or "auto". An output kept for later never creates a source, a track or a rebuild.
std::vector<AudioOutput> effective_audio_outputs(const Config& config);

struct AudioTrackInfo {
  int track = 1;                     // 1..6, as the product and OBS count them
  std::string role;                  // mix | desktop | microphone | output | combined | empty
  std::string title;                 // "BuffrX Mix", "BuffrX Microphone", "BuffrX Headphones", ...
  std::vector<std::string> sources;  // desktop | microphone | output:<device id>
};

struct AudioRoutingPlan {
  int track_count = 1;
  bool desktop_captured = true;  // false while per-output or automatic captures exist
  uint32_t desktop_mixers = 1u << 0;
  uint32_t mic_mixers = 1u << 0;
  std::vector<AudioOutput> outputs;    // effective, in slot order
  std::vector<uint32_t> output_mixers; // one per outputs entry
  std::vector<AudioTrackInfo> tracks;  // track_count entries, index 0 = track 1
};

AudioRoutingPlan plan_audio_routing(const Config& config);
// The automatic mode's six fixed tracks: 1 mix, 2 microphone, 3..5 the isolated programs the plan holds,
// 6 Other audio. The source ids are the stable bus ids (audio_auto.h: auto:bus:3..6), so the mix decomposes
// exactly into the lanes after it and the editor can level any lane without the file becoming "incomplete".
// Track 1's sources are the microphone and the buses that are on, never the desktop loopback: automatic
// mode has no fallback that could record an excluded program.
AudioRoutingPlan plan_auto_audio_routing(const Config& config, const std::vector<AutoAudioLane>& lanes);

// The outputs as they are stored and sent on the pipe.
Json audio_outputs_json(const std::vector<AudioOutput>& outputs);

// The labels and roles of the routing a file was recorded with, with no media identity: a sidecar needs
// `media` (audio_layout.h) before it is believed, and write_audio_sidecar supplies it.
Json audio_track_metadata(const AudioRoutingPlan& plan);

// `<file>.buffrx-audio.json`, beside the media file.
std::string audio_sidecar_path(const std::string& media_path);

// Writes the sidecar for a multitrack plan; a single-track file gets none. False only when the file
// could not be written, which never fails a clip: the footage is the product, the labels are not. The
// media's final identity is captured with it, so this is for a file whose writer has closed.
bool write_audio_sidecar(const std::string& media_path, const AudioRoutingPlan& plan);

// The same labels beside a file a writer still owns, with no identity yet: it reads as an import until
// bind_audio_layout (audio_layout.h) captures the final bytes at the close.
bool write_audio_sidecar_pending(const std::string& media_path, const AudioRoutingPlan& plan);

}  // namespace bx

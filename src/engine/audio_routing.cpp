#include "audio_routing.h"

#include <algorithm>
#include <cmath>

#include "../shared/files.h"

#include "audio_layout.h"

namespace bx {
namespace {

std::string join(const std::vector<std::string>& parts) {
  std::string out;
  for (const std::string& part : parts) {
    if (!out.empty()) out += " + ";
    out += part;
  }
  return out;
}

}  // namespace

double audio_output_volume(double value) {
  if (!std::isfinite(value)) return 1.0;
  return std::clamp(value, 0.0, 1.0);
}

std::string audio_output_key(const AudioOutput& output) {
  return output.device.empty() ? std::string("default") : output.device;
}

std::string audio_output_name(const AudioOutput& output, size_t slot) {
  std::string name = output.name;
  const size_t first = name.find_first_not_of(" \t\r\n");
  const size_t last = name.find_last_not_of(" \t\r\n");
  name = first == std::string::npos ? std::string() : name.substr(first, last - first + 1);
  if (name.empty()) name = "Output " + std::to_string(slot + 1);
  return name;
}

std::vector<AudioOutput> normalize_audio_outputs(const std::vector<AudioOutput>& outputs) {
  std::vector<AudioOutput> out;
  std::vector<std::string> seen;
  for (const AudioOutput& output : outputs) {
    if (out.size() >= static_cast<size_t>(kAudioOutputMax)) break;
    if (output.device.empty()) continue;
    const std::string key = audio_output_key(output);
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
    seen.push_back(key);
    AudioOutput normalized = output;
    normalized.volume = audio_output_volume(normalized.volume);
    out.push_back(std::move(normalized));
  }
  return out;
}

bool audio_outputs_equal(const std::vector<AudioOutput>& left, const std::vector<AudioOutput>& right) {
  const std::vector<AudioOutput> a = normalize_audio_outputs(left);
  const std::vector<AudioOutput> b = normalize_audio_outputs(right);
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (audio_output_key(a[i]) != audio_output_key(b[i])) return false;
  }
  return true;
}

bool audio_output_controls_equal(const std::vector<AudioOutput>& left, const std::vector<AudioOutput>& right) {
  const std::vector<AudioOutput> a = normalize_audio_outputs(left);
  const std::vector<AudioOutput> b = normalize_audio_outputs(right);
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (audio_output_key(a[i]) != audio_output_key(b[i])) return false;
    if (audio_output_volume(a[i].volume) != audio_output_volume(b[i].volume) || a[i].muted != b[i].muted) return false;
  }
  return true;
}

std::vector<AudioOutput> effective_audio_outputs(const Config& config) {
  if (config.audio_capture_mode != "outputs") return {};
  return normalize_audio_outputs(config.audio_outputs);
}

AudioRoutingPlan plan_audio_routing(const Config& config) {
  // Automatic mode has its own plan, built from the lanes the host is carrying. A caller that has only
  // the config gets the empty six-track shape: never the desktop fallback, which could record an
  // excluded program or BuffrX's own playback.
  if (config.audio_capture_mode == "auto") return plan_auto_audio_routing(config, {});
  AudioRoutingPlan plan;
  plan.outputs = effective_audio_outputs(config);
  plan.track_count = audio_track_count(config);
  // Per-output loopbacks are what record, so the single desktop loopback is off. In desktop mode the
  // loopback is what records, even with outputs kept for later.
  plan.desktop_captured = config.audio_capture_mode != "outputs";
  const bool desktop_separate = plan.desktop_captured && config.separate_tracks;
  // With per-output captures the desktop's track is free, so the microphone takes track 2 and the
  // outputs start at 3. Without them the old layout is kept exactly: desktop on 2, microphone on 3.
  const int mic_track = plan.desktop_captured ? 3 : 2;
  const bool mic_separate = (config.separate_tracks || !plan.desktop_captured) && plan.track_count >= mic_track;
  plan.desktop_mixers = (1u << 0) | (desktop_separate ? (1u << 1) : 0u);
  plan.mic_mixers = (1u << 0) | (mic_separate ? (1u << (mic_track - 1)) : 0u);
  for (size_t i = 0; i < plan.outputs.size(); ++i) {
    const int track = kAudioOutputFirstTrack + static_cast<int>(i);
    plan.output_mixers.push_back((1u << 0) | (1u << (track - 1)));
  }

  std::vector<std::vector<std::string>> names(static_cast<size_t>(plan.track_count));
  std::vector<std::vector<std::string>> sources(static_cast<size_t>(plan.track_count));
  if (!names.empty()) {
    names[0].push_back("Mix");
    // Device and output switches apply live without rebuilding the encoders. Reserve their identity even
    // while disabled, so enabling one later cannot make the saved mix disagree with its track labels.
    if (plan.desktop_captured) sources[0].push_back("desktop");
    if (mic_separate || config.mic_enabled) sources[0].push_back("microphone");
    for (size_t i = 0; i < plan.outputs.size(); ++i) {
      sources[0].push_back(kAudioOutputSourcePrefix + audio_output_key(plan.outputs[i]));
    }
  }
  if (desktop_separate) {
    names[1].push_back("Desktop");
    sources[1].push_back("desktop");
  }
  if (mic_separate) {
    names[static_cast<size_t>(mic_track - 1)].push_back("Microphone");
    sources[static_cast<size_t>(mic_track - 1)].push_back("microphone");
  }
  for (size_t i = 0; i < plan.outputs.size(); ++i) {
    const size_t index = static_cast<size_t>(kAudioOutputFirstTrack + static_cast<int>(i) - 1);
    const std::string name = audio_output_name(plan.outputs[i], i);
    names[index].push_back(name);
    sources[index].push_back(kAudioOutputSourcePrefix + audio_output_key(plan.outputs[i]));
  }

  for (int track = 1; track <= plan.track_count; ++track) {
    AudioTrackInfo info;
    info.track = track;
    info.sources = sources[static_cast<size_t>(track - 1)];
    const std::vector<std::string>& named = names[static_cast<size_t>(track - 1)];
    if (track == 1) {
      info.role = "mix";
      info.title = "BuffrX Mix";
    } else if (named.empty()) {
      info.role = "empty";
      info.title = "BuffrX Track " + std::to_string(track);
    } else {
      info.role = named.size() > 1 ? "combined"
                  : named[0] == "Desktop" ? "desktop"
                  : named[0] == "Microphone" ? "microphone"
                                             : "output";
      info.title = "BuffrX " + join(named);
    }
    plan.tracks.push_back(std::move(info));
  }
  return plan;
}

AudioRoutingPlan plan_auto_audio_routing(const Config& config, const std::vector<AutoAudioLane>& lanes) {
  AudioRoutingPlan plan;
  plan.track_count = 6;
  plan.desktop_captured = false;  // no loopback fallback: it would record an excluded program
  const bool mic = config.mic_enabled;
  plan.mic_mixers = (1u << 0) | (mic ? (1u << 1) : 0u);

  std::vector<std::vector<std::string>> names(6);
  std::vector<std::vector<std::string>> sources(6);
  std::vector<std::string> roles(6, "empty");
  std::vector<bool> present(6, false);
  sources[0].push_back("microphone");
  names[0].push_back("Mix");
  if (mic) {
    names[1].push_back("Microphone");
    sources[1].push_back("microphone");
    roles[1] = "microphone";
    present[1] = true;
  }
  for (const AutoAudioLane& lane : lanes) {
    if (lane.track < 3 || lane.track > 6) continue;
    const size_t index = static_cast<size_t>(lane.track - 1);
    const std::string bus = auto_bus_source(lane.track);
    names[index].push_back(lane.other ? std::string("Other audio") : lane.name);
    sources[index].push_back(bus);
    roles[index] = lane.other ? "combined" : "application";
    present[index] = true;
    sources[0].push_back(bus);
  }

  for (int track = 1; track <= 6; ++track) {
    AudioTrackInfo info;
    info.track = track;
    info.sources = sources[static_cast<size_t>(track - 1)];
    if (track == 1) {
      info.role = "mix";
      info.title = "BuffrX Mix";
    } else if (!present[static_cast<size_t>(track - 1)]) {
      info.role = "empty";
      info.title = "BuffrX Track " + std::to_string(track);
    } else {
      info.role = roles[static_cast<size_t>(track - 1)];
      info.title = "BuffrX " + join(names[static_cast<size_t>(track - 1)]);
    }
    plan.tracks.push_back(std::move(info));
  }
  return plan;
}

Json audio_outputs_json(const std::vector<AudioOutput>& outputs) {
  Json array = Json::array();
  for (const AudioOutput& output : outputs) {
    Json entry = Json::object();
    entry.set("device", Json(audio_output_key(output)));
    entry.set("name", Json(output.name));
    entry.set("volume", Json(audio_output_volume(output.volume)));
    entry.set("muted", Json(output.muted));
    array.push(entry);
  }
  return array;
}

Json audio_track_metadata(const AudioRoutingPlan& plan) {
  Json json = Json::object();
  json.set("version", Json(2));
  json.set("generator", Json(std::string("BuffrX")));
  Json tracks = Json::array();
  for (const AudioTrackInfo& track : plan.tracks) {
    Json entry = Json::object();
    entry.set("index", Json(track.track));  // 1-based, as the product and OBS count tracks
    entry.set("role", Json(track.role));
    entry.set("title", Json(track.title));
    Json list = Json::array();
    for (const std::string& source : track.sources) list.push(Json(source));
    entry.set("sources", list);
    tracks.push(entry);
  }
  json.set("tracks", tracks);
  return json;
}

std::string audio_sidecar_path(const std::string& media_path) {
  return media_path + kAudioSidecarSuffix;
}

namespace {
AudioLayout layout_from_plan(const AudioRoutingPlan& plan) {
  AudioLayout layout;
  layout.buffrx = true;
  for (const AudioTrackInfo& track : plan.tracks) {
    AudioTrackLabel label;
    label.index = track.track;
    label.role = track.role;
    label.title = track.title;
    label.sources = track.sources;
    layout.tracks.push_back(std::move(label));
  }
  return layout;
}
}  // namespace

bool write_audio_sidecar(const std::string& media_path, const AudioRoutingPlan& plan) {
  if (media_path.empty() || plan.track_count <= 1) return true;
  return write_audio_layout(media_path, layout_from_plan(plan));
}

bool write_audio_sidecar_pending(const std::string& media_path, const AudioRoutingPlan& plan) {
  if (media_path.empty() || plan.track_count <= 1) return true;
  return write_audio_layout_pending(media_path, layout_from_plan(plan));
}

}  // namespace bx

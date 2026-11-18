// What automatic capture fails to deliver, and when it is worth a card.
//
// A lane that is waiting for its program is not a failure: the page says so on its row, and a person who
// closed Discord on purpose must not be told three times a day that Discord is silent. A card is written
// once per unresolved problem, after it has lasted a grace period (a buffer start and a program that is
// still launching both look unresolved for a moment). All of it is pure policy, so the wording's timing
// is unit-tested without libobs.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cctype>

namespace bx {

struct AudioHealthSlot {
  std::string exe;
  bool connected = false;
};

struct AudioHealthFacts {
  // Something is being recorded now. Nothing is said about audio while the buffer is off.
  bool capture_active = false;
  std::vector<AudioHealthSlot> slots;  // the lanes that are on, in order
};

enum class AudioProblem {
  none,
  all_unavailable  // every lane is silent while something is being recorded
};

inline std::string audio_health_lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// The most important problem right now. `all_unavailable` needs at least one lane to call silent.
inline AudioProblem audio_health_problem(const AudioHealthFacts& facts) {
  if (!facts.capture_active || facts.slots.empty()) return AudioProblem::none;
  for (const AudioHealthSlot& slot : facts.slots) {
    if (slot.connected) return AudioProblem::none;
  }
  return AudioProblem::all_unavailable;
}

// The wire name `status.audio.problem` carries, so the page can draw the persistent line from the same pure
// result the card uses. Empty when there is nothing to say.
inline const char* audio_problem_name(AudioProblem problem) {
  switch (problem) {
    case AudioProblem::all_unavailable: return "all_unavailable";
    case AudioProblem::none: break;
  }
  return "";
}

// Whether the page should draw its line yet. A capture starting looks unresolved for a moment; a line
// under the lanes that appears and leaves within a frame is noise, not news.
inline bool audio_problem_steady(AudioProblem problem, uint32_t age_ms) {
  return problem != AudioProblem::none && age_ms >= 1500;
}

// The identity a problem is deduplicated by: the same set of lanes is not a new problem.
inline std::string audio_health_key(const AudioHealthFacts& facts, AudioProblem problem) {
  if (problem == AudioProblem::none) return std::string();
  std::string key = "all:";
  for (const AudioHealthSlot& slot : facts.slots) key += audio_health_lower(slot.exe) + ",";
  return key;
}

struct AudioNoticeState {
  std::string key;
  uint32_t first_seen_ms = 0;
  bool noticed = false;
};

// True exactly once per unresolved problem, after it has lasted `settle_ms`. A recovered problem (none) or
// a different key forgets the state, so the next occurrence is told once more.
inline bool audio_notice_due(AudioNoticeState& state, AudioProblem problem, const std::string& key,
                             uint32_t now_ms, uint32_t settle_ms = 10000) {
  if (problem == AudioProblem::none || key != state.key) {
    state.key = key;
    state.first_seen_ms = now_ms;
    state.noticed = false;
    return false;
  }
  if (!state.noticed && now_ms - state.first_seen_ms >= settle_ms) {
    state.noticed = true;
    return true;
  }
  return false;
}

}  // namespace bx

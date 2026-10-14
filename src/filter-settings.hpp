#pragma once

#include <obs.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace bb {

constexpr int kMaxTracks = 6;
constexpr int kMinDurationSec = 5;
constexpr int kMaxDurationSec = 600;
constexpr int kDefaultDurationSec = 30;
constexpr int kMinVideoBitrateKbps = 500;
constexpr int kMaxVideoBitrateKbps = 100000;
constexpr int kDefaultVideoBitrateKbps = 12000;
constexpr int kDefaultAudioBitrateKbps = 192;
constexpr int kDefaultReservedTrack = 6;

// Settings key prefix of the per-source checkboxes; the rest of the key is the source UUID.
extern const char *const kAudioSourceKeyPrefix;

extern const int kAudioBitrates[5];

struct VideoEncoderInfo {
	std::string id;
	std::string name;
};

enum class RecordMode { Always = 0, ProgramOnly = 1 };

// AllAudio: the first OBS mixer track (what streams and recordings use).
// SelectedSources: the reserved track, fed only by the sources the user ticked.
enum class AudioScope { AllAudio = 0, SelectedSources = 1 };

struct FilterSettings {
	bool enabled = true;
	RecordMode mode = RecordMode::Always;
	int duration_sec = kDefaultDurationSec;
	std::string encoder_id;
	int video_bitrate_kbps = kDefaultVideoBitrateKbps;
	AudioScope audio_scope = AudioScope::AllAudio;
	std::set<std::string> audio_sources;
	int reserved_track = kDefaultReservedTrack;
	int audio_bitrate_kbps = kDefaultAudioBitrateKbps;
	std::string directory;
	std::string filename_format;
	bool native_bridge = true;
};

void SetDefaults(obs_data_t *settings);
FilterSettings ReadSettings(obs_data_t *settings);

std::vector<VideoEncoderInfo> ListVideoEncoders();
std::string DefaultEncoderId();
// Returns `wanted` when it is available, otherwise the default encoder.
std::string ResolveEncoderId(const std::string &wanted);

// 0-based mixer track the clip audio is read from.
int AudioTrackIndex(const FilterSettings &settings);

std::string DefaultSaveDirectory();

// Replaces %SOURCE% with a sanitized source name; the remaining tokens are
// formatted by the replay buffer output when the clip is written.
std::string ExpandFilenameFormat(const std::string &format, const std::string &source_name);
std::string SanitizeFileName(const std::string &name);

} // namespace bb

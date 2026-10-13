#pragma once

#include <obs.h>

#include <cstdint>
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

extern const int kAudioBitrates[5];

struct VideoEncoderInfo {
	std::string id;
	std::string name;
};

struct FilterSettings {
	bool enabled = true;
	int duration_sec = kDefaultDurationSec;
	std::string encoder_id;
	int video_bitrate_kbps = kDefaultVideoBitrateKbps;
	uint32_t track_mask = 1;
	int audio_bitrate_kbps = kDefaultAudioBitrateKbps;
	std::string directory;
	std::string filename_format;
};

void SetDefaults(obs_data_t *settings);
FilterSettings ReadSettings(obs_data_t *settings);

// Settings that require rebuilding the pipeline when they change.
bool PipelineSettingsDiffer(const FilterSettings &a, const FilterSettings &b);

std::vector<VideoEncoderInfo> ListVideoEncoders();
std::string DefaultEncoderId();
// Returns `wanted` when it is available, otherwise the default encoder.
std::string ResolveEncoderId(const std::string &wanted);

std::string DefaultSaveDirectory();

// Replaces %SOURCE% with a sanitized source name; the remaining tokens are
// formatted by the replay buffer output when the clip is written.
std::string ExpandFilenameFormat(const std::string &format, const std::string &source_name);
std::string SanitizeFileName(const std::string &name);

} // namespace bb

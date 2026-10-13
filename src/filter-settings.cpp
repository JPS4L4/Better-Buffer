#include "filter-settings.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>
#include <plugin-support.h>

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace bb {

const int kAudioBitrates[5] = {128, 160, 192, 256, 320};

namespace {

const char *const kDefaultFilenameFormat = "%SOURCE% %CCYY-%MM-%DD %hh-%mm-%ss";

const char *const kPreferredEncoders[] = {
	"obs_nvenc_h264_tex", "obs_qsv11_v2", "h264_texture_amf", "com.apple.videotoolbox.videoencoder.ave.avc",
	"obs_x264",
};

bool IsSupportedCodec(const char *codec)
{
	return codec &&
	       (std::strcmp(codec, "h264") == 0 || std::strcmp(codec, "hevc") == 0 || std::strcmp(codec, "av1") == 0);
}

int ClampInt(int value, int low, int high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

std::string VideosDirectory()
{
#ifdef _WIN32
	PWSTR wide_path = nullptr;
	std::string result;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_DEFAULT, nullptr, &wide_path)) && wide_path) {
		const int needed = WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, nullptr, 0, nullptr, nullptr);
		if (needed > 1) {
			result.assign(static_cast<size_t>(needed), '\0');
			WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, &result[0], needed, nullptr, nullptr);
			result.resize(static_cast<size_t>(needed) - 1);
		}
	}
	if (wide_path)
		CoTaskMemFree(wide_path);
	return result;
#else
	const char *home = std::getenv("HOME");
	if (!home || !*home)
		return std::string();
#ifdef __APPLE__
	return std::string(home) + "/Movies";
#else
	return std::string(home) + "/Videos";
#endif
#endif
}

} // namespace

std::string DefaultSaveDirectory()
{
	// Follow the recording path OBS is configured with, so clips land next to recordings.
	config_t *config = obs_frontend_get_profile_config();
	if (config) {
		const char *mode = config_get_string(config, "Output", "Mode");
		const bool advanced = mode && std::strcmp(mode, "Advanced") == 0;
		const char *path = advanced ? config_get_string(config, "AdvOut", "RecFilePath")
					    : config_get_string(config, "SimpleOutput", "FilePath");
		if (path && *path)
			return path;
	}
	return VideosDirectory();
}

std::vector<VideoEncoderInfo> ListVideoEncoders()
{
	std::vector<VideoEncoderInfo> encoders;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
		if (!id || obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
			continue;
		if (!IsSupportedCodec(obs_get_encoder_codec(id)))
			continue;
		if (obs_get_encoder_caps(id) & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
			continue;

		const char *name = obs_encoder_get_display_name(id);
		encoders.push_back({id, name ? name : id});
	}
	return encoders;
}

std::string DefaultEncoderId()
{
	const std::vector<VideoEncoderInfo> encoders = ListVideoEncoders();
	for (const char *preferred : kPreferredEncoders) {
		for (const VideoEncoderInfo &encoder : encoders) {
			if (encoder.id == preferred)
				return encoder.id;
		}
	}
	for (const VideoEncoderInfo &encoder : encoders) {
		const char *codec = obs_get_encoder_codec(encoder.id.c_str());
		if (codec && std::strcmp(codec, "h264") == 0)
			return encoder.id;
	}
	return "obs_x264";
}

std::string ResolveEncoderId(const std::string &wanted)
{
	if (!wanted.empty()) {
		for (const VideoEncoderInfo &encoder : ListVideoEncoders()) {
			if (encoder.id == wanted)
				return wanted;
		}
		obs_log(LOG_WARNING, "Video encoder '%s' is not available, using the default one", wanted.c_str());
	}
	return DefaultEncoderId();
}

void SetDefaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "enabled", true);
	obs_data_set_default_int(settings, "mode", static_cast<int>(RecordMode::Always));
	obs_data_set_default_int(settings, "duration", kDefaultDurationSec);
	obs_data_set_default_string(settings, "encoder", DefaultEncoderId().c_str());
	obs_data_set_default_int(settings, "video_bitrate", kDefaultVideoBitrateKbps);
	obs_data_set_default_int(settings, "audio_mode", static_cast<int>(AudioMode::SceneSource));
	for (int track = 0; track < kMaxTracks; ++track) {
		const std::string key = "track" + std::to_string(track + 1);
		obs_data_set_default_bool(settings, key.c_str(), track == 0);
	}
	obs_data_set_default_int(settings, "audio_bitrate", kDefaultAudioBitrateKbps);
	obs_data_set_default_string(settings, "directory", DefaultSaveDirectory().c_str());
	obs_data_set_default_string(settings, "filename", kDefaultFilenameFormat);
	obs_data_set_default_bool(settings, "native_bridge", true);
}

FilterSettings ReadSettings(obs_data_t *settings)
{
	FilterSettings result;
	result.enabled = obs_data_get_bool(settings, "enabled");
	result.mode = obs_data_get_int(settings, "mode") == static_cast<int>(RecordMode::ProgramOnly)
			      ? RecordMode::ProgramOnly
			      : RecordMode::Always;
	result.duration_sec =
		ClampInt(static_cast<int>(obs_data_get_int(settings, "duration")), kMinDurationSec, kMaxDurationSec);
	result.encoder_id = obs_data_get_string(settings, "encoder");
	result.video_bitrate_kbps = ClampInt(static_cast<int>(obs_data_get_int(settings, "video_bitrate")),
					     kMinVideoBitrateKbps, kMaxVideoBitrateKbps);

	result.audio_mode = obs_data_get_int(settings, "audio_mode") == static_cast<int>(AudioMode::MixerTracks)
				    ? AudioMode::MixerTracks
				    : AudioMode::SceneSource;

	result.track_mask = 0;
	for (int track = 0; track < kMaxTracks; ++track) {
		const std::string key = "track" + std::to_string(track + 1);
		if (obs_data_get_bool(settings, key.c_str()))
			result.track_mask |= 1u << track;
	}
	if (result.track_mask == 0)
		result.track_mask = 1;

	result.audio_bitrate_kbps = kDefaultAudioBitrateKbps;
	const int audio_bitrate = static_cast<int>(obs_data_get_int(settings, "audio_bitrate"));
	for (int allowed : kAudioBitrates) {
		if (allowed == audio_bitrate)
			result.audio_bitrate_kbps = allowed;
	}

	result.directory = obs_data_get_string(settings, "directory");
	if (result.directory.empty())
		result.directory = DefaultSaveDirectory();
	result.filename_format = obs_data_get_string(settings, "filename");
	if (result.filename_format.empty())
		result.filename_format = kDefaultFilenameFormat;
	result.native_bridge = obs_data_get_bool(settings, "native_bridge");
	return result;
}

std::string SanitizeFileName(const std::string &name)
{
	std::string out;
	out.reserve(name.size());
	for (char c : name) {
		const auto byte = static_cast<unsigned char>(c);
		const bool invalid = byte < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
				     c == '\\' || c == '|' || c == '?' || c == '*' || c == '%';
		out.push_back(invalid ? '_' : c);
	}

	size_t pos = 0;
	while ((pos = out.find("..", pos)) != std::string::npos)
		out.replace(pos, 2, "_");

	const size_t first = out.find_first_not_of(". ");
	if (first == std::string::npos)
		return "source";
	const size_t last = out.find_last_not_of(". ");
	out = out.substr(first, last - first + 1);

	constexpr size_t kMaxLength = 80;
	if (out.size() > kMaxLength) {
		size_t cut = kMaxLength;
		// Do not cut in the middle of a UTF-8 sequence.
		while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
			--cut;
		out.resize(cut);
	}
	return out.empty() ? "source" : out;
}

std::string ExpandFilenameFormat(const std::string &format, const std::string &source_name)
{
	const std::string token = "%SOURCE%";
	const std::string safe_name = SanitizeFileName(source_name);
	std::string result = format;
	size_t pos = 0;
	while ((pos = result.find(token, pos)) != std::string::npos) {
		result.replace(pos, token.size(), safe_name);
		pos += safe_name.size();
	}
	return result;
}

} // namespace bb

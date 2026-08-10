#include "better-buffer-filter.hpp"
#include "status-dock.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-interaction.h>
#include <plugin-support.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <time.h>
#include <vector>

#ifdef _WIN32
#include <Shlobj.h>
#include <windows.h>
#endif

#ifdef __linux__
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <CoreServices/CoreServices.h>
#include <Foundation/Foundation.h>
#endif

// ============================================================
// BetterBufferFilterRuntime — guarda/lee la config de una instancia
// ============================================================

static std::string GetDefaultVideosDirectory()
{
#ifdef _WIN32
	PWSTR path = nullptr;
	HRESULT hr = SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_DEFAULT, nullptr, &path);
	if (SUCCEEDED(hr) && path) {
		std::wstring wide_path(path);
		CoTaskMemFree(path);
		int needed = WideCharToMultiByte(CP_UTF8, 0, wide_path.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (needed > 0) {
			std::string narrow_path(static_cast<size_t>(needed), '\0');
			WideCharToMultiByte(CP_UTF8, 0, wide_path.c_str(), -1, narrow_path.data(), needed, nullptr, nullptr);
			if (!narrow_path.empty() && narrow_path.back() == '\0')
				narrow_path.pop_back();
			return narrow_path;
		}
	}
	return "C:/Users/Public/Videos";
#elif defined(__APPLE__)
	// TODO(Fase 2, ROADMAP.md): usar NSSearchPathForDirectoriesInDomains(NSMoviesDirectory, NSUserDomainMask, YES)
	return "~/Movies";
#elif defined(__linux__)
	// TODO(Fase 2, ROADMAP.md): usar xdg-user-dir VIDEOS cuando la API de escritorio esté disponible.
	const char *home = std::getenv("HOME");
	if (home && home[0]) {
		std::string candidate = std::string(home) + "/Videos";
		return candidate;
	}
	return "/home/";
#else
	return "";
#endif
}

static void OnReplayBufferEvent(enum obs_frontend_event event, void *param)
{
	auto *runtime = static_cast<BetterBufferFilterRuntime *>(param);
	if (!runtime || event != OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED)
		return;

	if (!runtime->IsActive()) {
		obs_log(LOG_INFO,
			"Better Buffer: ignored global replay save because this source is not active in a live output.");
		return;
	}

	obs_output_t *replay_output = runtime->GetReplayOutput();
	if (!replay_output) {
		obs_log(LOG_WARNING,
			"Better Buffer: save event received, but this instance has no dedicated active replay_output.");
		return;
	}

	proc_handler_t *proc = obs_output_get_proc_handler(replay_output);
	if (!proc) {
		obs_log(LOG_WARNING, "Better Buffer: failed to get replay_output proc_handler to execute save().");
		obs_output_release(replay_output);
		return;
	}

	calldata_t cd;
	calldata_init(&cd);
	const bool save_called = proc_handler_call(proc, "save", &cd);
	calldata_free(&cd);
	obs_output_release(replay_output);

	if (!save_called) {
		obs_log(LOG_WARNING,
			"Better Buffer: proc_handler_call(save) returned false for the instance replay_output.");
		return;
	}

	runtime->SetLastStatusMessage("Replay saved");
	runtime->SetLastOutputPath(runtime->GetSavePath());
	obs_log(LOG_INFO, "Better Buffer: save() triggered on this instance dedicated replay_output.");
}

static void TriggerHotkeyCallback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;

	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	if (runtime)
		runtime->TriggerReplaySave();
}

static bool ParseTriggerCombo(const std::string &combo_text, obs_key_combination_t *combo)
{
	if (!combo)
		return false;

	combo->modifiers = 0;
	combo->key = OBS_KEY_NONE;

	if (combo_text.empty())
		return false;

	std::string input = combo_text;
	input.erase(std::remove_if(input.begin(), input.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }),
		    input.end());
	if (input.empty())
		return false;

	std::stringstream stream(input);
	std::string token;
	std::vector<std::string> parts;
	while (std::getline(stream, token, '+')) {
		if (!token.empty())
			parts.push_back(token);
	}
	if (parts.empty())
		return false;

	auto normalize = [](std::string value) {
		std::transform(value.begin(), value.end(), value.begin(),
			       [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
		return value;
	};

	for (const auto &part : parts) {
		const std::string normalized = normalize(part);
		if (normalized == "CTRL" || normalized == "CONTROL") {
			combo->modifiers |= INTERACT_CONTROL_KEY;
		} else if (normalized == "SHIFT") {
			combo->modifiers |= INTERACT_SHIFT_KEY;
		} else if (normalized == "ALT") {
			combo->modifiers |= INTERACT_ALT_KEY;
		} else if (normalized == "CMD" || normalized == "COMMAND" || normalized == "META" ||
			   normalized == "SUPER") {
			combo->modifiers |= INTERACT_COMMAND_KEY;
		} else {
			if (combo->key != OBS_KEY_NONE)
				return false;

			if (normalized == "NONE")
				combo->key = OBS_KEY_NONE;
			else if (normalized == "SPACE")
				combo->key = OBS_KEY_SPACE;
			else if (normalized == "ENTER" || normalized == "RETURN")
				combo->key = OBS_KEY_RETURN;
			else if (normalized == "ESC" || normalized == "ESCAPE")
				combo->key = OBS_KEY_ESCAPE;
			else if (normalized == "TAB")
				combo->key = OBS_KEY_TAB;
			else if (normalized == "BACKSPACE")
				combo->key = OBS_KEY_BACKSPACE;
			else if (normalized == "DELETE")
				combo->key = OBS_KEY_DELETE;
			else if (normalized == "PAUSE")
				combo->key = OBS_KEY_PAUSE;
			else if (normalized == "PRINT")
				combo->key = OBS_KEY_PRINT;
			else if (normalized == "HOME")
				combo->key = OBS_KEY_HOME;
			else if (normalized == "END")
				combo->key = OBS_KEY_END;
			else if (normalized == "LEFT")
				combo->key = OBS_KEY_LEFT;
			else if (normalized == "UP")
				combo->key = OBS_KEY_UP;
			else if (normalized == "RIGHT")
				combo->key = OBS_KEY_RIGHT;
			else if (normalized == "DOWN")
				combo->key = OBS_KEY_DOWN;
			else if (normalized == "PAGEUP")
				combo->key = OBS_KEY_PAGEUP;
			else if (normalized == "PAGEDOWN")
				combo->key = OBS_KEY_PAGEDOWN;
			else if (normalized == "F1")
				combo->key = OBS_KEY_F1;
			else if (normalized == "F2")
				combo->key = OBS_KEY_F2;
			else if (normalized == "F3")
				combo->key = OBS_KEY_F3;
			else if (normalized == "F4")
				combo->key = OBS_KEY_F4;
			else if (normalized == "F5")
				combo->key = OBS_KEY_F5;
			else if (normalized == "F6")
				combo->key = OBS_KEY_F6;
			else if (normalized == "F7")
				combo->key = OBS_KEY_F7;
			else if (normalized == "F8")
				combo->key = OBS_KEY_F8;
			else if (normalized == "F9")
				combo->key = OBS_KEY_F9;
			else if (normalized == "F10")
				combo->key = OBS_KEY_F10;
			else if (normalized == "F11")
				combo->key = OBS_KEY_F11;
			else if (normalized == "F12")
				combo->key = OBS_KEY_F12;
			else if (normalized == "A")
				combo->key = OBS_KEY_A;
			else if (normalized == "B")
				combo->key = OBS_KEY_B;
			else if (normalized == "C")
				combo->key = OBS_KEY_C;
			else if (normalized == "D")
				combo->key = OBS_KEY_D;
			else if (normalized == "E")
				combo->key = OBS_KEY_E;
			else if (normalized == "F")
				combo->key = OBS_KEY_F;
			else if (normalized == "G")
				combo->key = OBS_KEY_G;
			else if (normalized == "H")
				combo->key = OBS_KEY_H;
			else if (normalized == "I")
				combo->key = OBS_KEY_I;
			else if (normalized == "J")
				combo->key = OBS_KEY_J;
			else if (normalized == "K")
				combo->key = OBS_KEY_K;
			else if (normalized == "L")
				combo->key = OBS_KEY_L;
			else if (normalized == "M")
				combo->key = OBS_KEY_M;
			else if (normalized == "N")
				combo->key = OBS_KEY_N;
			else if (normalized == "O")
				combo->key = OBS_KEY_O;
			else if (normalized == "P")
				combo->key = OBS_KEY_P;
			else if (normalized == "Q")
				combo->key = OBS_KEY_Q;
			else if (normalized == "R")
				combo->key = OBS_KEY_R;
			else if (normalized == "S")
				combo->key = OBS_KEY_S;
			else if (normalized == "T")
				combo->key = OBS_KEY_T;
			else if (normalized == "U")
				combo->key = OBS_KEY_U;
			else if (normalized == "V")
				combo->key = OBS_KEY_V;
			else if (normalized == "W")
				combo->key = OBS_KEY_W;
			else if (normalized == "X")
				combo->key = OBS_KEY_X;
			else if (normalized == "Y")
				combo->key = OBS_KEY_Y;
			else if (normalized == "Z")
				combo->key = OBS_KEY_Z;
			else if (normalized == "0")
				combo->key = OBS_KEY_0;
			else if (normalized == "1")
				combo->key = OBS_KEY_1;
			else if (normalized == "2")
				combo->key = OBS_KEY_2;
			else if (normalized == "3")
				combo->key = OBS_KEY_3;
			else if (normalized == "4")
				combo->key = OBS_KEY_4;
			else if (normalized == "5")
				combo->key = OBS_KEY_5;
			else if (normalized == "6")
				combo->key = OBS_KEY_6;
			else if (normalized == "7")
				combo->key = OBS_KEY_7;
			else if (normalized == "8")
				combo->key = OBS_KEY_8;
			else if (normalized == "9")
				combo->key = OBS_KEY_9;
			else if (normalized == "?" || normalized == "¿")
				combo->key = OBS_KEY_QUESTION;
			else if (normalized == "¿")
				combo->key = OBS_KEY_QUESTIONDOWN;
			else if (normalized == ".")
				combo->key = OBS_KEY_PERIOD;
			else if (normalized == ",")
				combo->key = OBS_KEY_COMMA;
			else if (normalized == ";")
				combo->key = OBS_KEY_SEMICOLON;
			else if (normalized == ":")
				combo->key = OBS_KEY_COLON;
			else if (normalized == "-")
				combo->key = OBS_KEY_MINUS;
			else if (normalized == "=")
				combo->key = OBS_KEY_EQUAL;
			else if (normalized == "[")
				combo->key = OBS_KEY_BRACKETLEFT;
			else if (normalized == "]")
				combo->key = OBS_KEY_BRACKETRIGHT;
			else if (normalized == "\\")
				combo->key = OBS_KEY_BACKSLASH;
			else if (normalized == "/")
				combo->key = OBS_KEY_SLASH;
			else if (normalized == "'")
				combo->key = OBS_KEY_QUOTE;
			else if (normalized == "`")
				combo->key = OBS_KEY_QUOTELEFT;
			else
				return false;
		}
	}

	return combo->key != OBS_KEY_NONE || combo->modifiers != 0;
}

static std::vector<std::string> GetAvailableVideoEncoderIds()
{
	std::vector<std::string> encoder_ids;
	const char *encoder_id = nullptr;
	size_t index = 0;
	while (obs_enum_encoder_types(index, &encoder_id)) {
		if (encoder_id && obs_get_encoder_type(encoder_id) == OBS_ENCODER_VIDEO)
			encoder_ids.emplace_back(encoder_id);
		++index;
	}
	return encoder_ids;
}

static std::vector<std::string> GetAvailableAudioEncoderIds()
{
	std::vector<std::string> encoder_ids;
	const char *encoder_id = nullptr;
	size_t index = 0;
	while (obs_enum_encoder_types(index, &encoder_id)) {
		if (encoder_id && obs_get_encoder_type(encoder_id) == OBS_ENCODER_AUDIO)
			encoder_ids.emplace_back(encoder_id);
		++index;
	}
	return encoder_ids;
}

static std::string GetPreferredVideoEncoderId(const std::vector<std::string> &encoder_ids)
{
	auto is_hardware_encoder = [](const std::string &encoder_id) {
		std::string lower = encoder_id;
		for (char &c : lower)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return lower.find("nvenc") != std::string::npos || lower.find("qsv") != std::string::npos ||
		       lower.find("amf") != std::string::npos;
	};

	for (const auto &encoder_id : encoder_ids) {
		if (is_hardware_encoder(encoder_id))
			return encoder_id;
	}

	for (const auto &encoder_id : encoder_ids) {
		if (encoder_id == "obs_x264")
			return encoder_id;
	}

	return encoder_ids.empty() ? "obs_x264" : encoder_ids.front();
}

static std::string GetPreferredAudioEncoderId(const std::vector<std::string> &encoder_ids)
{
	for (const auto &encoder_id : encoder_ids) {
		if (encoder_id == "ffmpeg_aac")
			return encoder_id;
	}

	return encoder_ids.empty() ? std::string() : encoder_ids.front();
}

static size_t GetFirstActiveTrackIndex(uint32_t mask)
{
	for (size_t i = 0; i < 6; ++i) {
		if (mask & (1u << i))
			return i;
	}
	return 0;
}

static constexpr int kDefaultBitrateKbps = 6000;
static constexpr int kMinBitrateKbps = 2000;
static constexpr int kMaxBitrateKbps = 50000;

static int GetBitrateKbpsFromSettings(obs_data_t *settings)
{
	if (!settings)
		return kDefaultBitrateKbps;

	const char *bitrate_text = obs_data_get_string(settings, "bitrate_kbps");
	if (bitrate_text && *bitrate_text) {
		char *end = nullptr;
		const long parsed = std::strtol(bitrate_text, &end, 10);
		if (end && end != bitrate_text) {
			while (*end && std::isspace(static_cast<unsigned char>(*end)))
				++end;
			if (*end == '\0')
				return static_cast<int>(parsed);
		}
	}

	const int legacy_bitrate = static_cast<int>(obs_data_get_int(settings, "bitrate_kbps"));
	if (legacy_bitrate != 0)
		return legacy_bitrate;

	return kDefaultBitrateKbps;
}

static void SetBitrateKbpsInSettings(obs_data_t *settings, int bitrate_kbps)
{
	if (!settings)
		return;

	char bitrate_text[16];
	snprintf(bitrate_text, sizeof(bitrate_text), "%d", bitrate_kbps);
	obs_data_set_string(settings, "bitrate_kbps", bitrate_text);
}

static bool ValidateEncoderSettings(obs_data_t *settings)
{
	if (!settings)
		return true;

	const std::vector<std::string> available_encoders = GetAvailableVideoEncoderIds();
	const std::string preferred_encoder = GetPreferredVideoEncoderId(available_encoders);
	const char *current_encoder_id = obs_data_get_string(settings, "encoder_id");
	const bool encoder_exists = std::find_if(available_encoders.begin(), available_encoders.end(),
						 [current_encoder_id](const std::string &encoder_id) {
							 return current_encoder_id && encoder_id == current_encoder_id;
						 }) != available_encoders.end();

	if (!current_encoder_id || !*current_encoder_id || !encoder_exists) {
		obs_data_set_string(settings, "encoder_id", preferred_encoder.c_str());
		obs_log(LOG_WARNING,
			"Better Buffer: encoder_id is invalid or unavailable; using fallback '%s' to avoid configuration errors.",
			preferred_encoder.c_str());
	}

	int bitrate_kbps = GetBitrateKbpsFromSettings(settings);
	if (bitrate_kbps < kMinBitrateKbps || bitrate_kbps > kMaxBitrateKbps) {
		const int clamped = bitrate_kbps < kMinBitrateKbps ? kMinBitrateKbps : kMaxBitrateKbps;
		SetBitrateKbpsInSettings(settings, clamped);
		obs_log(LOG_WARNING,
			"Better Buffer: bitrate_kbps out of range (%d); clamped to %d to avoid excessive usage or incompatibilities.",
			bitrate_kbps, clamped);
		bitrate_kbps = clamped;
	} else {
		SetBitrateKbpsInSettings(settings, bitrate_kbps);
	}

	return true;
}

BetterBufferFilterRuntime::BetterBufferFilterRuntime() = default;
BetterBufferFilterRuntime::~BetterBufferFilterRuntime() = default;

void BetterBufferFilterRuntime::SetFilePattern(const char *pattern)
{
	if (!pattern)
		return;
	
	snprintf(file_pattern_, sizeof(file_pattern_), "%s", pattern);
}

const char *BetterBufferFilterRuntime::GetFilePattern() const
{
	return file_pattern_;
}

void BetterBufferFilterRuntime::SetSavePath(const char *path)
{
	if (!path)
		return;

	snprintf(save_path_, sizeof(save_path_), "%s", path);
}

const char *BetterBufferFilterRuntime::GetSavePath() const
{
	return save_path_;
}

void BetterBufferFilterRuntime::LoadFromSettings(obs_data_t *settings)
{
	if (!settings)
		return;

	enabled_ = obs_data_get_bool(settings, "enabled");
	buffer_duration_ = (int)obs_data_get_int(settings, "buffer_duration");
	SetUseNativeReplayBridge(obs_data_get_bool(settings, "use_native_replay_bridge"));
	audio_track_mask_ = (uint32_t)obs_data_get_int(settings, "audio_track_mask");
	SetFilePattern(obs_data_get_string(settings, "filename_pattern"));
	SetSavePath(obs_data_get_string(settings, "save_path"));
	SetEncoderId(obs_data_get_string(settings, "encoder_id"));
	SetBitrateKbps(GetBitrateKbpsFromSettings(settings));
	SetTriggerKey(obs_data_get_string(settings, "trigger_key"));

	obs_log(LOG_INFO,
		"Better Buffer: LoadFromSettings -> enabled=%s duration=%d native_bridge=%s audio_track_mask=0x%02X encoder=%s bitrate=%dkbps file_pattern=%s save_path=%s",
		enabled_ ? "true" : "false", buffer_duration_, GetUseNativeReplayBridge() ? "true" : "false",
		audio_track_mask_, GetEncoderId(), GetBitrateKbps(), GetFilePattern(), GetSavePath());
}

void BetterBufferFilterRuntime::SaveToSettings(obs_data_t *settings) const
{
	if (!settings)
		return;

	obs_data_set_bool(settings, "enabled", enabled_);
	obs_data_set_int(settings, "buffer_duration", buffer_duration_);
	obs_data_set_bool(settings, "use_native_replay_bridge", use_native_replay_bridge_);
	obs_data_set_int(settings, "audio_track_mask", static_cast<long long>(audio_track_mask_));
	SetBitrateKbpsInSettings(settings, bitrate_kbps_);
	obs_data_set_string(settings, "filename_pattern", GetFilePattern());
	obs_data_set_string(settings, "save_path", GetSavePath());
	obs_data_set_string(settings, "encoder_id", GetEncoderId());
	obs_data_set_string(settings, "trigger_key", GetTriggerKey());

	obs_log(LOG_INFO,
		"Better Buffer: SaveToSettings -> enabled=%s duration=%d native_bridge=%s audio_track_mask=0x%02X encoder=%s bitrate=%dkbps file_pattern=%s save_path=%s",
		enabled_ ? "true" : "false", buffer_duration_, GetUseNativeReplayBridge() ? "true" : "false",
		audio_track_mask_, GetEncoderId(), GetBitrateKbps(), GetFilePattern(), GetSavePath());
}

void BetterBufferFilterRuntime::SetEnabled(bool enabled)
{
	enabled_ = enabled;
}

bool BetterBufferFilterRuntime::IsEnabled() const
{
	return enabled_;
}

void BetterBufferFilterRuntime::SetBufferDuration(int seconds)
{
	buffer_duration_ = seconds;
}

int BetterBufferFilterRuntime::GetBufferDuration() const
{
	return buffer_duration_;
}

void BetterBufferFilterRuntime::SetEncoderId(const char *encoder_id)
{
	encoder_id_ = encoder_id ? encoder_id : "";
}

const char *BetterBufferFilterRuntime::GetEncoderId() const
{
	return encoder_id_.c_str();
}

void BetterBufferFilterRuntime::SetBitrateKbps(int bitrate_kbps)
{
	bitrate_kbps_ = bitrate_kbps;
}

int BetterBufferFilterRuntime::GetBitrateKbps() const
{
	return bitrate_kbps_;
}

void BetterBufferFilterRuntime::SetUseNativeReplayBridge(bool enabled)
{
	use_native_replay_bridge_ = enabled;
}

bool BetterBufferFilterRuntime::GetUseNativeReplayBridge() const
{
	return use_native_replay_bridge_;
}

void BetterBufferFilterRuntime::SetActive(bool active)
{
	const bool changed = is_active_.load(std::memory_order_relaxed) != active;
	is_active_.store(active, std::memory_order_relaxed);
	if (changed && source_) {
		obs_source_update_properties(source_);
	}
}

bool BetterBufferFilterRuntime::IsActive() const
{
	return is_active_.load(std::memory_order_relaxed);
}

void BetterBufferFilterRuntime::SetAudioTrackMask(uint32_t mask)
{
	audio_track_mask_ = mask;
}

uint32_t BetterBufferFilterRuntime::GetAudioTrackMask() const
{
	return audio_track_mask_;
}

void BetterBufferFilterRuntime::SetSource(obs_source_t *source)
{
	source_ = source;
}

obs_source_t *BetterBufferFilterRuntime::GetSource() const
{
	return source_;
}

obs_output_t *BetterBufferFilterRuntime::GetReplayOutput() const
{
	std::lock_guard<std::mutex> lock(pipeline_mutex_);
	if (!replay_output_)
		return nullptr;
	return obs_output_get_ref(replay_output_);
}

void BetterBufferFilterRuntime::LogSummary() const
{
	obs_log(LOG_INFO,
		"better_buffer filter: enabled=%s duration=%ds native_bridge=%s audio_track_mask=0x%02X encoder=%s bitrate=%dkbps",
		enabled_ ? "true" : "false", buffer_duration_, GetUseNativeReplayBridge() ? "true" : "false",
		audio_track_mask_, GetEncoderId(), GetBitrateKbps());
}

void BetterBufferFilterRuntime::SetLastStatusMessage(const char *message)
{
	last_status_message_ = message ? message : "";
}

const char *BetterBufferFilterRuntime::GetLastStatusMessage() const
{
	return last_status_message_.c_str();
}

void BetterBufferFilterRuntime::SetLastOutputPath(const char *path)
{
	last_output_path_ = path ? path : "";
}

const char *BetterBufferFilterRuntime::GetLastOutputPath() const
{
	return last_output_path_.c_str();
}

void BetterBufferFilterRuntime::SetTriggerKey(const char *key_name)
{
	trigger_key_name_ = key_name ? key_name : "";
	UpdateTriggerHotkeyBinding();
}

const char *BetterBufferFilterRuntime::GetTriggerKey() const
{
	return trigger_key_name_.c_str();
}

obs_hotkey_id BetterBufferFilterRuntime::GetTriggerHotkeyIdForDestroy() const
{
	return trigger_hotkey_id_;
}

void BetterBufferFilterRuntime::ResetTriggerHotkeyId()
{
	trigger_hotkey_id_ = OBS_INVALID_HOTKEY_ID;
}

void BetterBufferFilterRuntime::UpdateTriggerHotkeyBinding()
{
	if (!source_)
		return;

	if (trigger_hotkey_id_ == OBS_INVALID_HOTKEY_ID) {
		trigger_hotkey_id_ = obs_hotkey_register_source(source_, "BetterBuffer.Trigger",
								"Better Buffer trigger", TriggerHotkeyCallback, this);
	}

	if (trigger_hotkey_id_ == OBS_INVALID_HOTKEY_ID)
		return;

	if (trigger_key_name_.empty()) {
		obs_hotkey_load_bindings(trigger_hotkey_id_, nullptr, 0);
		return;
	}

	obs_key_combination_t combo{};
	if (!ParseTriggerCombo(trigger_key_name_, &combo)) {
		obs_hotkey_load_bindings(trigger_hotkey_id_, nullptr, 0);
		return;
	}
	obs_hotkey_load_bindings(trigger_hotkey_id_, &combo, 1);
}

bool BetterBufferFilterRuntime::TriggerReplaySave()
{
	if (!IsActive()) {
		obs_log(LOG_INFO, "Better Buffer: ignored trigger because this source is not active in a live output.");
		return false;
	}

	obs_output_t *replay_output = GetReplayOutput();
	if (!replay_output) {
		obs_log(LOG_WARNING,
			"Better Buffer: trigger received, but this instance has no dedicated active replay_output.");
		return false;
	}

	proc_handler_t *proc = obs_output_get_proc_handler(replay_output);
	if (!proc) {
		obs_log(LOG_WARNING,
			"Better Buffer: failed to get replay_output proc_handler to execute trigger save().");
		obs_output_release(replay_output);
		return false;
	}

	calldata_t cd;
	calldata_init(&cd);
	const bool save_called = proc_handler_call(proc, "save", &cd);
	calldata_free(&cd);
	obs_output_release(replay_output);

	if (!save_called) {
		obs_log(LOG_WARNING, "Better Buffer: trigger could not invoke save() on the instance replay_output.");
		return false;
	}

	SetLastStatusMessage("Replay saved");
	SetLastOutputPath(GetSavePath());
	if (source_)
		obs_source_update_properties(source_);
	return true;
}

void BetterBufferFilterRuntime::ApplyConfiguration()
{
	ApplyAudioTrackConfiguration();
	ApplyReplayBufferConfiguration();
}

bool BetterBufferFilterRuntime::IsPipelineActive() const
{
	std::lock_guard<std::mutex> lock(pipeline_mutex_);
	return pipeline_active_;
}

void BetterBufferFilterRuntime::CleanupPipelineResourcesLocked()
{
	if (video_encoder_) {
		obs_encoder_release(video_encoder_);
		video_encoder_ = nullptr;
	}

	if (audio_encoder_) {
		obs_encoder_release(audio_encoder_);
		audio_encoder_ = nullptr;
	}

	if (replay_output_) {
		obs_output_release(replay_output_);
		replay_output_ = nullptr;
	}

	if (view_) {
		obs_view_remove(view_);
		obs_view_set_source(view_, 0, nullptr);
		obs_view_destroy(view_);
		view_ = nullptr;
		view_video_ = nullptr;
	}
}

void BetterBufferFilterRuntime::StartPipeline()
{
	std::lock_guard<std::mutex> lock(pipeline_mutex_);
	StartPipelineLocked();
}

void BetterBufferFilterRuntime::RestartPipeline()
{
	std::lock_guard<std::mutex> lock(pipeline_mutex_);
	if (!pipeline_active_)
		return;
	StopPipelineLocked();
	StartPipelineLocked();
}

void BetterBufferFilterRuntime::StartPipelineLocked()
{
	if (pipeline_active_)
		return;

	if (!enabled_ || use_native_replay_bridge_) {
		obs_log(LOG_INFO,
			"Better Buffer: StartPipeline skipped because the filter is disabled or running in legacy mode.");
		return;
	}

	// Primero se valida el parent: sin fuente padre no existe objetivo para el view.
	obs_source_t *parent = GetSource() ? obs_filter_get_parent(GetSource()) : nullptr;
	if (!parent) {
		obs_log(LOG_WARNING, "Better Buffer: StartPipeline canceled because parent source is null.");
		return;
	}

	// Se crea la vista aislada antes de cualquier encoder para encapsular solo esta fuente.
	view_ = obs_view_create();
	if (!view_) {
		obs_log(LOG_WARNING, "Better Buffer: failed to create obs_view_t for dedicated pipeline.");
		return;
	}

	// El orden importa: primero se conecta la fuente al canal 0 de la vista,
	// luego se inicializa el video de la vista con obs_view_add().
	obs_view_set_source(view_, 0, parent);
	view_video_ = obs_view_add(view_);
	if (!view_video_) {
		obs_log(LOG_WARNING, "Better Buffer: obs_view_add returned null; cannot continue pipeline.");
		CleanupPipelineResourcesLocked();
		return;
	}

	// El encoder de video depende de view_video_ ya inicializado y conectado.
	obs_data_t *video_settings = obs_data_create();
	obs_data_set_int(video_settings, "bitrate", bitrate_kbps_);

	std::string selected_video_encoder = GetEncoderId() ? GetEncoderId() : "";
	if (selected_video_encoder.empty())
		selected_video_encoder = GetPreferredVideoEncoderId(GetAvailableVideoEncoderIds());
	const char *video_encoder_id = selected_video_encoder.c_str();

	char video_encoder_name[128];
	snprintf(video_encoder_name, sizeof(video_encoder_name), "better_buffer_video_encoder_%p", this);
	video_encoder_ = obs_video_encoder_create(video_encoder_id, video_encoder_name, video_settings, nullptr);
	obs_data_release(video_settings);
	if (!video_encoder_) {
		obs_log(LOG_WARNING, "Better Buffer: failed to create video encoder '%s' for dedicated pipeline.",
			video_encoder_id ? video_encoder_id : "<null>");
		CleanupPipelineResourcesLocked();
		return;
	}

	obs_encoder_set_video(video_encoder_, view_video_);

	// El output se crea después de encoders/settings para poder cablearlo de inmediato.
	obs_data_t *output_settings = obs_data_create();
	obs_data_set_int(output_settings, "max_time_sec", buffer_duration_);
	obs_data_set_int(output_settings, "max_size_mb", 0);
	obs_data_set_string(output_settings, "directory", GetResolvedSavePath().c_str());
	const std::string output_format = ExpandFilePattern(GetFilePattern(), obs_source_get_name(parent));
	obs_data_set_string(output_settings, "format", output_format.c_str());
	obs_data_set_string(output_settings, "extension", "mp4");

	char replay_output_name[128];
	snprintf(replay_output_name, sizeof(replay_output_name), "better_buffer_replay_output_%p", this);
	replay_output_ = obs_output_create("replay_buffer", replay_output_name, output_settings, nullptr);
	obs_data_release(output_settings);
	if (!replay_output_) {
		obs_log(LOG_WARNING,
			"Better Buffer: failed to create replay_buffer obs_output for dedicated pipeline.");
		CleanupPipelineResourcesLocked();
		return;
	}

	obs_output_set_video_encoder(replay_output_, video_encoder_);

	// Audio se monta después del output para enlazarlo al índice de track 0 del replay.
	const std::vector<std::string> audio_encoders = GetAvailableAudioEncoderIds();
	const std::string audio_encoder_id = GetPreferredAudioEncoderId(audio_encoders);
	if (audio_encoder_id.empty()) {
		obs_log(LOG_WARNING,
			"Better Buffer: no audio encoders available; cannot start dedicated replay_buffer.");
		CleanupPipelineResourcesLocked();
		return;
	}

	size_t mixer_idx = GetFirstActiveTrackIndex(audio_track_mask_);

	if ((audio_track_mask_ & (audio_track_mask_ - 1u)) != 0u) {
		// TODO(Fase 2): obs_audio_encoder_create selecciona un solo mixer_idx.
		// No encontramos en obs.h una API directa para asignar una máscara de
		// múltiples mixers a un solo audio encoder; para multitrack real habría
		// que crear varios audio encoders (uno por bit activo) y mapearlos con
		// obs_output_set_audio_encoder(output, encoder, idx).
		obs_log(LOG_WARNING,
			"Better Buffer: audio_track_mask has multiple tracks (0x%02X); only the first active track will be used (idx=%zu).",
			audio_track_mask_, mixer_idx);
	}

	obs_data_t *audio_settings = obs_data_create();
	char audio_encoder_name[128];
	snprintf(audio_encoder_name, sizeof(audio_encoder_name), "better_buffer_audio_encoder_%p", this);
	audio_encoder_ = obs_audio_encoder_create(audio_encoder_id.c_str(), audio_encoder_name, audio_settings,
						  mixer_idx, nullptr);
	obs_data_release(audio_settings);
	if (!audio_encoder_) {
		obs_log(LOG_WARNING, "Better Buffer: failed to create audio encoder '%s' (mixer_idx=%zu).",
			audio_encoder_id.c_str(), mixer_idx);
		CleanupPipelineResourcesLocked();
		return;
	}

	audio_t *obs_audio = obs_get_audio();
	if (!obs_audio) {
		obs_log(LOG_WARNING,
			"Better Buffer: obs_get_audio() returned null; cannot attach dedicated audio encoder.");
		CleanupPipelineResourcesLocked();
		return;
	}

	obs_encoder_set_audio(audio_encoder_, obs_audio);
	obs_output_set_audio_encoder(replay_output_, audio_encoder_, 0);

	// El start es el último paso: si falla, se limpia todo en orden inverso.
	if (!obs_output_start(replay_output_)) {
		const char *last_error = obs_output_get_last_error(replay_output_);
		obs_log(LOG_WARNING, "Better Buffer: failed to start dedicated replay_output. error=%s",
			(last_error && *last_error) ? last_error : "<sin detalle>");
		CleanupPipelineResourcesLocked();
		return;
	}

	pipeline_active_ = true;
	SetLastStatusMessage("Ready");
	BetterBufferStatusDock::SetMessage("Ready");
	obs_log(LOG_INFO,
		"Better Buffer: dedicated pipeline started for source '%s' (encoder=%s bitrate=%dkbps duration=%ds audio_track_mask=0x%02X selected_mixer_idx=%zu).",
		obs_source_get_name(parent), video_encoder_id, bitrate_kbps_, buffer_duration_, audio_track_mask_,
		mixer_idx);
}

void BetterBufferFilterRuntime::StopPipeline()
{
	std::lock_guard<std::mutex> lock(pipeline_mutex_);
	StopPipelineLocked();
}

void BetterBufferFilterRuntime::StopPipelineLocked()
{
	if (!pipeline_active_)
		return;

	if (replay_output_) {
		// Primer paso del teardown: detener captura antes de liberar encoders y view.
		obs_output_stop(replay_output_);
		// TODO(Fase 2): obs_output_stop no expone espera explícita por finalización
		// en esta ruta. Si en pruebas reales hiciera falta sincronizar teardown,
		// hay que enlazar señales de output (stopping/stop) y esperar confirmación.
	}

	// El resto se libera en inverso a la creación para evitar punteros colgantes.
	CleanupPipelineResourcesLocked();
	pipeline_active_ = false;
	SetLastStatusMessage("Stopped");
	BetterBufferStatusDock::SetMessage("Stopped");
	obs_log(LOG_INFO, "Better Buffer: dedicated pipeline stopped and released.");
}

std::string BetterBufferFilterRuntime::GetResolvedSavePath() const
{
	std::string resolved = save_path_;
	if (resolved.empty())
		resolved = GetDefaultVideosDirectory();

	std::filesystem::path path(resolved);
	if (path.empty())
		path = std::filesystem::path(GetDefaultVideosDirectory());

	try {
		std::filesystem::create_directories(path);
	} catch (const std::exception &e) {
		obs_log(LOG_WARNING, "Better Buffer: failed to create output folder %s: %s", resolved.c_str(),
			e.what());
	}

	return path.string();
}

std::string BetterBufferFilterRuntime::ExpandFilePattern(const char *pattern, const char *source_name) const
{
	if (!pattern || !*pattern)
		return "replay";

	std::string input(pattern);
	std::string output;
	output.reserve(input.size());

	std::time_t now = std::time(nullptr);
	std::tm tm_info{};
#ifdef _WIN32
	localtime_s(&tm_info, &now);
#else
	localtime_r(&now, &tm_info);
#endif

	auto append_padded = [](std::ostringstream &stream, int value, int width) {
		stream << std::setw(width) << std::setfill('0') << value;
	};

	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i] != '%') {
			output.push_back(input[i]);
			continue;
		}

		const size_t start = i;
		++i;
		std::string token;
		while (i < input.size() && input[i] != '%') {
			token.push_back(input[i]);
			++i;
		}

		if (i >= input.size()) {
			output.push_back(input[start]);
			break;
		}

		if (token == "SOURCE") {
			output += source_name && source_name[0] ? source_name : "source";
		} else if (token == "CCYY") {
			std::ostringstream stream;
			stream << (tm_info.tm_year + 1900);
			output += stream.str();
		} else if (token == "MM") {
			std::ostringstream stream;
			append_padded(stream, tm_info.tm_mon + 1, 2);
			output += stream.str();
		} else if (token == "DD") {
			std::ostringstream stream;
			append_padded(stream, tm_info.tm_mday, 2);
			output += stream.str();
		} else if (token == "hh") {
			std::ostringstream stream;
			append_padded(stream, tm_info.tm_hour, 2);
			output += stream.str();
		} else if (token == "mm") {
			std::ostringstream stream;
			append_padded(stream, tm_info.tm_min, 2);
			output += stream.str();
		} else if (token == "ss") {
			std::ostringstream stream;
			append_padded(stream, tm_info.tm_sec, 2);
			output += stream.str();
		} else {
			output.push_back('%');
			output += token;
			output.push_back('%');
		}
	}

	std::string sanitized;
	sanitized.reserve(output.size());
	for (char c : output) {
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
		    c == '|')
			c = '_';
		if (c == ' ')
			c = '_';
		sanitized.push_back(c);
	}

	return sanitized.empty() ? "replay" : sanitized;
}

void BetterBufferFilterRuntime::ApplyAudioTrackConfiguration()
{
	if (!enabled_)
		return;

	obs_source_t *parent_source = source_ ? obs_filter_get_parent(source_) : nullptr;
	if (!parent_source) {
		obs_log(LOG_WARNING, "Better Buffer: failed to apply audio track mask because parent source is null.");
		return;
	}

	const uint32_t selected_mask = audio_track_mask_ != 0 ? audio_track_mask_ : 1u;
	const size_t selected_mixer_idx = GetFirstActiveTrackIndex(selected_mask);
	obs_log(LOG_INFO,
		"Better Buffer: dedicated pipeline will use audio_track_mask=0x%02X (track %zu) for source %s",
		selected_mask, selected_mixer_idx + 1, obs_source_get_name(parent_source));
}

void BetterBufferFilterRuntime::ApplyReplayBufferConfiguration()
{
	if (!enabled_)
		return;

	if (use_native_replay_bridge_) {
		obs_log(LOG_INFO,
			"Better Buffer: legacy native replay bridge enabled; dedicated pipeline remains inactive for this instance.");
		return;
	}

	obs_log(LOG_INFO,
		"Better Buffer: configuration loaded for dedicated pipeline (obs_frontend_get_replay_buffer_output is not used).");
}

// ============================================================
// Puente hacia OBS: aquí es donde el filtro se vuelve "real"
// ============================================================

// Aquí es donde, en la Fase 2, se debe crear (en activate) y destruir
// (en deactivate) el obs_view_t + obs_output_t + encoders reales — así el
// pipeline de codificación solo existe mientras la fuente está efectivamente
// alimentando una salida en vivo, evitando consumo innecesario de CPU/GPU.
static void better_buffer_activate(void *data)
{
	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	if (!runtime)
		return;

	obs_source_t *parent_source = runtime->GetSource() ? obs_filter_get_parent(runtime->GetSource()) : nullptr;
	runtime->SetActive(true);
	obs_log(LOG_INFO,
		"Better Buffer: source '%s' ACTIVATED because it is present in the current scene; starting dedicated pipeline.",
		parent_source ? obs_source_get_name(parent_source) : "<no source>");
	runtime->SetLastStatusMessage("Waiting for output");
	BetterBufferStatusDock::SetMessage("Waiting for output");
	runtime->StartPipeline();
}

static void better_buffer_deactivate(void *data)
{
	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	if (!runtime)
		return;

	obs_source_t *parent_source = runtime->GetSource() ? obs_filter_get_parent(runtime->GetSource()) : nullptr;
	runtime->StopPipeline();
	runtime->SetActive(false);
	runtime->SetLastStatusMessage("Inactive");
	BetterBufferStatusDock::SetMessage("Inactive");
	obs_log(LOG_INFO,
		"Better Buffer: source '%s' DEACTIVATED because it is no longer present in the current scene; stopping dedicated pipeline.",
		parent_source ? obs_source_get_name(parent_source) : "<no source>");
}

static const char *better_buffer_get_name(void *)
{
	return "Better Buffer";
}

static void better_buffer_update(void *data, obs_data_t *settings)
{
	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	if (!runtime || !settings)
		return;

	ValidateEncoderSettings(settings);
	runtime->LoadFromSettings(settings);
	runtime->ApplyConfiguration();
	runtime->RestartPipeline();
	runtime->LogSummary();
}

static void better_buffer_save(void *data, obs_data_t *settings)
{
	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	if (!runtime || !settings)
		return;

	runtime->SaveToSettings(settings);
}

// Fase 2: aquí se va a crear el encoder real y el output cuando la fuente
// padre esté activa/visible (obs_source_active() / obs_source_showing()),
// de forma perezosa y no al aplicar el filtro.
static void *better_buffer_create(obs_data_t *settings, obs_source_t *source)
{
	auto *runtime = new BetterBufferFilterRuntime();
	runtime->SetSource(source);

	obs_source_t *parent_source = source ? obs_filter_get_parent(source) : nullptr;
	if (!parent_source) {
		obs_log(LOG_INFO,
			"Better Buffer: parent source is not yet available during filter creation; it will be resolved when the filter is attached.");
	} else {
		obs_log(LOG_INFO, "Better Buffer: parent source connected -> name=%s id=%s",
			obs_source_get_name(parent_source), obs_source_get_id(parent_source));
		if (settings)
			obs_data_set_string(settings, "parent_source_name", obs_source_get_name(parent_source));
	}

	obs_source_t *parent_source_for_state = parent_source;
	if (parent_source_for_state && obs_source_active(parent_source_for_state))
		runtime->SetActive(true);
	else
		runtime->SetActive(false);

	BetterBufferStatusDock::Init();
	obs_frontend_add_event_callback(OnReplayBufferEvent, runtime);
	runtime->LoadFromSettings(settings);
	runtime->ApplyConfiguration();
	if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
		runtime->SetLastStatusMessage("Output active");
		BetterBufferStatusDock::SetMessage("Output active");
		runtime->StartPipeline();
	}
	return runtime;
}

static void better_buffer_destroy(void *data)
{
	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	if (!runtime)
		return;

	if (runtime->GetTriggerHotkeyIdForDestroy() != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(runtime->GetTriggerHotkeyIdForDestroy());
		runtime->ResetTriggerHotkeyId();
	}

	if (runtime->IsActive())
		runtime->StopPipeline();

	obs_frontend_remove_event_callback(OnReplayBufferEvent, runtime);
	BetterBufferStatusDock::Shutdown();
	delete runtime;
}

static void better_buffer_defaults(obs_data_t *settings)
{
	std::string default_path = GetDefaultVideosDirectory();
	obs_data_set_default_bool(settings, "enabled", true);
	obs_data_set_default_int(settings, "buffer_duration", 10);
	obs_data_set_default_bool(settings, "use_native_replay_bridge", false);
	obs_data_set_default_int(settings, "audio_track_mask", 1); // Track 1
	obs_data_set_default_string(settings, "bitrate_kbps", "6000");
	obs_data_set_default_string(settings, "filename_pattern", "%SOURCE%_%CCYY-%MM-%DD_%hh-%mm-%ss");
	obs_data_set_default_string(settings, "save_path", default_path.c_str());
	obs_data_set_default_string(settings, "trigger_key", "");

	const std::string preferred_encoder = GetPreferredVideoEncoderId(GetAvailableVideoEncoderIds());
	obs_data_set_default_string(settings, "encoder_id", preferred_encoder.c_str());
}

static obs_properties_t *better_buffer_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *native_bridge_toggle = obs_properties_add_bool(props, "use_native_replay_bridge",
								       "Use OBS native replay buffer (legacy mode)");
	obs_property_set_long_description(
		native_bridge_toggle,
		"Disabled (recommended): prevents this instance from overwriting OBS global Replay Buffer settings. Enabled: applies path/name to OBS native replay buffer for temporary compatibility.");

	obs_property_t *pipeline_status =
		obs_properties_add_text(props, "pipeline_mode_status", "Pipeline mode", OBS_TEXT_INFO);
	obs_property_set_enabled(pipeline_status, false);
	obs_property_text_set_info_type(pipeline_status, OBS_TEXT_INFO_NORMAL);
	obs_property_text_set_info_word_wrap(pipeline_status, true);
	obs_property_set_long_description(
		pipeline_status,
		"Recommended setup: keep legacy mode off for a dedicated, per-source replay pipeline. This avoids sharing OBS global replay state and keeps each instance isolated.");

	obs_property_t *buffer_duration =
		obs_properties_add_int_slider(props, "buffer_duration", "Clip duration (seconds)", 5, 300, 5);
	obs_property_set_long_description(
		buffer_duration,
		"How long the replay buffer should stay available before saving. Larger values use more memory and CPU.");

	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	obs_property_t *encoder_list = obs_properties_add_list(props, "encoder_id", "Preferred encoder for clips",
							       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_set_long_description(
		encoder_list,
		"Select the encoder used for clips. Prefer hardware options such as NVENC/QSV/AMF when available to reduce CPU/GPU usage and improve export quality.");

	std::vector<std::string> available_encoders = GetAvailableVideoEncoderIds();
	for (const auto &encoder_id : available_encoders) {
		const char *display_name = obs_encoder_get_display_name(encoder_id.c_str());
		obs_property_list_add_string(encoder_list, display_name ? display_name : encoder_id.c_str(),
					     encoder_id.c_str());
	}
	if (available_encoders.empty()) {
		obs_property_list_add_string(encoder_list, "obs_x264", "obs_x264");
	}

	obs_property_t *bitrate_text =
		obs_properties_add_text(props, "bitrate_kbps", "Editing bitrate (Kbps)", OBS_TEXT_DEFAULT);
	obs_property_text_set_monospace(bitrate_text, true);
	obs_property_set_long_description(
		bitrate_text, "Enter a numeric value in Kbps. Default is 6000 and valid range is 2000 to 50000.");

	obs_property_t *encoder_bitrate_help =
		obs_properties_add_text(props, "encoder_bitrate_help", "Encoder and bitrate", OBS_TEXT_INFO);
	obs_property_set_enabled(encoder_bitrate_help, false);
	obs_property_text_set_info_type(encoder_bitrate_help, OBS_TEXT_INFO_NORMAL);
	obs_property_text_set_info_word_wrap(encoder_bitrate_help, true);
	obs_property_set_long_description(
		encoder_bitrate_help,
		"These controls are saved per filter instance and are ready for per-source dedicated pipelines. If legacy mode is enabled, OBS still uses its global replay buffer for saving.");
	obs_source_t *filter_source = runtime ? runtime->GetSource() : nullptr;
	obs_source_t *parent_source = filter_source ? obs_filter_get_parent(filter_source) : nullptr;

	obs_property_t *track_list = obs_properties_add_list(props, "audio_track_mask", "OBS audio track",
							     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	if (!parent_source) {
		for (int i = 0; i < 6; ++i) {
			char label[16];
			snprintf(label, sizeof(label), "Track %d", i + 1);
			obs_property_list_add_int(track_list, label, static_cast<int>(1u << i));
		}
		obs_properties_add_text(props, "audio_track_status", "Audio track", OBS_TEXT_DEFAULT);
		obs_property_set_long_description(
			track_list,
			"Parent source is not available yet. Showing all tracks until OBS reports active mixers for this source.");
	} else {
		uint32_t active_mixers = obs_source_get_audio_mixers(parent_source);
		for (int i = 0; i < 6; ++i) {
			uint32_t bit = static_cast<uint32_t>(1u) << i;
			const bool is_active = (active_mixers & bit) != 0;

			char label[48];
			snprintf(label, sizeof(label), "Track %d%s", i + 1,
				 is_active ? " (active in OBS)" : " (inactive in OBS)");
			obs_property_list_add_int(track_list, label, static_cast<int>(bit));
		}
	}

	obs_property_t *activity_status =
		obs_properties_add_text(props, "activity_status", "Activity status", OBS_TEXT_INFO);
	obs_property_set_enabled(activity_status, false);
	obs_property_text_set_info_type(activity_status, OBS_TEXT_INFO_NORMAL);
	obs_property_text_set_info_word_wrap(activity_status, true);
	obs_property_set_long_description(
		activity_status, "Indicates whether the parent source is currently feeding an active OBS output.");
	if (runtime && runtime->IsActive()) {
		obs_property_set_long_description(activity_status, "Active (this source is in a live output)");
	} else {
		obs_property_set_long_description(activity_status,
						  "Inactive (this source is not currently being streamed/recorded)");
	}

	obs_property_t *parent_source_property =
		obs_properties_add_text(props, "parent_source_name", "Attached source", OBS_TEXT_INFO);
	obs_property_set_long_description(
		parent_source_property,
		"This is the source that Better Buffer is watching. If you change scenes or sources, the status updates automatically.");
	obs_property_set_enabled(parent_source_property, false);
	obs_property_text_set_info_type(parent_source_property, OBS_TEXT_INFO_NORMAL);
	obs_property_text_set_info_word_wrap(parent_source_property, true);

	obs_property_t *pattern_property =
		obs_properties_add_text(props, "filename_pattern", "File name pattern", OBS_TEXT_DEFAULT);
	obs_property_set_long_description(
		pattern_property,
		"Use tokens like %SOURCE%, %CCYY%, %MM%, %DD%, %hh%, %mm% and %ss% to build a clear, consistent output name.");

	obs_property_t *save_path =
		obs_properties_add_path(props, "save_path", "Export path", OBS_PATH_DIRECTORY, nullptr, nullptr);
	obs_property_t *trigger_key = obs_properties_add_text(props, "trigger_key", "Trigger combo", OBS_TEXT_DEFAULT);
	OBS_COMBO_FORMAT_STRING;
	obs_property_set_long_description(
		trigger_key,
		"Enter a custom combo such as Ctrl+Shift+F12, Alt+?, Ctrl+Alt+Space, or Shift+¿. Leave it empty to disable the trigger.");
	obs_property_t *status_property =
		obs_properties_add_text(props, "status_message", "Current status", OBS_TEXT_INFO);
	obs_property_set_enabled(status_property, false);
	obs_property_set_long_description(
		save_path, "Folder where clips are exported. If left empty, Better Buffer uses your Videos folder.");
	if (runtime) {
		std::string status_summary;
		if (runtime->IsActive()) {
			status_summary = "Active (this source is in a live output)";
		} else {
			status_summary = "Inactive (this source is not currently being streamed/recorded)";
		}
		if (runtime->GetLastStatusMessage()[0] != '\0') {
			status_summary += "\n";
			status_summary += runtime->GetLastStatusMessage();
		}

		if (runtime->GetLastStatusMessage()[0] != '\0') {
			status_summary += "\nOutput: ";
			status_summary += runtime->GetLastOutputPath();
		}

		obs_property_set_long_description(status_property, status_summary.c_str());
	}

	return props;
}

// Filtro de video "passthrough": no modifica la imagen, solo observa.
// Obligatorio si output_flags incluye OBS_SOURCE_VIDEO, o la fuente se
// verá en negro mientras el filtro esté activo.
static void better_buffer_video_render(void *data, gs_effect_t *)
{
	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	obs_source_skip_video_filter(runtime->GetSource());
}

struct obs_source_info better_buffer_filter_info = {
	.id = "better_buffer_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = better_buffer_get_name,
	.create = better_buffer_create,
	.destroy = better_buffer_destroy,
	.get_defaults = better_buffer_defaults,
	.get_properties = better_buffer_properties,
	.update = better_buffer_update,
	.activate = better_buffer_activate,
	.deactivate = better_buffer_deactivate,
	.video_render = better_buffer_video_render,
	.save = better_buffer_save,
};

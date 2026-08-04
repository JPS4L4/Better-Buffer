#include "better-buffer-filter.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <cstdlib>
#include <string>

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
	if (!runtime)
		return;

	if (event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED) {
		obs_source_t *source = runtime->GetSource();
		obs_log(LOG_INFO, "Better Buffer: replay buffer guardado para la fuente %p", source);
	}
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
	audio_track_mask_ = (uint32_t)obs_data_get_int(settings, "audio_track_mask");
	SetFilePattern(obs_data_get_string(settings, "filename_pattern"));
	SetSavePath(obs_data_get_string(settings, "save_path"));

	obs_log(LOG_INFO,
		"Better Buffer: LoadFromSettings -> enabled=%s duration=%d audio_track_mask=0x%02X file_pattern=%s save_path=%s",
		enabled_ ? "true" : "false",
		buffer_duration_,
		audio_track_mask_,
		GetFilePattern(),
		GetSavePath());
}

void BetterBufferFilterRuntime::SaveToSettings(obs_data_t *settings) const
{
	if (!settings)
		return;

	obs_data_set_bool(settings, "enabled", enabled_);
	obs_data_set_int(settings, "buffer_duration", buffer_duration_);
	obs_data_set_int(settings, "audio_track_mask", static_cast<long long>(audio_track_mask_));
	obs_data_set_string(settings, "filename_pattern", GetFilePattern());
	obs_data_set_string(settings, "save_path", GetSavePath());

	obs_log(LOG_INFO,
		"Better Buffer: SaveToSettings -> enabled=%s duration=%d audio_track_mask=0x%02X file_pattern=%s save_path=%s",
		enabled_ ? "true" : "false",
		buffer_duration_,
		audio_track_mask_,
		GetFilePattern(),
		GetSavePath());
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

void BetterBufferFilterRuntime::LogSummary() const
{
	obs_log(LOG_INFO, "better_buffer filter: enabled=%s duration=%ds audio_track_mask=0x%02X",
		enabled_ ? "true" : "false", buffer_duration_, audio_track_mask_);
}

// ============================================================
// Puente hacia OBS: aquí es donde el filtro se vuelve "real"
// ============================================================

static const char *better_buffer_get_name(void *)
{
	return "Better Buffer";
}

static void better_buffer_update(void *data, obs_data_t *settings)
{
	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	if (!runtime || !settings)
		return;

	runtime->LoadFromSettings(settings);
	runtime->LogSummary();

	// TODO (Fase 2, ROADMAP.md): si ya existe un output activo,
	// reconfigurar max_time_sec y el enrutamiento de audio aquí en caliente.
}

static void better_buffer_save(void *data, obs_data_t *settings)
{
	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	if (!runtime || !settings)
		return;

	runtime->SaveToSettings(settings);
}

static void *better_buffer_create(obs_data_t *settings, obs_source_t *source)
{
	auto *runtime = new BetterBufferFilterRuntime();
	runtime->SetSource(source);

	obs_source_t *parent_source = source ? obs_filter_get_parent(source) : nullptr;
	if (!parent_source) {
		obs_log(LOG_WARNING, "Better Buffer: obs_filter_get_parent(source) devolvió null durante la creación del filtro.");
	} else {
		obs_log(LOG_INFO,
			"Better Buffer: fuente padre conectada -> name=%s id=%s",
			obs_source_get_name(parent_source),
			obs_source_get_id(parent_source));
		if (settings)
			obs_data_set_string(settings, "parent_source_name", obs_source_get_name(parent_source));
	}

	if (settings && !obs_data_has_user_value(settings, "buffer_duration")) {
		obs_output_t *replay_output = obs_frontend_get_replay_buffer_output();
		if (replay_output) {
			obs_data_t *output_settings = obs_output_get_settings(replay_output);
			if (output_settings) {
				int max_time_sec = (int)obs_data_get_int(output_settings, "max_time_sec");
				if (max_time_sec > 0) {
					obs_data_set_default_int(settings, "buffer_duration", max_time_sec);
					obs_log(LOG_INFO,
						"Better Buffer: tomando max_time_sec=%d del replay buffer como valor por defecto de buffer_duration.",
						max_time_sec);
				}
			}
		} else {
			obs_log(LOG_WARNING, "Better Buffer: obs_frontend_get_replay_buffer_output() devolvió null.");
		}
	}

	if (settings && parent_source) {
		obs_data_set_default_string(settings, "parent_source_name", obs_source_get_name(parent_source));
		obs_data_set_string(settings, "parent_source_name", obs_source_get_name(parent_source));
	}

	obs_frontend_add_event_callback(OnReplayBufferEvent, runtime);
	runtime->LoadFromSettings(settings);
	return runtime;
}

static void better_buffer_destroy(void *data)
{
	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	if (!runtime)
		return;

	obs_frontend_remove_event_callback(OnReplayBufferEvent, runtime);
	// TODO (Fase 2): detener y liberar output/encoders/vista antes de esto.
	delete runtime;
}

static void better_buffer_defaults(obs_data_t *settings)
{
	std::string default_path = GetDefaultVideosDirectory();
	obs_data_set_default_bool(settings, "enabled", true);
	obs_data_set_default_int(settings, "buffer_duration", 10);
	obs_data_set_default_int(settings, "audio_track_mask", 1); // Track 1
	obs_data_set_default_string(settings, "filename_pattern", "%SOURCE%_%CCYY-%MM-%DD_%hh-%mm-%ss");
	obs_data_set_default_string(settings, "save_path", default_path.c_str());
}

static obs_properties_t *better_buffer_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_int_slider(props, "buffer_duration", "Duración del buffer (segundos)", 5, 300, 5);

	auto *runtime = static_cast<BetterBufferFilterRuntime *>(data);
	obs_source_t *filter_source = runtime ? runtime->GetSource() : nullptr;
	obs_source_t *parent_source = filter_source ? obs_filter_get_parent(filter_source) : nullptr;

	obs_property_t *track_list = obs_properties_add_list(
		props,
		"audio_track_mask",
		"Pista de audio de OBS",
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);

	if (!parent_source) {
		obs_properties_add_text(props, "audio_track_status", "Audio Track", OBS_TEXT_DEFAULT);
		return props;
	}

	uint32_t active_mixers = obs_source_get_audio_mixers(parent_source);
	bool has_active_track = false;
	for (int i = 0; i < 6; ++i) {
		uint32_t bit = static_cast<uint32_t>(1u) << i;
		if (!(active_mixers & bit))
			continue;

		char label[16];
		snprintf(label, sizeof(label), "Track %d", i + 1);
		obs_property_list_add_int(track_list, label, bit);
		has_active_track = true;
	}

	if (!has_active_track) {
		obs_property_list_add_int(track_list, "Track 1", 1u);
	}

	obs_property_t *parent_source_property = obs_properties_add_text(
		props,
		"parent_source_name",
		"Fuente asociada",
		OBS_TEXT_INFO);
	obs_property_set_enabled(parent_source_property, false);
	obs_property_text_set_info_type(parent_source_property, OBS_TEXT_INFO_NORMAL);
	obs_property_text_set_info_word_wrap(parent_source_property, true);

	obs_property_t *pattern_property = obs_properties_add_text(
		props,
		"filename_pattern",
		"Patrón de nombre de archivo",
		OBS_TEXT_DEFAULT);
	obs_property_set_long_description(pattern_property,
		"Variables soportadas: %SOURCE% (nombre de la fuente), %CCYY% (año), %MM% (mes), %DD% (día), %hh% (hora), %mm% (minutos), %ss% (segundos).");

	obs_properties_add_path(
		props,
		"save_path",
		"Ruta de guardado",
		OBS_PATH_DIRECTORY,
		nullptr,
		nullptr);

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
	.video_render = better_buffer_video_render,
	.save = better_buffer_save,
};

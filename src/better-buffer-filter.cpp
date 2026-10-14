#include "better-buffer-filter.hpp"

#include "audio-routing.hpp"
#include "filter-instance.hpp"
#include "filter-settings.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <exception>
#include <string>

namespace bb {

namespace {

const char *const kStatusKey = "status_text";
const char *const kHotkeyNoteKey = "hotkey_note";
const char *const kCameraNoteKey = "camera_note";
const char *const kAudioSourcesKey = "audio_sources";
const char *const kAdvancedKey = "advanced";

FilterInstance *Instance(void *data)
{
	return static_cast<FilterInstance *>(data);
}

const char *GetName(void *)
{
	return obs_module_text("FilterName");
}

void *Create(obs_data_t *settings, obs_source_t *source)
{
	try {
		return CreateInstance(source, settings);
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "Could not create the filter: %s", e.what());
		return nullptr;
	}
}

void Destroy(void *data)
{
	if (data)
		DestroyInstance(Instance(data));
}

void Update(void *data, obs_data_t *settings)
{
	if (data)
		Instance(data)->Update(settings);
}

void GetDefaults(obs_data_t *settings)
{
	SetDefaults(settings);
}

void Activate(void *data)
{
	if (data)
		Instance(data)->SetProgramActive(true);
}

void Deactivate(void *data)
{
	if (data)
		Instance(data)->SetProgramActive(false);
}

void Tick(void *data, float seconds)
{
	if (data)
		Instance(data)->Tick(seconds);
}

void Render(void *data, gs_effect_t *)
{
	if (data)
		Instance(data)->RenderPassthrough();
}

// The status lines are display-only values; keep them out of the saved scene.
void Save(void *, obs_data_t *settings)
{
	obs_data_unset_user_value(settings, kStatusKey);
	obs_data_unset_user_value(settings, kHotkeyNoteKey);
	obs_data_unset_user_value(settings, kCameraNoteKey);
}

void AddInfoText(obs_properties_t *props, const char *name, const char *title)
{
	obs_property_t *info = obs_properties_add_text(props, name, title, OBS_TEXT_INFO);
	obs_property_text_set_info_type(info, OBS_TEXT_INFO_NORMAL);
	obs_property_text_set_info_word_wrap(info, true);
}

void SetVisible(obs_properties_t *props, const char *name, bool visible)
{
	obs_property_t *property = obs_properties_get(props, name);
	if (property)
		obs_property_set_visible(property, visible);
}

bool OnAudioScopeChanged(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	SetVisible(props, kAudioSourcesKey,
		   obs_data_get_int(settings, "audio_scope") == static_cast<int>(AudioScope::SelectedSources));
	return true;
}

bool OnShowAdvancedChanged(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	SetVisible(props, kAdvancedKey, obs_data_get_bool(settings, "show_advanced"));
	return true;
}

obs_properties_t *GetProperties(void *data)
{
	auto *instance = Instance(data);
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_bool(props, "enabled", obs_module_text("Enabled"));

	obs_property_t *duration = obs_properties_add_int_slider(props, "duration", obs_module_text("Duration"),
								 kMinDurationSec, kMaxDurationSec, 1);
	obs_property_int_set_suffix(duration, " s");
	obs_property_set_long_description(duration, obs_module_text("Duration.Description"));

	obs_property_t *scope = obs_properties_add_list(props, "audio_scope", obs_module_text("ClipAudio"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(scope, obs_module_text("ClipAudio.All"), static_cast<int>(AudioScope::AllAudio));
	obs_property_list_add_int(scope, obs_module_text("ClipAudio.Selected"),
				  static_cast<int>(AudioScope::SelectedSources));
	obs_property_set_long_description(scope, obs_module_text("ClipAudio.Description"));
	obs_property_set_modified_callback(scope, OnAudioScopeChanged);

	obs_properties_t *sources = obs_properties_create();
	for (const AudioSourceInfo &info : ListAudioSources()) {
		const std::string key = kAudioSourceKeyPrefix + info.uuid;
		obs_properties_add_bool(sources, key.c_str(), info.name.c_str());
	}
	obs_property_t *sources_group = obs_properties_add_group(
		props, kAudioSourcesKey, obs_module_text("AudioSources"), OBS_GROUP_NORMAL, sources);
	obs_property_set_visible(sources_group, false);

	AddInfoText(props, kHotkeyNoteKey, obs_module_text("Hint.Hotkey"));
	AddInfoText(props, kCameraNoteKey, obs_module_text("Hint.NoCamera"));

	obs_property_t *show_advanced =
		obs_properties_add_bool(props, "show_advanced", obs_module_text("ShowAdvanced"));
	obs_property_set_modified_callback(show_advanced, OnShowAdvancedChanged);

	obs_properties_t *advanced = obs_properties_create();

	obs_property_t *mode = obs_properties_add_list(advanced, "mode", obs_module_text("Mode"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, obs_module_text("Mode.Always"), static_cast<int>(RecordMode::Always));
	obs_property_list_add_int(mode, obs_module_text("Mode.ProgramOnly"), static_cast<int>(RecordMode::ProgramOnly));

	obs_property_t *encoder = obs_properties_add_list(advanced, "encoder", obs_module_text("Encoder"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const VideoEncoderInfo &info : ListVideoEncoders())
		obs_property_list_add_string(encoder, info.name.c_str(), info.id.c_str());

	obs_property_t *bitrate = obs_properties_add_int(advanced, "video_bitrate", obs_module_text("VideoBitrate"),
							 kMinVideoBitrateKbps, kMaxVideoBitrateKbps, 100);
	obs_property_int_set_suffix(bitrate, " kbps");

	obs_property_t *audio_bitrate = obs_properties_add_list(
		advanced, "audio_bitrate", obs_module_text("AudioBitrate"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int rate : kAudioBitrates) {
		const std::string label = std::to_string(rate) + " kbps";
		obs_property_list_add_int(audio_bitrate, label.c_str(), rate);
	}

	obs_properties_add_path(advanced, "directory", obs_module_text("Directory"), OBS_PATH_DIRECTORY, nullptr,
				nullptr);

	obs_property_t *filename =
		obs_properties_add_text(advanced, "filename", obs_module_text("Filename"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(filename, obs_module_text("Filename.Description"));

	obs_property_t *track = obs_properties_add_list(advanced, "reserved_track", obs_module_text("ReservedTrack"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int number = 1; number <= kMaxTracks; ++number) {
		const std::string label = std::string(obs_module_text("Track")) + " " + std::to_string(number);
		obs_property_list_add_int(track, label.c_str(), number);
	}
	obs_property_set_long_description(track, obs_module_text("ReservedTrack.Description"));

	obs_property_t *bridge = obs_properties_add_bool(advanced, "native_bridge", obs_module_text("NativeBridge"));
	obs_property_set_long_description(bridge, obs_module_text("NativeBridge.Description"));

	obs_property_t *advanced_group =
		obs_properties_add_group(props, kAdvancedKey, obs_module_text("Advanced"), OBS_GROUP_NORMAL, advanced);
	obs_property_set_visible(advanced_group, false);

	AddInfoText(props, kStatusKey, obs_module_text("Status"));

	if (instance) {
		obs_data_t *settings = obs_source_get_settings(instance->Source());
		if (settings) {
			obs_property_set_visible(sources_group, obs_data_get_int(settings, "audio_scope") ==
									static_cast<int>(AudioScope::SelectedSources));
			obs_property_set_visible(advanced_group, obs_data_get_bool(settings, "show_advanced"));
			obs_data_set_string(settings, kHotkeyNoteKey, obs_module_text("Hint.Hotkey"));
			obs_data_set_string(settings, kCameraNoteKey, obs_module_text("Hint.NoCamera"));
			obs_data_set_string(settings, kStatusKey, instance->StatusText().c_str());
			obs_data_release(settings);
		}
	}
	return props;
}

} // namespace

void RegisterFilter()
{
	static struct obs_source_info info = {};
	info.id = "better_buffer_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = GetName;
	info.create = Create;
	info.destroy = Destroy;
	info.get_defaults = GetDefaults;
	info.get_properties = GetProperties;
	info.update = Update;
	info.activate = Activate;
	info.deactivate = Deactivate;
	info.video_tick = Tick;
	info.video_render = Render;
	info.save = Save;
	obs_register_source(&info);
}

} // namespace bb

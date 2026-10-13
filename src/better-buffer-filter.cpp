#include "better-buffer-filter.hpp"

#include "filter-instance.hpp"
#include "filter-settings.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <exception>
#include <string>

namespace bb {

namespace {

const char *const kStatusKey = "status_text";
const char *const kTracksNoteKey = "tracks_note";

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
	obs_data_unset_user_value(settings, kTracksNoteKey);
}

void AddInfoText(obs_properties_t *props, const char *name, const char *title)
{
	obs_property_t *info = obs_properties_add_text(props, name, title, OBS_TEXT_INFO);
	obs_property_text_set_info_type(info, OBS_TEXT_INFO_NORMAL);
	obs_property_text_set_info_word_wrap(info, true);
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

	obs_property_t *encoder = obs_properties_add_list(props, "encoder", obs_module_text("Encoder"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const VideoEncoderInfo &info : ListVideoEncoders())
		obs_property_list_add_string(encoder, info.name.c_str(), info.id.c_str());

	obs_property_t *bitrate = obs_properties_add_int(props, "video_bitrate", obs_module_text("VideoBitrate"),
							 kMinVideoBitrateKbps, kMaxVideoBitrateKbps, 100);
	obs_property_int_set_suffix(bitrate, " kbps");

	obs_properties_t *tracks = obs_properties_create();
	AddInfoText(tracks, kTracksNoteKey, obs_module_text("Tracks.NoteTitle"));
	for (int track = 0; track < kMaxTracks; ++track) {
		const std::string key = "track" + std::to_string(track + 1);
		const std::string label = std::string(obs_module_text("Track")) + " " + std::to_string(track + 1);
		obs_properties_add_bool(tracks, key.c_str(), label.c_str());
	}
	obs_properties_add_group(props, "tracks", obs_module_text("Tracks"), OBS_GROUP_NORMAL, tracks);

	obs_property_t *audio_bitrate = obs_properties_add_list(props, "audio_bitrate", obs_module_text("AudioBitrate"),
								OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int rate : kAudioBitrates) {
		const std::string label = std::to_string(rate) + " kbps";
		obs_property_list_add_int(audio_bitrate, label.c_str(), rate);
	}

	obs_properties_add_path(props, "directory", obs_module_text("Directory"), OBS_PATH_DIRECTORY, nullptr, nullptr);

	obs_property_t *filename =
		obs_properties_add_text(props, "filename", obs_module_text("Filename"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(filename, obs_module_text("Filename.Description"));

	AddInfoText(props, kStatusKey, obs_module_text("Status"));

	if (instance) {
		obs_data_t *settings = obs_source_get_settings(instance->Source());
		if (settings) {
			obs_data_set_string(settings, kTracksNoteKey, obs_module_text("Tracks.Note"));
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
	info.video_tick = Tick;
	info.video_render = Render;
	info.save = Save;
	obs_register_source(&info);
}

} // namespace bb

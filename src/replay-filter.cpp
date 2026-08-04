#include "replay-filter.h"

#include <obs-module.h>
#include <obs.h>
#include <plugin-support.h>

#include <string>
#include <vector>

struct replay_filter_data {
	obs_source_t *source = nullptr;
	obs_source_t *parent = nullptr;
	obs_data_t *settings = nullptr;
};

const char *replay_filter_get_name(void)
{
	return "Better Buffer";
}

const char *replay_filter_get_id(void)
{
	return "better_buffer_filter";
}

void *replay_filter_create(obs_data_t *settings, obs_source_t *source)
{
	auto *data = new replay_filter_data();
	data->source = source;
	data->parent = obs_filter_get_parent(source);
	data->settings = settings;
	obs_data_addref(settings);
	return data;
}

void replay_filter_destroy(void *data)
{
	auto *filter = static_cast<replay_filter_data *>(data);
	if (!filter)
		return;

	if (filter->settings)
		obs_data_release(filter->settings);

	delete filter;
}

void replay_filter_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<replay_filter_data *>(data);
	if (!filter)
		return;

	if (filter->settings)
		obs_data_release(filter->settings);

	filter->settings = settings;
	obs_data_addref(settings);
}

obs_properties_t *replay_filter_properties(void *data)
{
	auto *filter = static_cast<replay_filter_data *>(data);
	obs_properties_t *props = obs_properties_create();

	if (!filter || !filter->parent) {
		obs_properties_add_text(props, "audio_track", "Audio Track", OBS_TEXT_DEFAULT);
		return props;
	}

	uint32_t mixers = obs_source_get_audio_mixers(filter->parent);
	std::vector<int> active_tracks;
	for (int i = 0; i < 6; ++i) {
		if (mixers & (1 << i))
			active_tracks.push_back(i + 1);
	}

	if (active_tracks.empty()) {
		obs_properties_add_text(props, "audio_track", "Audio Track", OBS_TEXT_DEFAULT);
		return props;
	}

	obs_property_t *audio_track = obs_properties_add_list(
		props,
		"audio_track",
		"Audio Track",
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);

	for (int track : active_tracks) {
		std::string label = "Track " + std::to_string(track);
		obs_property_list_add_int(audio_track, label.c_str(), track);
	}

	// TODO(Fase 2, ROADMAP.md): conectar esta selección de pista a la ruta de
	// audio del output/encoder del replay buffer. En esta fase solo se lee la
	// configuración real de Audio Mixers de la fuente padre y se guarda en
	// obs_data_t, porque todavía no existe el output/encoder dedicado.

	return props;
}

void replay_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "audio_track", 1);
}

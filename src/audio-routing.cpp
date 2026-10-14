#include "audio-routing.hpp"

#include "filter-settings.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <map>
#include <mutex>

namespace bb {

namespace {

struct Route {
	int track = 0;
	std::set<std::string> uuids;
};

std::mutex g_mutex;
std::map<const void *, Route> g_routes;
// Sources whose bit of each track was set by this module.
std::set<std::string> g_owned[kMaxTracks];
bool g_apply_pending = false;
bool g_active = false;

// Fills `out` with new references; the caller releases them.
void CollectAudioSources(std::vector<obs_source_t *> &out)
{
	auto add = [&out](obs_source_t *source) {
		if (!source)
			return;
		const bool audio = obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
				   (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
		bool known = false;
		for (obs_source_t *item : out)
			known = known || item == source;
		if (audio && !known) {
			out.push_back(source);
			return;
		}
		obs_source_release(source);
	};

	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			(*static_cast<decltype(add) *>(param))(obs_source_get_ref(source));
			return true;
		},
		&add);
	for (uint32_t channel = 0; channel < MAX_CHANNELS; ++channel)
		add(obs_get_output_source(channel));
}

void Apply()
{
	std::set<std::string> desired[kMaxTracks];
	std::set<std::string> owned[kMaxTracks];
	bool claimed[kMaxTracks] = {};
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_apply_pending = false;
		for (const auto &entry : g_routes) {
			const int track = entry.second.track;
			if (track < 0 || track >= kMaxTracks)
				continue;
			claimed[track] = true;
			desired[track].insert(entry.second.uuids.begin(), entry.second.uuids.end());
		}
		for (int track = 0; track < kMaxTracks; ++track)
			owned[track] = g_owned[track];
	}

	std::vector<obs_source_t *> sources;
	CollectAudioSources(sources);

	std::set<std::string> now_owned[kMaxTracks];
	for (obs_source_t *source : sources) {
		const char *uuid = obs_source_get_uuid(source);
		if (uuid) {
			const uint32_t before = obs_source_get_audio_mixers(source);
			uint32_t after = before;
			for (int track = 0; track < kMaxTracks; ++track) {
				const uint32_t bit = 1u << track;
				const bool wanted = desired[track].count(uuid) != 0;
				// Unticked sources lose the bit only on a track somebody uses, or one we set before.
				if (!wanted && !claimed[track] && owned[track].count(uuid) == 0)
					continue;
				after = wanted ? (after | bit) : (after & ~bit);
				if (wanted)
					now_owned[track].insert(uuid);
			}
			if (after != before)
				obs_source_set_audio_mixers(source, after);
		}
		obs_source_release(source);
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	for (int track = 0; track < kMaxTracks; ++track)
		g_owned[track] = std::move(now_owned[track]);
}

void ApplyTask(void *)
{
	Apply();
}

void ScheduleApply()
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_active || g_apply_pending)
			return;
		g_apply_pending = true;
	}
	obs_queue_task(OBS_TASK_UI, ApplyTask, nullptr, false);
}

void OnSourceCreated(void *, calldata_t *)
{
	// Loaded sources get their saved mixers after this signal, so apply from a queued task.
	ScheduleApply();
}

} // namespace

std::vector<AudioSourceInfo> ListAudioSources()
{
	std::vector<obs_source_t *> sources;
	CollectAudioSources(sources);

	std::vector<AudioSourceInfo> result;
	for (obs_source_t *source : sources) {
		const char *uuid = obs_source_get_uuid(source);
		const char *name = obs_source_get_name(source);
		if (uuid && name)
			result.push_back({uuid, name});
		obs_source_release(source);
	}
	return result;
}

void SetAudioRoute(const void *owner, int track, const std::set<std::string> &uuids)
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_routes[owner] = Route{track, uuids};
	}
	ScheduleApply();
}

void ClearAudioRoute(const void *owner)
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_routes.erase(owner) == 0)
			return;
	}
	ScheduleApply();
}

void RefreshAudioRouting()
{
	ScheduleApply();
}

void StartAudioRouting()
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_active = true;
	}
	signal_handler_connect(obs_get_signal_handler(), "source_create", OnSourceCreated, nullptr);
	signal_handler_connect(obs_get_signal_handler(), "source_load", OnSourceCreated, nullptr);
}

void StopAudioRouting()
{
	signal_handler_disconnect(obs_get_signal_handler(), "source_create", OnSourceCreated, nullptr);
	signal_handler_disconnect(obs_get_signal_handler(), "source_load", OnSourceCreated, nullptr);
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_routes.clear();
		g_active = false;
	}
	Apply();
}

} // namespace bb

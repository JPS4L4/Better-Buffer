#pragma once

#include <obs.h>

#include <set>
#include <string>
#include <vector>

namespace bb {

struct AudioSourceInfo {
	std::string uuid;
	std::string name;
};

// Audio-capable inputs of OBS, including the global channels (Desktop Audio, Mic/Aux).
// Call from the UI thread.
std::vector<AudioSourceInfo> ListAudioSources();

// Declares that `owner` wants the sources in `uuids` on the 0-based mixer `track`.
// Routes of several owners are merged (union of their ticks per track). Only the
// bit of that track is ever changed; every other mixer bit stays as the user set it.
// The work runs later on the UI thread.
void SetAudioRoute(const void *owner, int track, const std::set<std::string> &uuids);
// Removes the route of `owner`; the bits it set are cleared if nobody else wants them.
void ClearAudioRoute(const void *owner);
// Applies the routes again (scene collection loaded, source created, ...).
void RefreshAudioRouting();

void StartAudioRouting();
// Drops every route and clears the bits this module set. UI thread only.
void StopAudioRouting();

} // namespace bb

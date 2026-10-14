/*
Better Buffer for OBS Studio
Copyright (C) 2026 Juan Pablo Salazar Ceballos

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-hotkey.h>
#include <plugin-support.h>

#include "audio-routing.hpp"
#include "better-buffer-filter.hpp"
#include "filter-instance.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static const char *const kSaveAllHotkeyKey = "better_buffer_save_all";
static obs_hotkey_id save_all_hotkey = OBS_INVALID_HOTKEY_ID;

static void on_save_all_hotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;

	const int saved = bb::SaveAllClips(false);
	obs_log(LOG_INFO, "Save all hotkey: saving %d clip(s)", saved);
}

// OBS keeps the key bindings of frontend hotkeys inside the scene collection.
static void on_frontend_save(obs_data_t *save_data, bool saving, void *)
{
	if (save_all_hotkey == OBS_INVALID_HOTKEY_ID)
		return;

	if (saving) {
		obs_data_array_t *bindings = obs_hotkey_save(save_all_hotkey);
		obs_data_set_array(save_data, kSaveAllHotkeyKey, bindings);
		obs_data_array_release(bindings);
	} else {
		obs_data_array_t *bindings = obs_data_get_array(save_data, kSaveAllHotkeyKey);
		if (bindings) {
			obs_hotkey_load(save_all_hotkey, bindings);
			obs_data_array_release(bindings);
		}
	}
}

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED: {
		// Follow the native Save Replay for sources that opted in.
		const int saved = bb::SaveAllClips(true);
		if (saved > 0)
			obs_log(LOG_INFO, "Native Save Replay: saving %d clip(s)", saved);
		break;
	}
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		// Sources of a loaded collection get their saved mixers late; route the ticked ones again.
		bb::RefreshAudioRouting();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		// Release every encoder and output while OBS is still fully alive.
		bb::StopAllInstances();
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	bb::RegisterFilter();
	bb::StartAudioRouting();

	save_all_hotkey = obs_hotkey_register_frontend("BetterBuffer.SaveAll", obs_module_text("Hotkey.SaveAll"),
						       on_save_all_hotkey, nullptr);
	obs_frontend_add_save_callback(on_frontend_save, nullptr);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);

	obs_log(LOG_INFO, "Better Buffer loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	obs_frontend_remove_save_callback(on_frontend_save, nullptr);
	if (save_all_hotkey != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(save_all_hotkey);
		save_all_hotkey = OBS_INVALID_HOTKEY_ID;
	}

	bb::StopAllInstances();
	bb::StopAudioRouting();
	obs_log(LOG_INFO, "Better Buffer unloaded");
}

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
#include <plugin-support.h>

#include "better-buffer-filter.hpp"
#include "filter-instance.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
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
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_log(LOG_INFO, "Better Buffer loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	bb::StopAllInstances();
	obs_log(LOG_INFO, "Better Buffer unloaded");
}

/*
Replay Selector para OBS Studio
Copyright (C) 2026 Juan Pablo Salazar Ceballos

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <QMainWindow>

#include "replay-selector-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static ReplaySelectorDock *g_dock = nullptr;

static void CreateDockUI()
{
	QMainWindow *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_WARNING, "No se pudo obtener la ventana principal de OBS");
		return;
	}

	g_dock = new ReplaySelectorDock(mainWindow);
	g_dock->setObjectName("ReplaySelectorDock");

	// obs_frontend_add_dock_by_id es la API recomendada desde OBS 30+.
	// Si tu versión de OBS es anterior, usa obs_frontend_add_dock() en su lugar
	// (recibe un QDockWidget* envolviendo este widget).
	obs_frontend_add_dock_by_id("replay_selector_dock", "Replay Selector", g_dock);
}

static void OnFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		CreateDockUI();
	}
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "Replay Selector cargado (version %s)", PLUGIN_VERSION);
	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
	obs_log(LOG_INFO, "Replay Selector descargado");
}

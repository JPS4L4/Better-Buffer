/*
Replay Selector - dock que permite elegir qué fuentes (cámara, pantalla, etc.)
entran al buffer de repetición personalizado.

FASE ACTUAL (0.1.0): construir la UI y poder leer qué fuentes existen y cuáles
marcó el usuario. Todavía NO mezcla audio/video ni crea un output nuevo.
Ese es el siguiente hito (ver ROADMAP.md).
*/

#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <obs.h>

class ReplaySelectorDock : public QWidget {
	Q_OBJECT

public:
	explicit ReplaySelectorDock(QWidget *parent = nullptr);
	~ReplaySelectorDock() override;

private slots:
	// Vuelve a leer las fuentes actuales de OBS y refresca la lista.
	void RefreshSources();

	// Botón "Guardar clip": por ahora solo registra en el log
	// qué fuentes quedaron marcadas. Aquí se conectará el output
	// personalizado en la Fase 2.
	void OnSaveClipClicked();

private:
	QListWidget *sourceList;
	QPushButton *refreshButton;
	QPushButton *saveClipButton;
	QVBoxLayout *layout_;

	// Callback en C que obs_enum_sources() invoca por cada fuente existente.
	static bool EnumSourcesCallback(void *param, obs_source_t *source);
};

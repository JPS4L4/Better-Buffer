#include "replay-selector-dock.hpp"
#include <plugin-support.h>
#include <obs-module.h>

ReplaySelectorDock::ReplaySelectorDock(QWidget *parent) : QWidget(parent)
{
	layout_ = new QVBoxLayout(this);

	sourceList = new QListWidget(this);
	sourceList->setToolTip("Marca las fuentes que quieres incluir en el buffer");

	refreshButton = new QPushButton("Actualizar lista de fuentes", this);
	saveClipButton = new QPushButton("Guardar clip (buffer selectivo)", this);

	layout_->addWidget(sourceList);
	layout_->addWidget(refreshButton);
	layout_->addWidget(saveClipButton);
	setLayout(layout_);

	connect(refreshButton, &QPushButton::clicked, this, &ReplaySelectorDock::RefreshSources);
	connect(saveClipButton, &QPushButton::clicked, this, &ReplaySelectorDock::OnSaveClipClicked);

	RefreshSources();
}

ReplaySelectorDock::~ReplaySelectorDock() {}

bool ReplaySelectorDock::EnumSourcesCallback(void *param, obs_source_t *source)
{
	auto *list = static_cast<QListWidget *>(param);

	uint32_t flags = obs_source_get_output_flags(source);
	bool hasVideo = (flags & OBS_SOURCE_VIDEO) != 0;
	bool hasAudio = (flags & OBS_SOURCE_AUDIO) != 0;

	// Filtramos a solo fuentes "de captura" reales (cámara/pantalla/ventana/audio),
	// dejando fuera cosas como escenas anidadas o fuentes de color/browser si quieres.
	if (!hasVideo && !hasAudio)
		return true; // continuar enumerando

	const char *name = obs_source_get_name(source);
	if (!name)
		return true;

	QString label = QString::fromUtf8(name);
	if (hasVideo && hasAudio)
		label += "  [video+audio]";
	else if (hasVideo)
		label += "  [video]";
	else
		label += "  [audio]";

	auto *item = new QListWidgetItem(label, list);
	item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
	item->setCheckState(Qt::Unchecked);
	item->setData(Qt::UserRole, QString::fromUtf8(name)); // guardamos el nombre "limpio" para luego

	return true; // seguir enumerando
}

void ReplaySelectorDock::RefreshSources()
{
	sourceList->clear();
	// obs_enum_sources referencia cada fuente (hay que soltarla); lo hacemos
	// dentro del callback vía obs_source_get_name que no incrementa el refcount,
	// así que no necesitamos obs_source_release aquí.
	obs_enum_sources(&ReplaySelectorDock::EnumSourcesCallback, sourceList);
}

void ReplaySelectorDock::OnSaveClipClicked()
{
	// TODO (Fase 2): en vez de solo loguear, aquí se debe:
	//   1. Tomar los nombres marcados.
	//   2. Crear/activar un obs_output_t propio (o reconfigurar uno persistente)
	//      cuyo video_encoder / audio_encoder solo reciban frames de esas fuentes.
	//   3. Disparar el guardado del buffer circular a disco.
	obs_log(LOG_INFO, "== Guardar clip solicitado. Fuentes marcadas: ==");

	for (int i = 0; i < sourceList->count(); i++) {
		QListWidgetItem *item = sourceList->item(i);
		if (item->checkState() == Qt::Checked) {
			QByteArray name = item->data(Qt::UserRole).toString().toUtf8();
			obs_log(LOG_INFO, "  - %s", name.constData());
		}
	}
}

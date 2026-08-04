#pragma once

#include <obs.h>

/*
Estado de configuracion de una instancia del filtro Better Buffer.
Un objeto de esta clase vive por cada fuente donde el usuario aplique
el filtro (una para camara, otra para pantalla, etc. si aplica varias).

Campos alineados al ROADMAP.md actual:
- Fase 0: enabled_, buffer_duration_, source_ (necesario para passthrough).
- Fase 1: audio_track_mask_ (mascara de bits real de OBS, 1 bit por track).
No incluye seleccion de multiples fuentes ni "target" propio: el target
es implicito, es la fuente padre del filtro (obs_filter_get_parent()).
*/

class BetterBufferFilterRuntime {
public:
	BetterBufferFilterRuntime();
	~BetterBufferFilterRuntime();

	void LoadFromSettings(obs_data_t *settings);
	void SaveToSettings(obs_data_t *settings) const;

	void SetFilePattern(const char *pattern);
	const char *GetFilePattern() const;

	void SetSavePath(const char *path);
	const char *GetSavePath() const;

	void SetEnabled(bool enabled);
	bool IsEnabled() const;

	void SetBufferDuration(int seconds);
	int GetBufferDuration() const;

	// Mascara de bits real de OBS: bit 0 = Track 1, bit 1 = Track 2, etc.
	// Se obtiene de obs_source_get_audio_mixers() sobre la fuente padre.
	void SetAudioTrackMask(uint32_t mask);
	uint32_t GetAudioTrackMask() const;

	// Puntero a la fuente (el filtro en si, no el padre) — necesario para
	// obs_source_skip_video_filter() en el passthrough de video.
	void SetSource(obs_source_t *source);
	obs_source_t *GetSource() const;

	void LogSummary() const;

private:
	bool enabled_ = true;
	int buffer_duration_ = 10;
	uint32_t audio_track_mask_ = 1; // por defecto, Track 1
	obs_source_t *source_ = nullptr;
	char file_pattern_[256] = "%SOURCE%_%CCYY-%MM-%DD_%hh-%mm-%ss";
	char save_path_[512] = "";
};

// Struct que OBS usa para reconocer el filtro. Se registra una sola vez
// en obs_module_load() vía obs_register_source(&better_buffer_filter_info).
extern "C" {
extern struct obs_source_info better_buffer_filter_info;
}

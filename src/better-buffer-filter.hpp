#pragma once

#include <obs.h>
#include <obs-hotkey.h>

#include <atomic>
#include <mutex>
#include <string>

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

	void SetEncoderId(const char *encoder_id);
	const char *GetEncoderId() const;

	void SetBitrateKbps(int bitrate_kbps);
	int GetBitrateKbps() const;

	void SetUseNativeReplayBridge(bool enabled);
	bool GetUseNativeReplayBridge() const;

	void SetActive(bool active);
	bool IsActive() const;

	// Mascara de bits real de OBS: bit 0 = Track 1, bit 1 = Track 2, etc.
	// Se obtiene de obs_source_get_audio_mixers() sobre la fuente padre.
	void SetAudioTrackMask(uint32_t mask);
	uint32_t GetAudioTrackMask() const;

	// Puntero a la fuente (el filtro en si, no el padre) — necesario para
	// obs_source_skip_video_filter() en el passthrough de video.
	void SetSource(obs_source_t *source);
	obs_source_t *GetSource() const;
	obs_output_t *GetReplayOutput() const;

	void LogSummary() const;
	void ApplyConfiguration();
	void StartPipeline();
	void StopPipeline();
	void RestartPipeline();
	bool IsPipelineActive() const;
	void SetLastStatusMessage(const char *message);
	const char *GetLastStatusMessage() const;
	void SetLastOutputPath(const char *path);
	const char *GetLastOutputPath() const;
	void NotifyReplaySaved();
	bool TriggerReplaySave();
	void UpdateTriggerHotkeyBinding();
	obs_hotkey_id GetTriggerHotkeyIdForDestroy() const;
	void ResetTriggerHotkeyId();

private:
	void ApplyAudioTrackConfiguration();
	void ApplyReplayBufferConfiguration();
	void LoadTriggerHotkeyFromSettings(obs_data_t *settings);
	void SaveTriggerHotkeyToSettings(obs_data_t *settings) const;
	void CleanupPipelineResourcesLocked();
	void StartPipelineLocked();
	void StopPipelineLocked();
	std::string GetResolvedSavePath() const;
	std::string ExpandFilePattern(const char *pattern, const char *source_name) const;

	bool enabled_ = true;
	int buffer_duration_ = 10;
	int bitrate_kbps_ = 6000;
	bool use_native_replay_bridge_ = false;
	uint32_t audio_track_mask_ = 1; // por defecto, Track 1
	obs_source_t *source_ = nullptr;
	char file_pattern_[256] = "%SOURCE%_%CCYY-%MM-%DD_%hh-%mm-%ss";
	char save_path_[1024] = "";
	std::string encoder_id_;
	obs_view_t *view_ = nullptr;
	video_t *view_video_ = nullptr;
	obs_output_t *replay_output_ = nullptr;
	obs_encoder_t *video_encoder_ = nullptr;
	obs_encoder_t *audio_encoder_ = nullptr;
	bool pipeline_active_ = false;
	std::atomic<bool> is_active_{false};
	std::string last_status_message_;
	std::string last_output_path_;
	obs_hotkey_id trigger_hotkey_id_ = OBS_INVALID_HOTKEY_ID;
	mutable std::mutex pipeline_mutex_;
};

// Struct que OBS usa para reconocer el filtro. Se registra una sola vez
// en obs_module_load() vía obs_register_source(&better_buffer_filter_info).
extern "C" {
extern struct obs_source_info better_buffer_filter_info;
}

#pragma once

#include <obs.h>
#include <obs-hotkey.h>

#include <atomic>
#include <mutex>
#include <string>

// Configuration and pipeline state of one Better Buffer filter instance.

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

	void SetActive(bool active);
	bool IsActive() const;

	// OBS track bit mask: bit 0 = Track 1, bit 1 = Track 2, and so on.
	void SetAudioTrackMask(uint32_t mask);
	uint32_t GetAudioTrackMask() const;

	// The filter source itself (not the parent), needed by obs_source_skip_video_filter().
	void SetSource(obs_source_t *source);
	obs_source_t *GetSource() const;
	obs_output_t *GetReplayOutput() const;

	void LogSummary() const;
	void ApplyConfiguration();
	void StartPipeline();
	void StopPipeline();
	void RestartPipeline();
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
	uint32_t audio_track_mask_ = 1; // Track 1 by default
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

// Registered once from obs_module_load() with obs_register_source().
extern "C" {
extern struct obs_source_info better_buffer_filter_info;
}

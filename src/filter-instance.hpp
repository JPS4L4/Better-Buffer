#pragma once

#include "buffer-pipeline.hpp"
#include "filter-settings.hpp"

#include <obs.h>
#include <obs-hotkey.h>

#include <memory>
#include <mutex>
#include <string>

namespace bb {

// One Better Buffer filter attached to one source. The instance is owned by the
// registry (see CreateInstance/DestroyInstance); background work only keeps weak
// references, so it can never touch an instance after OBS destroyed the filter.
class FilterInstance : public std::enable_shared_from_this<FilterInstance> {
public:
	explicit FilterInstance(obs_source_t *source);
	~FilterInstance();

	FilterInstance(const FilterInstance &) = delete;
	FilterInstance &operator=(const FilterInstance &) = delete;

	void Init(obs_data_t *settings);
	void Update(obs_data_t *settings);
	void Shutdown();
	void StopForExit();

	// Graphics thread: must stay cheap and never block.
	void Tick(float seconds);
	void RenderPassthrough() const;

	bool SaveClip();
	std::string StatusText() const;
	obs_source_t *Source() const { return source_; }

private:
	enum class State { Disabled, WaitingForSource, Running, Error };

	static PipelineCallbacks MakeCallbacks(FilterInstance *self);
	static void HotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	static void ReconcileTask(void *param);
	static void RefreshTask(void *param);

	void ScheduleReconcile();
	void ScheduleRefresh();
	void Reconcile();
	void ReconcileLocked(obs_source_t *parent);
	void RefreshProperties();
	void SetState(State state, const std::string &error = std::string());
	void StopPipelineLocked();

	void OnClipSaved(const std::string &path);
	void OnPipelineStopped(int code, const std::string &error);
	void OnSourceRemoved();

	obs_source_t *const source_;
	obs_weak_source_t *self_weak_ = nullptr;
	obs_hotkey_id hotkey_ = OBS_INVALID_HOTKEY_ID;

	BufferPipeline pipeline_;

	// Serializes Reconcile() with Shutdown(); never held while waiting on other locks.
	std::mutex reconcile_mutex_;
	bool pipeline_active_ = false;

	mutable std::mutex mutex_;
	FilterSettings settings_;
	State state_ = State::Disabled;
	std::string error_;
	std::string last_clip_;
	bool has_clip_ = false;
	bool shutdown_ = false;
	bool exiting_ = false;
	bool reconcile_pending_ = false;
	bool source_removed_ = false;
	std::string cached_name_;
	std::string running_key_;
	std::string failure_key_;
	uint32_t failure_width_ = 0;
	uint32_t failure_height_ = 0;
	uint32_t pending_width_ = 0;
	uint32_t pending_height_ = 0;
	float stable_seconds_ = 0.0f;
};

FilterInstance *CreateInstance(obs_source_t *source, obs_data_t *settings);
void DestroyInstance(FilterInstance *instance);

// Stops every pipeline and keeps them stopped (used when OBS is closing).
void StopAllInstances();

} // namespace bb

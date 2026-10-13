#include "filter-instance.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace bb {

namespace {

std::mutex g_registry_mutex;
std::vector<std::shared_ptr<FilterInstance>> g_instances;

std::string PipelineKey(const PipelineConfig &config, uint32_t width, uint32_t height)
{
	// The track selection only matters when the OBS mixer tracks are recorded.
	const uint32_t track_mask = config.audio_mode == AudioMode::MixerTracks ? config.track_mask : 0;
	return config.encoder_id + '|' + std::to_string(config.video_bitrate_kbps) + '|' +
	       std::to_string(config.audio_bitrate_kbps) + '|' + std::to_string(config.duration_sec) + '|' +
	       std::to_string(static_cast<int>(config.audio_mode)) + '|' + std::to_string(track_mask) + '|' +
	       std::to_string(width) + 'x' + std::to_string(height);
}

} // namespace

PipelineCallbacks FilterInstance::MakeCallbacks(FilterInstance *self)
{
	PipelineCallbacks callbacks;
	callbacks.saved = [self](const std::string &path) {
		self->OnClipSaved(path);
	};
	callbacks.stopped = [self](int code, const std::string &error) {
		self->OnPipelineStopped(code, error);
	};
	callbacks.source_removed = [self]() {
		self->OnSourceRemoved();
	};
	return callbacks;
}

FilterInstance::FilterInstance(obs_source_t *source) : source_(source), pipeline_(MakeCallbacks(this))
{
	self_weak_ = obs_source_get_weak_source(source);
}

FilterInstance::~FilterInstance()
{
	pipeline_.Stop();
	if (self_weak_)
		obs_weak_source_release(self_weak_);
}

void FilterInstance::Init(obs_data_t *settings)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		settings_ = ReadSettings(settings);
	}

	// OBS stores and restores the key bindings of source hotkeys by itself.
	hotkey_ = obs_hotkey_register_source(source_, "BetterBuffer.SaveClip", obs_module_text("Hotkey.SaveClip"),
					     HotkeyCallback, this);
	ScheduleReconcile();
}

void FilterInstance::Update(obs_data_t *settings)
{
	FilterSettings updated = ReadSettings(settings);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (shutdown_)
			return;
		settings_ = std::move(updated);
	}
	// Reconcile() decides whether the change needs a restart; the buffer is kept otherwise.
	ScheduleReconcile();
}

void FilterInstance::Shutdown()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (shutdown_)
			return;
		shutdown_ = true;
	}

	// Unregistering waits for a running hotkey callback, so none can use this object afterwards.
	if (hotkey_ != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(hotkey_);
		hotkey_ = OBS_INVALID_HOTKEY_ID;
	}

	std::lock_guard<std::mutex> reconcile(reconcile_mutex_);
	StopPipelineLocked();
}

void FilterInstance::StopForExit()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		exiting_ = true;
	}
	std::lock_guard<std::mutex> reconcile(reconcile_mutex_);
	StopPipelineLocked();
}

void FilterInstance::StopPipelineLocked()
{
	pipeline_.Stop();
	pipeline_active_ = false;
}

void FilterInstance::RenderPassthrough() const
{
	obs_source_skip_video_filter(source_);
}

void FilterInstance::HotkeyCallback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		static_cast<FilterInstance *>(data)->SaveClip();
}

bool FilterInstance::NativeBridgeEnabled() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return settings_.native_bridge;
}

void FilterInstance::SetProgramActive(bool active)
{
	bool schedule = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		program_active_ = active;
		schedule = settings_.mode == RecordMode::ProgramOnly;
	}
	if (schedule)
		ScheduleReconcile();
}

bool FilterInstance::SaveClip()
{
	if (!pipeline_.Save()) {
		obs_log(LOG_INFO, "Save requested but the buffer is not running");
		return false;
	}
	obs_log(LOG_INFO, "Saving clip");
	return true;
}

void FilterInstance::Tick(float seconds)
{
	obs_source_t *parent = obs_filter_get_parent(source_);
	if (!parent)
		return;

	uint32_t width = 0;
	uint32_t height = 0;
	BufferPipeline::QuerySourceSize(parent, width, height);
	const char *name = obs_source_get_name(parent);
	const bool program_active = obs_source_active(parent);

	bool schedule = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (shutdown_ || exiting_ || !settings_.enabled || source_removed_)
			return;

		if (program_active != program_active_) {
			program_active_ = program_active;
			schedule = settings_.mode == RecordMode::ProgramOnly;
		}

		if (name && cached_name_ != name) {
			cached_name_ = name;
			schedule = true;
		}

		const uint32_t running_width = pipeline_.Width();
		const uint32_t running_height = pipeline_.Height();
		if (running_width != 0) {
			// Restart only after the new size has been stable for a moment.
			if (width != 0 && (width != running_width || height != running_height)) {
				if (width == pending_width_ && height == pending_height_) {
					stable_seconds_ += seconds;
				} else {
					pending_width_ = width;
					pending_height_ = height;
					stable_seconds_ = 0.0f;
				}
				if (stable_seconds_ >= 1.0f) {
					stable_seconds_ = 0.0f;
					schedule = true;
				}
			} else {
				stable_seconds_ = 0.0f;
			}
		} else if (state_ == State::WaitingForSource && width != 0) {
			schedule = true;
		} else if (state_ == State::Error && width != 0 &&
			   (width != failure_width_ || height != failure_height_)) {
			schedule = true;
		}
	}

	if (schedule)
		ScheduleReconcile();
}

void FilterInstance::ScheduleReconcile()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (shutdown_ || exiting_ || reconcile_pending_)
			return;
		reconcile_pending_ = true;
	}
	auto *handle = new std::weak_ptr<FilterInstance>(weak_from_this());
	obs_queue_task(OBS_TASK_UI, ReconcileTask, handle, false);
}

void FilterInstance::ScheduleRefresh()
{
	auto *handle = new std::weak_ptr<FilterInstance>(weak_from_this());
	obs_queue_task(OBS_TASK_UI, RefreshTask, handle, false);
}

void FilterInstance::ReconcileTask(void *param)
{
	std::unique_ptr<std::weak_ptr<FilterInstance>> handle(static_cast<std::weak_ptr<FilterInstance> *>(param));
	if (std::shared_ptr<FilterInstance> instance = handle->lock())
		instance->Reconcile();
}

void FilterInstance::RefreshTask(void *param)
{
	std::unique_ptr<std::weak_ptr<FilterInstance>> handle(static_cast<std::weak_ptr<FilterInstance> *>(param));
	if (std::shared_ptr<FilterInstance> instance = handle->lock())
		instance->RefreshProperties();
}

void FilterInstance::RefreshProperties()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (shutdown_)
			return;
	}
	obs_source_t *filter = obs_weak_source_get_source(self_weak_);
	if (!filter)
		return;
	obs_source_update_properties(filter);
	obs_source_release(filter);
}

void FilterInstance::SetState(State state, const std::string &error)
{
	bool changed = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		changed = state != state_ || error != error_;
		state_ = state;
		error_ = error;
	}
	if (changed)
		ScheduleRefresh();
}

void FilterInstance::Reconcile()
{
	obs_source_t *parent = nullptr;
	{
		std::lock_guard<std::mutex> reconcile(reconcile_mutex_);
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (shutdown_ || exiting_)
				return;
		}
		// The filter is alive here: Shutdown() waits for this lock before the filter is freed.
		parent = obs_source_get_ref(obs_filter_get_parent(source_));
		ReconcileLocked(parent);
	}
	// Dropping the last reference to the parent may destroy this filter, so do it unlocked.
	if (parent)
		obs_source_release(parent);
}

void FilterInstance::ReconcileLocked(obs_source_t *parent)
{
	FilterSettings settings;
	bool removed = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		settings = settings_;
		removed = source_removed_;
		reconcile_pending_ = false;
	}

	if (!settings.enabled || removed) {
		StopPipelineLocked();
		SetState(State::Disabled);
		return;
	}
	if (!parent) {
		StopPipelineLocked();
		SetState(State::WaitingForSource);
		return;
	}

	if (settings.mode == RecordMode::ProgramOnly && !obs_source_active(parent)) {
		StopPipelineLocked();
		SetState(State::WaitingForProgram);
		return;
	}

	const char *source_name = obs_source_get_name(parent);
	PipelineConfig config;
	config.source_name = source_name ? source_name : "";
	config.encoder_id = ResolveEncoderId(settings.encoder_id);
	config.video_bitrate_kbps = settings.video_bitrate_kbps;
	config.audio_bitrate_kbps = settings.audio_bitrate_kbps;
	config.duration_sec = settings.duration_sec;
	config.audio_mode = settings.audio_mode;
	config.track_mask = settings.track_mask;
	config.directory = settings.directory;
	config.format = ExpandFilenameFormat(settings.filename_format, config.source_name);

	uint32_t width = 0;
	uint32_t height = 0;
	BufferPipeline::QuerySourceSize(parent, width, height);

	if (pipeline_.IsRunning()) {
		if (width == 0 || height == 0) {
			width = pipeline_.Width();
			height = pipeline_.Height();
		}
		const std::string key = PipelineKey(config, width, height);
		bool same = false;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			same = key == running_key_;
		}
		if (same) {
			pipeline_.UpdatePaths(config.directory, config.format);
			SetState(State::Running);
			return;
		}
		obs_log(LOG_INFO, "Restarting the buffer for '%s' because its settings or size changed",
			config.source_name.c_str());
	}
	if (pipeline_active_)
		StopPipelineLocked();

	const std::string key = PipelineKey(config, width, height);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!failure_key_.empty() && key == failure_key_)
			return;
		failure_key_.clear();
	}

	if (config.directory.empty()) {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			failure_key_ = key;
			failure_width_ = width;
			failure_height_ = height;
		}
		SetState(State::Error, obs_module_text("Error.NoDirectory"));
		return;
	}

	const StartResult result = pipeline_.Start(parent, config);
	switch (result.code) {
	case StartResult::Code::Started: {
		const std::string started_key = PipelineKey(config, pipeline_.Width(), pipeline_.Height());
		pipeline_active_ = true;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			running_key_ = started_key;
		}
		SetState(State::Running);
		break;
	}
	case StartResult::Code::WaitingForSize:
		SetState(State::WaitingForSource);
		break;
	case StartResult::Code::Failed:
		obs_log(LOG_WARNING, "Could not start the buffer for '%s': %s", config.source_name.c_str(),
			result.error.c_str());
		{
			std::lock_guard<std::mutex> lock(mutex_);
			failure_key_ = key;
			failure_width_ = width;
			failure_height_ = height;
		}
		SetState(State::Error, result.error);
		break;
	}
}

void FilterInstance::OnClipSaved(const std::string &path)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		last_clip_ = path;
		has_clip_ = true;
	}
	obs_log(LOG_INFO, "Clip saved: %s", path.empty() ? "(unknown path)" : path.c_str());
	ScheduleRefresh();
}

void FilterInstance::OnPipelineStopped(int code, const std::string &error)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (shutdown_ || exiting_)
			return;
		failure_key_ = running_key_;
		failure_width_ = pipeline_.Width();
		failure_height_ = pipeline_.Height();
		state_ = State::Error;
		error_ = error.empty() ? std::string(obs_module_text("Error.Stopped")) : error;
	}
	obs_log(LOG_WARNING, "The buffer stopped unexpectedly (code %d): %s", code, error.c_str());
	// Reconcile() releases the stopped pipeline; it will not restart with unchanged settings.
	ScheduleReconcile();
	ScheduleRefresh();
}

void FilterInstance::OnSourceRemoved()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		source_removed_ = true;
	}
	// The pipeline keeps the removed source alive, so release it as soon as possible.
	ScheduleReconcile();
}

std::string FilterInstance::StatusText() const
{
	State state = State::Disabled;
	std::string error;
	std::string clip;
	bool has_clip = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		state = state_;
		error = error_;
		clip = last_clip_;
		has_clip = has_clip_;
	}

	std::string text;
	switch (state) {
	case State::Disabled:
		text = obs_module_text("Status.Disabled");
		break;
	case State::WaitingForSource:
		text = obs_module_text("Status.WaitingForSource");
		break;
	case State::WaitingForProgram:
		text = obs_module_text("Status.WaitingForProgram");
		break;
	case State::Running:
		text = obs_module_text("Status.Running");
		text += " (" + std::to_string(pipeline_.Width()) + "x" + std::to_string(pipeline_.Height()) + ")";
		break;
	case State::Error:
		text = obs_module_text("Status.Error");
		text += ": " + error;
		break;
	}

	if (has_clip) {
		text += "\n";
		text += obs_module_text("Status.LastClip");
		text += ": ";
		text += clip.empty() ? std::string(obs_module_text("Status.UnknownPath")) : clip;
	}
	return text;
}

FilterInstance *CreateInstance(obs_source_t *source, obs_data_t *settings)
{
	auto instance = std::make_shared<FilterInstance>(source);
	instance->Init(settings);

	std::lock_guard<std::mutex> lock(g_registry_mutex);
	g_instances.push_back(instance);
	return instance.get();
}

void DestroyInstance(FilterInstance *instance)
{
	std::shared_ptr<FilterInstance> owned;
	{
		std::lock_guard<std::mutex> lock(g_registry_mutex);
		auto it = std::find_if(g_instances.begin(), g_instances.end(),
				       [instance](const std::shared_ptr<FilterInstance> &item) {
					       return item.get() == instance;
				       });
		if (it != g_instances.end()) {
			owned = std::move(*it);
			g_instances.erase(it);
		}
	}
	if (owned)
		owned->Shutdown();
}

int SaveAllClips(bool native_bridge_only)
{
	std::vector<std::shared_ptr<FilterInstance>> snapshot;
	{
		std::lock_guard<std::mutex> lock(g_registry_mutex);
		snapshot = g_instances;
	}

	int saved = 0;
	for (const std::shared_ptr<FilterInstance> &instance : snapshot) {
		if (native_bridge_only && !instance->NativeBridgeEnabled())
			continue;
		if (instance->SaveClip())
			++saved;
	}
	return saved;
}

void StopAllInstances()
{
	std::vector<std::shared_ptr<FilterInstance>> snapshot;
	{
		std::lock_guard<std::mutex> lock(g_registry_mutex);
		snapshot = g_instances;
	}
	for (const std::shared_ptr<FilterInstance> &instance : snapshot)
		instance->StopForExit();
}

} // namespace bb

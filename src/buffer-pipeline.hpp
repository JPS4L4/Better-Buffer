#pragma once

#include <obs.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace bb {

struct PipelineConfig {
	std::string source_name;
	std::string encoder_id;
	int video_bitrate_kbps = 12000;
	int audio_bitrate_kbps = 192;
	int duration_sec = 30;
	uint32_t track_mask = 1;
	std::string directory;
	std::string format;
};

struct StartResult {
	enum class Code { Started, WaitingForSize, Failed };

	Code code = Code::Failed;
	std::string error;
};

struct PipelineCallbacks {
	// Called from the muxer thread once a clip is on disk. The path may be empty.
	std::function<void(const std::string &path)> saved;
	// Called from an output thread when the output stops by itself with an error.
	std::function<void(int code, const std::string &error)> stopped;
	// Called when the captured source is removed from OBS.
	std::function<void()> source_removed;
};

// Owns the private view, the encoders and the native replay_buffer output that
// record one source. Start() and Stop() are serialized; Save() and IsRunning()
// are cheap and may be called from any thread.
class BufferPipeline {
public:
	explicit BufferPipeline(PipelineCallbacks callbacks);
	~BufferPipeline();

	BufferPipeline(const BufferPipeline &) = delete;
	BufferPipeline &operator=(const BufferPipeline &) = delete;

	StartResult Start(obs_source_t *source, const PipelineConfig &config);
	void Stop();
	bool IsRunning() const;
	bool Save();
	void UpdatePaths(const std::string &directory, const std::string &format);

	uint32_t Width() const { return width_.load(); }
	uint32_t Height() const { return height_.load(); }

	// Size of the source as the recorder will use it (even, multiple of 4, 0 if unknown).
	static void QuerySourceSize(obs_source_t *source, uint32_t &width, uint32_t &height);

private:
	struct Resources {
		obs_source_t *source = nullptr;
		obs_view_t *view = nullptr;
		obs_encoder_t *video_encoder = nullptr;
		std::vector<obs_encoder_t *> audio_encoders;
		obs_output_t *output = nullptr;
	};

	static void OnSaved(void *param, calldata_t *data);
	static void OnStopped(void *param, calldata_t *data);
	static void OnSourceRemoved(void *param, calldata_t *data);

	void HandleSaved();
	void ReleaseShowingHold();
	void HoldSourceShowing(obs_source_t *source);
	void Teardown(Resources &res);

	PipelineCallbacks callbacks_;

	std::mutex lifecycle_mutex_;
	obs_weak_source_t *showing_hold_ = nullptr;

	mutable std::mutex state_mutex_;
	Resources res_;
	std::string applied_directory_;
	std::string applied_format_;

	std::atomic<obs_output_t *> signal_output_{nullptr};
	std::atomic<uint32_t> width_{0};
	std::atomic<uint32_t> height_{0};
};

} // namespace bb

#pragma once

#include <obs.h>

#include <string>

namespace bb {

// A private audio output that carries the sound of one source: the mix of its
// active children when it is a scene or group, otherwise its own audio. The
// audio thread of the output pulls the data, so nothing here takes a lock.
//
// Close() joins that audio thread; the object must outlive it and must be
// closed (and every encoder that reads Audio() released) before it is deleted.
class SceneAudio {
public:
	SceneAudio() = default;
	~SceneAudio();

	SceneAudio(const SceneAudio &) = delete;
	SceneAudio &operator=(const SceneAudio &) = delete;

	bool Open(obs_source_t *source, const std::string &name);
	void Close();

	audio_t *Audio() const { return audio_; }

private:
	static bool OnInput(void *param, uint64_t start_ts, uint64_t end_ts, uint64_t *out_ts, uint32_t mixers,
			    struct audio_output_data *mixes);

	void Mix(obs_source_t *source, uint64_t *out_ts, struct audio_output_data &mix) const;

	audio_t *audio_ = nullptr;
	obs_weak_source_t *weak_source_ = nullptr;
	std::string name_;
	uint32_t sample_rate_ = 0;
	speaker_layout source_layout_ = SPEAKERS_STEREO;
	bool raw_callback_added_ = false;
};

} // namespace bb

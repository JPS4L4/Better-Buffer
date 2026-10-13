#include "scene-audio.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <cstddef>

namespace bb {

namespace {

struct MixContext {
	uint64_t base_ts = 0;
	uint32_t sample_rate = 0;
	speaker_layout layout = SPEAKERS_STEREO;
	size_t channels = 2;
	float *left = nullptr;
	float *right = nullptr;
};

// Only leaf sources with audio contribute: a nested scene is a composite and
// its own children are visited by the tree walk, so counting it would double
// its sound.
bool IsAudioLeaf(obs_source_t *source)
{
	if (!source || obs_source_audio_pending(source))
		return false;
	const uint32_t flags = obs_source_get_output_flags(source);
	return (flags & OBS_SOURCE_AUDIO) != 0 && (flags & OBS_SOURCE_COMPOSITE) == 0;
}

void FindMinTimestamp(obs_source_t *, obs_source_t *child, void *param)
{
	if (!IsAudioLeaf(child))
		return;
	const uint64_t ts = obs_source_get_audio_timestamp(child);
	auto *min_ts = static_cast<uint64_t *>(param);
	if (ts && (*min_ts == 0 || ts < *min_ts))
		*min_ts = ts;
}

// Gain of each source channel into the left/right output, a plain fold-down.
void Downmix(speaker_layout layout, size_t channel, float &left, float &right)
{
	constexpr float kHalfPower = 0.707f;
	left = 0.0f;
	right = 0.0f;

	if (layout == SPEAKERS_MONO) {
		left = 1.0f;
		right = 1.0f;
		return;
	}
	if (channel == 0) {
		left = 1.0f;
		return;
	}
	if (channel == 1) {
		right = 1.0f;
		return;
	}

	switch (layout) {
	case SPEAKERS_4POINT0: // FL FR FC RC
		left = right = channel == 2 ? kHalfPower : 0.5f;
		break;
	case SPEAKERS_4POINT1: // FL FR FC LFE RC
		if (channel == 2)
			left = right = kHalfPower;
		else if (channel == 4)
			left = right = 0.5f;
		break;
	case SPEAKERS_5POINT1: // FL FR FC LFE RL RR
	case SPEAKERS_7POINT1: // FL FR FC LFE RL RR SL SR
		if (channel == 2) {
			left = right = kHalfPower;
		} else if (channel >= 4) {
			// Even indexes (RL, SL) are left, odd ones (RR, SR) are right.
			(channel % 2 == 0 ? left : right) = kHalfPower;
		}
		break;
	default: // 2.1: the LFE channel is dropped
		break;
	}
}

void MixChild(obs_source_t *, obs_source_t *child, void *param)
{
	auto *ctx = static_cast<MixContext *>(param);
	if (!IsAudioLeaf(child))
		return;

	const uint64_t ts = obs_source_get_audio_timestamp(child);
	if (ts == 0 || ts < ctx->base_ts)
		return;
	const size_t pos = static_cast<size_t>(ns_to_audio_frames(ctx->sample_rate, ts - ctx->base_ts));
	if (pos >= AUDIO_OUTPUT_FRAMES)
		return;
	const size_t count = AUDIO_OUTPUT_FRAMES - pos;

	// Mix 0 of a source already carries its own volume, mute and fades.
	struct obs_source_audio_mix mix;
	obs_source_get_audio_mix(child, &mix);
	for (size_t ch = 0; ch < ctx->channels && ch < MAX_AUDIO_CHANNELS; ++ch) {
		const float *in = mix.output[0].data[ch];
		if (!in)
			continue;
		float gain_left = 0.0f;
		float gain_right = 0.0f;
		Downmix(ctx->layout, ch, gain_left, gain_right);
		for (size_t i = 0; i < count; ++i) {
			ctx->left[pos + i] += in[i] * gain_left;
			ctx->right[pos + i] += in[i] * gain_right;
		}
	}
}

// The audio thread may hold the last reference to the source; destroying it
// there would end in Close() joining that same thread, so drop it on the UI thread.
void ReleaseSourceTask(void *param)
{
	obs_source_release(static_cast<obs_source_t *>(param));
}

// Keeps mix 0 of the main audio active, so sources always render track 1.
void KeepMixerActive(void *, size_t, struct audio_data *) {}

} // namespace

SceneAudio::~SceneAudio()
{
	Close();
}

bool SceneAudio::Open(obs_source_t *source, const std::string &name)
{
	Close();

	struct obs_audio_info oai = {};
	if (!source || !obs_get_audio_info(&oai))
		return false;

	weak_source_ = obs_source_get_weak_source(source);
	if (!weak_source_)
		return false;

	name_ = name;
	sample_rate_ = oai.samples_per_sec;
	source_layout_ = oai.speakers;

	struct audio_output_info info = {};
	info.name = name_.c_str();
	info.samples_per_sec = sample_rate_;
	info.format = AUDIO_FORMAT_FLOAT_PLANAR;
	info.speakers = SPEAKERS_STEREO;
	info.input_callback = OnInput;
	info.input_param = this;
	if (audio_output_open(&audio_, &info) != AUDIO_OUTPUT_SUCCESS) {
		audio_ = nullptr;
		obs_weak_source_release(weak_source_);
		weak_source_ = nullptr;
		return false;
	}

	obs_add_raw_audio_callback(0, nullptr, KeepMixerActive, this);
	raw_callback_added_ = true;
	return true;
}

void SceneAudio::Close()
{
	if (raw_callback_added_) {
		obs_remove_raw_audio_callback(0, KeepMixerActive, this);
		raw_callback_added_ = false;
	}
	if (audio_) {
		// Joins the audio thread: OnInput never runs after this returns.
		audio_output_close(audio_);
		audio_ = nullptr;
	}
	if (weak_source_) {
		obs_weak_source_release(weak_source_);
		weak_source_ = nullptr;
	}
}

bool SceneAudio::OnInput(void *param, uint64_t start_ts, uint64_t, uint64_t *out_ts, uint32_t mixers,
			 struct audio_output_data *mixes)
{
	const auto *self = static_cast<const SceneAudio *>(param);

	// Silence stamped with the output's own clock when there is nothing to play.
	*out_ts = start_ts;
	if ((mixers & 1) == 0)
		return true;

	obs_source_t *source = obs_weak_source_get_source(self->weak_source_);
	if (!source)
		return true;
	self->Mix(source, out_ts, mixes[0]);
	obs_queue_task(OBS_TASK_UI, ReleaseSourceTask, source, false);
	return true;
}

void SceneAudio::Mix(obs_source_t *source, uint64_t *out_ts, struct audio_output_data &mix) const
{
	if (!mix.data[0] || !mix.data[1] || obs_source_removed(source))
		return;

	MixContext ctx;
	ctx.sample_rate = sample_rate_;
	ctx.layout = source_layout_;
	ctx.channels = get_audio_channels(source_layout_);
	ctx.left = mix.data[0];
	ctx.right = mix.data[1];

	uint64_t min_ts = 0;
	const uint32_t flags = obs_source_get_output_flags(source);
	if ((flags & OBS_SOURCE_COMPOSITE) != 0) {
		obs_source_enum_active_tree(source, FindMinTimestamp, &min_ts);
		if (min_ts == 0)
			return;
		ctx.base_ts = min_ts;
		obs_source_enum_active_tree(source, MixChild, &ctx);
	} else {
		FindMinTimestamp(nullptr, source, &min_ts);
		if (min_ts == 0)
			return;
		ctx.base_ts = min_ts;
		MixChild(nullptr, source, &ctx);
	}
	*out_ts = min_ts;
}

} // namespace bb

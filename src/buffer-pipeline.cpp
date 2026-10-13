#include "buffer-pipeline.hpp"

#include "scene-audio.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <cstring>
#include <exception>
#include <utility>

namespace bb {

namespace {

std::string FindAacEncoderId()
{
	const char *id = nullptr;
	std::string fallback;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
		if (!id || obs_get_encoder_type(id) != OBS_ENCODER_AUDIO)
			continue;
		if (std::strcmp(id, "ffmpeg_aac") == 0)
			return id;
		const char *codec = obs_get_encoder_codec(id);
		if (fallback.empty() && codec && std::strcmp(codec, "AAC") == 0)
			fallback = id;
	}
	return fallback;
}

} // namespace

BufferPipeline::BufferPipeline(PipelineCallbacks callbacks) : callbacks_(std::move(callbacks)) {}

BufferPipeline::~BufferPipeline()
{
	Stop();
}

void BufferPipeline::QuerySourceSize(obs_source_t *source, uint32_t &width, uint32_t &height)
{
	width = 0;
	height = 0;
	if (!source)
		return;

	// obs_source_get_width() includes the filter chain, which is what the view renders.
	width = obs_source_get_width(source) & ~3u;
	height = obs_source_get_height(source) & ~3u;
	if (width == 0 || height == 0) {
		width = 0;
		height = 0;
	}
}

void BufferPipeline::HoldSourceShowing(obs_source_t *source)
{
	if (showing_hold_)
		return;
	showing_hold_ = obs_source_get_weak_source(source);
	if (showing_hold_)
		obs_source_inc_showing(source);
}

void BufferPipeline::ReleaseShowingHold()
{
	if (!showing_hold_)
		return;
	obs_source_t *source = obs_weak_source_get_source(showing_hold_);
	if (source) {
		obs_source_dec_showing(source);
		obs_source_release(source);
	}
	obs_weak_source_release(showing_hold_);
	showing_hold_ = nullptr;
}

StartResult BufferPipeline::Start(obs_source_t *source, const PipelineConfig &config)
{
	std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);

	StartResult result;
	if (!source) {
		result.error = "No source to record";
		return result;
	}

	// Release anything left by a previous run (for example an output that stopped on its own).
	Resources stale;
	{
		std::lock_guard<std::mutex> state(state_mutex_);
		stale = res_;
		res_ = Resources();
	}
	Teardown(stale);

	uint32_t width = 0;
	uint32_t height = 0;
	QuerySourceSize(source, width, height);
	if (width == 0 || height == 0) {
		// Some sources only report a size while they are shown somewhere.
		HoldSourceShowing(source);
		result.code = StartResult::Code::WaitingForSize;
		return result;
	}

	struct obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi)) {
		result.error = "Video is not initialized";
		return result;
	}
	ovi.base_width = width;
	ovi.base_height = height;
	ovi.output_width = width;
	ovi.output_height = height;

	Resources res;
	res.source = source;

	res.view = obs_view_create();
	if (!res.view) {
		result.error = "Could not create the capture view";
		return result;
	}
	obs_view_set_source(res.view, 0, source);
	video_t *video = obs_view_add2(res.view, &ovi);
	if (!video) {
		result.error = "Could not create the capture video";
		Teardown(res);
		return result;
	}
	ReleaseShowingHold();

	const std::string name_suffix = "Better Buffer - " + config.source_name;

	obs_data_t *video_settings = obs_data_create();
	obs_data_set_string(video_settings, "rate_control", "CBR");
	obs_data_set_int(video_settings, "bitrate", config.video_bitrate_kbps);
	obs_data_set_int(video_settings, "keyint_sec", 2);
	if (config.encoder_id == "obs_x264") {
		obs_data_set_string(video_settings, "preset", "veryfast");
		obs_data_set_string(video_settings, "profile", "high");
	}
	res.video_encoder = obs_video_encoder_create(config.encoder_id.c_str(), (name_suffix + " video").c_str(),
						     video_settings, nullptr);
	obs_data_release(video_settings);
	if (!res.video_encoder) {
		result.error = "Could not create the video encoder '" + config.encoder_id + "'";
		Teardown(res);
		return result;
	}
	obs_encoder_set_video(res.video_encoder, video);

	const std::string aac_id = FindAacEncoderId();
	if (aac_id.empty()) {
		result.error = "No AAC audio encoder is available";
		Teardown(res);
		return result;
	}

	// The private audio output must exist before the encoder that reads it.
	audio_t *audio = nullptr;
	std::vector<int> tracks;
	if (config.audio_mode == AudioMode::SceneSource) {
		res.scene_audio = new SceneAudio();
		if (!res.scene_audio->Open(source, name_suffix)) {
			result.error = "Could not create the audio output for the source";
			Teardown(res);
			return result;
		}
		audio = res.scene_audio->Audio();
		tracks.push_back(0);
	} else {
		audio = obs_get_audio();
		for (int track = 0; track < kMaxTracks; ++track) {
			if ((config.track_mask & (1u << track)) != 0)
				tracks.push_back(track);
		}
	}
	if (!audio) {
		result.error = "Audio is not initialized";
		Teardown(res);
		return result;
	}
	for (int track : tracks) {
		obs_data_t *audio_settings = obs_data_create();
		obs_data_set_string(audio_settings, "rate_control", "CBR");
		obs_data_set_int(audio_settings, "bitrate", config.audio_bitrate_kbps);
		const std::string audio_name = name_suffix + " audio " + std::to_string(track + 1);
		obs_encoder_t *encoder = obs_audio_encoder_create(aac_id.c_str(), audio_name.c_str(), audio_settings,
								  static_cast<size_t>(track), nullptr);
		obs_data_release(audio_settings);
		if (!encoder) {
			result.error = "Could not create the audio encoder for track " + std::to_string(track + 1);
			Teardown(res);
			return result;
		}
		obs_encoder_set_audio(encoder, audio);
		res.audio_encoders.push_back(encoder);
	}

	obs_data_t *output_settings = obs_data_create();
	obs_data_set_int(output_settings, "max_time_sec", config.duration_sec);
	obs_data_set_int(output_settings, "max_size_mb", 0);
	obs_data_set_string(output_settings, "directory", config.directory.c_str());
	obs_data_set_string(output_settings, "format", config.format.c_str());
	obs_data_set_string(output_settings, "extension", "mp4");
	obs_data_set_bool(output_settings, "allow_spaces", true);
	res.output = obs_output_create("replay_buffer", name_suffix.c_str(), output_settings, nullptr);
	obs_data_release(output_settings);
	if (!res.output) {
		result.error = "Could not create the replay buffer output";
		Teardown(res);
		return result;
	}

	obs_output_set_video_encoder(res.output, res.video_encoder);
	for (size_t i = 0; i < res.audio_encoders.size(); ++i)
		obs_output_set_audio_encoder(res.output, res.audio_encoders[i], i);

	signal_handler_t *output_signals = obs_output_get_signal_handler(res.output);
	signal_handler_connect(output_signals, "saved", OnSaved, this);
	signal_handler_connect(output_signals, "stop", OnStopped, this);
	signal_handler_connect(obs_source_get_signal_handler(source), "remove", OnSourceRemoved, this);
	signal_output_.store(res.output);

	if (!obs_output_start(res.output)) {
		const char *last_error = obs_output_get_last_error(res.output);
		result.error = (last_error && *last_error) ? last_error : "The replay buffer failed to start";
		Teardown(res);
		return result;
	}

	width_.store(width);
	height_.store(height);
	{
		std::lock_guard<std::mutex> state(state_mutex_);
		res_ = res;
		applied_directory_ = config.directory;
		applied_format_ = config.format;
	}

	obs_log(LOG_INFO, "Recording '%s' at %ux%u (encoder %s, %d kbps, %d s, %s audio, %zu track(s))",
		config.source_name.c_str(), width, height, config.encoder_id.c_str(), config.video_bitrate_kbps,
		config.duration_sec, config.audio_mode == AudioMode::SceneSource ? "source" : "mixer",
		res.audio_encoders.size());

	result.code = StartResult::Code::Started;
	return result;
}

void BufferPipeline::Teardown(Resources &res)
{
	if (res.output) {
		signal_handler_t *output_signals = obs_output_get_signal_handler(res.output);
		signal_handler_disconnect(output_signals, "saved", OnSaved, this);
		signal_handler_disconnect(output_signals, "stop", OnStopped, this);
	}
	if (res.source)
		signal_handler_disconnect(obs_source_get_signal_handler(res.source), "remove", OnSourceRemoved, this);

	if (res.output) {
		if (obs_output_active(res.output))
			obs_output_force_stop(res.output);
		// Releasing the last reference joins the muxer thread, so no signal can fire afterwards.
		obs_output_release(res.output);
		signal_output_.store(nullptr);
		res.output = nullptr;
	}

	for (obs_encoder_t *encoder : res.audio_encoders)
		obs_encoder_release(encoder);
	res.audio_encoders.clear();

	// Only after the encoders are gone: closing joins the audio thread they read from.
	if (res.scene_audio) {
		res.scene_audio->Close();
		delete res.scene_audio;
		res.scene_audio = nullptr;
	}

	if (res.video_encoder) {
		obs_encoder_release(res.video_encoder);
		res.video_encoder = nullptr;
	}

	if (res.view) {
		obs_view_remove(res.view);
		obs_view_set_source(res.view, 0, nullptr);
		obs_view_destroy(res.view);
		res.view = nullptr;
	}
	res.source = nullptr;
}

void BufferPipeline::Stop()
{
	std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);

	Resources res;
	{
		std::lock_guard<std::mutex> state(state_mutex_);
		res = res_;
		res_ = Resources();
	}
	Teardown(res);
	ReleaseShowingHold();
	width_.store(0);
	height_.store(0);
}

bool BufferPipeline::IsRunning() const
{
	std::lock_guard<std::mutex> state(state_mutex_);
	return res_.output && obs_output_active(res_.output);
}

bool BufferPipeline::Save()
{
	std::lock_guard<std::mutex> state(state_mutex_);
	if (!res_.output || !obs_output_active(res_.output))
		return false;

	proc_handler_t *handler = obs_output_get_proc_handler(res_.output);
	if (!handler)
		return false;

	calldata_t data;
	calldata_init(&data);
	const bool called = proc_handler_call(handler, "save", &data);
	calldata_free(&data);
	return called;
}

void BufferPipeline::UpdatePaths(const std::string &directory, const std::string &format)
{
	std::lock_guard<std::mutex> state(state_mutex_);
	if (!res_.output || (directory == applied_directory_ && format == applied_format_))
		return;

	// The replay buffer reads directory and format again every time a clip is written.
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "directory", directory.c_str());
	obs_data_set_string(settings, "format", format.c_str());
	obs_output_update(res_.output, settings);
	obs_data_release(settings);
	applied_directory_ = directory;
	applied_format_ = format;
}

void BufferPipeline::HandleSaved()
{
	obs_output_t *output = signal_output_.load();
	std::string path;
	if (output) {
		proc_handler_t *handler = obs_output_get_proc_handler(output);
		calldata_t data;
		calldata_init(&data);
		if (handler && proc_handler_call(handler, "get_last_replay", &data)) {
			const char *value = calldata_string(&data, "path");
			if (value)
				path = value;
		}
		calldata_free(&data);
	}

	if (callbacks_.saved)
		callbacks_.saved(path);
}

void BufferPipeline::OnSaved(void *param, calldata_t *)
{
	try {
		static_cast<BufferPipeline *>(param)->HandleSaved();
	} catch (const std::exception &e) {
		obs_log(LOG_WARNING, "Error while handling a saved clip: %s", e.what());
	}
}

void BufferPipeline::OnStopped(void *param, calldata_t *data)
{
	const auto code = static_cast<int>(calldata_int(data, "code"));
	if (code == 0)
		return;

	const char *error = calldata_string(data, "last_error");
	auto *self = static_cast<BufferPipeline *>(param);
	try {
		if (self->callbacks_.stopped)
			self->callbacks_.stopped(code, error ? error : "");
	} catch (const std::exception &e) {
		obs_log(LOG_WARNING, "Error while handling an output stop: %s", e.what());
	}
}

void BufferPipeline::OnSourceRemoved(void *param, calldata_t *)
{
	auto *self = static_cast<BufferPipeline *>(param);
	try {
		if (self->callbacks_.source_removed)
			self->callbacks_.source_removed();
	} catch (const std::exception &e) {
		obs_log(LOG_WARNING, "Error while handling a source removal: %s", e.what());
	}
}

} // namespace bb

/*
Cube Music Visualizer for OBS Studio
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <plugin-support.h>

#include <graphics/vec4.h>
#include <media-io/audio-io.h>
#include <pthread.h>
#include <math.h>
#include <string.h>

#define SOURCE_ID "cube_music_visualizer"
#define FFT_SIZE 2048
#define MAX_BANDS 64
#define RING_SIZE 8192
#define PI_F 3.14159265358979323846f

#define S_AUDIO_SOURCE "audio_source"
#define S_WIDTH "width"
#define S_HEIGHT "height"
#define S_BANDS "bands"
#define S_MAX_CUBES "max_cubes"
#define S_CUBE_SIZE "cube_size"
#define S_GAP "gap"
#define S_RIGHT_MARGIN "right_margin"
#define S_COLOR "color"
#define S_SENSITIVITY "sensitivity"
#define S_ATTACK "attack"
#define S_RELEASE "release"

struct cube_visualizer {
	obs_source_t *source;
	obs_source_t *audio_source;
	pthread_mutex_t audio_mutex;
	float ring[RING_SIZE];
	size_t write_pos;
	size_t sample_count;
	uint64_t total_samples;
	uint64_t analyzed_samples;
	float window[FFT_SIZE];
	float levels[MAX_BANDS];

	uint32_t width;
	uint32_t height;
	uint32_t bands;
	uint32_t max_cubes;
	uint32_t cube_size;
	uint32_t gap;
	uint32_t right_margin;
	uint32_t color;
	float sensitivity;
	float attack;
	float release;
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static const char *cube_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("CubeVisualizer");
}

static inline float clampf(float value, float low, float high)
{
	return value < low ? low : (value > high ? high : value);
}

static void capture_audio(void *param, obs_source_t *source, const struct audio_data *audio, bool muted)
{
	struct cube_visualizer *cv = param;
	UNUSED_PARAMETER(source);

	if (!audio || audio->frames == 0)
		return;

	const size_t channels = audio_output_get_channels(obs_get_audio());
	const size_t usable_channels = channels > MAX_AV_PLANES ? MAX_AV_PLANES : channels;

	pthread_mutex_lock(&cv->audio_mutex);
	for (uint32_t frame = 0; frame < audio->frames; frame++) {
		float mono = 0.0f;
		size_t active = 0;

		if (!muted) {
			for (size_t channel = 0; channel < usable_channels; channel++) {
				if (audio->data[channel]) {
					mono += ((const float *)audio->data[channel])[frame];
					active++;
				}
			}
		}

		if (active)
			mono /= (float)active;

		cv->ring[cv->write_pos] = mono;
		cv->write_pos = (cv->write_pos + 1) % RING_SIZE;
		if (cv->sample_count < RING_SIZE)
			cv->sample_count++;
		cv->total_samples++;
	}
	pthread_mutex_unlock(&cv->audio_mutex);
}

static void detach_audio_source(struct cube_visualizer *cv)
{
	if (!cv->audio_source)
		return;

	obs_source_remove_audio_capture_callback(cv->audio_source, capture_audio, cv);
	obs_source_release(cv->audio_source);
	cv->audio_source = NULL;
}

static void attach_audio_source(struct cube_visualizer *cv, const char *name)
{
	obs_source_t *next = (name && *name) ? obs_get_source_by_name(name) : NULL;
	if (next == cv->source) {
		obs_source_release(next);
		next = NULL;
	}

	if (next == cv->audio_source) {
		obs_source_release(next);
		return;
	}

	detach_audio_source(cv);
	cv->audio_source = next;
	if (cv->audio_source)
		obs_source_add_audio_capture_callback(cv->audio_source, capture_audio, cv);

	pthread_mutex_lock(&cv->audio_mutex);
	memset(cv->ring, 0, sizeof(cv->ring));
	cv->write_pos = 0;
	cv->sample_count = 0;
	cv->total_samples = 0;
	cv->analyzed_samples = 0;
	pthread_mutex_unlock(&cv->audio_mutex);
	memset(cv->levels, 0, sizeof(cv->levels));
}

static void cube_update(void *data, obs_data_t *settings)
{
	struct cube_visualizer *cv = data;
	cv->width = (uint32_t)obs_data_get_int(settings, S_WIDTH);
	cv->height = (uint32_t)obs_data_get_int(settings, S_HEIGHT);
	cv->bands = (uint32_t)obs_data_get_int(settings, S_BANDS);
	cv->max_cubes = (uint32_t)obs_data_get_int(settings, S_MAX_CUBES);
	cv->cube_size = (uint32_t)obs_data_get_int(settings, S_CUBE_SIZE);
	cv->gap = (uint32_t)obs_data_get_int(settings, S_GAP);
	cv->right_margin = (uint32_t)obs_data_get_int(settings, S_RIGHT_MARGIN);
	cv->color = (uint32_t)obs_data_get_int(settings, S_COLOR);
	cv->sensitivity = (float)obs_data_get_double(settings, S_SENSITIVITY);
	cv->attack = (float)obs_data_get_double(settings, S_ATTACK);
	cv->release = (float)obs_data_get_double(settings, S_RELEASE);

	if (cv->bands < 4)
		cv->bands = 4;
	if (cv->bands > MAX_BANDS)
		cv->bands = MAX_BANDS;

	attach_audio_source(cv, obs_data_get_string(settings, S_AUDIO_SOURCE));
}

static void *cube_create(obs_data_t *settings, obs_source_t *source)
{
	struct cube_visualizer *cv = bzalloc(sizeof(*cv));
	cv->source = source;
	pthread_mutex_init(&cv->audio_mutex, NULL);

	for (size_t i = 0; i < FFT_SIZE; i++)
		cv->window[i] = 0.5f - 0.5f * cosf((2.0f * PI_F * (float)i) / (float)(FFT_SIZE - 1));

	cube_update(cv, settings);
	return cv;
}

static void cube_destroy(void *data)
{
	struct cube_visualizer *cv = data;
	detach_audio_source(cv);
	pthread_mutex_destroy(&cv->audio_mutex);
	bfree(cv);
}

static void cube_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, S_AUDIO_SOURCE, "");
	obs_data_set_default_int(settings, S_WIDTH, 900);
	obs_data_set_default_int(settings, S_HEIGHT, 720);
	obs_data_set_default_int(settings, S_BANDS, 34);
	obs_data_set_default_int(settings, S_MAX_CUBES, 30);
	obs_data_set_default_int(settings, S_CUBE_SIZE, 14);
	obs_data_set_default_int(settings, S_GAP, 5);
	obs_data_set_default_int(settings, S_RIGHT_MARGIN, 18);
	obs_data_set_default_int(settings, S_COLOR, 0xFFFFFFFF);
	obs_data_set_default_double(settings, S_SENSITIVITY, 4.0);
	obs_data_set_default_double(settings, S_ATTACK, 0.58);
	obs_data_set_default_double(settings, S_RELEASE, 0.16);
}

struct source_list_info {
	obs_property_t *list;
	obs_source_t *self;
};

static bool add_audio_source(void *data, obs_source_t *source)
{
	struct source_list_info *info = data;
	if (source == info->self)
		return true;
	if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
		return true;

	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(info->list, name, name);
	return true;
}

static obs_properties_t *cube_properties(void *data)
{
	struct cube_visualizer *cv = data;
	obs_properties_t *props = obs_properties_create();
	obs_property_t *audio = obs_properties_add_list(props, S_AUDIO_SOURCE, obs_module_text("AudioSource"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(audio, obs_module_text("None"), "");
	struct source_list_info info = {audio, cv ? cv->source : NULL};
	obs_enum_sources(add_audio_source, &info);

	obs_properties_add_int(props, S_WIDTH, obs_module_text("Width"), 64, 3840, 1);
	obs_properties_add_int(props, S_HEIGHT, obs_module_text("Height"), 64, 2160, 1);
	obs_properties_add_int_slider(props, S_BANDS, obs_module_text("Bands"), 4, MAX_BANDS, 1);
	obs_properties_add_int_slider(props, S_MAX_CUBES, obs_module_text("MaxCubes"), 1, 100, 1);
	obs_properties_add_int_slider(props, S_CUBE_SIZE, obs_module_text("CubeSize"), 2, 80, 1);
	obs_properties_add_int_slider(props, S_GAP, obs_module_text("Gap"), 0, 40, 1);
	obs_properties_add_int_slider(props, S_RIGHT_MARGIN, obs_module_text("RightMargin"), 0, 500, 1);
	obs_properties_add_color_alpha(props, S_COLOR, obs_module_text("Color"));

	obs_property_t *gain = obs_properties_add_float_slider(props, S_SENSITIVITY,
							      obs_module_text("Sensitivity"), -20.0, 30.0, 0.5);
	obs_property_float_set_suffix(gain, " dB");
	obs_properties_add_float_slider(props, S_ATTACK, obs_module_text("Attack"), 0.05, 1.0, 0.01);
	obs_properties_add_float_slider(props, S_RELEASE, obs_module_text("Release"), 0.01, 0.5, 0.01);
	return props;
}

static void calculate_bands(struct cube_visualizer *cv, const float *samples, uint32_t sample_rate)
{
	const float min_frequency = 45.0f;
	const float max_frequency = fminf(16000.0f, (float)sample_rate * 0.45f);
	const float ratio = max_frequency / min_frequency;

	for (uint32_t band = 0; band < cv->bands; band++) {
		const float position = ((float)band + 0.5f) / (float)cv->bands;
		const float frequency = min_frequency * powf(ratio, position);
		const float omega = 2.0f * PI_F * frequency / (float)sample_rate;
		const float coefficient = 2.0f * cosf(omega);
		float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;

		for (size_t i = 0; i < FFT_SIZE; i++) {
			s0 = samples[i] * cv->window[i] + coefficient * s1 - s2;
			s2 = s1;
			s1 = s0;
		}

		const float power = fmaxf(s1 * s1 + s2 * s2 - coefficient * s1 * s2, 0.0f);
		const float magnitude = sqrtf(power) / ((float)FFT_SIZE * 0.5f);
		const float db = 20.0f * log10f(magnitude + 1.0e-9f) + cv->sensitivity;
		const float target = clampf((db + 72.0f) / 54.0f, 0.0f, 1.0f);
		const float smoothing = target > cv->levels[band] ? cv->attack : cv->release;
		cv->levels[band] += (target - cv->levels[band]) * smoothing;
	}
}

static void cube_tick(void *data, float seconds)
{
	struct cube_visualizer *cv = data;
	float samples[FFT_SIZE];
	bool enough_samples = false;
	UNUSED_PARAMETER(seconds);

	pthread_mutex_lock(&cv->audio_mutex);
	if (cv->sample_count >= FFT_SIZE && cv->total_samples != cv->analyzed_samples) {
		size_t start = (cv->write_pos + RING_SIZE - FFT_SIZE) % RING_SIZE;
		for (size_t i = 0; i < FFT_SIZE; i++)
			samples[i] = cv->ring[(start + i) % RING_SIZE];
		cv->analyzed_samples = cv->total_samples;
		enough_samples = true;
	}
	pthread_mutex_unlock(&cv->audio_mutex);

	if (enough_samples) {
		calculate_bands(cv, samples, audio_output_get_sample_rate(obs_get_audio()));
	} else {
		for (uint32_t band = 0; band < cv->bands; band++)
			cv->levels[band] *= 1.0f - cv->release;
	}
}

static void add_rectangle(float x, float y, float width, float height, uint32_t color)
{
	gs_color(color);
	gs_vertex2f(x, y);
	gs_color(color);
	gs_vertex2f(x + width, y);
	gs_color(color);
	gs_vertex2f(x + width, y + height);

	gs_color(color);
	gs_vertex2f(x, y);
	gs_color(color);
	gs_vertex2f(x + width, y + height);
	gs_color(color);
	gs_vertex2f(x, y + height);
}

static void cube_render(void *data, gs_effect_t *unused)
{
	struct cube_visualizer *cv = data;
	UNUSED_PARAMETER(unused);

	const float pitch = (float)(cv->cube_size + cv->gap);
	const float total_height = (float)cv->bands * pitch - (float)cv->gap;
	const float top = fmaxf(((float)cv->height - total_height) * 0.5f, 0.0f);
	const uint32_t fit = pitch > 0.0f && cv->width > cv->right_margin
				     ? (uint32_t)(((float)(cv->width - cv->right_margin)) / pitch)
				     : 0;
	const uint32_t maximum = cv->max_cubes < fit ? cv->max_cubes : fit;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
	struct vec4 white;
	vec4_set(&white, 1.0f, 1.0f, 1.0f, 1.0f);
	gs_effect_set_vec4(color_param, &white);

	while (gs_effect_loop(solid, "SolidColored")) {
		gs_render_start(true);
		for (uint32_t band = 0; band < cv->bands; band++) {
			const float shaped = powf(clampf(cv->levels[band], 0.0f, 1.0f), 0.82f);
			const uint32_t count = (uint32_t)lroundf(shaped * (float)maximum);
			const float y = (float)cv->height - top - (float)(band + 1) * pitch + (float)cv->gap;

			for (uint32_t cube = 0; cube < count; cube++) {
				const float x = (float)cv->width - (float)cv->right_margin -
						(float)cv->cube_size - (float)cube * pitch;
				add_rectangle(x, y, (float)cv->cube_size, (float)cv->cube_size, cv->color);
			}
		}
		gs_render_stop(GS_TRIS);
	}
}

static uint32_t cube_width(void *data)
{
	return ((struct cube_visualizer *)data)->width;
}

static uint32_t cube_height(void *data)
{
	return ((struct cube_visualizer *)data)->height;
}

static struct obs_source_info cube_source_info = {
	.id = SOURCE_ID,
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.get_name = cube_get_name,
	.create = cube_create,
	.destroy = cube_destroy,
	.update = cube_update,
	.get_defaults = cube_defaults,
	.get_properties = cube_properties,
	.video_tick = cube_tick,
	.video_render = cube_render,
	.get_width = cube_width,
	.get_height = cube_height,
	.icon_type = OBS_ICON_TYPE_COLOR,
};

bool obs_module_load(void)
{
	obs_register_source(&cube_source_info);
	obs_log(LOG_INFO, "Cube Music Visualizer loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "Cube Music Visualizer unloaded");
}

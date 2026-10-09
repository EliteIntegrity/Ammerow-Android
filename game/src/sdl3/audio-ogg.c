/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/audio-ogg.c
 * \brief Ogg Vorbis decoding and streaming for SDL3 presentation audio.
 *
 * Uses SDL_mixer's SDL-adapted copy of stb_vorbis (public domain), compiled
 * here with SDL_mixer's own configuration, so file access goes through
 * SDL_IOStream and allocation through SDL.
 */

#include "sdl3/audio-ogg.h"

#include <SDL3/SDL.h>

#define STB_VORBIS_SDL 1  /* SDL_mixer's SDL_IOStream patches */
#define STB_VORBIS_NO_STDIO 1
#define STB_VORBIS_NO_CRT 1
#define STB_VORBIS_NO_PUSHDATA_API 1
#define STB_VORBIS_MAX_CHANNELS 8
#define STB_FORCEINLINE SDL_FORCE_INLINE
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define STB_VORBIS_BIG_ENDIAN 1
#endif
#define STBV_CDECL SDLCALL

#ifdef assert
#undef assert
#endif
#ifdef memset
#undef memset
#endif
#ifdef memcpy
#undef memcpy
#endif
#define assert SDL_assert
#define memset SDL_memset
#define memcmp SDL_memcmp
#define memcpy SDL_memcpy
#define qsort SDL_qsort
#define malloc SDL_malloc
#define realloc SDL_realloc
#define free SDL_free

#define pow SDL_pow
#define floor SDL_floor
#define ldexp(v, e) SDL_scalbn((v), (e))
#define abs(x) SDL_abs(x)
#define cos(x) SDL_cos(x)
#define sin(x) SDL_sin(x)
#define log(x) SDL_log(x)
#define exp(x) SDL_exp(x)

#define STB_VORBIS_STDINT_DEFINED 1
typedef Uint8 uint8;
typedef Sint8 int8;
typedef Uint16 uint16;
typedef Sint16 int16;
typedef Uint32 uint32;
typedef Sint32 int32;

#include "stb_vorbis/stb_vorbis.h"

#define SDL3_AUDIO_OGG_FREQUENCY 48000
#define SDL3_AUDIO_OGG_CHANNELS 2

struct sdl3_audio_ogg_stream {
	stb_vorbis *vorbis;
};

static stb_vorbis *open_vorbis(const char *path)
{
	SDL_IOStream *io;
	stb_vorbis *vorbis;
	int error = 0;

	if (!path || !path[0]) {
		SDL_InvalidParamError("path");
		return NULL;
	}
	io = SDL_IOFromFile(path, "rb");
	if (!io) return NULL;
	vorbis = stb_vorbis_open_io(io, 1, &error, NULL);
	if (!vorbis) {
		SDL_CloseIO(io);
		SDL_SetError("Could not open Ogg Vorbis cue (stb_vorbis error %d)",
			error);
	}
	return vorbis;
}

bool sdl3_audio_ogg_decode(const char *path, float **pcm, size_t *frame_count)
{
	stb_vorbis *vorbis;
	stb_vorbis_info info;
	float *source = NULL;
	size_t frames = 0;
	size_t capacity;
	SDL_AudioSpec from = { 0 };
	SDL_AudioSpec to = { 0 };
	Uint8 *converted = NULL;
	int converted_length = 0;
	const int frame_bytes = (int)(sizeof(float) * SDL3_AUDIO_OGG_CHANNELS);

	if (!pcm) return SDL_InvalidParamError("pcm");
	if (!frame_count) return SDL_InvalidParamError("frame_count");
	*pcm = NULL;
	*frame_count = 0;
	vorbis = open_vorbis(path);
	if (!vorbis) return false;
	info = stb_vorbis_get_info(vorbis);
	if (info.channels < 1 || info.channels > 2 || info.sample_rate == 0) {
		stb_vorbis_close(vorbis);
		return SDL_SetError("Authored Ogg cue has an unsupported layout");
	}
	capacity = stb_vorbis_stream_length_in_samples(vorbis);
	if (!capacity) capacity = info.sample_rate;
	source = SDL_malloc(capacity * sizeof(float) * (size_t)info.channels);
	if (!source) {
		stb_vorbis_close(vorbis);
		return false;
	}
	for (;;) {
		int read;

		if (frames == capacity) {
			float *grown;

			capacity *= 2;
			grown = SDL_realloc(source,
				capacity * sizeof(float) * (size_t)info.channels);
			if (!grown) {
				SDL_free(source);
				stb_vorbis_close(vorbis);
				return false;
			}
			source = grown;
		}
		read = stb_vorbis_get_samples_float_interleaved(vorbis, info.channels,
			source + frames * (size_t)info.channels,
			(int)((capacity - frames) * (size_t)info.channels));
		if (read <= 0) break;
		frames += (size_t)read;
	}
	stb_vorbis_close(vorbis);
	if (!frames) {
		SDL_free(source);
		return SDL_SetError("Authored Ogg cue is empty");
	}
	if (info.channels == SDL3_AUDIO_OGG_CHANNELS &&
			info.sample_rate == SDL3_AUDIO_OGG_FREQUENCY) {
		*pcm = source;
		*frame_count = frames;
		return true;
	}

	/* Mono, or another rate: convert once, as the WAV loader does. */
	from.format = SDL_AUDIO_F32;
	from.channels = info.channels;
	from.freq = (int)info.sample_rate;
	to.format = SDL_AUDIO_F32;
	to.channels = SDL3_AUDIO_OGG_CHANNELS;
	to.freq = SDL3_AUDIO_OGG_FREQUENCY;
	if (!SDL_ConvertAudioSamples(&from, (const Uint8 *)source,
			(int)(frames * sizeof(float) * (size_t)info.channels), &to,
			&converted, &converted_length)) {
		SDL_free(source);
		return false;
	}
	SDL_free(source);
	if (converted_length <= 0 || converted_length % frame_bytes != 0) {
		SDL_free(converted);
		return SDL_SetError("Authored Ogg cue decoded to an invalid length");
	}
	*pcm = (float *)converted;
	*frame_count = (size_t)(converted_length / frame_bytes);
	return true;
}

struct sdl3_audio_ogg_stream *sdl3_audio_ogg_stream_open(const char *path)
{
	struct sdl3_audio_ogg_stream *stream;
	stb_vorbis *vorbis = open_vorbis(path);
	stb_vorbis_info info;

	if (!vorbis) return NULL;
	info = stb_vorbis_get_info(vorbis);
	if (info.channels != SDL3_AUDIO_OGG_CHANNELS ||
			info.sample_rate != SDL3_AUDIO_OGG_FREQUENCY) {
		stb_vorbis_close(vorbis);
		SDL_SetError("Streamed Ogg cues must be 48 kHz stereo");
		return NULL;
	}
	stream = SDL_calloc(1, sizeof(*stream));
	if (!stream) {
		stb_vorbis_close(vorbis);
		return NULL;
	}
	stream->vorbis = vorbis;
	return stream;
}

size_t sdl3_audio_ogg_stream_read(struct sdl3_audio_ogg_stream *stream,
		float *out, size_t frame_count, bool loop)
{
	size_t written = 0;
	bool restarted = false;

	if (!stream || !stream->vorbis || !out) return 0;
	while (written < frame_count) {
		int read = stb_vorbis_get_samples_float_interleaved(stream->vorbis,
			SDL3_AUDIO_OGG_CHANNELS, out + written * SDL3_AUDIO_OGG_CHANNELS,
			(int)((frame_count - written) * SDL3_AUDIO_OGG_CHANNELS));

		if (read > 0) {
			written += (size_t)read;
			restarted = false;
			continue;
		}
		/* End of stream (or an error). Restart a loop once; a second empty
		 * read straight after a restart means the file yields nothing. */
		if (!loop || restarted || !stb_vorbis_seek_start(stream->vorbis)) {
			break;
		}
		restarted = true;
	}
	return written;
}

void sdl3_audio_ogg_stream_rewind(struct sdl3_audio_ogg_stream *stream)
{
	if (stream && stream->vorbis) (void)stb_vorbis_seek_start(stream->vorbis);
}

void sdl3_audio_ogg_stream_close(struct sdl3_audio_ogg_stream *stream)
{
	if (!stream) return;
	if (stream->vorbis) stb_vorbis_close(stream->vorbis);
	SDL_free(stream);
}

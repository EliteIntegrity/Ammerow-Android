/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/audio-ogg.h
 * \brief Ogg Vorbis decoding and streaming for SDL3 presentation audio.
 *
 * Builds that package compressed audio (SDL3_AUDIO_OGG) ship each authored
 * cue as .ogg instead of .wav. Effects decode once into the mixer's
 * 48 kHz float stereo format, as WAVs do; long background loops and music
 * are streamed from the file while they play so that only a small decoder
 * state stays in memory.
 */

#ifndef INCLUDED_SDL3_AUDIO_OGG_H
#define INCLUDED_SDL3_AUDIO_OGG_H

#include <stdbool.h>
#include <stddef.h>

struct sdl3_audio_ogg_stream;

/**
 * Decode a whole Ogg Vorbis file to interleaved 48 kHz float stereo. The
 * caller releases the buffer with sdl3_audio_wav_free() (SDL_free()).
 */
bool sdl3_audio_ogg_decode(const char *path, float **pcm, size_t *frame_count);

/**
 * Open a file for streaming. Only 48 kHz stereo files stream directly, since
 * the mixer reads them without resampling; returns NULL otherwise (the
 * caller then decodes the whole file).
 */
struct sdl3_audio_ogg_stream *sdl3_audio_ogg_stream_open(const char *path);

/**
 * Read up to frame_count interleaved stereo float frames; when loop is set,
 * the stream restarts at the end so the buffer is always filled. Returns the
 * frames written (fewer than requested only at the end of a non-looping
 * stream or on a decode error).
 */
size_t sdl3_audio_ogg_stream_read(struct sdl3_audio_ogg_stream *stream,
		float *out, size_t frame_count, bool loop);

/** Restart from the beginning. */
void sdl3_audio_ogg_stream_rewind(struct sdl3_audio_ogg_stream *stream);

void sdl3_audio_ogg_stream_close(struct sdl3_audio_ogg_stream *stream);

#endif /* INCLUDED_SDL3_AUDIO_OGG_H */

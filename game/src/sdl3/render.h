/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/render.h
 * \brief Character-cell compositor for the SDL3 frontend.
 *
 *
 */

#ifndef INCLUDED_SDL3_RENDER_H
#define INCLUDED_SDL3_RENDER_H

#include "sdl3/presenter.h"

struct sdl3_visual;

bool sdl3_visual_init(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const char *font_path, const char *map_font_path, int cols, int rows,
		int map_zoom_percent);
void sdl3_visual_free(struct sdl3_visual *visual);
bool sdl3_visual_resize(struct sdl3_visual *visual, SDL_Renderer *renderer);
bool sdl3_visual_set_grid_size(struct sdl3_visual *visual,
		SDL_Renderer *renderer, int cols, int rows);
bool sdl3_visual_set_font(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const char *font_path);
bool sdl3_visual_set_map_font(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const char *font_path);
bool sdl3_visual_set_map_zoom(struct sdl3_visual *visual,
		SDL_Renderer *renderer, int zoom_percent);
void sdl3_visual_invalidate_grid_cache(struct sdl3_visual *visual);
bool sdl3_visual_recreate_renderer_resources(struct sdl3_visual *visual,
		SDL_Renderer *renderer);
void sdl3_visual_render(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const struct sdl3_presentation_frame *frame);
bool sdl3_visual_render_overlay_frame(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_overlay_frame *frame);
bool sdl3_visual_point_to_cell(const struct sdl3_visual *visual, float x,
		float y, const struct sdl3_map_view *map_view, int *col, int *row);
void sdl3_visual_set_map_insets(int left, int top, int right, int bottom);
/* Output pixels along the left and right edges that the host covers with
 * controls of its own: the text grid is fitted between them, while the map is
 * still drawn out to the edges.  Desktop leaves them at zero.  A change takes
 * effect at the next sdl3_visual_resize(). */
void sdl3_visual_set_grid_insets(int left, int right);
/* Output pixels from the left edge that a host control in the bottom left
 * corner reaches (a touch d-pad): the cave's status strip starts clear of it,
 * as a thumb rests there.  Desktop leaves it at zero. */
void sdl3_visual_set_corner_inset(int width);
/* Whether Hybrid's magnified map cells snap to whole 32px multiples (crisp
 * pixel art at the zoom steps a keyboard or wheel takes). A host whose zoom
 * follows a pinch continuously turns it off, so the map ends the gesture at
 * the size the pinch showed. Desktop leaves it on. */
void sdl3_visual_set_pixel_art_snap(bool snap);

/*
 * Map preview for a continuous zoom gesture (e.g. a pinch).  Begin snapshots
 * the map on the next render; while active the map is drawn from that
 * snapshot scaled by `scale` about the player, which costs one texture draw
 * instead of re-rasterising every glyph at each intermediate size.  End
 * returns to normal drawing; the caller then commits the real zoom once.
 */
void sdl3_visual_begin_map_preview(struct sdl3_visual *visual);
void sdl3_visual_set_map_preview_scale(struct sdl3_visual *visual,
		float scale);
void sdl3_visual_end_map_preview(struct sdl3_visual *visual);

#endif /* INCLUDED_SDL3_RENDER_H */

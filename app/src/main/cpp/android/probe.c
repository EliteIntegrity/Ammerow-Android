/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * Layout probe, for automated layout checks (debug
 * builds only; release builds keep just a no-op entry point).
 *
 * On request the next frame is drawn in full (the frontend's grid cache is
 * invalidated first) and followed through its drawing: each glyph lands in
 * a cell of whatever it is drawn into (the screen, or the grid and scene
 * textures later copied to it), opaque fills and other images cover what is
 * under them, and the result is the game's text as it ends up on screen,
 * wherever it came from: semantic screens, Home, the sidebar, messages,
 * prompts, cards. Glyphs drawn clipped to the map's camera are the map, not
 * text; of those only the player and the town's shop entrances are noted, by
 * position, for the tour to tap. The text grid, its geometry and those marks
 * go to probe-game.txt in the game's directory (the app's files directory).
 */

#include <SDL3/SDL.h>
#include <jni.h>
#include <stdio.h>

#ifdef AMMEROW_LAYOUT_PROBE

#include "angband.h"
#include "cave.h"
#include "game-world.h"
#include "sdl3/font.h"
#include "sdl3/map-view.h"
#include "sdl3/presenter.h"
#include "sdl3/render.h"
#include "sdl3/render-internal.h"
#include "sdl3/screen-model.h"
#include "sdl3/term.h"
#include "touch.h"

/* The sequence number of the request still to be met (0 for none), and of
 * the frame being recorded. */
static SDL_AtomicInt probe_request;
static int recording;

/* The visual being recorded: its own font is the interface font. */
static const struct sdl3_visual *probe_visual;

#define PROBE_MAX_ROWS 200
#define PROBE_MAX_COLS 400
#define PROBE_TARGETS 6

/* What each render target holds, by cell: the screen (texture NULL) and the
 * textures drawn into and then copied to it. */
static struct target {
	bool used;
	SDL_Texture *texture;
	char cells[PROBE_MAX_ROWS][PROBE_MAX_COLS];
} targets[PROBE_TARGETS];
static int probe_rows, probe_cols;

/* Drawing state, followed as the frame is drawn. */
static SDL_Texture *current_target;
static bool clipped;
static SDL_Rect clip;
static Uint8 draw_alpha = 255;
static int in_glyph;
/* The map: the base scene copies the grid's texture and then draws the map
 * through its camera, between setting a clip rectangle and clearing it
 * (draw_base_scene and draw_scaled_map in sdl3/render.c). */
static bool grid_copied, map_phase;
/* The map's area in output pixels while it is shown (w 0 otherwise). */
static SDL_FRect map_area;
/* What the frame did, for checking the probe itself. */
static int stat_clips, stat_grid_copies, stat_map_glyphs, stat_text_glyphs, stat_textures;
static int stat_grid_text, stat_other_text, stat_clipped_text;
static char trace[2048];
static int trace_len, trace_run;
static void trace_event(char c)
{
	if (!recording) return;
	if (trace_len > 0 && trace[trace_len - 1] == c) {
		trace_run++;
		return;
	}
	if (trace_run > 1 && trace_len < (int)sizeof(trace) - 12) {
		trace_len += SDL_snprintf(trace + trace_len, sizeof(trace) - trace_len, "%d", trace_run);
	}
	trace_run = 1;
	if (trace_len < (int)sizeof(trace) - 2) {
		trace[trace_len++] = c;
		trace[trace_len] = '\0';
	}
}

#define PROBE_MAX_MARKS 256
enum mark_space { MARK_SCREEN, MARK_GRID, MARK_ZOOM };
static struct {
	char glyph;
	enum mark_space space;
	float x, y, w, h;
} probe_marks[PROBE_MAX_MARKS];
static int probe_mark_count;

static struct target *target_for(SDL_Texture *texture, bool create)
{
	int i, free_slot = -1;

	for (i = 0; i < PROBE_TARGETS; i++) {
		if (targets[i].used && targets[i].texture == texture) return &targets[i];
		if (!targets[i].used && free_slot < 0) free_slot = i;
	}
	if (!create || free_slot < 0) return NULL;
	targets[free_slot].used = true;
	targets[free_slot].texture = texture;
	SDL_memset(targets[free_slot].cells, ' ', sizeof(targets[free_slot].cells));
	return &targets[free_slot];
}

/* Where cell (0, 0) is drawn in the current target: the grid's texture
 * holds the grid alone, the screen (and the scene texture) has it at the
 * visual's origin. */
static void target_origin(float *x, float *y)
{
	bool grid = current_target && current_target == probe_visual->grid_texture;

	*x = grid ? 0.0f : (float)probe_visual->origin_x;
	*y = grid ? 0.0f : (float)probe_visual->origin_y;
}

/* The cells whose centres lie in rect (pixels of the current target), clipped. */
static bool cell_span(const SDL_FRect *rect, int *c0, int *r0, int *c1, int *r1)
{
	float left = rect->x, top = rect->y;
	float right = rect->x + rect->w, bottom = rect->y + rect->h;
	const struct sdl3_visual *v = probe_visual;
	float ox, oy;

	target_origin(&ox, &oy);

	if (clipped) {
		left = SDL_max(left, (float)clip.x);
		top = SDL_max(top, (float)clip.y);
		right = SDL_min(right, (float)(clip.x + clip.w));
		bottom = SDL_min(bottom, (float)(clip.y + clip.h));
	}
	if (right <= left || bottom <= top) return false;
	*c0 = (int)SDL_ceilf((left - ox) / v->cell_width - 0.5f);
	*r0 = (int)SDL_ceilf((top - oy) / v->cell_height - 0.5f);
	*c1 = (int)SDL_floorf((right - ox) / v->cell_width - 0.5f);
	*r1 = (int)SDL_floorf((bottom - oy) / v->cell_height - 0.5f);
	*c0 = SDL_max(*c0, 0);
	*r0 = SDL_max(*r0, 0);
	*c1 = SDL_min(*c1, probe_cols - 1);
	*r1 = SDL_min(*r1, probe_rows - 1);
	return *c1 >= *c0 && *r1 >= *r0;
}

/* Something opaque drawn over rect in the current target. */
static void cover(const SDL_FRect *rect)
{
	struct target *t = target_for(current_target, true);
	SDL_FRect whole;
	int c0, r0, c1, r1, row;

	if (!t) return;
	if (!rect) {
		whole = (SDL_FRect){ 0, 0, (float)probe_visual->output_width,
			(float)probe_visual->output_height };
		rect = &whole;
	}
	if (!cell_span(rect, &c0, &r0, &c1, &r1)) return;
	for (row = r0; row <= r1; row++) {
		SDL_memset(&t->cells[row][c0], ' ', (size_t)(c1 - c0 + 1));
	}
}

static bool probing(void)
{
	return recording && probe_visual && !in_glyph;
}

bool __real_sdl3_font_draw(struct sdl3_font *font, uint32_t codepoint,
	SDL_Color color, float cell_x, float cell_y, float cell_width,
	float cell_height);
bool __wrap_sdl3_font_draw(struct sdl3_font *font, uint32_t codepoint,
	SDL_Color color, float cell_x, float cell_y, float cell_width,
	float cell_height)
{
	bool result;

	if (probing() && codepoint > ' ') {
		const struct sdl3_visual *v = probe_visual;
		float ox, oy, x, y;
		bool grid, in_map_area, map;

		target_origin(&ox, &oy);
		x = cell_x - ox;
		y = cell_y - oy;
		bool zoom = current_target && current_target == v->zoom_out_texture;

		grid = current_target && current_target == v->grid_texture;
		in_map_area = map_area.w > 0 && x >= map_area.x &&
			x < map_area.x + map_area.w && y >= map_area.y &&
			y < map_area.y + map_area.h;
		/* The map is drawn in its own font when there is one. Otherwise it
		 * is in the grid's texture (where the sidebar and top line, also in
		 * it, are drawn again over it), and through the map's camera on top:
		 * clipped to it, or into its own texture first. The player's health
		 * mark is drawn clipped to the camera too. */
		map = font == &v->map_font || map_phase || zoom ||
			(clipped && codepoint == '@') || (in_map_area && grid);

		if (map) {
			stat_map_glyphs++;
			trace_event('m');
			if ((codepoint == '@' || (codepoint >= '1' && codepoint <= '9')) &&
					probe_mark_count < PROBE_MAX_MARKS) {
				probe_marks[probe_mark_count].glyph = (char)codepoint;
				probe_marks[probe_mark_count].space = zoom ? MARK_ZOOM :
					grid ? MARK_GRID : MARK_SCREEN;
				probe_marks[probe_mark_count].x = grid ? cell_x + v->origin_x : cell_x;
				probe_marks[probe_mark_count].y = grid ? cell_y + v->origin_y : cell_y;
				probe_marks[probe_mark_count].w = cell_width;
				probe_marks[probe_mark_count].h = cell_height;
				probe_mark_count++;
			}
		} else if ((font == &v->font || font == &v->card_font) &&
				v->cell_width > 0 && v->cell_height > 0) {
			struct target *t = target_for(current_target, true);
			int col = (int)SDL_floorf(x / v->cell_width + 0.5f);
			int row = (int)SDL_floorf(y / v->cell_height + 0.5f);

			stat_text_glyphs++;
			trace_event(grid ? 'g' : current_target ? 'o' : 't');
			if (grid) stat_grid_text++; else if (current_target) stat_other_text++; else if (clipped) stat_clipped_text++;
			if (t && row >= 0 && row < probe_rows && col >= 0 && col < probe_cols) {
				t->cells[row][col] = codepoint < 127 ? (char)codepoint : '*';
			}
		}
	}
	in_glyph++;
	result = __real_sdl3_font_draw(font, codepoint, color, cell_x, cell_y,
		cell_width, cell_height);
	in_glyph--;
	return result;
}

bool __real_SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture);
bool __wrap_SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
	bool result = __real_SDL_SetRenderTarget(renderer, texture);

	if (result) current_target = texture;
	trace_event(!texture ? 'S' : (probe_visual && texture == probe_visual->grid_texture) ? 'G' : 'T');
	return result;
}

bool __real_SDL_SetRenderClipRect(SDL_Renderer *renderer, const SDL_Rect *rect);
bool __wrap_SDL_SetRenderClipRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
	clipped = rect != NULL;
	if (rect) clip = *rect;
	if (recording && rect) stat_clips++;
	trace_event(rect ? 'C' : 'U');
	if (rect && grid_copied) {
		map_phase = true;
		grid_copied = false;
	} else if (!rect) {
		map_phase = false;
	}
	return __real_SDL_SetRenderClipRect(renderer, rect);
}

bool __real_SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g,
	Uint8 b, Uint8 a);
bool __wrap_SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g,
	Uint8 b, Uint8 a)
{
	draw_alpha = a;
	return __real_SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

bool __real_SDL_RenderClear(SDL_Renderer *renderer);
bool __wrap_SDL_RenderClear(SDL_Renderer *renderer)
{
	if (probing()) {
		bool was_clipped = clipped;

		/* Clearing ignores the clip rectangle. */
		clipped = false;
		cover(NULL);
		clipped = was_clipped;
	}
	return __real_SDL_RenderClear(renderer);
}

bool __real_SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_FRect *rect);
bool __wrap_SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_FRect *rect)
{
	/* Mostly opaque fills hide what is under them. */
	if (probing() && draw_alpha >= 200) cover(rect);
	return __real_SDL_RenderFillRect(renderer, rect);
}

bool __real_SDL_RenderFillRects(SDL_Renderer *renderer, const SDL_FRect *rects,
	int count);
bool __wrap_SDL_RenderFillRects(SDL_Renderer *renderer, const SDL_FRect *rects,
	int count)
{
	int i;

	if (probing() && draw_alpha >= 200) {
		for (i = 0; i < count; i++) cover(&rects[i]);
	}
	return __real_SDL_RenderFillRects(renderer, rects, count);
}

/* A texture drawn onto the current target: one the frame drew into (the
 * grid or scene cache) brings its text, anything else covers. */
bool __real_SDL_RenderTexture(SDL_Renderer *renderer, SDL_Texture *texture,
	const SDL_FRect *srcrect, const SDL_FRect *dstrect);
bool __wrap_SDL_RenderTexture(SDL_Renderer *renderer, SDL_Texture *texture,
	const SDL_FRect *srcrect, const SDL_FRect *dstrect)
{
	if (probing()) {
		struct target *source = target_for(texture, false);
		struct target *dest = target_for(current_target, true);

		stat_textures++;
		trace_event(texture == probe_visual->grid_texture ? 'X' : 'Y');
		if (texture && texture == probe_visual->zoom_out_texture && dstrect &&
				!srcrect && probe_visual->zoom_out_texture_width > 0 &&
				probe_visual->zoom_out_texture_height > 0) {
			float sx = dstrect->w / probe_visual->zoom_out_texture_width;
			float sy = dstrect->h / probe_visual->zoom_out_texture_height;
			int i;

			for (i = 0; i < probe_mark_count; i++) {
				if (probe_marks[i].space != MARK_ZOOM) continue;
				probe_marks[i].space = MARK_SCREEN;
				probe_marks[i].x = dstrect->x + probe_marks[i].x * sx;
				probe_marks[i].y = dstrect->y + probe_marks[i].y * sy;
				probe_marks[i].w *= sx;
				probe_marks[i].h *= sy;
			}
		}
		if (texture && texture == probe_visual->grid_texture) {
			grid_copied = true;
			stat_grid_copies++;
		}
		if (source && dest && !srcrect) {
			/* Drawn at the grid's own scale: cell for cell. */
			int row, col;

			for (row = 0; row < probe_rows; row++) {
				for (col = 0; col < probe_cols; col++) {
					if (source->cells[row][col] != ' ') {
						dest->cells[row][col] = source->cells[row][col];
					}
				}
			}
		} else {
			cover(dstrect);
		}
	}
	return __real_SDL_RenderTexture(renderer, texture, srcrect, dstrect);
}

/* The nearest known water to the character, as an offset in squares, for
 * the tour to walk to and fish from. */
static void write_water(FILE *fp)
{
	struct loc grid, best = loc(0, 0);
	int best_distance = -1;

	if (!player || !cave || !player->cave || !character_dungeon) return;
	for (grid.y = 1; grid.y < cave->height - 1; grid.y++) {
		for (grid.x = 1; grid.x < cave->width - 1; grid.x++) {
			int dx = grid.x - player->grid.x, dy = grid.y - player->grid.y;
			int distance = dx * dx + dy * dy;

			if (square_iswater(cave, grid) &&
					square_isknown(player->cave, grid) &&
					(best_distance < 0 || distance < best_distance)) {
				best = loc(dx, dy);
				best_distance = distance;
			}
		}
	}
	if (best_distance < 0) return;
	fprintf(fp, "water %d %d\n", best.x, best.y);
	/* A known open square on the way, a short walk off: the next hop. */
	{
		struct loc target = loc_sum(player->grid, best), hop = player->grid;
		int hop_distance = -1;

		for (grid.y = player->grid.y - 10; grid.y <= player->grid.y + 10; grid.y++) {
			for (grid.x = player->grid.x - 30; grid.x <= player->grid.x + 30; grid.x++) {
				int dx = target.x - grid.x, dy = target.y - grid.y;
				int distance = dx * dx + dy * dy;

				if (!square_in_bounds_fully(cave, grid) ||
						!square_isknown(player->cave, grid) ||
						!square_ispassable(cave, grid)) {
					continue;
				}
				if (hop_distance < 0 || distance < hop_distance) {
					hop = grid;
					hop_distance = distance;
				}
			}
		}
		fprintf(fp, "hop %d %d\n", hop.x - player->grid.x, hop.y - player->grid.y);
	}
}

static void write_probe(const struct sdl3_visual *visual,
	const struct sdl3_presentation_frame *frame)
{
	const struct sdl3_map_view *map = frame ? frame->map_view : NULL;
	struct target *screen = target_for(NULL, true);
	FILE *fp = fopen("probe-game.tmp", "w");
	char line[PROBE_MAX_COLS + 1];
	bool camera_marks = false;
	int row, i;

	if (!fp) return;
	fprintf(fp, "seq %d\n", recording);
	fprintf(fp, "output %d %d\n", visual->output_width, visual->output_height);
	fprintf(fp, "grid %d %d %d %d %d %d\n", visual->cols, visual->rows,
		visual->cell_width, visual->cell_height, visual->origin_x,
		visual->origin_y);
	fprintf(fp, "fonts %d %d\n", visual->font_loaded ? 1 : 0,
		visual->map_font_loaded ? 1 : 0);
	fprintf(fp, "screen %d\n", frame && frame->screen && frame->screen->active ?
		(int)frame->screen->kind : 0);
	fprintf(fp, "map %d %d %d %d %d %d\n", map && map->active ? 1 : 0,
		map ? map->col : 0, map ? map->row : 0, map ? map->cols : 0,
		map ? map->rows : 0, map ? map->zoom_percent : 0);
	for (i = 0; i < probe_mark_count; i++) {
		if (probe_marks[i].space == MARK_SCREEN) camera_marks = true;
	}
	for (i = 0; i < probe_mark_count; i++) {
		if (probe_marks[i].space == MARK_ZOOM ||
				(camera_marks && probe_marks[i].space == MARK_GRID)) {
			continue;
		}
		/* Not one under text drawn over the map (the sidebar's figures are
		 * in the grid's texture too, under the sidebar drawn over it). */
		int col = (int)SDL_floorf((probe_marks[i].x - visual->origin_x) /
			visual->cell_width + 0.5f);
		int row = (int)SDL_floorf((probe_marks[i].y - visual->origin_y) /
			visual->cell_height + 0.5f);

		if (row >= 0 && row < probe_rows && col >= 0 && col < probe_cols &&
				screen->cells[row][col] != ' ') {
			continue;
		}
		fprintf(fp, "mark %c %.1f %.1f %.1f %.1f\n", probe_marks[i].glyph,
			probe_marks[i].x, probe_marks[i].y, probe_marks[i].w,
			probe_marks[i].h);
	}
	write_water(fp);
	fprintf(fp, "stats clips %d grid-copies %d textures %d map-glyphs %d text-glyphs %d\n",
		stat_clips, stat_grid_copies, stat_textures, stat_map_glyphs, stat_text_glyphs);
	fprintf(fp, "stats2 grid-text %d other-target-text %d clipped-screen-text %d\n",
		stat_grid_text, stat_other_text, stat_clipped_text);
	fprintf(fp, "trace %s\n", trace);
	fprintf(fp, "text %d %d\n", probe_rows, probe_cols);
	for (row = 0; row < probe_rows; row++) {
		SDL_memcpy(line, screen->cells[row], (size_t)probe_cols);
		line[probe_cols] = '\0';
		fprintf(fp, "|%s|\n", line);
	}
	fclose(fp);
	/* Whole or not at all, for the reader. */
	(void)rename("probe-game.tmp", "probe-game.txt");
}

/* The frontend draws a frame here (from presenter.c). Asked for a probe, the
 * frame is drawn in full and followed. */
void __real_sdl3_visual_render(struct sdl3_visual *visual,
	SDL_Renderer *renderer, const struct sdl3_presentation_frame *frame);
void __wrap_sdl3_visual_render(struct sdl3_visual *visual,
	SDL_Renderer *renderer, const struct sdl3_presentation_frame *frame)
{
	int request = SDL_SetAtomicInt(&probe_request, 0);

	if (!request || !visual || !frame || !frame->grid) {
		if (request) SDL_SetAtomicInt(&probe_request, request);
		__real_sdl3_visual_render(visual, renderer, frame);
		return;
	}
	probe_visual = visual;
	probe_rows = SDL_min(visual->rows, PROBE_MAX_ROWS);
	probe_cols = SDL_min(visual->cols, PROBE_MAX_COLS);
	SDL_memset(targets, 0, sizeof(targets));
	probe_mark_count = 0;
	current_target = SDL_GetRenderTarget(renderer);
	clipped = SDL_RenderClipEnabled(renderer);
	if (clipped) SDL_GetRenderClipRect(renderer, &clip);
	grid_copied = map_phase = false;
	map_area = (SDL_FRect){ 0, 0, 0, 0 };
	if (frame->map_view && frame->map_view->active) {
		const struct sdl3_map_view *m = frame->map_view;

		/* In cell-grid pixels, without the origin. */
		map_area = (SDL_FRect){
			(float)(m->col * visual->cell_width),
			(float)(m->row * visual->cell_height),
			(float)(m->cols * visual->cell_width),
			(float)(m->rows * visual->cell_height)
		};
	}
	stat_clips = stat_grid_copies = stat_map_glyphs = stat_text_glyphs = stat_textures = 0;
	stat_grid_text = stat_other_text = stat_clipped_text = 0;
	trace_len = trace_run = 0;
	trace[0] = '\0';
	/* Everything, not just what changed since the last frame. */
	sdl3_visual_invalidate_grid_cache(visual);
	sdl3_grid_mark_all_dirty(frame->grid);
	if (frame->dock_grid) sdl3_grid_mark_all_dirty(frame->dock_grid);
	recording = request;
	__real_sdl3_visual_render(visual, renderer, frame);
	write_probe(visual, frame);
	recording = 0;
}

TOUCH_CHECK_WRAP(sdl3_font_draw);
TOUCH_CHECK_WRAP(sdl3_visual_render);
TOUCH_CHECK_WRAP(SDL_SetRenderTarget);
TOUCH_CHECK_WRAP(SDL_SetRenderClipRect);
TOUCH_CHECK_WRAP(SDL_SetRenderDrawColor);
TOUCH_CHECK_WRAP(SDL_RenderClear);
TOUCH_CHECK_WRAP(SDL_RenderFillRect);
TOUCH_CHECK_WRAP(SDL_RenderFillRects);
TOUCH_CHECK_WRAP(SDL_RenderTexture);

/* From the debug build's probe broadcast (AmmerowActivity), any thread. */
JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_layoutProbe(JNIEnv *env,
	jclass cls, jint seq)
{
	SDL_Event event;

	(void)env;
	(void)cls;
	SDL_SetAtomicInt(&probe_request, seq > 0 ? seq : 1);
	/* Draws a frame even when nothing on screen changes. */
	SDL_zero(event);
	event.type = SDL_EVENT_RENDER_TARGETS_RESET;
	SDL_PushEvent(&event);
}

#else

JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_layoutProbe(JNIEnv *env,
	jclass cls, jint seq)
{
	(void)env;
	(void)cls;
	(void)seq;
}

#endif /* AMMEROW_LAYOUT_PROBE */

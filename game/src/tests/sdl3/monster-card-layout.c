/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/monster-card-layout.c */
/* Exercise expanded and compact monster-card geometry. */

#include "unit-test.h"

#include "sdl3/monster-card-layout.h"

int setup_tests(void **data)
{
	(void)data;
	return 0;
}

int teardown_tests(void *data)
{
	(void)data;
	return 0;
}

static int test_big_layout_uses_current_panel(void *state)
{
	struct sdl3_monster_card_layout layout;
	(void)state;

	require(sdl3_monster_card_layout_compute(&layout, 10, 0, 100, 40,
		20, 10, 20, true));
	eq(layout.panel_col, 66);
	eq(layout.panel_row, 1);
	eq(layout.panel_cols, 44);
	eq(layout.panel_rows, 39);
	eq(layout.text_cols, 40);
	eq(layout.portrait_rows, 20);
	eq(layout.detail_row, 26);
	ok;
}

static int test_compact_layout_is_smaller(void *state)
{
	struct sdl3_monster_card_layout layout;
	(void)state;

	require(sdl3_monster_card_layout_compute(&layout, 10, 2, 100, 40,
		90, 10, 20, false));
	eq(layout.panel_col, 10);
	eq(layout.panel_row, 8);
	eq(layout.panel_cols, 34);
	eq(layout.panel_rows, 28);
	eq(layout.portrait_rows, 7);
	eq(layout.detail_row, 20);
	ok;
}

static int test_short_layout_keeps_its_text(void *state)
{
	struct sdl3_monster_card_layout layout;
	(void)state;

	/* Seventeen rows below the target line: a small portrait above six rows
	 * of facts. */
	require(sdl3_monster_card_layout_compute(&layout, 10, 0, 100, 18,
		20, 10, 20, true));
	eq(layout.panel_row, 1);
	eq(layout.panel_rows, 17);
	eq(layout.portrait_rows, 5);
	eq(layout.detail_row, 11);
	eq(layout.detail_end, 17);
	/* Thirteen: no room for a portrait, so the text alone. */
	require(sdl3_monster_card_layout_compute(&layout, 10, 0, 100, 14,
		20, 10, 20, false));
	eq(layout.panel_row, 1);
	eq(layout.panel_rows, 13);
	eq(layout.portrait_rows, 0);
	eq(layout.detail_row, 6);
	eq(layout.detail_end, 13);
	/* Fewer than ten: no card. */
	require(!sdl3_monster_card_layout_compute(&layout, 10, 0, 100, 10,
		20, 10, 20, true));
	ok;
}

static int test_layout_rejects_tiny_view(void *state)
{
	struct sdl3_monster_card_layout layout;
	(void)state;

	require(!sdl3_monster_card_layout_compute(&layout, 0, 0, 31, 18,
		0, 10, 20, true));
	require(!sdl3_monster_card_layout_compute(NULL, 0, 0, 100, 40,
		0, 10, 20, true));
	ok;
}

const char *suite_name = "sdl3/monster-card-layout";
struct test tests[] = {
	{ "big layout uses current panel", test_big_layout_uses_current_panel },
	{ "compact layout is smaller", test_compact_layout_is_smaller },
	{ "short layout keeps its text", test_short_layout_keeps_its_text },
	{ "layout rejects tiny view", test_layout_rejects_tiny_view },
	{ NULL, NULL },
};

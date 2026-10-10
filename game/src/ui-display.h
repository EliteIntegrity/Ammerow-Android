/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-display.h
 * \brief Handles the setting up updating, and cleaning up of the game display.
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2007 Antony Sidwell
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#ifndef INCLUDED_UI_DISPLAY_H
#define INCLUDED_UI_DISPLAY_H

#include "angband.h"
#include "cmd-core.h"

extern const char *stat_names[STAT_MAX];
extern const char *stat_names_reduced[STAT_MAX];
extern const char *window_flag_desc[32];

/**
 * Set by a frontend whose own controls cover the foot of the left-hand
 * sidebar (a touch screen's d-pad), to say how many of its rows they cover:
 * the sidebar keeps above them, leaving out its least important rows when
 * they do not all fit, as on a shorter screen.
 */
extern int (*sidebar_covered_rows_hook)(void);

uint8_t monster_health_attr(void);
void cnv_stat(int val, char *out_val, size_t out_len);
void allow_animations(void);
void disallow_animations(void);
void idle_update(void);
void toggle_inven_equip(void);
void subwindows_set_flags(uint32_t *new_flags, size_t n_subwindows);
void init_display(void);
void textui_disable_init_status_pauses(void);

#endif /* INCLUDED_UI_DISPLAY_H */

/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-context.h
 * \brief Show player and terrain context menus.
 *
 * Copyright (c) 2011 Brett Reid
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband license":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#ifndef UI_CONTEXT_H
#define UI_CONTEXT_H

#include "cave.h"
#include "ui-input.h"

/**
 * Additional constants for menu item values. The values must not collide
 * with the cmd_code enum, since those are the main values for these menu items.
 */
enum context_menu_value_e {
    MENU_VALUE_INSPECT = CMD_REPEAT + 1000,
    MENU_VALUE_DROP_ALL,
	MENU_VALUE_LOOK,
	MENU_VALUE_RECALL,
	MENU_VALUE_REST,
	MENU_VALUE_INVENTORY,
	MENU_VALUE_FLOOR,
	MENU_VALUE_CHARACTER,
	MENU_VALUE_OTHER,
	MENU_VALUE_KNOWLEDGE,
	MENU_VALUE_MAP,
	MENU_VALUE_MESSAGES,
	MENU_VALUE_OBJECTS,
	MENU_VALUE_MONSTERS,
	MENU_VALUE_TOGGLE_IGNORED,
	MENU_VALUE_OPTIONS,
	MENU_VALUE_HELP,
};

/**
 * One entry of the menu context_menu_object() shows for an object.
 */
struct context_menu_object_action {
	const char *label;
	/* A cmd_code, or MENU_VALUE_INSPECT or MENU_VALUE_DROP_ALL. */
	int value;
	/* Its selection key in the current keyset (0 for none). */
	char key;
	/* False when listed but not available, e.g. aiming an empty wand. */
	bool valid;
};

#define CONTEXT_MENU_OBJECT_ACTIONS_MAX 16

int context_menu_player(int mx, int my);
int context_menu_cave(struct chunk *c, int y, int x, int adjacent, int mx,
					  int my);
int context_menu_object_actions(struct object *obj,
		struct context_menu_object_action *actions, int max);
int context_menu_object(struct object *obj);
int context_menu_command(int mx, int my);
void textui_process_click(ui_event e);
struct cmd_info *textui_action_menu_choose(void);
/* NULL list_name previews the root; otherwise use a cmds_all family name. */
bool textui_action_menu_present(const char *list_name, int cursor);

#endif /* UI_CONTEXT_H */

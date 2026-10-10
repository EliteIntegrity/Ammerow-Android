/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * Shared between the touch layer's files (context.c, pins.c): what the game
 * is waiting for, the highlighted item or spell, and how to press keys and
 * call the Activity from the game thread.
 */

#ifndef AMMEROW_ANDROID_TOUCH_H
#define AMMEROW_ANDROID_TOUCH_H

#include <SDL3/SDL.h>
#include <jni.h>
#include <stdbool.h>

#include "angband.h"
#include "cmd-core.h"
#include "object.h"
#include "ui-event.h"

/*
 * A link-time wrapper (-Wl,--wrap, see CMakeLists.txt) declares its own copy
 * of the wrapped function's signature, which nothing else checks: compared
 * here with the game's declaration, a desktop change to it fails the build
 * instead of passing the wrong arguments.
 */
#define TOUCH_CHECK_WRAP(name) \
	_Static_assert(__builtin_types_compatible_p(__typeof__(&name), \
		__typeof__(&__wrap_##name)), "the game changed " #name "()")

/* Keep the numbers in step with ControlLayouts.kt. */
enum control_mode {
	MODE_NONE = 0,     /* Home, character creation: no run in play */
	MODE_MAP = 1,      /* top-down command prompt */
	MODE_MORE = 2,     /* a -more- prompt */
	MODE_PROMPT = 3,   /* menus, item and direction prompts, targeting, text */
	MODE_CAVE = 4,     /* side-view command prompt */
	MODE_FISHING = 5,  /* fishing command prompt */
	MODE_TEXT = 6,     /* a text or number prompt; detail says which */
	MODE_CHECK = 7,    /* a [y/n] question */
	MODE_AIM = 8,      /* "Direction or target?" */
	MODE_TARGET = 9,   /* interactive targeting or looking; detail = TARGET_* bits */
	MODE_STORE = 10,   /* a shop's or the home's item list; detail = STORE_* bits */
	MODE_ITEMS = 11,   /* an item list opened to browse it; detail = enum item_tab */
	MODE_SPELLS = 12   /* a book's spell list; detail = enum spell_kind */
};

/* What the game is waiting for now (game thread). */
int touch_mode(void);

/* In a browsed item list, the highlighted item; otherwise NULL. */
const struct object *touch_focused_item(void);

/* In a spell list, the highlighted spell and its book. */
bool touch_highlighted_spell(int *spell, struct object **book);

/* Presses a key as a keyboard would (no keyset translation). */
void touch_press(SDL_Keycode key, SDL_Keymod mod, int text);

/* The key a button presses for one of the game's keycodes. */
bool touch_key_for_code(keycode_t code, SDL_Keycode *key, SDL_Keymod *mod,
	int *text);

/* Rewrites a button's key for the roguelike keyset where it is read as a
 * command; returns whether it changed. */
bool touch_translate_command_key(SDL_Keycode *key, SDL_Keymod *mod, int *text);

/* Calls a void method on the Activity; false if it is missing or threw. */
bool touch_call_activity(JNIEnv *env, jobject activity, const char *name,
	const char *signature, ...);

/* A short note over the game (an Android toast), from the game thread. */
void touch_notice(const char *fmt, ...);

/* How far the d-pad reaches up from the drawing's bottom, in drawing pixels
 * (from the controls): the sidebar keeps above it. */
void touch_set_pad_height(int height);

/* ---- The quick bar (pins.c) ---- */

/* At every game wait (game thread): pending requests, the current
 * character's pins, and what the bar shows. */
void pins_wait(void);

/* Moves pins files left beside the savefiles to their own folder. */
void pins_tidy_save_folder(void);

/* Whether an item action (a cmd_code) can go on the bar. */
bool pins_verb_pinnable(int verb);

/* The game asks which item for cmd: answers it for a running pin. True if
 * answered, with the game's result in *result. */
bool pins_answer_item(struct object **choice, cmd_code cmd,
	item_tester tester, int mode, bool *result);

/* The game asks which spell to cast: answers it for a running spell pin.
 * True if answered, with the spell (or -1) in *spell. */
bool pins_answer_spell(cmd_code cmd, item_tester book_filter,
	bool (*spell_filter)(const struct player *p, int spell_index),
	struct object **rtn_book, int *spell);

#endif /* AMMEROW_ANDROID_TOUCH_H */

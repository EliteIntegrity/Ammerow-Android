/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * The quick bar: up to twelve slots the player fills with what this
 * character keeps reaching for (a potion, a scroll, ammunition, a spell, a
 * weapon to swap to, any command from More), shown on the map.
 *
 * A slot holds:
 *   an item action: the command (Quaff, Wield, Fire, ...) and the item's
 *     kind, never its inventory letter, so reordering the pack or using up
 *     a stack and buying another does not break it;
 *   a spell: its book's kind and the spell;
 *   a command: the keys a More button presses.
 *
 * Running an item or spell slot presses the command's key in the current
 * keyset (cmd_lookup_key) and then answers the game's own "which item?" or
 * "which spell?" itself (context.c passes those hooks here), choosing an
 * item of the kind the prompt accepts, one that can be used first (an empty
 * wand last). The game's inscription checks still apply (get_item_allow),
 * and anything the command asks next (a direction, a target) comes up as
 * usual.
 *
 * Slots belong to the character: they are kept as "<savefile name>.pins" in
 * the user directory's pins folder (not beside the savefile, where Home's
 * Load Run lists every file as a run), items and books by name (so a data
 * update that renumbers kinds does not break them), with the character's
 * flavour seed so another character later given the same name does not
 * inherit them. They are saved on every change.
 *
 * Kotlin shows the bar (AmmerowActivity.onGameBar), sends taps and pins
 * (NativeBridge), and shows notices (touch_notice).
 */

#include "touch.h"

#include <stdarg.h>

#include "game-input.h"
#include "game-world.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "player-util.h"
#include "savefile.h"
#include "ui-context.h"
#include "ui-game.h"
#include "ui-input.h"
#include "ui-keymap.h"
#include "ui-object.h"

#define PIN_SLOTS 12
#define PIN_KEYS 8
#define PIN_FILE_HEADER "ammerow-pins"
#define PIN_FILE_VERSION 1

enum pin_kind {
	PIN_NONE = 0,
	PIN_ITEM = 1,
	PIN_SPELL = 2,
	PIN_COMMAND = 3
};

struct pin {
	enum pin_kind kind;
	int verb;                          /* PIN_ITEM: the command */
	const struct object_kind *object;  /* PIN_ITEM: the item; PIN_SPELL: the book */
	int spell;                         /* PIN_SPELL: class spell index */
	char label[48];                    /* PIN_COMMAND: the More button's label */
	int keys[PIN_KEYS][3];             /* PIN_COMMAND: key, modifiers, text */
	int key_count;
};

static struct pin pins[PIN_SLOTS];

/* The savefile the pins above belong to. */
static char pins_for[1024];

/* A pin being run: its command's key has been pressed, and the game's
 * question about which item or spell is to be answered for it. */
static struct {
	enum pin_kind kind;
	int verb;
	const struct object_kind *object;
	int spell;
} running;

/* Requests from Kotlin (any thread), handled at the next game wait. */
static SDL_AtomicInt run_request, remove_request, pin_item_request,
	pin_spell_request, pin_command_ready;
static char pin_command_label[48];
static int pin_command_keys[PIN_KEYS][3], pin_command_key_count;

/* ---- Item actions ---------------------------------------------------- */

static const struct {
	int verb;
	const char *label;
} item_verbs[] = {
	{ CMD_QUAFF, "Quaff" },
	{ CMD_READ_SCROLL, "Read" },
	{ CMD_EAT, "Eat" },
	{ CMD_USE_WAND, "Aim" },
	{ CMD_USE_ROD, "Zap" },
	{ CMD_USE_STAFF, "Use" },
	{ CMD_ACTIVATE, "Activate" },
	{ CMD_FIRE, "Fire" },
	{ CMD_USE, "Use" },
	{ CMD_REFILL, "Refill" },
	{ CMD_TAKEOFF, "Take off" },
	{ CMD_WIELD, "Wield" },
	{ CMD_THROW, "Throw" },
	{ CMD_CAST, "Use magic from" }
};

/* Whether an item action can go on the bar (for the item list's Pin). */
bool pins_verb_pinnable(int verb)
{
	size_t i;

	for (i = 0; i < N_ELEMENTS(item_verbs); i++) {
		if (item_verbs[i].verb == verb) return true;
	}
	return false;
}

static const char *verb_label(int verb, const struct object_kind *kind)
{
	size_t i;

	if (verb == CMD_WIELD && kind) {
		switch (kind->tval) {
		case TV_SWORD: case TV_HAFTED: case TV_POLEARM: case TV_DIGGING:
		case TV_BOW: case TV_LIGHT:
			return "Wield";
		default:
			return "Wear";
		}
	}
	for (i = 0; i < N_ELEMENTS(item_verbs); i++) {
		if (item_verbs[i].verb == verb) return item_verbs[i].label;
	}
	return "Use";
}

static int keymap_mode(void)
{
	return OPT(player, rogue_like_commands) ? KEYMAP_MODE_ROGUE :
		KEYMAP_MODE_ORIG;
}

/* Whether obj suits the action: equipped or not as it needs, and usable
 * (a wand or staff with charges, a rod that is charged) if prefer_usable. */
static bool object_suits(const struct object *obj, int verb, bool prefer_usable)
{
	bool equipped = object_is_equipped(player->body, obj);

	switch (verb) {
	case CMD_WIELD: if (equipped) return false; break;
	case CMD_TAKEOFF: if (!equipped) return false; break;
	case CMD_ACTIVATE:
		if (!equipped) return false;
		if (prefer_usable && !obj_can_activate(obj)) return false;
		break;
	case CMD_USE_WAND:
	case CMD_USE_STAFF:
		if (prefer_usable && !obj_has_charges(obj)) return false;
		break;
	case CMD_USE_ROD:
		if (prefer_usable && !obj_can_zap(obj)) return false;
		break;
	default: break;
	}
	return true;
}

/* An item of the kind among those the game offers (mode, tester), one that
 * can be used now if there is one. */
static struct object *choose_object(const struct object_kind *kind, int verb,
	item_tester tester, int mode)
{
	struct object *list[256], *fallback = NULL;
	int count, i;

	count = scan_items(list, N_ELEMENTS(list), player,
		mode & (USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR), tester);
	for (i = 0; i < count; i++) {
		if (list[i]->kind != kind || !object_suits(list[i], verb, false)) continue;
		if (object_suits(list[i], verb, true)) return list[i];
		if (!fallback) fallback = list[i];
	}
	return fallback;
}

/* How many of the kind the character carries or wears that suit the action,
 * and whether any can be used now. */
static int count_objects(const struct object_kind *kind, int verb, bool *usable)
{
	struct object *list[256];
	int count, i, number = 0;

	*usable = false;
	count = scan_items(list, N_ELEMENTS(list), player,
		USE_EQUIP | USE_INVEN | USE_QUIVER, NULL);
	for (i = 0; i < count; i++) {
		if (list[i]->kind != kind || !object_suits(list[i], verb, false)) continue;
		number += list[i]->number;
		if (object_suits(list[i], verb, true)) *usable = true;
	}
	return number;
}

/* The kind's name as the character knows it (a flavour until learned). */
static void kind_name(char *buf, size_t size, const struct object_kind *kind)
{
	object_kind_name(buf, size, kind, false);
}

/* The kind's name as stored (lookup_sval's form). */
static void kind_store_name(char *buf, size_t size,
	const struct object_kind *kind)
{
	obj_desc_name_format(buf, size, 0, kind->name, NULL, false);
}

static const struct object_kind *kind_from_names(const char *tval_name,
	const char *name)
{
	int tval = tval_find_idx(tval_name);
	int sval;

	if (tval <= 0) return NULL;
	sval = lookup_sval(tval, name);
	return sval < 0 ? NULL : lookup_kind(tval, sval);
}

static const struct class_spell *pin_spell(const struct pin *pin)
{
	if (!player || !player->class || pin->spell < 0 ||
			pin->spell >= player->class->magic.total_spells) {
		return NULL;
	}
	return spell_by_index(player, pin->spell);
}

/* "Quaff Cure Light Wounds", "Use Magic Missile", "Rest". */
static void pin_title(const struct pin *pin, char *buf, size_t size)
{
	char name[80];

	switch (pin->kind) {
	case PIN_ITEM:
		kind_name(name, sizeof(name), pin->object);
		strnfmt(buf, size, "%s %s", verb_label(pin->verb, pin->object), name);
		break;
	case PIN_SPELL: {
		const struct class_spell *spell = pin_spell(pin);

		strnfmt(buf, size, "Use %s", spell ? spell->name : "that magic");
		break;
	}
	case PIN_COMMAND:
		my_strcpy(buf, pin->label, size);
		break;
	default:
		my_strcpy(buf, "Nothing", size);
		break;
	}
}

/* ---- Saving and loading ---------------------------------------------- */

static void pins_folder(char *buf, size_t size)
{
	path_build(buf, size, ANGBAND_DIR_USER, "pins");
}

static void pins_path(char *buf, size_t size)
{
	char folder[1024], name[300];

	pins_folder(folder, sizeof(folder));
	strnfmt(name, sizeof(name), "%s.pins", pins_for + path_filename_index(pins_for));
	path_build(buf, size, folder, name);
}

static void save_pins(void)
{
	char path[1100], temporary[1110], name[120];
	FILE *fp;
	int i, k;

	if (!pins_for[0]) return;
	pins_folder(path, sizeof(path));
	if (!dir_exists(path) && !dir_create(path)) return;
	pins_path(path, sizeof(path));
	strnfmt(temporary, sizeof(temporary), "%s.new", path);
	fp = fopen(temporary, "w");
	if (!fp) return;
	fprintf(fp, "%s\t%d\t%lu\n", PIN_FILE_HEADER, PIN_FILE_VERSION,
		(unsigned long)seed_flavor);
	for (i = 0; i < PIN_SLOTS; i++) {
		const struct pin *pin = &pins[i];

		switch (pin->kind) {
		case PIN_ITEM:
			kind_store_name(name, sizeof(name), pin->object);
			fprintf(fp, "item\t%d\t%s\t%s\n", pin->verb,
				tval_find_name(pin->object->tval), name);
			break;
		case PIN_SPELL: {
			const struct class_spell *spell = pin_spell(pin);

			kind_store_name(name, sizeof(name), pin->object);
			fprintf(fp, "spell\t%s\t%s\t%s\n",
				tval_find_name(pin->object->tval), name,
				spell ? spell->name : "");
			break;
		}
		case PIN_COMMAND:
			fprintf(fp, "command\t%s\t", pin->label);
			for (k = 0; k < pin->key_count; k++) {
				fprintf(fp, "%s%d,%d,%d", k ? ";" : "", pin->keys[k][0],
					pin->keys[k][1], pin->keys[k][2]);
			}
			fputc('\n', fp);
			break;
		default:
			fputs("none\n", fp);
			break;
		}
	}
	if (fclose(fp) != 0 || rename(temporary, path) != 0) unlink(temporary);
}

/* Splits a tab-separated line in place; returns the number of fields. */
static int split_fields(char *line, char **fields, int max)
{
	int count = 0;

	while (count < max) {
		char *tab = strchr(line, '\t');

		fields[count++] = line;
		if (!tab) break;
		*tab = '\0';
		line = tab + 1;
	}
	return count;
}

static int spell_by_name(const char *name)
{
	int i;

	if (!player || !player->class) return -1;
	for (i = 0; i < player->class->magic.total_spells; i++) {
		const struct class_spell *spell = spell_by_index(player, i);

		if (spell && spell->name && streq(spell->name, name)) return i;
	}
	return -1;
}

static void load_pins(void)
{
	char path[1100], line[512];
	FILE *fp;
	int slot = 0;

	memset(pins, 0, sizeof(pins));
	pins_path(path, sizeof(path));
	fp = fopen(path, "r");
	if (!fp) return;
	if (!fgets(line, sizeof(line), fp)) {
		fclose(fp);
		return;
	}
	{
		char *fields[3];

		line[strcspn(line, "\r\n")] = '\0';
		/* Another character's (an earlier one with the same name): ignore. */
		if (split_fields(line, fields, 3) != 3 ||
				!streq(fields[0], PIN_FILE_HEADER) ||
				strtoul(fields[2], NULL, 10) != (unsigned long)seed_flavor) {
			fclose(fp);
			return;
		}
	}
	while (slot < PIN_SLOTS && fgets(line, sizeof(line), fp)) {
		struct pin *pin = &pins[slot++];
		char *fields[4];
		int count;

		line[strcspn(line, "\r\n")] = '\0';
		count = split_fields(line, fields, 4);
		if (streq(fields[0], "item") && count == 4) {
			pin->object = kind_from_names(fields[2], fields[3]);
			pin->verb = atoi(fields[1]);
			if (pin->object && pins_verb_pinnable(pin->verb)) pin->kind = PIN_ITEM;
		} else if (streq(fields[0], "spell") && count == 4) {
			pin->object = kind_from_names(fields[1], fields[2]);
			pin->spell = spell_by_name(fields[3]);
			if (pin->object && pin->spell >= 0) pin->kind = PIN_SPELL;
		} else if (streq(fields[0], "command") && count >= 2) {
			char *keys = count >= 3 ? fields[2] : "";

			my_strcpy(pin->label, fields[1], sizeof(pin->label));
			while (*keys && pin->key_count < PIN_KEYS) {
				int *key = pin->keys[pin->key_count];
				char *next = strchr(keys, ';');

				if (sscanf(keys, "%d,%d,%d", &key[0], &key[1], &key[2]) == 3) {
					pin->key_count++;
				}
				if (!next) break;
				keys = next + 1;
			}
			if (pin->key_count > 0) pin->kind = PIN_COMMAND;
		}
	}
	fclose(fp);
}

/*
 * Pins files kept beside the savefiles, as they were before they had their
 * own folder, move there (a newer one there wins). Home's Load Run listed
 * each as a damaged run.
 */
void pins_tidy_save_folder(void)
{
	ang_dir *dir = my_dopen(ANGBAND_DIR_SAVE);
	char name[256], from[1024], folder[1024], to[1300];

	if (!dir) return;
	pins_folder(folder, sizeof(folder));
	while (my_dread(dir, name, sizeof(name))) {
		if (!suffix(name, ".pins")) continue;
		if (!dir_exists(folder) && !dir_create(folder)) break;
		path_build(from, sizeof(from), ANGBAND_DIR_SAVE, name);
		path_build(to, sizeof(to), folder, name);
		if (file_exists(to)) {
			file_delete(from);
		} else {
			file_move(from, to);
		}
	}
	my_dclose(dir);
}

/* The pins follow the character in play (game thread). */
static void follow_character(void)
{
	if (!player || !character_dungeon || !savefile[0] ||
			streq(pins_for, savefile)) {
		return;
	}
	my_strcpy(pins_for, savefile, sizeof(pins_for));
	running.kind = PIN_NONE;
	load_pins();
}

/* ---- Adding, removing and running ----------------------------------- */

static bool same_pin(const struct pin *a, const struct pin *b)
{
	if (a->kind != b->kind) return false;
	switch (a->kind) {
	case PIN_ITEM: return a->verb == b->verb && a->object == b->object;
	case PIN_SPELL: return a->object == b->object && a->spell == b->spell;
	case PIN_COMMAND: return streq(a->label, b->label);
	default: return false;
	}
}

static void add_pin(const struct pin *pin)
{
	char title[120];
	int i, free_slot = -1;

	pin_title(pin, title, sizeof(title));
	for (i = 0; i < PIN_SLOTS; i++) {
		if (same_pin(&pins[i], pin)) {
			touch_notice("%s is already on the bar.", title);
			return;
		}
		if (free_slot < 0 && pins[i].kind == PIN_NONE) free_slot = i;
	}
	if (free_slot < 0) {
		touch_notice("The bar is full. Hold a slot to empty it.");
		return;
	}
	pins[free_slot] = *pin;
	save_pins();
	touch_notice("%s is on the bar.", title);
}

static void remove_pin(int slot)
{
	char title[120];

	if (slot < 0 || slot >= PIN_SLOTS || pins[slot].kind == PIN_NONE) return;
	pin_title(&pins[slot], title, sizeof(title));
	memset(&pins[slot], 0, sizeof(pins[slot]));
	save_pins();
	touch_notice("%s is off the bar.", title);
}

static bool press_command_key(cmd_code verb)
{
	SDL_Keycode key;
	SDL_Keymod mod;
	int text;
	unsigned char code = cmd_lookup_key(verb, keymap_mode());

	if (!code || !touch_key_for_code(code, &key, &mod, &text)) return false;
	touch_press(key, mod, text);
	return true;
}

static void run_pin(int slot)
{
	struct pin *pin = &pins[slot];
	char name[80];
	bool usable;
	int k;

	switch (pin->kind) {
	case PIN_ITEM:
		if (count_objects(pin->object, pin->verb, &usable) == 0) {
			kind_name(name, sizeof(name), pin->object);
			touch_notice(pin->verb == CMD_TAKEOFF || pin->verb == CMD_ACTIVATE ?
				"You are not wearing %s." : "You have no %s.", name);
			return;
		}
		running.kind = PIN_ITEM;
		running.verb = pin->verb;
		running.object = pin->object;
		if (!press_command_key(pin->verb)) running.kind = PIN_NONE;
		break;
	case PIN_SPELL: {
		struct object *list[64];
		int count = scan_items(list, N_ELEMENTS(list), player,
			USE_INVEN | USE_FLOOR, NULL), i;

		for (i = 0; i < count && list[i]->kind != pin->object; i++) {}
		if (i == count) {
			kind_name(name, sizeof(name), pin->object);
			touch_notice("You need %s for that.", name);
			return;
		}
		running.kind = PIN_SPELL;
		running.object = pin->object;
		running.spell = pin->spell;
		if (!press_command_key(CMD_CAST)) running.kind = PIN_NONE;
		break;
	}
	case PIN_COMMAND:
		for (k = 0; k < pin->key_count; k++) {
			SDL_Keycode key = (SDL_Keycode)pin->keys[k][0];
			SDL_Keymod mod = (SDL_Keymod)pin->keys[k][1];
			int text = pin->keys[k][2];

			touch_translate_command_key(&key, &mod, &text);
			touch_press(key, mod, text);
		}
		break;
	default:
		break;
	}
}

bool pins_answer_item(struct object **choice, cmd_code cmd,
	item_tester tester, int mode, bool *result)
{
	struct object *obj;

	if (running.kind != PIN_ITEM) {
		/* A spell pin's book is answered in pins_answer_spell. */
		return false;
	}
	running.kind = PIN_NONE;
	if (running.verb != cmd) return false;
	obj = choose_object(running.object, running.verb, tester, mode);
	/* Gone since (or not where this prompt looks): the game asks. */
	if (!obj) return false;
	/* The game's inscription checks, as its own item menu makes them. */
	*result = get_item_allow(obj, cmd_lookup_key(cmd, keymap_mode()), cmd,
		(mode & IS_HARMLESS) != 0);
	if (*result) *choice = obj;
	return true;
}

bool pins_answer_spell(cmd_code cmd, item_tester book_filter,
	bool (*spell_filter)(const struct player *p, int spell_index),
	struct object **rtn_book, int *spell)
{
	struct object *book;
	const struct class_spell *info;

	if (running.kind != PIN_SPELL) return false;
	running.kind = PIN_NONE;
	if (cmd != CMD_CAST) return false;
	book = choose_object(running.object, CMD_CAST, book_filter,
		USE_INVEN | USE_FLOOR);
	if (!book) return false;
	if (spell_filter && !spell_filter(player, running.spell)) {
		info = spell_by_index(player, running.spell);
		msg("You cannot use %s now.", info ? info->name : "that magic");
		*spell = -1;
		return true;
	}
	if (rtn_book) *rtn_book = book;
	*spell = running.spell;
	return true;
}

/* A pin whose key the game read without asking for its item or spell (it
 * refused the command first) is forgotten once the game is back at the
 * command prompt with nothing more queued. */
static void expire_running(int mode)
{
	if (running.kind == PIN_NONE || !inkey_flag || mode != MODE_MAP) return;
	if (SDL_PeepEvents(NULL, 0, SDL_PEEKEVENT, SDL_EVENT_KEY_DOWN,
			SDL_EVENT_TEXT_INPUT) > 0) {
		return;
	}
	running.kind = PIN_NONE;
}

/* ---- What the bar shows ---------------------------------------------- */

/* One line per slot: kind, verb, name, count, available (tab-separated). */
static void describe_bar(char *text, size_t size)
{
	size_t used = 0;
	int i;

	text[0] = '\0';
	for (i = 0; i < PIN_SLOTS; i++) {
		const struct pin *pin = &pins[i];
		char name[80] = "";
		const char *verb = "";
		int count = 0, written;
		bool enabled = false;

		switch (pin->kind) {
		case PIN_ITEM:
			kind_name(name, sizeof(name), pin->object);
			verb = verb_label(pin->verb, pin->object);
			count = count_objects(pin->object, pin->verb, &enabled);
			/* Something to try: the game explains an empty wand itself. */
			enabled = count > 0;
			break;
		case PIN_SPELL: {
			const struct class_spell *spell = pin_spell(pin);
			struct object *list[64];
			int found = scan_items(list, N_ELEMENTS(list), player,
				USE_INVEN | USE_FLOOR, NULL), k;

			for (k = 0; k < found && list[k]->kind != pin->object; k++) {}
			my_strcpy(name, spell ? spell->name : "?", sizeof(name));
			verb = "Use";
			enabled = k < found && player_can_cast(player, false) &&
				spell_okay_to_cast(player, pin->spell);
			break;
		}
		case PIN_COMMAND:
			my_strcpy(name, pin->label, sizeof(name));
			enabled = true;
			break;
		default:
			break;
		}
		written = snprintf(text + used, size - used, "%d\t%s\t%s\t%d\t%d\n",
			(int)pin->kind, verb, name, count, enabled ? 1 : 0);
		if (written < 0 || (size_t)written >= size - used) break;
		used += (size_t)written;
	}
}

static void publish_bar(void)
{
	static char last[2048] = "-";
	char text[2048];
	JNIEnv *env;
	jobject activity;
	jstring string;

	describe_bar(text, sizeof(text));
	if (streq(text, last)) return;
	env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	activity = (jobject)SDL_GetAndroidActivity();
	if (!env || !activity) return;
	string = (*env)->NewStringUTF(env, text);
	if (string && touch_call_activity(env, activity, "onGameBar",
			"(Ljava/lang/String;)V", string)) {
		my_strcpy(last, text, sizeof(last));
	}
	if (string) (*env)->DeleteLocalRef(env, string);
	(*env)->DeleteLocalRef(env, activity);
}

/* ---- At every wait --------------------------------------------------- */

static void handle_requests(int mode)
{
	int request;

	request = SDL_SetAtomicInt(&remove_request, 0);
	if (request) remove_pin(request - 1);

	request = SDL_SetAtomicInt(&pin_item_request, 0);
	if (request && mode == MODE_ITEMS) {
		const struct object *obj = touch_focused_item();
		struct pin pin = { 0 };

		if (obj && obj->kind && pins_verb_pinnable(request - 1)) {
			pin.kind = PIN_ITEM;
			pin.verb = request - 1;
			pin.object = obj->kind;
			add_pin(&pin);
		}
	}

	request = SDL_SetAtomicInt(&pin_spell_request, 0);
	if (request && mode == MODE_SPELLS) {
		struct object *book;
		struct pin pin = { 0 };
		int spell;

		if (touch_highlighted_spell(&spell, &book)) {
			pin.kind = PIN_SPELL;
			pin.object = book->kind;
			pin.spell = spell;
			add_pin(&pin);
		}
	}

	if (SDL_GetAtomicInt(&pin_command_ready)) {
		struct pin pin = { 0 };

		pin.kind = PIN_COMMAND;
		my_strcpy(pin.label, pin_command_label, sizeof(pin.label));
		pin.key_count = pin_command_key_count;
		memcpy(pin.keys, pin_command_keys, sizeof(pin.keys));
		SDL_SetAtomicInt(&pin_command_ready, 0);
		if (pins_for[0]) add_pin(&pin);
	}

	request = SDL_SetAtomicInt(&run_request, 0);
	/* Only at the command prompt: the bar is not on screen elsewhere. */
	if (request && mode == MODE_MAP && inkey_flag && running.kind == PIN_NONE) {
		run_pin(request - 1);
	}
}

void pins_wait(void)
{
	int mode;

	if (!SDL_IsMainThread()) return;
	follow_character();
	if (!pins_for[0]) return;
	/* Only in a run: describing a slot looks at the floor under the
	 * character (a spell's book may lie there), and Home has no level. */
	if (!player || !cave || !player->cave || !character_dungeon) {
		running.kind = PIN_NONE;
		return;
	}
	mode = touch_mode();
	expire_running(mode);
	handle_requests(mode);
	publish_bar();
}

/* ---- JNI ------------------------------------------------------------- */

static void wake_game(void)
{
	SDL_Event event;

	SDL_zero(event);
	event.type = SDL_EVENT_KEY_UP;
	event.key.key = SDLK_UNKNOWN;
	SDL_PushEvent(&event);
}

JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_runPin(JNIEnv *env,
	jclass cls, jint slot)
{
	(void)env;
	(void)cls;
	if (slot < 0 || slot >= PIN_SLOTS) return;
	SDL_SetAtomicInt(&run_request, slot + 1);
	wake_game();
}

JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_removePin(JNIEnv *env,
	jclass cls, jint slot)
{
	(void)env;
	(void)cls;
	if (slot < 0 || slot >= PIN_SLOTS) return;
	SDL_SetAtomicInt(&remove_request, slot + 1);
	wake_game();
}

JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_pinItem(JNIEnv *env,
	jclass cls, jint verb)
{
	(void)env;
	(void)cls;
	SDL_SetAtomicInt(&pin_item_request, verb + 1);
	wake_game();
}

JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_pinSpell(JNIEnv *env,
	jclass cls)
{
	(void)env;
	(void)cls;
	SDL_SetAtomicInt(&pin_spell_request, 1);
	wake_game();
}

JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_pinCommand(JNIEnv *env,
	jclass cls, jstring label, jintArray keys)
{
	const char *chars;
	jint *values;
	jsize length;
	int i;

	(void)cls;
	/* One at a time: a second hold before the game took the first is dropped. */
	if (SDL_GetAtomicInt(&pin_command_ready) || !label || !keys) return;
	chars = (*env)->GetStringUTFChars(env, label, NULL);
	if (!chars) return;
	my_strcpy(pin_command_label, chars, sizeof(pin_command_label));
	(*env)->ReleaseStringUTFChars(env, label, chars);
	length = (*env)->GetArrayLength(env, keys);
	values = (*env)->GetIntArrayElements(env, keys, NULL);
	if (!values) return;
	pin_command_key_count = 0;
	for (i = 0; i + 2 < length && pin_command_key_count < PIN_KEYS; i += 3) {
		pin_command_keys[pin_command_key_count][0] = values[i];
		pin_command_keys[pin_command_key_count][1] = values[i + 1];
		pin_command_keys[pin_command_key_count][2] = values[i + 2];
		pin_command_key_count++;
	}
	(*env)->ReleaseIntArrayElements(env, keys, values, JNI_ABORT);
	if (pin_command_key_count == 0) return;
	SDL_SetAtomicInt(&pin_command_ready, 1);
	wake_game();
}

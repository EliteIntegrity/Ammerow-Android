/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * Tells the touch controls what the game is waiting for.
 *
 * Every time the game is about to wait for input it calls SDL_WaitEvent or
 * SDL_WaitEventTimeout. The link step wraps both (-Wl,--wrap, see
 * CMakeLists.txt), so this runs on the game thread at exactly the moments the
 * player can act, with no change to the game. It reads the game's own state,
 * works out the control mode and the context action, and passes them to
 * AmmerowActivity.onGameContext() when they change.
 *
 * Prompts are recognised the same way: the game's input hooks (get_string,
 * get_quantity, get_check, get_aim_dir) are replaced by pass-through
 * versions, and calls across files to askfor_aux, the character-name and
 * quantity editors and target_set_interactive are wrapped at link time. Each
 * only counts how deep the game is inside it.
 *
 * In an item list opened to browse it (Inventory, Equipment, Quiver), the
 * highlighted item's actions are sent too (AmmerowActivity.onGameVerbs), taken
 * from the game's own menu for that item (context_menu_object_actions
 * in src/ui-context.c), and a button runs one by the game's own route: Enter picks
 * the item, and its menu returns that action without being shown.
 *
 * Keep the numbers in step with ControlLayouts.kt.
 */

#include <SDL3/SDL.h>
#include <jni.h>
#include <stdarg.h>

#include "angband.h"
#include "cave.h"
#include "cmd-core.h"
#include "game-world.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "mon-predicate.h"
#include "obj-util.h"
#include "player-spell.h"
#include "option.h"
#include "player.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "store.h"
#include "ui-map.h"
#include "target.h"
#include "ui-birth-screen.h"
#include "ui-context.h"
#include "ui-display.h"
#include "ui-help.h"
#include "ui-menu.h"
#include "ui-object-screen.h"
#include "ui-screen.h"
#include "ui-store.h"
#include "ui-store-screen.h"
#include "ui-spelunking-input.h"
#include "game-input.h"
#include "ui-input.h"
#include "ui-target.h"
#include "ui-travel.h"
#include "ui-output.h"
#include "ui-term.h"
#include "sdl3/home.h"
#include "sdl3/home-model.h"
#include "sdl3/inspect-card-presenter.h"
#include "sdl3/monster-card-layout.h"
#include "sdl3/presenter.h"
#include "sdl3/render.h"
#include "sdl3/render-internal.h"
#include "sdl3/host.h"
#include "touch.h"
#include "world-entry.h"
#include "world-fishing.h"
#include "world-fishing-site.h"
#include "world-spelunking.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-passage.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-system.h"
#include "world-spelunking-transition.h"
#include "world-turn.h"

/* MODE_STORE's detail. */
#define STORE_HOME 1          /* the home: Take and Stash */
#define STORE_DONATIONS 2     /* takes a fishing catch for the village larder */

/* MODE_TARGET's detail: what is under the cursor, and how it began. */
#define TARGET_ON_MONSTER 1
#define TARGET_ON_OBJECT 2
#define TARGET_CHOOSING 4     /* choosing a target (aiming), not just looking */
#define TARGET_CAN_WALK 8     /* g walks there */
#define TARGET_ON_SELF 16     /* on the player (where Look starts) */

/* MODE_SPELLS's detail: what the list is for. */
enum spell_kind {
	SPELL_CAST = 0,
	SPELL_STUDY = 1,
	SPELL_BROWSE = 2,
	SPELL_OTHER = 3
};

/* The browsed list's tab, as detail of MODE_ITEMS. */
enum item_tab {
	TAB_INVENTORY = 0,
	TAB_EQUIPMENT = 1,
	TAB_QUIVER = 2,
	TAB_FLOOR = 3
};

enum text_kind {
	TEXT_GENERAL = 0,
	TEXT_NUMBER = 1,
	TEXT_NAME = 2
};

/* Outside a run (MODE_NONE), which screen is up, as detail. */
enum outside_page {
	PAGE_OTHER = 0,                 /* Home, the run summary, ... */
	PAGE_BIRTH_MENU = 1,            /* origin, class, attribute method: = opens birth options */
	PAGE_BIRTH_POINTS = 2,          /* point-based attributes: R resets */
	PAGE_BIRTH_ROLLER = 3,          /* rolled attributes: R rerolls */
	PAGE_BIRTH_ROLLER_PREVIOUS = 4, /* the same, and P brings back the previous roll */
	PAGE_BIRTH_QUICKSTART = 5,      /* "New character based on previous one": Y, N, C, = */
	PAGE_OPTIONS = 6,               /* an options page (birth options): Enter toggles */
	PAGE_RUN_SUMMARY = 7,           /* the end of a run: I, M, X, H, V show its record */
	PAGE_HOME_LOAD = 8,             /* Home's saved runs: Delete removes the highlighted one */
	PAGE_HOME_DELETE = 9,           /* "Delete saved run?": Y deletes, N keeps it */
	PAGE_HOME_MENU = 10             /* Home's own menu */
};

/* MODE_AIM's detail: a target is set, which At target aims at. */
#define AIM_TARGET_SET 1

/* MODE_PROMPT's detail: the character dossier, where C renames, an options
 * page, where Enter toggles, or the message log, where = finds text (and,
 * once it has, - finds the next match). */
#define PROMPT_DOSSIER 1
#define PROMPT_OPTIONS 2
#define PROMPT_LOG 3
#define PROMPT_LOG_FOUND 4
#define PROMPT_LEVEL_MAP 5
#define PROMPT_HELP 6
#define PROMPT_STORY 7

/* MODE_MAP's detail, with a monster in view: what the game's "repeat
 * previous command" would do again as it was (the same ammunition, spell or
 * missile, at the same target), named in the verbs text. */
enum again_kind {
	AGAIN_NONE = 0,
	AGAIN_FIRE = 1,
	AGAIN_CAST = 2,
	AGAIN_THROW = 3
};

/* Set as each screen is shown (game thread only); a run in play resets it. */
static int outside_page;
/* The game's semantic screen on show (from its screen hook), or UI_SCREEN_NONE.
 * The game clears each when it closes. */
static enum ui_screen_kind shown_screen = UI_SCREEN_NONE;
/* The character dossier itself, not a briefing shown in its frame (a shop's
 * optional quests, which any key passes on into the shop). */
static bool dossier_shown;
/* Whether that screen is the message log, and whether it has found text. */
static bool log_shown, log_found;
/* Home (from sdl3_home_open), for its page. */
static const struct sdl3_home_screen *home_screen;

/* How deep the game is inside each kind of prompt (game thread only). */
static int text_depth, number_depth, name_depth, check_depth, aim_depth,
	target_depth, item_depth, menu_depth, view_depth;
/* Inside the field guide (do_cmd_help), from its rail button or the menus. */
static int help_depth;

/* A shop's own yes/no (store_get_check): its screen is drawn, then one key
 * is read. Set by the presenter, cleared when the store list is back. */
static bool store_confirming;

/* Inside a store: the menu depth of its item list, whether it is the home,
 * and whether it takes a fishing catch for the village larder. */
static int store_menu_depth;
static bool store_is_home;
static bool store_takes_donations;

/* A book's spell list: the depth of its menu (0 when none is open) and
 * what it is for (enum spell_kind). Its prompt announces it (touch-text.c);
 * what it is for comes from the filter the game passed with its request
 * (casting and learning lists name the realm's own verb, "Invoke which
 * formula?", so the prompt does not say). */
static int spell_menu_depth;
static int spell_kind;
static bool (*spell_request_filter)(const struct player *p, int spell_index);

/* Looking or targeting: how it began, and the square under the cursor
 * (from move_cursor_relative, which the look code calls for each square). */
static int look_mode;
static bool look_can_walk;
static bool look_grid_known;
static struct loc look_grid;

/* Inside an item list: the depth of its menu, the command it picks an item
 * for (CMD_NULL when the list is only browsed) and the highlighted item. */
static int item_menu_depth;
static cmd_code item_cmd;
static const struct object *item_focus;

/* True while an item list is open just to browse it, with nothing over it.
 * After death the only list is the death screen's Examine items, which picks
 * an item only to describe it: no actions apply. */
static bool browsing_items(void)
{
	return item_depth > 0 && item_cmd == CMD_NULL &&
		menu_depth == item_menu_depth && view_depth == 0 && player &&
		!player->is_dead && !player_is_shapechanged(player);
}

/* For touch-text.c, which words the list's hints for touch. */
bool touch_browsing_items(void)
{
	return browsing_items();
}

static int item_tab(void)
{
	switch (player->upkeep->command_wrk) {
	case USE_EQUIP: return TAB_EQUIPMENT;
	case USE_QUIVER: return TAB_QUIVER;
	case USE_FLOOR: return TAB_FLOOR;
	default: return TAB_INVENTORY;
	}
}

enum primary_action {
	PRIMARY_NONE = 0,     /* nothing in particular here */
	PRIMARY_GET = 1,
	PRIMARY_DOWN = 2,
	PRIMARY_UP = 3,
	PRIMARY_ENTER = 4,   /* a route into another mode, e.g. a cave mouth */
	PRIMARY_SHOP = 5,
	PRIMARY_FISH = 6,
	PRIMARY_OPEN = 7,
	/* Side-view caves */
	PRIMARY_CAVE_JUMP = 20,
	PRIMARY_CAVE_GRIP = 21,
	PRIMARY_CAVE_LEAVE = 22,   /* standing on the way out */
	PRIMARY_CAVE_WAIT = 23     /* swimming */
	/* In caves PRIMARY_GET, PRIMARY_UP and PRIMARY_DOWN (passages) apply too. */
};

enum context_flag {
	FLAG_CAN_CAST = 1,         /* knows a spell and can cast now */
	FLAG_CAN_FIRE = 2,         /* a launcher, and ammunition for it in the quiver */
	FLAG_ROGUELIKE_KEYS = 4,
	FLAG_CAN_THROW = 8,        /* carries a throwing weapon (or flask) */
	FLAG_MONSTER_IN_VIEW = 16, /* as the game checks before exploring */
	FLAG_IN_RUN = 32,          /* a run is in play (any screen) */
	FLAG_TRAVEL = 64           /* on the surface, '<' opens known-world travel */
};

/* True when row 0 of the main term shows the -more- prompt. */
static bool more_prompt_showing(void)
{
	term *main_term = angband_term[0];
	term *old = Term;
	bool found = false;
	int x;

	if (!main_term) return false;
	Term_activate(main_term);
	for (x = 0; x + 5 < main_term->wid && !found; x++) {
		static const char more[] = "-more-";
		int i;

		for (i = 0; i < 6; i++) {
			int attr;
			wchar_t c;

			Term_what(x + i, 0, &attr, &c);
			if (c != (wchar_t)more[i]) break;
		}
		found = i == 6;
	}
	Term_activate(old);
	return found;
}

static bool known_closed_door_adjacent(void)
{
	int dir;

	for (dir = 0; dir < 8; dir++) {
		struct loc grid = loc_sum(player->grid, ddgrid_ddd[dir]);

		if (square_in_bounds(player->cave, grid) &&
				square_iscloseddoor(player->cave, grid)) {
			return true;
		}
	}
	return false;
}

static int primary_action(void)
{
	struct world_fishing_site site;
	struct loc grid = player->grid;

	/* What the player knows is under them, never what is really there. */
	if (square_in_bounds(player->cave, grid) &&
			square_object(player->cave, grid)) {
		return PRIMARY_GET;
	}
	if (square_isshop(cave, grid)) return PRIMARY_SHOP;
	if (square_isdownstairs(cave, grid)) return PRIMARY_DOWN;
	if (square_isupstairs(cave, grid)) return PRIMARY_UP;
	if (world_entry_cross_mode_route_here(cave, grid)) return PRIMARY_ENTER;
	if (world_player_mode_has_capability(player, WORLD_MODE_CAP_FISHING) &&
			world_fishing_site_query(player, cave, &site) ==
				WORLD_FISHING_SITE_OK) {
		return PRIMARY_FISH;
	}
	if (known_closed_door_adjacent()) return PRIMARY_OPEN;
	return PRIMARY_NONE;
}

/* What the player can do where they are in a side-view cave. */
static int cave_primary_action(void)
{
	const struct world_spelunk_runtime *runtime = player->spelunking;
	const struct world_spelunk_state *state;
	enum world_spelunk_passage_direction direction;
	struct loc grid;
	size_t i;

	if (!world_spelunk_player_is_active(player) || !runtime) {
		return PRIMARY_CAVE_JUMP;
	}
	state = &runtime->state;
	switch (state->movement) {
	case WORLD_SPELUNK_STANDING:
		break;
	case WORLD_SPELUNK_SWIMMING:
		return PRIMARY_CAVE_WAIT;
	default:
		/* Climbing, hanging, falling, or at the top of a jump: holds matter. */
		return PRIMARY_CAVE_GRIP;
	}
	if (world_spelunk_runtime_ground_object_at(runtime, state->x, state->y)) {
		return PRIMARY_GET;
	}
	for (i = 0; world_spelunk_system_passage_grid_at(player->spelunking_system,
			runtime, i, &grid, &direction); i++) {
		if (grid.x == state->x && grid.y == state->y) {
			return direction == WORLD_SPELUNK_PASSAGE_UP ? PRIMARY_UP :
				PRIMARY_DOWN;
		}
	}
	for (i = 0; world_spelunking_exit_grid_at(player->spelunking_system,
			runtime, i, &grid); i++) {
		if (grid.x == state->x && grid.y == state->y) {
			return PRIMARY_CAVE_LEAVE;
		}
	}
	{
		/* Beside water the cave can be fished from (its status says so). */
		struct world_fishing_site site;

		if (world_fishing_site_query(player, NULL, &site) ==
				WORLD_FISHING_SITE_OK) {
			return PRIMARY_FISH;
		}
	}
	return PRIMARY_CAVE_JUMP;
}

/* What the look or target cursor is on, as MODE_TARGET's detail. */
static int target_detail(void)
{
	int detail = 0;

	if (look_mode & TARGET_KILL) detail |= TARGET_CHOOSING;
	if (look_can_walk) detail |= TARGET_CAN_WALK;
	if (look_grid_known && cave && player && player->cave &&
			square_in_bounds(cave, look_grid)) {
		struct monster *mon = square_monster(cave, look_grid);

		if (loc_eq(look_grid, player->grid)) {
			detail |= TARGET_ON_SELF;
		} else if (mon && monster_is_obvious(mon)) {
			detail |= TARGET_ON_MONSTER;
		} else if (square_in_bounds(player->cave, look_grid) &&
				square_object(player->cave, look_grid)) {
			/* What the player remembers there, never what is really there. */
			detail |= TARGET_ON_OBJECT;
		}
	}
	return detail;
}

/* Prompts the game can be in whether or not a run is in play. */
static bool prompt_context(int *mode, int *detail)
{
	/* A story card (a quest briefing, a donation's receipt) waits for OK or
	 * Back over whatever opened it, a shop's list included. */
	if (shown_screen == UI_SCREEN_STORY) {
		*mode = MODE_PROMPT;
		*detail = PROMPT_STORY;
	} else if (msg_flag && more_prompt_showing()) {
		*mode = MODE_MORE;
	} else if (name_depth > 0) {
		*mode = MODE_TEXT;
		*detail = TEXT_NAME;
	} else if (number_depth > 0) {
		*mode = MODE_TEXT;
		*detail = TEXT_NUMBER;
	} else if (text_depth > 0) {
		*mode = MODE_TEXT;
		*detail = TEXT_GENERAL;
	} else if (check_depth > 0 || store_confirming) {
		*mode = MODE_CHECK;
	} else if (target_depth > 0 && view_depth == 0 &&
			shown_screen != UI_SCREEN_MONSTER_LORE) {
		/* (A monster's recall shown from looking is a plain page, which
		 * any key closes: MODE_PROMPT.) */
		*mode = MODE_TARGET;
		*detail = target_detail();
	} else if (aim_depth > 0) {
		*mode = MODE_AIM;
		if (target_okay()) *detail = AIM_TARGET_SET;
	} else if (spell_menu_depth > 0 && menu_depth == spell_menu_depth &&
			view_depth == 0) {
		*mode = MODE_SPELLS;
		*detail = spell_kind;
	} else if (store_menu_depth > 0 && menu_depth == store_menu_depth &&
			item_depth == 0 && view_depth == 0) {
		/* The store's own list, not an item picker or action menu over it. */
		*mode = MODE_STORE;
		*detail = (store_is_home ? STORE_HOME : 0) |
			(store_takes_donations ? STORE_DONATIONS : 0);
	} else if (browsing_items()) {
		*mode = MODE_ITEMS;
		*detail = item_tab();
	} else if (shown_screen == UI_SCREEN_HELP) {
		/* The field guide, from Home as well as in a run. */
		*mode = MODE_PROMPT;
		*detail = PROMPT_HELP;
	} else {
		return false;
	}
	return true;
}

/* A launcher, and ammunition for it in the quiver (what Fire at nearest needs). */
static bool can_fire(void)
{
	int i;

	if (!player_can_fire(player, false)) return false;
	for (i = 0; i < z_info->quiver_size; i++) {
		const struct object *obj = player->upkeep->quiver[i];

		if (obj && obj->tval == player->state.ammo_tval) return true;
	}
	return false;
}

/* Able to cast now and knows a spell (a new Ranger can cast, but knows none). */
static bool can_cast(void)
{
	int i;

	if (!player_can_cast(player, false)) return false;
	for (i = 0; i < player->class->magic.total_spells; i++) {
		if (player->spell_flags[i] & PY_SPELL_LEARNED) return true;
	}
	return false;
}

/* Carries something made for throwing (throwing weapons, flasks of oil). */
static bool can_throw(void)
{
	int i;

	for (i = 0; i < z_info->pack_size && player->upkeep->inven[i]; i++) {
		if (obj_is_throwing(player->upkeep->inven[i])) return true;
	}
	for (i = 0; i < z_info->quiver_size; i++) {
		const struct object *obj = player->upkeep->quiver[i];

		if (obj && obj_is_throwing(obj)) return true;
	}
	return false;
}

/* MODE_PROMPT's detail for the screen on show. */
static int prompt_screen(void)
{
	if (shown_screen == UI_SCREEN_CHARACTER_DOSSIER && dossier_shown) {
		return PROMPT_DOSSIER;
	}
	if (shown_screen == UI_SCREEN_OPTIONS) return PROMPT_OPTIONS;
	if (shown_screen == UI_SCREEN_DUNGEON_MAP) return PROMPT_LEVEL_MAP;
	if (shown_screen == UI_SCREEN_HELP) return PROMPT_HELP;
	if (shown_screen == UI_SCREEN_STORY) return PROMPT_STORY;
	if (shown_screen == UI_SCREEN_HISTORY && log_shown) {
		return log_found ? PROMPT_LOG_FOUND : PROMPT_LOG;
	}
	return 0;
}

/* What Again would repeat: the last command, if it is a shot, a spell or a
 * throw the game can repeat as it was, with its ammunition, spell or missile
 * named in name. */
static enum again_kind again_kind(char *name, size_t size)
{
	struct command *last = cmdq_peek();
	struct object *obj = NULL;
	int spell = -1;

	name[0] = '\0';
	if (!last) return AGAIN_NONE;
	switch (last->code) {
	case CMD_FIRE:
	case CMD_THROW:
		/* The game will not repeat it once the stack is used up, when the
		 * object may be gone, so it is only looked at while carried. */
		if (cmd_get_arg_item(last, "item", &obj) != CMD_OK || !obj ||
				!pile_contains(player->gear, obj)) {
			return AGAIN_NONE;
		}
		object_desc(name, size, obj, ODESC_BASE | ODESC_PLURAL, player);
		return last->code == CMD_FIRE ? AGAIN_FIRE : AGAIN_THROW;
	case CMD_CAST: {
		const struct class_spell *chosen;

		if (cmd_get_arg_choice(last, "spell", &spell) != CMD_OK) {
			return AGAIN_NONE;
		}
		chosen = spell_by_index(player, spell);
		if (!chosen) return AGAIN_NONE;
		my_strcpy(name, chosen->name, size);
		return AGAIN_CAST;
	}
	default:
		return AGAIN_NONE;
	}
}

/* Home's page, outside a run: the saved runs and deleting one have buttons. */
static int home_page(void)
{
	if (!home_screen || !home_screen->visible) return PAGE_OTHER;
	if (home_screen->page == SDL3_HOME_LOAD) return PAGE_HOME_LOAD;
	if (home_screen->page == SDL3_HOME_DELETE_CONFIRM) return PAGE_HOME_DELETE;
	if (home_screen->page == SDL3_HOME_ROOT) return PAGE_HOME_MENU;
	return PAGE_OTHER;
}

static void current_context(int *mode, int *primary, int *flags, int *detail)
{
	/* Whether this run has had its touch-friendly options set. */
	static bool run_configured = false;

	bool in_run = player && player->upkeep && character_generated &&
		character_dungeon && cave && player->cave && !player->is_dead &&
		player->upkeep->playing;

	*mode = MODE_NONE;
	*primary = PRIMARY_NONE;
	*flags = 0;
	*detail = 0;
	/* For every screen, prompts included: a run is in play (otherwise the
	 * rail offers only Menu and Help), and the keyset (some menus follow it). */
	if (in_run) *flags |= FLAG_IN_RUN;
	if (player && OPT(player, rogue_like_commands)) *flags |= FLAG_ROGUELIKE_KEYS;
	/* Character creation asks for a name and questions before any run. */
	if (!inkey_flag && prompt_context(mode, detail)) return;
	if (!in_run) {
		run_configured = false;
		*detail = shown_screen == UI_SCREEN_OPTIONS ? PAGE_OPTIONS :
			shown_screen == UI_SCREEN_RUN_SUMMARY ? PAGE_RUN_SUMMARY :
			home_page() != PAGE_OTHER ? home_page() : outside_page;
		return;
	}
	/* Whatever follows this run (death, Home) starts from a plain page. */
	outside_page = PAGE_OTHER;
	if (!run_configured) {
		/* Explore (under More) needs the game's autoexplore commands, which
		 * are off by default; with them on, Up and Down away from stairs
		 * walk to the nearest known staircase. Set once per run, so a
		 * player who turns them off in Options keeps that choice. */
		player->opts.opt[OPT_autoexplore_commands] = true;
		run_configured = true;
	}
	if (can_cast()) *flags |= FLAG_CAN_CAST;
	if (can_fire()) *flags |= FLAG_CAN_FIRE;
	if (can_throw()) *flags |= FLAG_CAN_THROW;

	if (!inkey_flag) {
		if (!prompt_context(mode, detail)) {
			*mode = MODE_PROMPT;
			*detail = prompt_screen();
		}
		return;
	}
	if (screen_save_depth > 0) {
		*mode = MODE_PROMPT;
		*detail = prompt_screen();
	} else if (player->fishing && player->fishing->active) {
		*mode = MODE_FISHING;
		/* enum world_fishing_phase: 1 waiting, 2 bite, 3 winding. */
		*detail = (int)player->fishing->phase;
	} else if (world_player_mode_has_capability(player,
			WORLD_MODE_CAP_SIDE_VIEW)) {
		*mode = MODE_CAVE;
		*primary = cave_primary_action();
	} else {
		char name[80];

		*mode = MODE_MAP;
		*primary = primary_action();
		if (textui_travel_uses_up_key_here(player, cave)) *flags |= FLAG_TRAVEL;
		if (player_has_monster_in_view(player)) {
			*flags |= FLAG_MONSTER_IN_VIEW;
			*detail = again_kind(name, sizeof(name));
		}
	}
}

/* ---- The highlighted item's actions ---------------------------------------- */

/* The game says Equip; the button says what it does to this item. Its
 * magic entries are shortened to fit a button. */
static const char *action_label(const struct object *obj,
	const struct context_menu_object_action *action)
{
	if (action->value == CMD_CAST) return "Use magic";
	if (action->value == CMD_STUDY) return "Learn magic";
	if (action->value != CMD_WIELD) return action->label;
	return (tval_is_melee_weapon(obj) || tval_is_launcher(obj) ||
		tval_is_digger(obj) || tval_is_light(obj)) ? "Wield" : "Wear";
}

/* How well an action suits the large button, best first: picking up a floor
 * item, then using, wearing or taking off, then throwing, then Inspect; never
 * Drop, Inscribe or Ignore unless nothing else is possible. */
static int action_rank(int value)
{
	switch (value) {
	case CMD_PICKUP: return 0;
	case CMD_THROW: return 2;
	case MENU_VALUE_INSPECT: return 3;
	case CMD_DROP:
	case MENU_VALUE_DROP_ALL:
	case CMD_INSCRIBE:
	case CMD_UNINSCRIBE:
	case CMD_IGNORE: return 4;
	default: return 1;
	}
}

/* The item's actions in button order: the large button's, then Inspect, then
 * the rest in the game's order. Returns how many were written. */
static int focus_actions(const struct object *obj,
	struct context_menu_object_action *out, int max)
{
	struct context_menu_object_action all[CONTEXT_MENU_OBJECT_ACTIONS_MAX];
	int count, i, best = -1, n = 0;

	count = context_menu_object_actions((struct object *)obj, all,
		(int)N_ELEMENTS(all));
	for (i = 0; i < count; i++) {
		if (all[i].valid && (best < 0 ||
				action_rank(all[i].value) < action_rank(all[best].value))) {
			best = i;
		}
	}
	if (best >= 0 && n < max) out[n++] = all[best];
	for (i = 0; i < count && n < max; i++) {
		if (i != best && all[i].value == MENU_VALUE_INSPECT) out[n++] = all[i];
	}
	for (i = 0; i < count && n < max; i++) {
		if (i != best && all[i].value != MENU_VALUE_INSPECT) out[n++] = all[i];
	}
	return n;
}

/* The highlighted item's actions for the buttons, one per line: value, 1 or 0
 * (available) and label, separated by tabs. On the map, with something to do
 * again, one line naming it (its ammunition, spell or missile); otherwise
 * empty. */
static void describe_verbs(char *text, size_t size, int mode, int detail)
{
	struct context_menu_object_action actions[CONTEXT_MENU_OBJECT_ACTIONS_MAX];
	size_t used = 0;
	int count, i;

	text[0] = '\0';
	if (mode == MODE_MAP && detail != AGAIN_NONE) {
		char name[80];

		if (again_kind(name, sizeof(name)) != AGAIN_NONE) {
			snprintf(text, size, "%d\t1\t%s\t0\n", detail, name);
		}
		return;
	}
	if (mode != MODE_ITEMS || !item_focus) return;
	count = focus_actions(item_focus, actions, (int)N_ELEMENTS(actions));
	for (i = 0; i < count; i++) {
		/* The fourth field: whether it can go on the quick bar (pins.c). */
		int written = snprintf(text + used, size - used, "%d\t%d\t%s\t%d\n",
			actions[i].value, actions[i].valid ? 1 : 0,
			action_label(item_focus, &actions[i]),
			pins_verb_pinnable(actions[i].value) ? 1 : 0);

		if (written < 0 || (size_t)written >= size - used) break;
		used += (size_t)written;
	}
}

bool touch_call_activity(JNIEnv *env, jobject activity, const char *name,
	const char *signature, ...)
{
	jclass activity_class = (*env)->GetObjectClass(env, activity);
	jmethodID method = (*env)->GetMethodID(env, activity_class, name, signature);
	bool called = false;

	if (method) {
		va_list args;

		va_start(args, signature);
		(*env)->CallVoidMethodV(env, activity, method, args);
		va_end(args);
		called = true;
	}
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionClear(env);
		called = false;
	}
	(*env)->DeleteLocalRef(env, activity_class);
	return called;
}

void touch_notice(const char *fmt, ...)
{
	char text[200];
	va_list args;
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity;
	jstring string;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	if (!env) return;
	activity = (jobject)SDL_GetAndroidActivity();
	if (!activity) return;
	string = (*env)->NewStringUTF(env, text);
	if (string) {
		touch_call_activity(env, activity, "onNotice",
			"(Ljava/lang/String;)V", string);
		(*env)->DeleteLocalRef(env, string);
	}
	(*env)->DeleteLocalRef(env, activity);
}

/* The game's text grid (from touch-text.c, as text is drawn): its rows, their
 * height, and where the first begins, in pixels of an output this high. */
static int grid_rows, grid_cell_height, grid_origin_y, grid_output_height;

void touch_note_text_grid(int rows, int cell_height, int origin_y,
	int output_height)
{
	grid_rows = rows;
	grid_cell_height = cell_height;
	grid_origin_y = origin_y;
	grid_output_height = output_height;
}

static void publish_context(void)
{
	static int last_mode = -1, last_primary = -1, last_flags = -1;
	static int last_detail = -1;
	static char last_verbs[1024] = "-";
	static int last_grid[4];
	char verbs[1024];
	int grid[4] = { grid_rows, grid_cell_height, grid_origin_y,
		grid_output_height };
	int mode, primary, flags, detail;
	bool context_changed, verbs_changed, grid_changed;

	if (!SDL_IsMainThread()) return;
	current_context(&mode, &primary, &flags, &detail);
	/* While browsing items or a shop's stock, a tap on a row only highlights
	 * it (src/sdl3/frontend-events.c); the buttons then act on it. */
	sdl3_host_list_tap_highlights = mode == MODE_ITEMS || mode == MODE_STORE;
	describe_verbs(verbs, sizeof(verbs), mode, detail);
	context_changed = mode != last_mode || primary != last_primary ||
		flags != last_flags || detail != last_detail;
	verbs_changed = strcmp(verbs, last_verbs) != 0;
	grid_changed = grid_rows > 0 && memcmp(grid, last_grid, sizeof(grid)) != 0;
	if (!context_changed && !verbs_changed && !grid_changed) return;

	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = (jobject)SDL_GetAndroidActivity();
	if (!env || !activity) return;
	/* Actions first, so the buttons never show a list's mode without them. */
	if (verbs_changed) {
		jstring text = (*env)->NewStringUTF(env, verbs);

		if (text && touch_call_activity(env, activity, "onGameVerbs",
				"(Ljava/lang/String;)V", text)) {
			my_strcpy(last_verbs, verbs, sizeof(last_verbs));
		}
		if (text) (*env)->DeleteLocalRef(env, text);
	}
	if (context_changed && touch_call_activity(env, activity, "onGameContext",
			"(IIII)V", mode, primary, flags, detail)) {
		last_mode = mode;
		last_detail = detail;
		last_primary = primary;
		last_flags = flags;
	}
	/* Where the hint row is, which the d-pad and buttons keep clear. */
	if (grid_changed && touch_call_activity(env, activity, "onGameGrid",
			"(IIII)V", grid[0], grid[1], grid[2], grid[3])) {
		memcpy(last_grid, grid, sizeof(grid));
	}
	(*env)->DeleteLocalRef(env, activity);
}

/* ---- Prompt tracking -------------------------------------------------- */

static bool (*game_get_string)(const char *prompt, char *buf, size_t len);
static int (*game_get_quantity)(const char *prompt, int max);
static bool (*game_get_check)(const char *prompt);
static bool (*game_get_aim_dir)(int *dir);
static bool (*game_get_item)(struct object **choice, const char *pmt,
	const char *str, cmd_code cmd, item_tester tester, int mode);

/* An action chosen from the buttons, waiting for the item's menu to ask for it
 * (game thread only; see run_requested_verb). */
#define NO_VERB (-1)
static int pending_verb = NO_VERB;
static const struct object *pending_verb_item;

static bool touch_get_item(struct object **choice, const char *pmt,
	const char *str, cmd_code cmd, item_tester tester, int mode)
{
	int outer_menu_depth = item_menu_depth;
	cmd_code outer_cmd = item_cmd;
	bool result;

	/* A quick-bar item answers this itself (pins.c). */
	if (pins_answer_item(choice, cmd, tester, mode, &result)) return result;
	item_depth++;
	/* The item list is the next menu opened. */
	item_menu_depth = menu_depth + 1;
	item_cmd = cmd;
	result = game_get_item(choice, pmt, str, cmd, tester, mode);
	item_cmd = outer_cmd;
	item_menu_depth = outer_menu_depth;
	item_depth--;
	item_focus = NULL;
	if (!result) pending_verb = NO_VERB;
	return result;
}

static bool touch_get_string(const char *prompt, char *buf, size_t len)
{
	bool result;

	text_depth++;
	result = game_get_string(prompt, buf, len);
	text_depth--;
	return result;
}

static int touch_get_quantity(const char *prompt, int max)
{
	int result;

	number_depth++;
	result = game_get_quantity(prompt, max);
	number_depth--;
	return result;
}

static bool touch_get_check(const char *prompt)
{
	bool result;

	check_depth++;
	result = game_get_check(prompt);
	check_depth--;
	return result;
}

static bool touch_get_aim_dir(int *dir)
{
	bool result;

	aim_depth++;
	result = game_get_aim_dir(dir);
	aim_depth--;
	return result;
}

static int (*game_get_spell)(struct player *p, const char *verb,
	item_tester book_filter, cmd_code cmd, const char *book_error,
	bool (*spell_filter)(const struct player *p, int spell_index),
	const char *spell_error, struct object **rtn_book);

/* A quick-bar spell answers the game's "which spell?" itself (pins.c). */
static int touch_get_spell(struct player *p, const char *verb,
	item_tester book_filter, cmd_code cmd, const char *book_error,
	bool (*spell_filter)(const struct player *p, int spell_index),
	const char *spell_error, struct object **rtn_book)
{
	bool (*outer)(const struct player *p, int spell_index) =
		spell_request_filter;
	int spell;

	if (pins_answer_spell(cmd, book_filter, spell_filter, rtn_book, &spell)) {
		return spell;
	}
	spell_request_filter = spell_filter;
	spell = game_get_spell(p, verb, book_filter, cmd, book_error, spell_filter,
		spell_error, rtn_book);
	spell_request_filter = outer;
	return spell;
}

/* The same from a given book (its menu's Use learned magic or Learn book
 * magic). */
static int (*game_get_spell_from_book)(struct player *p, const char *verb,
	struct object *book, const char *error,
	bool (*spell_filter)(const struct player *p, int spell_index));

static int touch_get_spell_from_book(struct player *p, const char *verb,
	struct object *book, const char *error,
	bool (*spell_filter)(const struct player *p, int spell_index))
{
	bool (*outer)(const struct player *p, int spell_index) =
		spell_request_filter;
	int spell;

	spell_request_filter = spell_filter;
	spell = game_get_spell_from_book(p, verb, book, error, spell_filter);
	spell_request_filter = outer;
	return spell;
}

/* touch-text.c: a semantic screen's context block worded for touch (it is
 * wrapped before it is drawn, so line rewrites cannot see it whole). */
const char *touch_block_text(const char *text, char *buffer, size_t size);

/* The frontend's semantic screen hook on the main term, passed through so
 * the screen on show is known (options, the dossier, the run summary). */
static void (*frontend_screen_hook)(const struct ui_screen *screen);

static void touch_screen_hook(const struct ui_screen *screen)
{
	static char context[4096];
	const char *text;

	shown_screen = screen ? screen->kind : UI_SCREEN_NONE;
	dossier_shown = screen && screen->kind == UI_SCREEN_CHARACTER_DOSSIER &&
		!(screen->help && strstr(screen->help, "Press any key"));
	log_shown = screen && screen->kind == UI_SCREEN_HISTORY && screen->title &&
		streq(screen->title, "Message History");
	/* Its hint offers "- next match" once text has been found. */
	log_found = log_shown && screen->help &&
		strstr(screen->help, "- next match") != NULL;
	if (screen && screen->context) {
		text = touch_block_text(screen->context, context, sizeof(context));
		if (text != screen->context) {
			struct ui_screen worded = *screen;

			worded.context = text;
			frontend_screen_hook(&worded);
			return;
		}
	}
	frontend_screen_hook(screen);
}

/* ui-input.c installs the game's hooks during start-up, before the first
 * wait for input; replace each with a counting pass-through once. */
static void install_prompt_hooks(void)
{
	/* The frontend sets the screen hook when it builds its terms, again
	 * if it rebuilds them, so this is checked at every wait. */
	if (angband_term[0] && angband_term[0]->screen_hook &&
			angband_term[0]->screen_hook != touch_screen_hook) {
		frontend_screen_hook = angband_term[0]->screen_hook;
		angband_term[0]->screen_hook = touch_screen_hook;
	}
	if (get_string_hook && get_string_hook != touch_get_string) {
		game_get_string = get_string_hook;
		get_string_hook = touch_get_string;
	}
	if (get_quantity_hook && get_quantity_hook != touch_get_quantity) {
		game_get_quantity = get_quantity_hook;
		get_quantity_hook = touch_get_quantity;
	}
	if (get_check_hook && get_check_hook != touch_get_check) {
		game_get_check = get_check_hook;
		get_check_hook = touch_get_check;
	}
	if (get_item_hook && get_item_hook != touch_get_item) {
		game_get_item = get_item_hook;
		get_item_hook = touch_get_item;
	}
	if (get_aim_dir_hook && get_aim_dir_hook != touch_get_aim_dir) {
		game_get_aim_dir = get_aim_dir_hook;
		get_aim_dir_hook = touch_get_aim_dir;
	}
	if (get_spell_hook && get_spell_hook != touch_get_spell) {
		game_get_spell = get_spell_hook;
		get_spell_hook = touch_get_spell;
	}
	if (get_spell_from_book_hook &&
			get_spell_from_book_hook != touch_get_spell_from_book) {
		game_get_spell_from_book = get_spell_from_book_hook;
		get_spell_from_book_hook = touch_get_spell_from_book;
	}
}

typedef bool (*askfor_keypress)(char *, size_t, size_t *, size_t *,
	struct keypress, bool);

bool __real_askfor_aux(char *buf, size_t len, askfor_keypress keypress_h);
bool __wrap_askfor_aux(char *buf, size_t len, askfor_keypress keypress_h)
{
	bool result;

	text_depth++;
	result = __real_askfor_aux(buf, len, keypress_h);
	text_depth--;
	return result;
}

bool __real_get_character_name(char *buf, size_t buflen);
bool __wrap_get_character_name(char *buf, size_t buflen)
{
	bool result;

	name_depth++;
	result = __real_get_character_name(buf, buflen);
	name_depth--;
	return result;
}

bool __real_get_character_name_with_refresh(char *buf, size_t buflen,
	textui_edit_refresh_hook refresh, void *user);
bool __wrap_get_character_name_with_refresh(char *buf, size_t buflen,
	textui_edit_refresh_hook refresh, void *user)
{
	bool result;

	name_depth++;
	result = __real_get_character_name_with_refresh(buf, buflen, refresh,
		user);
	name_depth--;
	return result;
}

int __real_textui_get_quantity_with_refresh(const char *prompt, int max,
	textui_edit_refresh_hook refresh, void *user);
int __wrap_textui_get_quantity_with_refresh(const char *prompt, int max,
	textui_edit_refresh_hook refresh, void *user)
{
	int result;

	number_depth++;
	result = __real_textui_get_quantity_with_refresh(prompt, max, refresh,
		user);
	number_depth--;
	return result;
}

ui_event __real_menu_select(struct menu *menu, int notify, bool popup);
/* The open spell list's menu, for its highlighted row (Pin). */
static struct menu *spell_menu;

ui_event __wrap_menu_select(struct menu *menu, int notify, bool popup)
{
	struct menu *outer_spell_menu = spell_menu;
	ui_event result;

	menu_depth++;
	if (menu_depth == spell_menu_depth) spell_menu = menu;
	result = __real_menu_select(menu, notify, popup);
	/* A spell list closing. */
	if (menu_depth == spell_menu_depth) spell_menu_depth = 0;
	spell_menu = outer_spell_menu;
	menu_depth--;
	return result;
}

bool touch_highlighted_spell(int *spell, struct object **book)
{
	struct object *obj = player && player->upkeep ? player->upkeep->object : NULL;
	int *spells = NULL;
	int count;

	/* The list's book is the tracked object: browsing, casting and studying
	 * all track it before the list opens. */
	if (!spell_menu || spell_menu_depth == 0 || menu_depth != spell_menu_depth ||
			!obj || !obj->kind || !obj_can_browse(obj)) {
		return false;
	}
	count = spell_collect_from_book(player, obj, &spells);
	if (spell_menu->cursor < 0 || spell_menu->cursor >= count) {
		mem_free(spells);
		return false;
	}
	*spell = spells[spell_menu->cursor];
	*book = obj;
	mem_free(spells);
	return true;
}

int touch_mode(void)
{
	int mode, primary, flags, detail;

	current_context(&mode, &primary, &flags, &detail);
	return mode;
}

const struct object *touch_focused_item(void)
{
	return browsing_items() ? item_focus : NULL;
}

/* From touch-text.c: a spell list's prompt ("Cast which formula? ...",
 * "Browsing formulas. ...") is drawn, just before its menu opens. */
void touch_note_spell_menu(const char *prompt)
{
	/* Drawn again inside the list: it is already known. */
	if (spell_menu_depth > 0 && spell_menu_depth <= menu_depth) return;
	spell_menu_depth = menu_depth + 1;
	if (strncmp(prompt, "Browsing", 8) == 0) {
		spell_kind = SPELL_BROWSE;
	} else if (spell_request_filter == spell_okay_to_cast) {
		spell_kind = SPELL_CAST;
	} else if (spell_request_filter == spell_okay_to_study) {
		spell_kind = SPELL_STUDY;
	} else {
		spell_kind = SPELL_OTHER;
	}
}

/* Set while the item's menu opened for a button's action is being built: the
 * next dynamic menu is that one, and answers with the action unseen. */
static bool answer_item_menu;

int __real_menu_dynamic_select(struct menu *m);
int __wrap_menu_dynamic_select(struct menu *m)
{
	int result;

	if (answer_item_menu) {
		answer_item_menu = false;
		result = pending_verb;
		pending_verb = NO_VERB;
		return result;
	}
	menu_depth++;
	result = __real_menu_dynamic_select(m);
	menu_depth--;
	return result;
}

/* A text page shown until a key is pressed (e.g. examining an item). */
struct keypress __real_textui_textblock_show(textblock *tb, region orig_area,
	const char *header);
struct keypress __wrap_textui_textblock_show(textblock *tb, region orig_area,
	const char *header)
{
	struct keypress result;

	view_depth++;
	result = __real_textui_textblock_show(tb, orig_area, header);
	view_depth--;
	return result;
}

struct store;
void __real_ui_store_screen_present(struct menu *menu, struct store *store,
	struct object *const *stock, bool inspect_only, int usable_width,
	const char *notice);
void __wrap_ui_store_screen_present(struct menu *menu, struct store *store,
	struct object *const *stock, bool inspect_only, int usable_width,
	const char *notice)
{
	store_confirming = false;
	__real_ui_store_screen_present(menu, store, stock, inspect_only,
		usable_width, notice);
}

void __real_ui_store_screen_present_confirmation(struct store *store,
	const char *title, const char *prompt, int32_t price, int usable_width);
void __wrap_ui_store_screen_present_confirmation(struct store *store,
	const char *title, const char *prompt, int32_t price, int usable_width)
{
	store_confirming = true;
	__real_ui_store_screen_present_confirmation(store, title, prompt, price,
		usable_width);
}

/* Entering a shop or the home: its item list is the next menu opened. */
void __real_use_store(game_event_type type, game_event_data *data, void *user);
void __wrap_use_store(game_event_type type, game_event_data *data, void *user)
{
	struct store *store = (player && cave) ? store_at(cave, player->grid) : NULL;
	int outer = store_menu_depth;
	bool outer_home = store_is_home;
	bool outer_donations = store_takes_donations;

	store_menu_depth = menu_depth + 1;
	store_is_home = store && store->feat == FEAT_HOME;
	store_takes_donations = store && store_accepts_larder_donations(store);
	store_confirming = false;
	__real_use_store(type, data, user);
	store_confirming = false;
	store_menu_depth = outer;
	store_is_home = outer_home;
	store_takes_donations = outer_donations;
}

/* ---- Pages outside a run ------------------------------------------------ */

/* Character creation draws each page through these (from ui-birth.c), and
 * Home through sdl3_home_open (from main-sdl3.c); each says which page is up. */
void __real_ui_birth_screen_present(struct menu *menu,
	enum ui_birth_screen_stage stage, const char *const *items,
	int item_count, const char *hint);
void __wrap_ui_birth_screen_present(struct menu *menu,
	enum ui_birth_screen_stage stage, const char *const *items,
	int item_count, const char *hint)
{
	outside_page = PAGE_BIRTH_MENU;
	__real_ui_birth_screen_present(menu, stage, items, item_count, hint);
}

void __real_ui_birth_screen_present_points(int selected, const int spent[],
	const int increase[], int remaining, const int buysell[]);
void __wrap_ui_birth_screen_present_points(int selected, const int spent[],
	const int increase[], int remaining, const int buysell[])
{
	outside_page = PAGE_BIRTH_POINTS;
	__real_ui_birth_screen_present_points(selected, spent, increase, remaining,
		buysell);
}

void __real_ui_birth_screen_present_roll(bool previous_available);
void __wrap_ui_birth_screen_present_roll(bool previous_available)
{
	outside_page = previous_available ? PAGE_BIRTH_ROLLER_PREVIOUS :
		PAGE_BIRTH_ROLLER;
	__real_ui_birth_screen_present_roll(previous_available);
}

void __real_ui_birth_screen_present_name(const char *name, size_t cursor,
	bool first_time);
void __wrap_ui_birth_screen_present_name(const char *name, size_t cursor,
	bool first_time)
{
	outside_page = PAGE_OTHER;
	__real_ui_birth_screen_present_name(name, cursor, first_time);
}

void __real_ui_birth_screen_present_history(const char *history,
	size_t cursor, bool editing);
void __wrap_ui_birth_screen_present_history(const char *history,
	size_t cursor, bool editing)
{
	outside_page = PAGE_OTHER;
	__real_ui_birth_screen_present_history(history, cursor, editing);
}

void __real_ui_birth_screen_present_confirm(void);
void __wrap_ui_birth_screen_present_confirm(void)
{
	outside_page = PAGE_OTHER;
	__real_ui_birth_screen_present_confirm();
}

struct sdl3_home_screen;
void __real_sdl3_home_open(struct sdl3_home_screen *home);
void __wrap_sdl3_home_open(struct sdl3_home_screen *home)
{
	/* Home covers whatever screen was up. */
	outside_page = PAGE_OTHER;
	shown_screen = UI_SCREEN_NONE;
	home_screen = home;
	__real_sdl3_home_open(home);
}

/* Home lists every file in the save folder as a run: the quick bar's files
 * go elsewhere first (pins.c). */
bool __real_sdl3_home_refresh_saves(struct sdl3_home_screen *home);
bool __wrap_sdl3_home_refresh_saves(struct sdl3_home_screen *home)
{
	pins_tidy_save_folder();
	return __real_sdl3_home_refresh_saves(home);
}

/* From touch-text.c: the quickstart prompt is being drawn ("New character
 * based on previous one"), a page with no screen of its own. */
void touch_note_quickstart(void)
{
	outside_page = PAGE_BIRTH_QUICKSTART;
}

/* Looking or targeting, saved around a nested one. */
struct look_state {
	int mode;
	bool can_walk;
	bool grid_known;
	struct loc grid;
};

static struct look_state begin_look(int mode, bool allow_pathfinding)
{
	struct look_state outer = { look_mode, look_can_walk, look_grid_known,
		look_grid };

	target_depth++;
	look_mode = mode;
	look_can_walk = allow_pathfinding;
	look_grid_known = false;
	return outer;
}

static void end_look(struct look_state outer)
{
	look_mode = outer.mode;
	look_can_walk = outer.can_walk;
	look_grid_known = outer.grid_known;
	look_grid = outer.grid;
	target_depth--;
}

bool __real_target_set_interactive(int mode, int x, int y,
	bool allow_pathfinding);
bool __wrap_target_set_interactive(int mode, int x, int y,
	bool allow_pathfinding)
{
	struct look_state outer = begin_look(mode, allow_pathfinding);
	bool result = __real_target_set_interactive(mode, x, y,
		allow_pathfinding);

	end_look(outer);
	return result;
}

/* The Target command (under More) targets from inside ui-target.c, past the
 * wrapper above; it chooses a target as aiming does. */
void __real_textui_target(void);
void __wrap_textui_target(void)
{
	struct look_state outer = begin_look(TARGET_KILL, true);

	__real_textui_target();
	end_look(outer);
}

/*
 * The card fills the map's height, on the side away from the cursor, where
 * it would run under the d-pad (on the left) or the buttons (on the right),
 * over the end of its description. On either side it stops above the higher
 * of them, so it is the same size wherever it goes; when that leaves too
 * little room for its portrait, the game keeps the facts and shrinks or drops
 * the portrait (src/sdl3/monster-card-layout.c). The controls say how far up
 * they reach.
 */
static SDL_AtomicInt card_inset;

JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_setCardInset(
	JNIEnv *env, jclass cls, jint bottom)
{
	(void)env;
	(void)cls;
	SDL_SetAtomicInt(&card_inset, bottom > 0 ? bottom : 0);
}

bool __real_sdl3_monster_card_layout_compute(
	struct sdl3_monster_card_layout *layout,
	int view_col, int view_row, int view_cols, int view_rows,
	int cursor_col, int cell_width, int cell_height, bool big);
bool __wrap_sdl3_monster_card_layout_compute(
	struct sdl3_monster_card_layout *layout,
	int view_col, int view_row, int view_cols, int view_rows,
	int cursor_col, int cell_width, int cell_height, bool big)
{
	int inset = SDL_GetAtomicInt(&card_inset);

	if (inset > 0 && cell_height > 0 && grid_output_height > 0) {
		int limit = grid_output_height - inset;
		int bottom = grid_origin_y + (view_row + view_rows) * cell_height;
		int rows = view_rows -
			(bottom - limit + cell_height - 1) / cell_height;

		/* Not so short that the game would show no card at all. */
		if (bottom > limit && __real_sdl3_monster_card_layout_compute(layout,
				view_col, view_row, view_cols, rows, cursor_col,
				cell_width, cell_height, big)) {
			return true;
		}
	}
	return __real_sdl3_monster_card_layout_compute(layout, view_col, view_row,
		view_cols, view_rows, cursor_col, cell_width, cell_height, big);
}

/*
 * The sidebar keeps above the d-pad, so the thumb on it hides none of the
 * hit points, stamina and the rest: with fewer rows than it has lines, the
 * game leaves out its least important, as on a shorter screen
 * (src/ui-display.c). The game's drawing is kept from when the presenter
 * makes it; the controls say how far up the d-pad reaches.
 */
static const struct sdl3_visual *game_visual;
static SDL_AtomicInt pad_height;

void touch_set_pad_height(int height)
{
	SDL_SetAtomicInt(&pad_height, height > 0 ? height : 0);
}

/* The rows at the foot of the sidebar that reach below the pad's top by more
 * than a quarter of a row, which its round top leaves clear. */
static int sidebar_rows_under_pad(void)
{
	const struct sdl3_visual *v = game_visual;
	int height = SDL_GetAtomicInt(&pad_height);
	int last_clear;

	if (!v || height <= 0 || v->cell_height <= 0 || v->rows < 3) return 0;
	last_clear = (v->output_height - height + v->cell_height / 4 -
		v->origin_y) / v->cell_height - 1;
	return SDL_max(0, v->rows - 2 - SDL_max(0, last_clear));
}

bool __real_sdl3_visual_init(struct sdl3_visual *visual, SDL_Renderer *renderer,
	const char *font_path, const char *map_font_path, int cols, int rows,
	int map_zoom_percent);
bool __wrap_sdl3_visual_init(struct sdl3_visual *visual, SDL_Renderer *renderer,
	const char *font_path, const char *map_font_path, int cols, int rows,
	int map_zoom_percent)
{
	if (!__real_sdl3_visual_init(visual, renderer, font_path, map_font_path,
			cols, rows, map_zoom_percent)) {
		return false;
	}
	game_visual = visual;
	sidebar_covered_rows_hook = sidebar_rows_under_pad;
	return true;
}

void __real_sdl3_visual_free(struct sdl3_visual *visual);
void __wrap_sdl3_visual_free(struct sdl3_visual *visual)
{
	if (visual == game_visual) game_visual = NULL;
	__real_sdl3_visual_free(visual);
}

/* The field guide, whose rail button closes it when it is open. */
void __real_do_cmd_help(void);
void __wrap_do_cmd_help(void)
{
	help_depth++;
	__real_do_cmd_help();
	help_depth--;
}

/*
 * The map can show squares beyond Angband's own map panel (zoomed out, most
 * of all), and the desktop drops clicks on them: a click reaches the game as
 * a cell of that panel, which those squares have none of. A tap on a door in
 * plain sight that does nothing is a dead end on a touch screen, so at the
 * command prompt, with nothing else waiting to be read, such a tap goes to
 * the game as a click on the panel's first square ("stand_in"), the square
 * really tapped kept here for __wrap_textui_process_click.
 */
static struct {
	bool pending;
	struct loc stand_in;
	struct loc grid;
} far_tap;

bool __real_sdl3_presenter_point_to_cell(const struct sdl3_presenter *presenter,
	float x, float y, const struct sdl3_map_view *map_view, int *col, int *row);
bool __wrap_sdl3_presenter_point_to_cell(const struct sdl3_presenter *presenter,
	float x, float y, const struct sdl3_map_view *map_view, int *col, int *row)
{
	struct sdl3_map_view whole;
	int mode, primary, flags, detail, source_col, source_row;

	if (__real_sdl3_presenter_point_to_cell(presenter, x, y, map_view, col,
			row)) {
		return true;
	}
	if (!map_view || !map_view->expanded_source || !col || !row || !Term ||
			Term->key_head != Term->key_tail) {
		return false;
	}
	current_context(&mode, &primary, &flags, &detail);
	if (mode != MODE_MAP) return false;
	/* The square under the tap, in the shown map's own cells. */
	whole = *map_view;
	whole.expanded_source = false;
	if (!__real_sdl3_presenter_point_to_cell(presenter, x, y, &whole,
			&source_col, &source_row)) {
		return false;
	}
	far_tap.grid = loc(map_view->dungeon_col + source_col - map_view->source_col,
		map_view->dungeon_row + source_row - map_view->source_row);
	far_tap.stand_in = loc(map_view->term_offset_col, map_view->term_offset_row);
	far_tap.pending = true;
	*col = map_view->term_col;
	*row = map_view->term_row;
	return true;
}

/* A plain tap on a square away from the player, as textui_process_click
 * takes one: a step to it if it is next to the player, otherwise a walk. */
static void walk_to(struct loc grid)
{
	if (!OPT(player, mouse_movement) || !square_in_bounds_fully(cave, grid) ||
			loc_eq(grid, player->grid) ||
			!square_isknown(player->cave, grid)) {
		return;
	}
	if (player->timed[TMD_CONFUSED]) {
		cmdq_push(CMD_WALK);
	} else if (ABS(grid.x - player->grid.x) <= 1 &&
			ABS(grid.y - player->grid.y) <= 1) {
		cmdq_push(CMD_WALK);
		cmd_set_arg_direction(cmdq_peek(), "direction",
			motion_dir(player->grid, grid));
	} else {
		cmdq_push(CMD_PATHFIND);
		cmd_set_arg_point(cmdq_peek(), "point", grid);
	}
}

/*
 * A tap on the map walks to that square by the game's pathfinding, which,
 * for a square the character has never seen, guesses a route through the
 * unknown and wanders until something stops it. On a touch screen such a tap
 * is nearly always a finger that missed a button, so a plain tap on an
 * unknown square does nothing; the d-pad (and Explore) go into the unknown.
 */
void __real_textui_process_click(ui_event e);
void __wrap_textui_process_click(ui_event e)
{
	if (far_tap.pending) {
		far_tap.pending = false;
		if (e.type == EVT_MOUSE && player && cave && player->cave &&
				loc_eq(loc(KEY_GRID_X(e), KEY_GRID_Y(e)), far_tap.stand_in)) {
			if (e.mouse.button == 1 && !e.mouse.mods) walk_to(far_tap.grid);
			return;
		}
	}
	if (e.type == EVT_MOUSE && e.mouse.button == 1 && !e.mouse.mods &&
			player && cave && player->cave) {
		struct loc grid = loc(KEY_GRID_X(e), KEY_GRID_Y(e));

		if (square_in_bounds_fully(cave, grid) &&
				!loc_eq(grid, player->grid) &&
				!square_isknown(player->cave, grid)) {
			return;
		}
	}
	__real_textui_process_click(e);
}

/* The look code moves the cursor to each square it describes. */
void __real_move_cursor_relative(int y, int x);
void __wrap_move_cursor_relative(int y, int x)
{
	if (target_depth > 0) {
		look_grid = loc(x, y);
		look_grid_known = true;
	}
	__real_move_cursor_relative(y, x);
}

/* ---- The roguelike keyset ------------------------------------------------ */

/*
 * The buttons send the original keyset's keys. With the roguelike keyset on,
 * a key read at the command prompt means that keyset's command, so a
 * button's key is first translated through the game's own command table:
 * the command the key runs in the original keyset, and that command's key
 * in the roguelike one (Look: l becomes x; Fire at nearest: h becomes Tab).
 * Only the command prompt reads keys through the keyset, so prompt answers
 * are left alone, as are keys a side-view cave or fishing reads for itself
 * (Grip is h in either keyset).
 */

/* The game's keycode for a button's key, or 0 if it has none here. */
static keycode_t button_keycode(SDL_Keycode key, SDL_Keymod mod, int text)
{
	if (text >= 32 && text <= 126) return (keycode_t)text;
	if ((mod & SDL_KMOD_CTRL) && key >= 'a' && key <= 'z') return KTRL(key);
	if (key == SDLK_TAB) return KC_TAB;
	return 0;
}

/* The key a button presses for the game's keycode, as typed() would. */
static bool button_key(keycode_t code, SDL_Keycode *key, SDL_Keymod *mod,
	int *text)
{
	if (code >= 'A' && code <= 'Z') {
		*key = (SDL_Keycode)(code - 'A' + 'a');
		*mod = SDL_KMOD_LSHIFT;
		*text = (int)code;
	} else if (code >= 32 && code <= 126) {
		*key = (SDL_Keycode)code;
		*mod = SDL_KMOD_NONE;
		*text = (int)code;
	} else if (code >= 1 && code <= 26) {
		*key = (SDL_Keycode)('a' + code - 1);
		*mod = SDL_KMOD_LCTRL;
		*text = 0;
	} else if (code == KC_TAB) {
		*key = SDLK_TAB;
		*mod = SDL_KMOD_NONE;
		*text = 0;
	} else {
		return false;
	}
	return true;
}

/* The command a key runs in the original keyset (later lists win, as in
 * cmd_init), or NULL. */
static const struct cmd_info *original_command(keycode_t code)
{
	const struct cmd_info *found = NULL;
	size_t i, j;

	for (j = 0; cmds_all[j].list; j++) {
		if (cmds_all[j].keymap != 0) continue;
		for (i = 0; i < cmds_all[j].len; i++) {
			if (cmds_all[j].list[i].key[0] == code) found = &cmds_all[j].list[i];
		}
	}
	return found;
}

/* Rewrites a button's key for the roguelike keyset where it is read as a
 * command; returns whether it changed. Safe from any thread: it reads flags
 * and the command table, which is fixed once the game has started. */
bool touch_translate_command_key(SDL_Keycode *key, SDL_Keymod *mod, int *text)
{
	const struct cmd_info *cmd;
	keycode_t code, rogue;

	if (!inkey_flag || !player || !character_dungeon ||
			!OPT(player, rogue_like_commands)) {
		return false;
	}
	if (player->fishing && player->fishing->active) return false;
	code = button_keycode(*key, *mod, *text);
	if (!code) return false;
	if (world_player_mode_has_capability(player, WORLD_MODE_CAP_SIDE_VIEW)) {
		struct textui_spelunking_binding binding;
		struct keypress press = { 0 };

		press.type = EVT_KBRD;
		press.code = code;
		if (textui_spelunking_translate_key(press, &binding) !=
				TEXTUI_SPELUNKING_UNHANDLED) {
			return false;
		}
	}
	cmd = original_command(code);
	if (!cmd) return false;
	rogue = cmd->key[1];
	if (!rogue || rogue == code) return false;
	return button_key(rogue, key, mod, text);
}

/* ---- Commands from the rail --------------------------------------------- */

/*
 * A rail button (Inventory, Map, Char, ...) is a map command. Pressed inside
 * another screen it would mean something else there, so the command waits:
 * each time the game waits for input away from the map, Back is pressed,
 * and the command is pressed once the map is reached. Pressed while its own
 * screen is up, it closes that screen instead (Back until it has gone). At
 * most a few Backs are tried, so a screen that cannot be left this way is not
 * fought.
 */
#define COMMAND_MAX_BACKS 4

static SDL_AtomicInt command_pending;
static int command_key, command_mod, command_text, command_backs;
static bool command_closing;

static void push_key_event(SDL_Keycode key, SDL_Keymod mod, bool down)
{
	SDL_Event event;

	SDL_zero(event);
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.key = key;
	event.key.scancode = SDL_GetScancodeFromKey(key, NULL);
	event.key.mod = mod;
	event.key.down = down;
	SDL_PushEvent(&event);
}

static void press(SDL_Keycode key, SDL_Keymod mod, int text)
{
	push_key_event(key, mod, true);
	if (text >= 32 && text <= 126) {
		static char table[95][2];
		SDL_Event event;

		table[text - 32][0] = (char)text;
		SDL_zero(event);
		event.type = SDL_EVENT_TEXT_INPUT;
		event.text.text = table[text - 32];
		SDL_PushEvent(&event);
	}
	push_key_event(key, mod, false);
}

void touch_press(SDL_Keycode key, SDL_Keymod mod, int text)
{
	press(key, mod, text);
}

bool touch_key_for_code(keycode_t code, SDL_Keycode *key, SDL_Keymod *mod,
	int *text)
{
	return button_key(code, key, mod, text);
}

JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_runCommand(JNIEnv *env,
	jclass cls, jint key, jint mod, jint text)
{
	(void)env;
	(void)cls;
	command_key = key;
	command_mod = mod;
	command_text = text;
	command_backs = 0;
	command_closing = false;
	SDL_SetAtomicInt(&command_pending, 1);
	/* Wake the game so the command is looked at straight away. */
	push_key_event(SDLK_UNKNOWN, SDL_KMOD_NONE, false);
}

/* Whether the screen the pending rail command opens is the one up (its key in
 * the original keyset). */
static bool command_screen_open(int mode)
{
	switch (button_keycode((SDL_Keycode)command_key, (SDL_Keymod)command_mod,
			command_text)) {
	case 'i': return mode == MODE_ITEMS;
	case 'C': return shown_screen == UI_SCREEN_CHARACTER_DOSSIER;
	case 'M': return shown_screen == UI_SCREEN_DUNGEON_MAP ||
		shown_screen == UI_SCREEN_WORLD_MAP;
	case KTRL('P'): return log_shown;
	case '?': return help_depth > 0;
	default: return false;
	}
}

static void run_pending_command(void)
{
	int mode, primary, flags, detail;

	if (!SDL_GetAtomicInt(&command_pending)) return;
	current_context(&mode, &primary, &flags, &detail);
	/* Its screen up, or found under one being backed out of (an item's menu
	 * over the inventory): it closes rather than opening again. */
	if (command_screen_open(mode)) command_closing = true;
	if (command_closing) {
		if (command_screen_open(mode) && command_backs++ < COMMAND_MAX_BACKS) {
			press(SDLK_ESCAPE, SDL_KMOD_NONE, 0);
		} else {
			SDL_SetAtomicInt(&command_pending, 0);
		}
		return;
	}
	if (mode == MODE_MAP || mode == MODE_CAVE || mode == MODE_NONE) {
		SDL_Keycode key = (SDL_Keycode)command_key;
		SDL_Keymod mod = (SDL_Keymod)command_mod;
		int text = command_text;

		SDL_SetAtomicInt(&command_pending, 0);
		touch_translate_command_key(&key, &mod, &text);
		press(key, mod, text);
	} else if (command_backs++ < COMMAND_MAX_BACKS) {
		press(SDLK_ESCAPE, SDL_KMOD_NONE, 0);
	} else {
		SDL_SetAtomicInt(&command_pending, 0);
	}
}

/* ---- Actions on the highlighted item ---------------------------------------- */

/*
 * In a browsed item list (Inventory, Equipment, Quiver) the buttons offer the
 * highlighted item's actions. One runs by the game's own route, so the game
 * still checks and confirms it as usual: Enter picks the item, the list calls
 * context_menu_object() for it, and its menu returns the action unseen.
 * Inspect and Browse come back to the list with the item still highlighted,
 * where the game would show the item's menu again.
 */

/* From the buttons (any thread): the chosen action's value + 1, 0 for none. */
static SDL_AtomicInt verb_request;

/* After Inspect or Browse from a button: the item whose menu is not wanted
 * again, then the item to highlight when the list is back. */
static const struct object *return_to_list;
static const struct object *restore_cursor;

JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_runVerb(JNIEnv *env,
	jclass cls, jint value)
{
	(void)env;
	(void)cls;
	SDL_SetAtomicInt(&verb_request, value + 1);
	/* Wake the game so the action is looked at straight away. */
	push_key_event(SDLK_UNKNOWN, SDL_KMOD_NONE, false);
}

static void run_requested_verb(void)
{
	struct context_menu_object_action actions[CONTEXT_MENU_OBJECT_ACTIONS_MAX];
	int request = SDL_SetAtomicInt(&verb_request, 0);
	int count, i;

	if (!request || !browsing_items() || !item_focus) return;
	count = context_menu_object_actions((struct object *)item_focus, actions,
		(int)N_ELEMENTS(actions));
	for (i = 0; i < count; i++) {
		if (actions[i].value == request - 1 && actions[i].valid) break;
	}
	if (i == count) return;
	pending_verb = request - 1;
	pending_verb_item = item_focus;
	press(SDLK_RETURN, SDL_KMOD_NONE, 0);
}

void __real_ui_object_screen_present(struct menu *menu,
	const struct ui_object_screen_item *items, int item_count,
	olist_detail_t detail_mode, int item_mode, bool allow_all,
	const char *prompt, const char *header);
void __wrap_ui_object_screen_present(struct menu *menu,
	const struct ui_object_screen_item *items, int item_count,
	olist_detail_t detail_mode, int item_mode, bool allow_all,
	const char *prompt, const char *header)
{
	item_focus = menu && items && menu->cursor >= 0 &&
		menu->cursor < item_count ? items[menu->cursor].object : NULL;
	/* Drawn again before Enter reached the item's menu: drop the action. */
	pending_verb = NO_VERB;
	if (restore_cursor) {
		int i = 0;

		while (i < item_count && items[i].object != restore_cursor) i++;
		restore_cursor = NULL;
		if (menu && i < item_count && menu->cursor >= 0) {
			int delta;

			for (delta = i - menu->cursor; delta > 0; delta--) {
				press(SDLK_DOWN, SDL_KMOD_NONE, 0);
			}
		}
	}
	__real_ui_object_screen_present(menu, items, item_count, detail_mode,
		item_mode, allow_all, prompt, header);
}

int __real_context_menu_object(struct object *obj);
int __wrap_context_menu_object(struct object *obj)
{
	bool chosen;
	int result;

	if (obj && obj == return_to_list) {
		/* 3: as if the menu were closed, so the list shows again. */
		return_to_list = NULL;
		restore_cursor = obj;
		return 3;
	}
	return_to_list = NULL;
	chosen = pending_verb != NO_VERB && obj == pending_verb_item;
	if (!chosen) pending_verb = NO_VERB;
	answer_item_menu = chosen;
	result = __real_context_menu_object(obj);
	answer_item_menu = false;
	pending_verb = NO_VERB;
	/* 2: shown and finished (Inspect, Browse); the game would reopen the menu. */
	if (chosen && result == 2) return_to_list = obj;
	return result;
}

/* ---- Waiting for input ------------------------------------------------- */

bool __real_SDL_WaitEvent(SDL_Event *event);
bool __real_SDL_WaitEventTimeout(SDL_Event *event, Sint32 timeoutMS);

bool __wrap_SDL_WaitEvent(SDL_Event *event)
{
	install_prompt_hooks();
	run_pending_command();
	run_requested_verb();
	pins_wait();
	publish_context();
	return __real_SDL_WaitEvent(event);
}

bool __wrap_SDL_WaitEventTimeout(SDL_Event *event, Sint32 timeoutMS)
{
	install_prompt_hooks();
	run_pending_command();
	run_requested_verb();
	pins_wait();
	publish_context();
	return __real_SDL_WaitEventTimeout(event, timeoutMS);
}

/* Each wrapper above keeps the game's signature (touch.h). */
TOUCH_CHECK_WRAP(SDL_WaitEvent);
TOUCH_CHECK_WRAP(SDL_WaitEventTimeout);
TOUCH_CHECK_WRAP(askfor_aux);
TOUCH_CHECK_WRAP(get_character_name);
TOUCH_CHECK_WRAP(get_character_name_with_refresh);
TOUCH_CHECK_WRAP(textui_get_quantity_with_refresh);
TOUCH_CHECK_WRAP(menu_select);
TOUCH_CHECK_WRAP(menu_dynamic_select);
TOUCH_CHECK_WRAP(textui_textblock_show);
TOUCH_CHECK_WRAP(ui_store_screen_present);
TOUCH_CHECK_WRAP(ui_store_screen_present_confirmation);
TOUCH_CHECK_WRAP(use_store);
TOUCH_CHECK_WRAP(ui_birth_screen_present);
TOUCH_CHECK_WRAP(ui_birth_screen_present_points);
TOUCH_CHECK_WRAP(ui_birth_screen_present_roll);
TOUCH_CHECK_WRAP(ui_birth_screen_present_name);
TOUCH_CHECK_WRAP(ui_birth_screen_present_history);
TOUCH_CHECK_WRAP(ui_birth_screen_present_confirm);
TOUCH_CHECK_WRAP(sdl3_home_open);
TOUCH_CHECK_WRAP(sdl3_home_refresh_saves);
TOUCH_CHECK_WRAP(target_set_interactive);
TOUCH_CHECK_WRAP(textui_target);
TOUCH_CHECK_WRAP(move_cursor_relative);
TOUCH_CHECK_WRAP(ui_object_screen_present);
TOUCH_CHECK_WRAP(context_menu_object);
TOUCH_CHECK_WRAP(do_cmd_help);
TOUCH_CHECK_WRAP(textui_process_click);
TOUCH_CHECK_WRAP(sdl3_presenter_point_to_cell);
TOUCH_CHECK_WRAP(sdl3_monster_card_layout_compute);
TOUCH_CHECK_WRAP(sdl3_visual_init);
TOUCH_CHECK_WRAP(sdl3_visual_free);

/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * Touch wording for the game's keyboard hints.
 *
 * The desktop game explains its controls in keyboard terms ("Up/Down moves
 * Enter selects Escape returns", "F1 / ? opens field guide"). On a touch
 * screen those keys are the d-pad and the OK, Back and Help buttons, so the
 * two functions that put hint text on screen are wrapped at link time
 * (-Wl,--wrap, see CMakeLists.txt) and the text is reworded here first.
 *
 * Whole lines are replaced exactly; footer hints are then reworded phrase by
 * phrase. Only key-hint phrases are matched, never ordinary words, so game
 * text such as "Enter and explore a cave" is left alone.
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <string.h>

#include "angband.h"
#include "ui-term.h"
#include "sdl3/monster-card-presenter.h"
#include "sdl3/render-internal.h"
#include "sdl3/ui-draw.h"
#include "touch.h"
#include "z-textblock.h"

struct rewrite {
	const char *from;
	const char *to;
};

/* Whole strings, replaced exactly. */
static const struct rewrite lines[] = {
	/* Help / Controls (game menu) and Records & About (Home) */
	{ "Arrow keys, keypad, or classic roguelike keys",
		"D-pad, or tap a square to walk there" },
	{ "L, then movement keys; Escape cancels Look",
		"Look, then the d-pad; Back ends Look" },
	{ "L, then movement keys; Escape closes the card",
		"Look, then the d-pad; Back closes the card" },
	{ "Mouse wheel or Ctrl + Plus/Minus; Ctrl + 0 resets",
		"Pinch the map, or Zoom in / Zoom out under More" },
	{ "Ctrl + Alt + S", "Menu, then Settings" },
	{ "Ctrl + S saves; Ctrl + X saves and returns Home",
		"Saved whenever you leave the app; Menu > Save and Return Home" },
	{ "F1 or ? opens controls, world, fishing, and cave help",
		"The Help button opens controls, world, fishing, and cave help" },
	{ "F1 or ? opens Ammerow controls, modes, and map reference",
		"The Help button opens controls, modes, and map reference" },
	{ "Ctrl+1/2/3 toggles top messages, stats, bottom messages",
		"Log shows earlier messages; Char shows your character" },
	{ "F11 or Alt + Enter", "Always full screen on Android" },
	{ "Ctrl + 1/2/3 during play", "Log and Char buttons during play" },
	{ "Ctrl+1 top msg   Ctrl+2 stats   Ctrl+3 bottom msg", "" },
	{ "Ctrl+Plus/Minus or the mouse wheel zooms the play area, Ctrl+0 resets map zoom",
		"Pinch the map to zoom the play area" },
	{ "Escape returns home   F1 / ? opens field guide",
		"Back returns home   Help opens the field guide" },
	{ "MOUSE OR ARROWS SELECT   ENTER CONFIRMS   F1 HELP",
		"TAP A CHOICE   OK CONFIRMS   HELP OPENS THE GUIDE" },
	{ "Click cycles   Arrows select/change   Escape categories   F1 / ? manual",
		"Tap cycles   D-pad selects and changes   Back for categories   Help opens the guide" },
	{ "PgUp/PgDn / wheel scroll   Esc home", "PgUp/PgDn scroll   Back returns home" },

	/* Prompts */
	{ "Direction ('*' or <click> to target, \"'\" for closest, Escape to cancel)? ",
		"Direction? D-pad, tap a target, Nearest, Choose, or Back: " },
	{ "Direction ('5' for target, '*' or <click> to re-target, Escape to cancel)? ",
		"Direction? At target, tap or Choose to re-target, or Back: " },
	{ "Direction (Escape to cancel)? ", "Direction? (d-pad, or Back) " },
	{ "Direction or <click> (Escape to cancel)? ", "Direction? (d-pad or tap, or Back) " },
	{ "Enter a name for your character (* for a random name): ",
		"Name your character (Random for a random name): " },
	{ "Typing replaces the suggested name; an arrow key keeps it.\n",
		"Type... replaces the suggested name.\n" },
	{ "Fish on the hook! Press Enter to strike; you have three waits of grace.",
		"Fish on the hook! Strike now; you have three waits of grace." },
	{ "Press Return (or Escape).", "Press OK." },
	{ "[Press Space to advance, or ESC to exit.]", "[Tap or PgDn advances, Back exits.]" },
	{ "[Press a Letter, or ESC to exit.]", "[Tap a section, or Back to exit.]" },
	{ "[Press ESC to exit.]", "[Back to exit.]" },
	{ "Type an amount   * takes all   Enter accepts   Escape cancels",
		"Digits set the amount   All takes all   OK accepts   Back cancels" },
	{ "Type to edit   Left/Right moves   * randomises   Enter accepts   Escape goes back",
		"Random picks a name   Type... edits   OK accepts   Back goes back" },
	{ "Type to edit   Arrows move   Enter accepts   Escape cancels",
		"Type... edits   OK accepts   Back cancels" },
	{ "Up/Down moves   Enter selects   * random   @ finish randomly   Escape goes back",
		"Tap or d-pad chooses   OK selects   Random   Auto finishes randomly   Back goes back" },
	{ "Up/Down moves   Enter selects   = birth options   * random   @ finish randomly   Escape goes back",
		"Tap or d-pad chooses   OK selects   Options   Random   Auto finishes randomly   Back goes back" },
	{ "Up/Down selects  Left/Right adjusts  R resets  Enter accepts  Esc back",
		"D-pad up/down selects, left/right adjusts   Reset   OK accepts   Back goes back" },
	{ "R rerolls   P restores previous   Enter accepts   Escape goes back",
		"Reroll   Previous brings back the last roll   OK accepts   Back goes back" },
	{ "R rerolls   Enter accepts   Escape goes back", "Reroll   OK accepts   Back goes back" },
	{ "Enter confirms | Esc returns Home | I character | M messages | X items | H history | F dump | V scores | S spoilers",
		"OK confirms   Back returns Home   Character, Messages, Items, History and Scores are beside the d-pad" },
	{ "Up/Down or 8/2 scroll   H or Left/Right changes page   C renames   F exports   Escape returns",
		"D-pad scrolls, left/right changes page   Rename   Back returns" },
	{ "Up/Down selects   Enter/click toggles   Y/N sets   Escape returns",
		"Tap an option, or Toggle, to switch it   Back returns" },
	{ "Enter toggles  Y/N sets  S saves defaults  R restores  X resets  Esc returns",
		"Tap an option, or Toggle, to switch it   Back returns" },
	{ "Arrows / mouse move   Enter / click selects   Escape goes back",
		"Tap a command, or the d-pad and OK   Back goes back" },
	{ "Left/Right reach   Up/Down depth   Space wait   Esc stop",
		"D-pad sets reach and depth   Wait   Leave stops" },
	{ "Enter strike   Space wait   Esc stop", "Strike!   Wait   Leave stops" },
	{ "Enter actions   D sells   L examines   ? help   Escape leaves",
		"Tap an item, then Buy or Examine   Sell offers your own   Back leaves" },
	{ "Enter actions   d sells   Shift+D donates   L examines   Escape leaves",
		"Tap an item, then Buy or Examine   Sell or Donate yours   Back leaves" },
	{ "Enter actions   D stashes   L examines   ? help   Escape leaves",
		"Tap an item, then Take or Examine   Stash stores your own   Back leaves" },
	{ "Enter examines   Escape returns", "Tap an item to examine it   Back returns" },
	{ "Press '?' for help.", "" },
	{ "(Press any key to continue.)", "(Tap or OK to continue.)" },
	{ "[Press any key to continue]", "[Tap or OK to continue]" },
	{ "Press any key to continue", "Tap or OK to continue" },
	{ "  Ctrl-C Fish", "  Fish (More)" },
	{ "Enter begins   Escape revisits history   S starts character creation over",
		"OK begins   Back revisits history" },
	{ "Y or Enter accepts   N edits   Escape returns to naming",
		"OK accepts   Back returns to naming" },
	{ "['Enter' to begin, 'ESC' to step back, or 'S' to start over]",
		"[OK begins, Back steps back]" },

	/* The Fishing Shop's quest briefing (Char opens the quest log) */
	{ "Press any key to enter the shop   Reopen with Shift+C, then Quests",
		"OK enters the shop   Reopen with Char, then Quests" },
	/* The field guide (its own buttons: PgDn, Back, PgUp, Find) */
	{ "Letter: open | Arrows: scroll | Space/PgUp: page | /: search | Esc: back",
		"Tap a section to open it | D-pad: scroll | PgUp/PgDn: page | Find | Back: return" },
	{ "Arrows: scroll | Space/PgUp: page | /: search | Esc: back | ?: close guide",
		"D-pad: scroll | Tap or PgDn: next page | PgUp: back a page | Find | Back: return" },
	/* Story cards: the opening, quest briefings and donation receipts */
	{ "Enter / Space / Escape or click to continue", "Tap, OK or Back to continue" },
	/* Home's About page */
	{ "Escape returns to About   F1 / ? opens the Field Guide",
		"Back returns to About   Help opens the Field Guide" },
	/* The cave's status strip, when there is no climbing cost before it */
	{ "Ctrl-C: Fish", "Fish: large button" },
	/* Looking: the line along the bottom, which only shows beside the map
	 * (the buttons say the rest) */
	{ "Look: arrows/keypad move one grid; Esc exits; ? for help.", "Looking" },
	/* Knowledge, abilities and the world's introduction */
	{ "Up/Down or 8/2 browses   Enter opens   Letters open entries   1-9 opens shops   Escape returns",
		"Tap an entry, or the d-pad and OK   Back returns" },
	{ "Up/Down or 8/2 browses   Letter shortcuts work   Escape returns",
		"D-pad browses   Back returns" },
	{ "Left/Right tabs  Up/Down list/scroll",
		"D-pad left/right changes tab, up/down scrolls" },
	/* Nearby creatures: the order is a keyboard extra */
	{ "Up/Down scrolls   PgUp/PgDn pages   X changes order   Escape or [ returns",
		"D-pad scrolls   PgUp/PgDn pages   Back returns" },
	/* The message log: Find and Next match are beside the d-pad */
	{ "Up/Down scrolls   PgUp/PgDn pages   Left/Right slides   = finds text   Escape returns",
		"D-pad scrolls and slides   PgUp/PgDn pages   Find searches   Back returns" },
	{ "Up/Down scrolls   PgUp/PgDn pages   Left/Right slides   - next match   = new find   Escape returns",
		"D-pad scrolls and slides   Next match   Find searches again   Back returns" },
	/* Fishing as text (the buttons name each action); no longer than the
	 * original, which is placed by its length */
	{ "Up/KP8 reel   Esc stop", "Reel   Leave stops" },
	/* The General Store's prompt line */
	{ "Press '?' for help; D donates a fishing catch.",
		"Donate gives a fishing catch to the village larder." },
	/* A long calculation the player may stop */
	{ "To break out, press Escape or click the second mouse button.",
		"To break out, press Back." },
};

/* Phrases within footer hints, applied in order, every occurrence. */
static const struct rewrite phrases[] = {
	{ "(ESC to cancel, Enter to select)", "(tap or OK selects, Back cancels)" },
	{ "(Esc to cancel, Enter to select)", "(tap or OK selects, Back cancels)" },
	{ "(Enter to select command, ESC to cancel)", "(OK selects, Back cancels)" },
	{ "(Enter to select, ESC)", "(OK selects, Back cancels)" },
	{ "Escape or any key returns", "Back returns" },
	{ "Enter or Escape returns", "OK returns" },
	{ "Esc returns", "Back returns" },
	{ "Escape or ] returns", "Back returns" },
	{ "Escape or [ returns", "Back returns" },
	{ "N or Escape cancels", "No cancels" },
	{ "Any key accepts", "Yes accepts" },
	{ "Y deletes", "Yes deletes" },
	{ "Escape returns", "Back returns" },
	{ "Escape cancels", "Back cancels" },
	{ "Escape goes back", "Back goes back" },
	{ "Escape leaves", "Back leaves" },
	{ "Escape closes", "Back closes" },
	{ "Escape categories", "Back for categories" },
	{ "Click or Enter", "Tap or OK" },
	{ "Enter, Down or Space advances", "OK or the d-pad advances" },
	{ "Enter accepts", "OK accepts" },
	{ "Enter selects", "OK selects" },
	{ "Enter confirms", "OK confirms" },
	{ "Enter opens", "OK opens" },
	{ "Enter loads", "OK loads" },
	{ "Enter chooses", "OK chooses" },
	{ "Enter travels", "OK travels" },
	{ "Enter examines", "OK examines" },
	{ "Enter to travel", "OK to travel" },
	{ "Up/Down or 8/2 browses", "D-pad browses" },
	{ "Up/Down or 8/2 scroll", "D-pad scrolls" },
	{ "H or Left/Right changes page", "D-pad left/right changes page" },
	{ "PgUp/PgDn or Space pages", "PgUp/PgDn pages" },
	{ "Up shows the previous page", "D-pad up shows the previous page" },
	{ "Up/Down scroll ", "D-pad scrolls " },
	{ "Left/Right", "D-pad left/right" },
	{ "Up/Down", "D-pad" },
	/* Only as the start of a hint: "Arrows" alone is also the name of an
	 * item (and in "Create Arrows"), which must keep it. */
	{ "Arrows browse", "D-pad browses" },
	{ "Arrows choose", "D-pad chooses" },
	{ "Arrows move", "D-pad moves" },
	{ "F1 / ? opens field guide", "Help opens the field guide" },
	{ "F1 / ? field guide", "Help opens the field guide" },
	{ "Delete / D removes", "Delete removes" },
	/* Some of these wait for a key alone, which OK always is. */
	{ "Press any key to return", "OK returns" },
	{ "Press any key to continue", "OK continues" },
	{ "Hit any key to continue", "OK continues" },
	/* The quest log is a page of the character sheet (Char). */
	{ "Shift+C > Quests", "Char > Quests" },
	{ "   Ctrl-C: Fish", "   Fish: large button" },
};

/* android/context.c: an item list is open just to browse it, and the buttons
 * offer the highlighted item's actions. */
bool touch_browsing_items(void);

/* android/context.c: the quickstart prompt is up (it has no screen of its own,
 * so its text is how the buttons learn of it). */
void touch_note_quickstart(void);

#define QUICKSTART_PROMPT \
	"['Y': use as is; 'N': redo; 'C': change name/history; '=': set birth options]"

/* android/context.c: a spell list's prompt is drawn; its menu opens next. */
void touch_note_spell_menu(const char *prompt);

/* How every spell list's prompt ends ("Cast which formula? ...", "Browsing
 * formulas. ..."), and what the touch version says instead. */
#define SPELL_PROMPT_END "('?' to toggle description)"
#define SPELL_PROMPT_TOUCH "(Details shows or hides what each does)"

/* An item list's key summary, e.g. "(Inven: a-d, / for Equip, ESC)", which
 * the game pads with spaces to the list's width. */
static bool item_list_keys(const char *text)
{
	static const char *const starts[] = {
		"(Inven:", "(Equip:", "(Quiver:", "(Floor:", "(Throwing items:"
	};
	size_t i, length = strlen(text);

	while (length > 0 && text[length - 1] == ' ') length--;
	if (length < 6 || strncmp(text + length - 5, " ESC)", 5) != 0) return false;
	for (i = 0; i < N_ELEMENTS(starts); i++) {
		if (strncmp(text, starts[i], strlen(starts[i])) == 0) return true;
	}
	return false;
}

/* Phrases in a semantic screen's context block, reworded before the block
 * is wrapped into lines (see touch_block_text). */
static const struct rewrite block_phrases[] = {
	{ "The immediately previous roll is still available with P.",
		"The previous roll can still be brought back with Previous." },
	/* Naming the character */
	{ "Leave it blank and press Enter for a random name.",
		"Leave it blank and press OK for a random name." },
	{ "Typing replaces the suggested name; an arrow key keeps it.",
		"Type... replaces the suggested name; OK keeps it." },
	/* The Fishing Shop's quest briefing */
	{ "Press Shift+C > Quests at any time", "Tap Char, then Quests, at any time" },
	/* The First Sounding (lib/gamedata/story.txt) and donation receipts */
	{ "Shift+C > Quests", "Char > Quests" },
};

/* Replaces every occurrence of each phrase in buffer; true if any changed. */
static bool substitute(const struct rewrite *list, size_t count, char *buffer,
	size_t size)
{
	size_t i;
	bool changed = false;

	for (i = 0; i < count; i++) {
		char *found;
		size_t from_len = strlen(list[i].from);
		size_t to_len = strlen(list[i].to);

		while ((found = strstr(buffer, list[i].from)) != NULL) {
			size_t tail = strlen(found + from_len);

			if ((size_t)(found - buffer) + to_len + tail + 1 > size) break;
			memmove(found + to_len, found + from_len, tail + 1);
			memcpy(found, list[i].to, to_len);
			changed = true;
		}
	}
	return changed;
}

/* For android/context.c: a screen's context block worded for touch, in
 * buffer, or text itself when nothing applies. */
const char *touch_block_text(const char *text, char *buffer, size_t size)
{
	size_t i;

	if (!text || strlen(text) >= size) return text;
	for (i = 0; i < N_ELEMENTS(block_phrases); i++) {
		if (strstr(text, block_phrases[i].from)) break;
	}
	if (i == N_ELEMENTS(block_phrases)) return text;
	my_strcpy(buffer, text, size);
	return substitute(block_phrases, N_ELEMENTS(block_phrases), buffer, size) ?
		buffer : text;
}

/* Rewrites into buffer; returns text itself when nothing applies. */
static const char *touch_text(const char *text, char *buffer, size_t size)
{
	size_t i;

	if (!text || !text[0]) return text;
	/* A new character based on the previous one: its keys are buttons. */
	if (strcmp(text, QUICKSTART_PROMPT) == 0) {
		touch_note_quickstart();
		return "[Use as is, Redo, Change name, or Options]";
	}
	/* A spell list: the buttons are Cast (or Study) and Describe. */
	{
		size_t length = strlen(text), end = strlen(SPELL_PROMPT_END);

		if (length >= end && strcmp(text + length - end, SPELL_PROMPT_END) == 0) {
			touch_note_spell_menu(text);
			if (length - end + strlen(SPELL_PROMPT_TOUCH) < size) {
				memcpy(buffer, text, length - end);
				my_strcpy(buffer + length - end, SPELL_PROMPT_TOUCH,
					size - (length - end));
				return buffer;
			}
			return text;
		}
	}
	/* Item lists: tabs replace the letter keys; browsing, a tap shows the
	 * item's actions on the buttons, otherwise it chooses the item. */
	if (item_list_keys(text)) {
		return touch_browsing_items() ? "Tap an item to see what you can do with it" :
			"Tap an item to choose it";
	}
	if (touch_browsing_items() && strcmp(text,
			"Up/Down moves   Enter selects   Left/Right changes list   Escape cancels") == 0) {
		return "D-pad moves   D-pad left/right changes list   Back closes";
	}
	for (i = 0; i < N_ELEMENTS(lines); i++) {
		if (strcmp(text, lines[i].from) == 0) return lines[i].to;
	}
	/* Hints always name one of these; skip the phrase pass for other text. */
	if (!strstr(text, "Esc") && !strstr(text, "ESC") && !strstr(text, "Enter") &&
			!strstr(text, "Up/Down") && !strstr(text, "Left/Right") &&
			!strstr(text, "Arrows ") && !strstr(text, "F1") &&
			!strstr(text, "Click") && !strstr(text, "Any key") &&
			!strstr(text, "any key") && !strstr(text, "Shift+") &&
			!strstr(text, "Y deletes") && !strstr(text, "Up shows") &&
			!strstr(text, "Ctrl-C")) {
		return text;
	}
	if (strlen(text) >= size) return text;
	my_strcpy(buffer, text, size);
	return substitute(phrases, N_ELEMENTS(phrases), buffer, size) ? buffer : text;
}

/*
 * A spell list's reminders name the keys for learning and using magic. They
 * are written through text_out_c a letter at a time (spell_menu_browser in
 * ui-spell.c), past the line wrappers below, so their formats are swapped
 * here for touch ones taking the same arguments (the keys go unused).
 */
static const struct rewrite text_out_formats[] = {
	{ "\nActive %s, not passive. Learn: %c   Use: %c.\n",
		"\nActive %s, not passive: Learn magic once, then Use magic each time.\n" },
	{ "Already learned; use it with %c.\n", "Already learned: Use magic uses it.\n" },
	{ "Not learned: leave this menu and press %c to study.\n",
		"Not learned: leave this list and Learn magic first.\n" },
};

/* As text_out_c (z-textblock.c) does, with a touch format where one applies. */
void __wrap_text_out_c(uint8_t a, const char *fmt, ...)
{
	const char *format = fmt;
	char buf[1024];
	va_list vp;
	size_t i;

	for (i = 0; i < N_ELEMENTS(text_out_formats); i++) {
		if (streq(fmt, text_out_formats[i].from)) {
			format = text_out_formats[i].to;
			break;
		}
	}
	va_start(vp, fmt);
	(void)vstrnfmt(buf, sizeof(buf), format, vp);
	va_end(vp);
	text_out_hook(a, buf);
}

/* The look card's footer is drawn glyph by glyph, past the wrappers below,
 * so it is reworded where the card is filled in (from inspect-card-presenter.c). */
void __real_sdl3_monster_card_configure(struct sdl3_inspect_card *card,
	const struct sdl3_map_view *view, struct term *main_term,
	int cursor_col, int cursor_row, bool cursor_visible,
	bool settings_visible, bool big);
void __wrap_sdl3_monster_card_configure(struct sdl3_inspect_card *card,
	const struct sdl3_map_view *view, struct term *main_term,
	int cursor_col, int cursor_row, bool cursor_visible,
	bool settings_visible, bool big)
{
	__real_sdl3_monster_card_configure(card, view, main_term, cursor_col,
		cursor_row, cursor_visible, settings_visible, big);
	if (card && strcmp(card->footer, "r: full recall") == 0) {
		my_strcpy(card->footer, "Recall: all you know of it",
			sizeof(card->footer));
	}
}

errr __real_Term_putstr(int x, int y, int n, int a, const char *s);
errr __wrap_Term_putstr(int x, int y, int n, int a, const char *s)
{
	char buffer[1024];
	const char *text;

	/* A counted fragment is part of something longer; leave it as it is. */
	if (n >= 0 && s && (size_t)n < strlen(s)) return __real_Term_putstr(x, y, n, a, s);
	text = touch_text(s, buffer, sizeof(buffer));
	return __real_Term_putstr(x, y, text == s ? n : -1, a, text);
}

/* android/context.c: the text grid, so the buttons can keep its hint row
 * clear. */
void touch_note_text_grid(int rows, int cell_height, int origin_y,
	int output_height);

/* Screens put their hint on the second row from the bottom, which the d-pad
 * and the buttons beside it keep clear; these pages put theirs a row higher
 * (Home's saved runs, records and controls, the credits and licences, the
 * settings, the game menu's controls), so they are drawn a row lower. */
static bool hint_a_row_high(const char *text)
{
	static const char *const starts[] = {
		"Enter loads   ",
		"Click or Enter opens   ",
		"Escape returns home   F1",
		"Escape returns to About",
		"Up/Down scroll   ",
		"Click cycles   ",
		"Escape returns to the game menu",
	};
	size_t i;

	for (i = 0; i < N_ELEMENTS(starts); i++) {
		if (strncmp(text, starts[i], strlen(starts[i])) == 0) return true;
	}
	return false;
}

void __real_sdl3_ui_draw_text(struct sdl3_visual *visual, const char *text,
	int col, int row, int maximum, SDL_Color color);
void __wrap_sdl3_ui_draw_text(struct sdl3_visual *visual, const char *text,
	int col, int row, int maximum, SDL_Color color)
{
	char buffer[1024];

	if (visual) {
		touch_note_text_grid(visual->rows, visual->cell_height,
			visual->origin_y, visual->output_height);
		if (text && row == visual->rows - 3 && hint_a_row_high(text)) {
			row = visual->rows - 2;
		}
	}
	__real_sdl3_ui_draw_text(visual, touch_text(text, buffer, sizeof(buffer)),
		col, row, maximum, color);
}

/* prt() and c_prt() write prompts and footers through Term_addstr. */
errr __real_Term_addstr(int n, int a, const char *buf);
errr __wrap_Term_addstr(int n, int a, const char *buf)
{
	char buffer[1024];
	const char *text;

	if (n >= 0 && buf && (size_t)n < strlen(buf)) return __real_Term_addstr(n, a, buf);
	text = touch_text(buf, buffer, sizeof(buffer));
	return __real_Term_addstr(text == buf ? n : -1, a, text);
}

/* Each wrapper above keeps the game's signature (touch.h). */
TOUCH_CHECK_WRAP(sdl3_monster_card_configure);
TOUCH_CHECK_WRAP(Term_putstr);
TOUCH_CHECK_WRAP(sdl3_ui_draw_text);
TOUCH_CHECK_WRAP(Term_addstr);
TOUCH_CHECK_WRAP(text_out_c);

/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * Android entry point for Ammerow.
 *
 * Installs the runtime tree (lib/) from the APK into internal storage, makes
 * that the working directory so the desktop's relative ./lib/ paths work
 * unchanged, then runs the desktop main() (compiled as ammerow_main).
 *
 * Saves the run when Android sends the game to the background, since
 * Android may then end the process without warning, and offers that run as
 * Home's Continue on the next launch.
 *
 * Also hosts the small JNI surface the Kotlin touch controls use: every
 * control presses the game's own keys, exactly as a keyboard would.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <errno.h>
#include <jni.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "angband.h"
#include "game-world.h"
#include "player.h"
#include "savefile.h"
#include "sdl3/config.h"
#include "sdl3/home-model.h"
#include "sdl3/host.h"
#include "sdl3/layout.h"
#include "sdl3/render.h"
#include "touch.h"
#include "ui-game.h"

#define INDEX_NAME "ammerow-assets.txt"
#define INDEX_HEADER "ammerow-assets 1"
#define STAMP_NAME ".ammerow-assets-stamp"
/* Names the run saved on entering the background; removed on return. */
#define RESUME_MARKER "lib/user/android-resume.txt"

int ammerow_main(int argc, char *argv[]);

static AAssetManager *open_asset_manager(JNIEnv *env, jobject *global_ref)
{
	jobject activity = (jobject)SDL_GetAndroidActivity();
	AAssetManager *manager = NULL;
	if (!env || !activity) return NULL;
	jclass activity_class = (*env)->GetObjectClass(env, activity);
	jmethodID get_assets = (*env)->GetMethodID(env, activity_class, "getAssets",
		"()Landroid/content/res/AssetManager;");
	jobject assets = get_assets ? (*env)->CallObjectMethod(env, activity, get_assets) : NULL;
	if (assets) {
		*global_ref = (*env)->NewGlobalRef(env, assets);
		manager = AAssetManager_fromJava(env, *global_ref);
		(*env)->DeleteLocalRef(env, assets);
	}
	(*env)->DeleteLocalRef(env, activity_class);
	(*env)->DeleteLocalRef(env, activity);
	return manager;
}

/* mkdir -p for the directories above path. */
static void make_parents(char *path)
{
	for (char *p = path + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(path, 0755);
		*p = '/';
	}
}

static bool read_installed_stamp(const char *root, char *stamp, size_t size)
{
	char path[1024];
	FILE *fp;
	snprintf(path, sizeof(path), "%s/%s", root, STAMP_NAME);
	fp = fopen(path, "r");
	if (!fp) return false;
	bool ok = fgets(stamp, (int)size, fp) != NULL;
	fclose(fp);
	if (ok) stamp[strcspn(stamp, "\r\n")] = '\0';
	return ok;
}

/*
 * Copies every file listed in the packaged index unless the installed stamp
 * already matches. Only listed files are written, so saves, scores and
 * settings in the writable parts of lib/ survive an upgrade.
 */
static bool install_runtime(const char *root, char *error, size_t error_size)
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject assets_ref = NULL;
	AAssetManager *manager = open_asset_manager(env, &assets_ref);
	AAsset *index_asset;
	char *index = NULL, *line, *save = NULL;
	char installed[64] = "", stamp[64] = "";
	bool ok = false;
	Uint64 started = SDL_GetTicks();
	int files = 0;

	if (!manager) {
		snprintf(error, error_size, "The Android asset manager is unavailable.");
		goto done;
	}
	index_asset = AAssetManager_open(manager, INDEX_NAME, AASSET_MODE_BUFFER);
	if (!index_asset) {
		snprintf(error, error_size, "The packaged file index is missing.");
		goto done;
	}
	{
		off64_t length = AAsset_getLength64(index_asset);
		index = malloc((size_t)length + 1);
		if (index) {
			memcpy(index, AAsset_getBuffer(index_asset), (size_t)length);
			index[length] = '\0';
		}
		AAsset_close(index_asset);
	}
	if (!index) {
		snprintf(error, error_size, "Out of memory reading the file index.");
		goto done;
	}

	line = strtok_r(index, "\r\n", &save);
	if (!line || strcmp(line, INDEX_HEADER) != 0) {
		snprintf(error, error_size, "The packaged file index is not recognised.");
		goto done;
	}
	line = strtok_r(NULL, "\r\n", &save);
	if (!line || strncmp(line, "stamp ", 6) != 0) {
		snprintf(error, error_size, "The packaged file index has no stamp.");
		goto done;
	}
	snprintf(stamp, sizeof(stamp), "%s", line + 6);
	if (read_installed_stamp(root, installed, sizeof(installed)) &&
			strcmp(installed, stamp) == 0) {
		ok = true;
		goto done;
	}

	{
		char stamp_path[1024];
		snprintf(stamp_path, sizeof(stamp_path), "%s/%s", root, STAMP_NAME);
		unlink(stamp_path);
	}

	static char buffer[1 << 16];
	while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
		char *space = strchr(line, ' ');
		char target[1024];
		long long expected, written = 0;
		int count = 0;
		AAsset *asset;
		FILE *out;

		if (!space || space == line) continue;
		*space = '\0';
		expected = atoll(line);
		asset = AAssetManager_open(manager, space + 1, AASSET_MODE_STREAMING);
		if (!asset) {
			snprintf(error, error_size, "A packaged file is missing: %s", space + 1);
			goto done;
		}
		snprintf(target, sizeof(target), "%s/%s", root, space + 1);
		make_parents(target);
		out = fopen(target, "wb");
		while (out && (count = AAsset_read(asset, buffer, sizeof(buffer))) > 0) {
			if (fwrite(buffer, 1, (size_t)count, out) != (size_t)count) break;
			written += count;
		}
		AAsset_close(asset);
		if (!out || fclose(out) != 0 || count < 0 || written != expected) {
			snprintf(error, error_size, "Could not install %s (storage full?)", space + 1);
			goto done;
		}
		files++;
	}

	{
		char stamp_path[1024];
		FILE *fp;
		snprintf(stamp_path, sizeof(stamp_path), "%s/%s", root, STAMP_NAME);
		fp = fopen(stamp_path, "w");
		if (!fp || fprintf(fp, "%s\n", stamp) < 0 || fclose(fp) != 0) {
			snprintf(error, error_size, "Could not record the installed game files.");
			goto done;
		}
	}
	SDL_Log("Installed %d runtime files in %llu ms", files,
		(unsigned long long)(SDL_GetTicks() - started));
	ok = true;

done:
	free(index);
	if (assets_ref) (*env)->DeleteGlobalRef(env, assets_ref);
	return ok;
}

/* Copies the game's stdout and stderr (quit messages, warnings) to logcat. */
static int SDLCALL forward_output(void *data)
{
	int fd = (int)(intptr_t)data;
	char line[1024];
	size_t used = 0;
	char c;
	while (read(fd, &c, 1) == 1) {
		if (c == '\n' || used == sizeof(line) - 1) {
			line[used] = '\0';
			__android_log_write(ANDROID_LOG_INFO, "Ammerow", line);
			used = 0;
			if (c == '\n') continue;
		}
		line[used++] = c;
	}
	return 0;
}

static void redirect_output_to_logcat(void)
{
	int fds[2];
	if (pipe(fds) != 0) return;
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);
	close(fds[1]);
	SDL_Thread *thread = SDL_CreateThread(forward_output, "stdio-logcat", (void *)(intptr_t)fds[0]);
	if (thread) SDL_DetachThread(thread);
}

/* Android's Back key arrives as AC_BACK; the game treats it as Escape. */
static bool SDLCALL map_back_to_escape(void *userdata, SDL_Event *event)
{
	(void)userdata;
	if ((event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) &&
			event->key.key == SDLK_AC_BACK) {
		event->key.key = SDLK_ESCAPE;
		event->key.scancode = SDL_SCANCODE_ESCAPE;
	}
	return true;
}

/*
 * Names the run in play, or the one Home offers as Continue, in the resume
 * marker, so Home offers it again if Android ends the app (resume_argument).
 * A dead or damaged save is not named.
 */
static void mark_for_resume(void)
{
	const char *name = savefile + path_filename_index(savefile);
	FILE *marker;

	if (!savefile[0] || !file_exists(savefile) ||
			!sdl3_home_save_is_live(name, savefile_get_description(savefile))) {
		return;
	}
	marker = fopen(RESUME_MARKER, "w");
	if (marker) {
		fprintf(marker, "%s\n", name);
		fclose(marker);
	}
}

/*
 * Saves the run when Android sends the app to the background.
 *
 * SDL delivers SDL_EVENT_WILL_ENTER_BACKGROUND from the game thread's next
 * event pump, and blocks that thread until the app returns, so this runs on
 * the game thread while it is waiting for input or checking for a
 * disturbance, never part-way through game logic. savefile_save() refuses
 * inconsistent states and replaces the old save atomically. The UI-free save
 * is used because save_game() may show a -more- prompt, which would block
 * until the player came back.
 *
 * A dead character is saved too. The game saves one only when its death
 * screen closes, and on a phone the app is often left at that screen, or at
 * the -more- before it, until Android ends it: the save stayed the living
 * character's, so Load Run offered the run, and loading it brought the
 * character back from before its death. No resume marker for it.
 *
 * A save the game refuses (it checks the world before writing; a duplicate
 * village once failed that check) leaves the last good save in place, so
 * play since then would be lost without a word if Android ended the app:
 * the player is told on returning, and Save and Return Home tries again
 * with the game's own prompt.
 */
static bool background_save_failed;

static bool SDLCALL save_on_background(void *userdata, SDL_Event *event)
{
	(void)userdata;
	if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
		unlink(RESUME_MARKER);
		if (background_save_failed) {
			background_save_failed = false;
			touch_notice("Your run was not saved when you left. "
				"Use Menu, Save and Return Home.");
		}
		return true;
	}
	if (event->type != SDL_EVENT_WILL_ENTER_BACKGROUND || !SDL_IsMainThread()) {
		return true;
	}
	if (player && player->is_dead && character_generated && savefile[0]) {
		if (!savefile_save(savefile)) {
			SDL_Log("Background save of the dead character failed for %s",
				savefile);
		}
		return true;
	}
	if (!player || !player->upkeep || !character_generated ||
			!character_dungeon || player->is_dead ||
			!player->upkeep->playing || !savefile[0]) {
		/* Home, after Save and Return Home: its Continue stays offered. */
		mark_for_resume();
		SDL_Log("Background: no run in play, nothing to save");
		return true;
	}

	Uint64 started = SDL_GetTicks();
	my_strcpy(player->died_from, "(saved)", sizeof(player->died_from));
	if (!savefile_save(savefile)) {
		SDL_Log("Background save failed for %s", savefile);
		background_save_failed = true;
		return true;
	}

	mark_for_resume();
	SDL_Log("Background save of %s took %llu ms", savefile,
		(unsigned long long)(SDL_GetTicks() - started));
	return true;
}

/* The device's smallest width in dp (Configuration.smallestScreenWidthDp):
 * 600 or more is a tablet. 0 if it cannot be read. */
static int smallest_width_dp(void)
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = (jobject)SDL_GetAndroidActivity();
	int dp = 0;

	if (!env || !activity) return 0;
	jclass activity_class = (*env)->GetObjectClass(env, activity);
	jmethodID get_resources = (*env)->GetMethodID(env, activity_class,
		"getResources", "()Landroid/content/res/Resources;");
	jobject resources = get_resources ?
		(*env)->CallObjectMethod(env, activity, get_resources) : NULL;
	if (resources) {
		jclass resources_class = (*env)->GetObjectClass(env, resources);
		jmethodID get_configuration = (*env)->GetMethodID(env, resources_class,
			"getConfiguration", "()Landroid/content/res/Configuration;");
		jobject configuration = get_configuration ?
			(*env)->CallObjectMethod(env, resources, get_configuration) : NULL;
		if (configuration) {
			jclass configuration_class = (*env)->GetObjectClass(env, configuration);
			jfieldID field = (*env)->GetFieldID(env, configuration_class,
				"smallestScreenWidthDp", "I");
			if (field) dp = (*env)->GetIntField(env, configuration, field);
			(*env)->DeleteLocalRef(env, configuration_class);
			(*env)->DeleteLocalRef(env, configuration);
		}
		(*env)->DeleteLocalRef(env, resources_class);
		(*env)->DeleteLocalRef(env, resources);
	}
	if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
	(*env)->DeleteLocalRef(env, activity_class);
	(*env)->DeleteLocalRef(env, activity);
	return dp;
}

/*
 * The Android default layout, applied once (and again only if LAYOUT_VERSION
 * rises): the message history at the top right, where the touch controls
 * leave room, four lines deep on a phone and six on a tablet. It edits the
 * frontend's own settings file before the game reads it, so it is the
 * player's setting from then on, changed in Settings like any other.
 */
#define LAYOUT_MARKER "lib/user/android-layout.txt"
#define LAYOUT_VERSION 1
#define SETTINGS_PATH "lib/user/Ammerow/" SDL3_CONFIG_FILE

static void apply_default_layout(void)
{
	char lines[64][160];
	int count = 0, applied = 0, rows, i;
	bool saw_placement = false, saw_rows = false;
	FILE *fp = fopen(LAYOUT_MARKER, "r");

	if (fp) {
		if (fscanf(fp, "%d", &applied) != 1) applied = 0;
		fclose(fp);
	}
	if (applied >= LAYOUT_VERSION) return;
	rows = smallest_width_dp() >= 600 ? 6 : 4;

	fp = fopen(SETTINGS_PATH, "r");
	if (fp) {
		while (count < (int)N_ELEMENTS(lines) &&
				fgets(lines[count], sizeof(lines[count]), fp)) {
			lines[count][strcspn(lines[count], "\r\n")] = '\0';
			if (strncmp(lines[count], "dock_placement=", 15) == 0) {
				snprintf(lines[count], sizeof(lines[count]), "dock_placement=%d",
					(int)SDL3_DOCK_TOP_RIGHT);
				saw_placement = true;
			} else if (strncmp(lines[count], "dock_rows=", 10) == 0) {
				snprintf(lines[count], sizeof(lines[count]), "dock_rows=%d", rows);
				saw_rows = true;
			}
			count++;
		}
		fclose(fp);
	} else {
		/* First run: only what differs; the rest takes the game's defaults. */
		snprintf(lines[count++], sizeof(lines[0]), "version=%d",
			SDL3_CONFIG_VERSION);
	}
	if (!saw_placement) {
		snprintf(lines[count++], sizeof(lines[0]), "dock_placement=%d",
			(int)SDL3_DOCK_TOP_RIGHT);
	}
	if (!saw_rows) snprintf(lines[count++], sizeof(lines[0]), "dock_rows=%d", rows);

	{
		char path[] = SETTINGS_PATH;
		char temporary[] = SETTINGS_PATH ".new";

		make_parents(path);
		fp = fopen(temporary, "w");
		if (!fp) return;
		for (i = 0; i < count; i++) fprintf(fp, "%s\n", lines[i]);
		if (fclose(fp) != 0 || rename(temporary, path) != 0) {
			unlink(temporary);
			return;
		}
	}
	fp = fopen(LAYOUT_MARKER, "w");
	if (fp) {
		fprintf(fp, "%d\n", LAYOUT_VERSION);
		fclose(fp);
	}
	SDL_Log("Applied the Android default layout (messages top right, %d lines)",
		rows);
}

/*
 * Names the run Home should offer as Continue with -u: the one in the resume
 * marker after Android ended the process in the background, otherwise the
 * last run the game's own settings remember. The game reads that setting
 * only when no player name was given, and on Android (a UNIX build) the
 * account's user name is always given first, so Home offered no Continue
 * after a cold start. Returns the argument, or NULL.
 */
static char *resume_argument(void)
{
	static char argument[300];
	char name[256] = "", path[512], line[512];
	FILE *fp = fopen(RESUME_MARKER, "r");

	if (fp) {
		if (!fgets(name, sizeof(name), fp)) name[0] = '\0';
		fclose(fp);
		unlink(RESUME_MARKER);
	} else if ((fp = fopen(SETTINGS_PATH, "r")) != NULL) {
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "last_save=", 10) == 0) {
				my_strcpy(name, line + 10, sizeof(name));
			}
		}
		fclose(fp);
	}
	name[strcspn(name, "\r\n")] = '\0';
	snprintf(path, sizeof(path), "lib/save/%s", name);
	if (!name[0] || strchr(name, '/') || access(path, R_OK) != 0) return NULL;
	snprintf(argument, sizeof(argument), "-u%s", name);
	SDL_Log("Home offers %s as Continue", name);
	return argument;
}

int main(int argc, char *argv[])
{
	char error[512] = "";
	const char *root;
	char *args[] = { "ammerow", NULL, NULL };
	int count = 1;

	(void)argc;
	(void)argv;
	redirect_output_to_logcat();
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
	/* The on-screen keyboard appears only when the touch controls ask for
	 * it; the desktop frontend keeps SDL text input on permanently. */
	SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");

	root = SDL_GetAndroidInternalStoragePath();
	if (!root) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ammerow",
			"Internal storage is unavailable.", NULL);
		return 1;
	}
	if (!install_runtime(root, error, sizeof(error))) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ammerow", error, NULL);
		return 1;
	}
	if (chdir(root) != 0) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ammerow",
			strerror(errno), NULL);
		return 1;
	}

	/* SDL 3.4.16 bug: on Android, SDL_WaitEvent's loop pushes a poll
	 * sentinel on every pass, and each push wakes the wait at once, so the
	 * game's blocking waits spin at 100% CPU. Without sentinels the wait
	 * blocks as on desktop. Initialising the events subsystem here keeps the
	 * setting when the game's SDL_Init(SDL_INIT_VIDEO) runs. */
	SDL_InitSubSystem(SDL_INIT_EVENTS);
	SDL_SetEventEnabled(SDL_EVENT_POLL_SENTINEL, false);
	/* Pinch zoom (src/sdl3/frontend-events.c): the frontend handles this event type. */
	sdl3_host_zoom_event = SDL_RegisterEvents(1);
	/* A tap on the message history opens the log (src/sdl3/frontend-events.c). */
	sdl3_host_message_tap_opens_log = true;
	/* The buttons and d-pad name the fishing rig's actions (src/sdl3/fishing.c). */
	sdl3_host_draws_fishing_controls = true;
	/* Hybrid's map cells follow a pinch continuously instead of snapping to
	 * 32px steps, which shrank a 1.5x pinch back to 1.07x on release
	 * (src/sdl3/render.c). */
	sdl3_visual_set_pixel_art_snap(false);
	apply_default_layout();
	SDL_SetEventFilter(map_back_to_escape, NULL);
	SDL_AddEventWatch(save_on_background, NULL);

	args[count] = resume_argument();
	if (args[count]) count++;
	return ammerow_main(count, args);
}

/* ---- JNI: touch controls ------------------------------------------------ */

/* Static text for synthetic SDL_EVENT_TEXT_INPUT events (printable ASCII). */
static const char *ascii_text(int codepoint)
{
	static char table[95][2];
	if (codepoint < 32 || codepoint > 126) return NULL;
	char *text = table[codepoint - 32];
	text[0] = (char)codepoint;
	text[1] = '\0';
	return text;
}

static void push_key(SDL_Keycode key, SDL_Keymod mod, bool down)
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

/*
 * Presses and releases one key the way a keyboard does: key down, then the
 * companion text event for printable characters, then key up. The desktop
 * frontend's input gate only accepts text that follows a fresh key down.
 */
/* android/context.c: a command key for the roguelike keyset, when it is on. */
bool touch_translate_command_key(SDL_Keycode *key, SDL_Keymod *mod, int *text);

JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_pressKey(JNIEnv *env,
	jclass cls, jint key, jint mod, jint text)
{
	SDL_Keycode keycode = (SDL_Keycode)key;
	SDL_Keymod keymod = (SDL_Keymod)mod;
	int codepoint = text;

	(void)env;
	(void)cls;
	/* Buttons send the original keyset's keys. */
	touch_translate_command_key(&keycode, &keymod, &codepoint);
	push_key(keycode, keymod, true);
	const char *chars = ascii_text(codepoint);
	if (chars) {
		SDL_Event event;
		SDL_zero(event);
		event.type = SDL_EVENT_TEXT_INPUT;
		event.text.text = chars;
		SDL_PushEvent(&event);
	}
	push_key(keycode, keymod, false);
}

/* Holds or releases a key (held Peek in side-view caves). */
JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_holdKey(JNIEnv *env,
	jclass cls, jint key, jboolean down)
{
	(void)env;
	(void)cls;
	push_key((SDL_Keycode)key, SDL_KMOD_NONE, down);
}

/*
 * The edges of the game's drawing the touch controls cover, in drawing
 * pixels, so the map can scroll its own edges clear of them
 * (src/sdl3/render.c).
 */
JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_setMapInsets(JNIEnv *env,
	jclass cls, jint left, jint top, jint right, jint bottom)
{
	SDL_Event event;

	(void)env;
	(void)cls;
	sdl3_visual_set_map_insets(left, top, right, bottom);
	/* Redraw with the new camera limits. */
	SDL_zero(event);
	event.type = SDL_EVENT_RENDER_TARGETS_RESET;
	SDL_PushEvent(&event);
}

/*
 * How far the d-pad in the bottom left corner reaches from the drawing's left
 * edge, in drawing pixels: the cave's status strip starts clear of it, so the
 * thumb on the pad hides none of it (src/sdl3/render.c).
 */
JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_setCornerInset(JNIEnv *env,
	jclass cls, jint width)
{
	SDL_Event event;

	(void)env;
	(void)cls;
	sdl3_visual_set_corner_inset(width);
	SDL_zero(event);
	event.type = SDL_EVENT_RENDER_TARGETS_RESET;
	SDL_PushEvent(&event);
}

/*
 * The strips at the drawing's left and right the rail covers, in drawing
 * pixels: the game keeps its text clear of them while the map runs on under
 * them (src/sdl3/render.c). The game takes them up when it
 * next fits its layout, as for a new window size.
 */
JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_setGridInsets(JNIEnv *env,
	jclass cls, jint left, jint right)
{
	SDL_Event event;

	(void)env;
	(void)cls;
	sdl3_visual_set_grid_insets(left, right);
	SDL_zero(event);
	event.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
	SDL_PushEvent(&event);
}

/*
 * A tap or long press on the game, as a mouse click at drawing pixel (x, y):
 * button 1 walks or selects, button 2 opens the game's own context menu (the
 * frontend passes SDL button numbers through to Term_mousepress, and the
 * game's menus are on its button 2, SDL's middle button).
 */
JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_click(JNIEnv *env,
	jclass cls, jfloat x, jfloat y, jint button)
{
	SDL_Event event;
	int count = 0;
	SDL_Window **windows = SDL_GetWindows(&count);
	SDL_WindowID window = (windows && count > 0) ? SDL_GetWindowID(windows[0]) : 0;

	(void)env;
	(void)cls;
	SDL_free(windows);

	/* No motion event first: in list screens motion moves the highlight
	 * (desktop hover), and the press below does that itself. */
	SDL_zero(event);
	event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	event.button.windowID = window;
	event.button.button = (Uint8)button;
	event.button.down = true;
	event.button.clicks = 1;
	event.button.x = x;
	event.button.y = y;
	SDL_PushEvent(&event);

	event.type = SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.down = false;
	SDL_PushEvent(&event);
}

/*
 * Pinch zoom: phase is enum sdl3_host_zoom_phase; factor is the pinch's
 * scale since it began, in thousandths.
 */
JNIEXPORT void JNICALL Java_com_ammerow_game_NativeBridge_pinchZoom(JNIEnv *env,
	jclass cls, jint phase, jint factor)
{
	SDL_Event event;

	(void)env;
	(void)cls;
	if (!sdl3_host_zoom_event) return;
	SDL_zero(event);
	event.type = sdl3_host_zoom_event;
	event.user.code = phase;
	event.user.data1 = (void *)(intptr_t)factor;
	SDL_PushEvent(&event);
}

/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/host.h
 * \brief Events a hosting platform layer can send the SDL3 frontend.
 *
 * A host (e.g. the Android touch controls) registers an SDL event type and
 * stores it here; the frontend then handles events of that type on the game
 * thread. Desktop builds leave it zero.
 */

#ifndef INCLUDED_SDL3_HOST_H
#define INCLUDED_SDL3_HOST_H

#include <SDL3/SDL.h>

/**
 * Continuous map zoom, e.g. from a pinch. event->user.code is the phase:
 * SDL3_HOST_ZOOM_BEGIN records the current zoom, SDL3_HOST_ZOOM_UPDATE sets
 * it to that zoom times (intptr_t)data1 / 1000, and SDL3_HOST_ZOOM_END
 * applies the last factor and saves the setting.
 */
extern Uint32 sdl3_host_zoom_event;

enum sdl3_host_zoom_phase {
	SDL3_HOST_ZOOM_BEGIN = 0,
	SDL3_HOST_ZOOM_UPDATE = 1,
	SDL3_HOST_ZOOM_END = 2
};

/**
 * Set by a host while a list is open for browsing (an item list rather than
 * one picking an item for a command, or a shop's stock): a click on a row
 * then only highlights it (the host offers that row's actions), and a click
 * on the highlighted row accepts it.
 */
extern bool sdl3_host_list_tap_highlights;

/**
 * Set by a host to make a click on the message history drawn over the map
 * (at the command prompt) open the full message log instead of acting on the
 * map square beneath it.
 */
extern bool sdl3_host_message_tap_opens_log;

/**
 * Set by a host whose own controls name the fishing rig's actions: the rig's
 * panel of keyboard controls is then not drawn over them.
 */
extern bool sdl3_host_draws_fishing_controls;

#endif /* INCLUDED_SDL3_HOST_H */

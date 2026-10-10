/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

package com.ammerow.game

/** Calls into android/android-main.c. Safe from any thread: SDL queues the events. */
object NativeBridge {
    /** Presses and releases [key] (an SDL keycode); [text] is the printable character it types, or 0. */
    @JvmStatic external fun pressKey(key: Int, mod: Int, text: Int)

    /** Holds ([down]) or releases an SDL keycode. */
    @JvmStatic external fun holdKey(key: Int, down: Boolean)

    /** A mouse click at drawing pixel ([x], [y]): button 1 walks or selects, 2 opens the game's menu. */
    @JvmStatic external fun click(x: Float, y: Float, button: Int)

    /** A map command: pressed once the game is back at the map (Back is pressed until it is). */
    @JvmStatic external fun runCommand(key: Int, mod: Int, text: Int)

    /** One of the highlighted item's actions ([Verb.value]), in a browsed item list. */
    @JvmStatic external fun runVerb(value: Int)

    /** The quick bar (android/pins.c): runs a slot (at the map). */
    @JvmStatic external fun runPin(slot: Int)

    /** Empties a quick-bar slot. */
    @JvmStatic external fun removePin(slot: Int)

    /** Puts the highlighted item's action ([Verb.value]) on the quick bar, in a browsed item list. */
    @JvmStatic external fun pinItem(verb: Int)

    /** Puts the highlighted spell on the quick bar, in a spell list. */
    @JvmStatic external fun pinSpell()

    /** Puts a More command on the quick bar: [keys] are key, modifiers, text triplets. */
    @JvmStatic external fun pinCommand(label: String, keys: IntArray)

    /** Pinch zoom: [phase] 0 begin, 1 update, 2 end; [factor] the scale since it began, in thousandths. */
    @JvmStatic external fun pinchZoom(phase: Int, factor: Int)

    /** Drawing pixels the controls cover along each edge; the map scrolls its edges clear of them. */
    @JvmStatic external fun setMapInsets(left: Int, top: Int, right: Int, bottom: Int)

    /** Drawing pixels the rail covers at the left and right; the game's text keeps clear of them, the map does not. */
    @JvmStatic external fun setGridInsets(left: Int, right: Int)

    /** Drawing pixels the d-pad reaches in from the left and up from the bottom; the cave's status strip starts clear of it, and the sidebar keeps above it. */
    @JvmStatic external fun setCornerInset(width: Int, height: Int)

    /** Drawing pixels the d-pad and the right-hand buttons cover up from the bottom (the higher of them); the look card stops above them, on either side. */
    @JvmStatic external fun setCardInset(bottom: Int)

    /** Debug builds: records the next frame's text for a layout check (android/probe.c). */
    @JvmStatic external fun layoutProbe(seq: Int)
}

/** SDL3 keycodes and modifiers used by the touch controls. */
object Keys {
    private const val SCANCODE_MASK = 1 shl 30

    const val RETURN = 0x0D
    const val ESCAPE = 0x1B
    const val BACKSPACE = 0x08
    const val TAB = 0x09
    const val DELETE = 0x7F
    const val EQUALS = '='.code
    const val MINUS = '-'.code
    const val P = 'p'.code

    val PAGE_UP = SCANCODE_MASK or 75
    val PAGE_DOWN = SCANCODE_MASK or 78
    val RIGHT = SCANCODE_MASK or 79
    val LEFT = SCANCODE_MASK or 80
    val DOWN = SCANCODE_MASK or 81
    val UP = SCANCODE_MASK or 82

    /** Keypad 1..9 (SDLK_KP_1 is scancode 89); the game reads them as directions. */
    fun keypad(digit: Int): Int = SCANCODE_MASK or (88 + digit)

    const val MOD_SHIFT = 0x0001
    const val MOD_CTRL = 0x0040
    const val MOD_ALT = 0x0100
}

/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

package com.ammerow.game

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ActivityInfo
import android.content.pm.ApplicationInfo
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.Rect
import android.graphics.RectF
import android.os.Build
import android.os.Bundle
import android.text.InputType
import android.view.KeyEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowManager
import android.widget.RelativeLayout
import android.widget.Toast
import java.io.File
import org.libsdl.app.SDLActivity
import kotlin.math.max
import kotlin.math.roundToInt

class AmmerowActivity : SDLActivity() {
    private var controls: TouchControlsView? = null
    private var keyboardShown = false
    private var clusterWidth = 0f
    private var reportedRight = -1

    override fun getLibraries(): Array<String> =
        arrayOf("SDL3", "SDL3_image", "SDL3_ttf", "main")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Build.VERSION.SDK_INT < 35) drawBarBackgrounds()
        val layout = SDLActivity.mLayout ?: return // SDL could not start and is showing its own error.
        SDLActivity.mSurface.layoutParams = RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        // No keyboard-focus outline around the game when a hardware keyboard is used.
        SDLActivity.mSurface.defaultFocusHighlightEnabled = false
        val touchControls = TouchControlsView(this)
        touchControls.onToggleKeyboard = ::toggleKeyboard
        touchControls.toGamePoint = ::gamePoint
        touchControls.onClusterWidthChanged = { width ->
            clusterWidth = width
            reportMapInsets()
        }
        touchControls.onControlsHeightChanged = { height ->
            NativeBridge.setCardInset(height.roundToInt().coerceAtLeast(0))
        }
        touchControls.onPadChanged = { right, height ->
            NativeBridge.setCornerInset(right.roundToInt().coerceAtLeast(0), height.roundToInt().coerceAtLeast(0))
        }
        touchControls.onBarHeightChanged = { height ->
            barHeight = height
            reportMapInsets()
        }
        touchControls.onNotice = ::showNotice
        layout.addView(touchControls, RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        controls = touchControls
        SDLActivity.mSurface.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> updateFrame() }
        SDLActivity.mSurface.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                // 60 Hz is plenty for a turn-based game and spares the battery on 90/120 Hz screens.
                if (Build.VERSION.SDK_INT >= 30) {
                    holder.surface.setFrameRate(60f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
                }
            }
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit
            override fun surfaceDestroyed(holder: SurfaceHolder) = Unit
        })
        layout.setOnApplyWindowInsetsListener { _, insets ->
            applyInsets(insets)
            insets
        }
        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) {
            val filter = IntentFilter(PROBE_ACTION)
            if (Build.VERSION.SDK_INT >= 33) {
                registerReceiver(probeReceiver, filter, Context.RECEIVER_EXPORTED)
            } else {
                registerReceiver(probeReceiver, filter)
            }
            probeRegistered = true
        }
    }

    /**
     * The system bars are hidden while the game is up, but before Android 15 a
     * window that leaves the bars' backgrounds to the system still shows black
     * where the hidden navigation bar would be (seen on Android 13): over the
     * game's bottom edge and its hint row, or with three-button navigation its
     * right edge and the large button. The window draws them itself, clear.
     */
    @Suppress("DEPRECATION")
    private fun drawBarBackgrounds() {
        window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS)
        window.navigationBarColor = Color.TRANSPARENT
        window.statusBarColor = Color.TRANSPARENT
    }

    override fun onDestroy() {
        if (probeRegistered) unregisterReceiver(probeReceiver)
        probeRegistered = false
        super.onDestroy()
    }

    /**
     * Landscape either way up (SDL asks for it as the game's window is made,
     * from the hint in android-main.c), except on a nearly square screen, such
     * as a foldable opened out, which is used whole however it is held: locked
     * to landscape there, Android shows the game in a 4:3 box with bands above
     * and below.
     */
    override fun setOrientationBis(w: Int, h: Int, resizable: Boolean, hint: String) {
        if (nearlySquare()) {
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        } else {
            super.setOrientationBis(w, h, resizable, hint)
        }
    }

    /** Folding or unfolding changes the screen without starting the activity again. */
    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        requestedOrientation = if (nearlySquare()) {
            ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        } else {
            ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE
        }
    }

    private fun nearlySquare(): Boolean {
        val bounds = if (Build.VERSION.SDK_INT >= 30) {
            windowManager.maximumWindowMetrics.bounds
        } else {
            Rect(0, 0, resources.displayMetrics.widthPixels, resources.displayMetrics.heightPixels)
        }
        val long = max(bounds.width(), bounds.height())
        val short = minOf(bounds.width(), bounds.height())
        return short > 0 && long < short * NEARLY_SQUARE
    }

    /**
     * Debug builds: a layout check asks for a layout probe by broadcast.
     * The controls' report is written here, the game's text by android/probe.c
     * with the next frame; both carry the request's sequence number.
     */
    private var probeRegistered = false
    private val probeReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val seq = intent.getIntExtra("seq", 1)
            val report = controls?.probeReport(seq) ?: return
            val temp = File(filesDir, "probe-controls.tmp")
            temp.writeText(report)
            temp.renameTo(File(filesDir, "probe-controls.json"))
            NativeBridge.layoutProbe(seq)
        }
    }

    /** Called by the game (android/context.c) from its own thread. */
    @Suppress("unused")
    fun onGameContext(mode: Int, primary: Int, flags: Int, detail: Int) {
        runOnUiThread {
            val context = GameContext(ControlMode.of(mode), primary, flags, detail)
            // The keyboard closes when the prompt that needed it ends.
            if (keyboardShown && context.mode != ControlMode.TEXT) toggleKeyboard()
            controls?.setGameContext(context)
        }
    }

    /**
     * Called by the game (android/context.c) from its own thread: its text grid
     * (rows, their height, where the first begins, all in pixels of an output
     * [outputHeight] high), so the controls keep its hint row clear.
     */
    @Suppress("unused")
    fun onGameGrid(rows: Int, cellHeight: Int, originY: Int, outputHeight: Int) {
        runOnUiThread { controls?.setTextGrid(rows, cellHeight, originY, outputHeight) }
    }

    /** Called by the game (android/context.c) from its own thread: the highlighted item's actions, or "". */
    @Suppress("unused")
    fun onGameVerbs(text: String) {
        val verbs = Verb.parse(text)
        runOnUiThread { controls?.setVerbs(verbs) }
    }

    /** Called by the game (android/pins.c) from its own thread: the quick bar's slots. */
    @Suppress("unused")
    fun onGameBar(text: String) {
        val slots = BarSlot.parse(text)
        runOnUiThread { controls?.setBar(slots) }
    }

    /** Called by the game (touch_notice) from its own thread: a pin added or removed, or why not; a failed save. */
    @Suppress("unused")
    fun onNotice(text: String) {
        runOnUiThread { showNotice(text) }
    }

    private var notice: Toast? = null

    /** A short note over the game; a newer one replaces it at once. */
    private fun showNotice(text: String) {
        notice?.cancel()
        notice = Toast.makeText(this, text, Toast.LENGTH_SHORT).also { it.show() }
    }

    /** Back closes the command drawer; otherwise SDL passes it to the game as Escape. */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BACK && controls?.drawerIsOpen == true) {
            if (event.action == KeyEvent.ACTION_UP) controls?.closeDrawer()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    /**
     * The desktop frontend keeps SDL text input on all the time, so the screen
     * keyboard is disabled in android-main.c and shown only on request here.
     * Typed text reaches the game through SDL's own input connection.
     */
    private fun toggleKeyboard() {
        keyboardShown = !keyboardShown
        if (keyboardShown) {
            // A password-style keyboard commits each character as it is typed;
            // the game takes typed text one character at a time.
            SDLActivity.showTextInput(InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS, 0, 0, 1, 1)
        } else {
            sendCommand(COMMAND_TEXTEDIT_HIDE, null)
        }
    }

    private var railOnRight = false
    private var railWidth = 0
    private val cutoutRect = RectF()

    /**
     * The rail of system buttons goes in the camera-cutout strip, which the
     * game cannot use for text anyway; without a side cutout it takes a strip
     * on the left. The map is drawn on under the rail, around the camera; the
     * game's text (the stats panel, every screen) keeps clear of it, as do the
     * other buttons. Any other cutout is kept clear of.
     */
    private fun applyInsets(insets: WindowInsets) {
        val cutout = if (Build.VERSION.SDK_INT >= 28) insets.displayCutout else null
        var left = cutout?.safeInsetLeft ?: 0
        val top = cutout?.safeInsetTop ?: 0
        var right = cutout?.safeInsetRight ?: 0
        val bottom = cutout?.safeInsetBottom ?: 0
        val minimum = (RAIL_WIDTH_DP * resources.displayMetrics.density).roundToInt()
        railOnRight = right > left
        railWidth = max(if (railOnRight) right else left, minimum)
        if (railOnRight) right = 0 else left = 0
        cutoutRect.setEmpty()
        cutout?.boundingRects?.firstOrNull { !it.isEmpty && (it.left == 0 || it.right >= window.decorView.width - 1) }
            ?.let { cutoutRect.set(Rect(it)) }
        // Before the drawing changes size, so the game fits its text to both at once.
        NativeBridge.setGridInsets(if (railOnRight) 0 else railWidth, if (railOnRight) railWidth else 0)

        val params = SDLActivity.mSurface.layoutParams as RelativeLayout.LayoutParams
        if (params.leftMargin != left || params.topMargin != top || params.rightMargin != right || params.bottomMargin != bottom) {
            params.setMargins(left, top, right, bottom)
            SDLActivity.mSurface.layoutParams = params
        }
        updateFrame()
    }

    private fun updateFrame() {
        val surface = SDLActivity.mSurface ?: return
        if (surface.width <= 0) return
        val drawing = RectF(surface.left.toFloat(), surface.top.toFloat(), surface.right.toFloat(), surface.bottom.toFloat())
        // The buttons go in the drawing beside the rail.
        val game = RectF(drawing)
        val rail = if (railOnRight) {
            game.right -= railWidth
            RectF(game.right, 0f, drawing.right, drawing.bottom)
        } else {
            game.left += railWidth
            RectF(drawing.left, 0f, game.left, drawing.bottom)
        }
        controls?.setFrame(game, rail, cutoutRect, drawing)
        reportMapInsets()
    }

    /**
     * Tells the game how far the rail and the right-hand buttons reach over its
     * drawing, and the quick bar over its bottom, so the map can scroll those
     * edges clear of them. The surface is drawn at full size, so view pixels
     * are drawing pixels.
     */
    private fun reportMapInsets() {
        val left = if (railOnRight) 0 else railWidth
        val right = clusterWidth.roundToInt().coerceAtLeast(0) + if (railOnRight) railWidth else 0
        val bottom = barHeight.roundToInt().coerceAtLeast(0)
        if (left == reportedLeft && right == reportedRight && bottom == reportedBottom) return
        reportedLeft = left
        reportedRight = right
        reportedBottom = bottom
        NativeBridge.setMapInsets(left, 0, right, bottom)
    }

    private var barHeight = 0f
    private var reportedBottom = -1
    private var reportedLeft = -1

    /** Maps a position on screen to the game's drawing pixels. */
    private fun gamePoint(x: Float, y: Float): Pair<Float, Float>? {
        val surface = SDLActivity.mSurface ?: return null
        val surfaceX = x - surface.left
        val surfaceY = y - surface.top
        if (surfaceX < 0 || surfaceY < 0 || surfaceX >= surface.width || surfaceY >= surface.height) return null
        return Pair(surfaceX, surfaceY)
    }

    private companion object {
        const val RAIL_WIDTH_DP = 56f
        const val PROBE_ACTION = "com.ammerow.game.PROBE"
        /** Screens with sides closer than this (long / short) count as nearly square. */
        const val NEARLY_SQUARE = 1.25f
    }
}

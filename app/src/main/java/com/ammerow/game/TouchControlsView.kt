/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

package com.ammerow.game

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.os.Handler
import android.os.Looper
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import org.json.JSONArray
import org.json.JSONObject

/**
 * Touch controls drawn over the game, following what the game is waiting for
 * ([GameContext], from android/context.c):
 *
 * - a rail of system buttons beside the game, in the camera-cutout strip
 *   where there is one, so nothing covers the game's top line;
 * - a fixed 8-way d-pad bottom-left (hold to repeat);
 * - bottom-right, one large button and up to three around it. Their places
 *   never change, only their labels, so a thumb learns them;
 * - the More drawer: every command, grouped.
 *
 * Every control presses the game's own keys. Touches elsewhere go to the game
 * as mouse clicks: a tap walks there or picks a menu row; on the map a long
 * press opens the game's own menu for that square.
 */
class TouchControlsView(context: Context) : View(context) {
    var onToggleKeyboard: (() -> Unit)? = null

    /** Maps a view position to the game's drawing pixels, or null if outside the game. */
    var toGamePoint: ((Float, Float) -> Pair<Float, Float>?)? = null

    /** Reports how far the right-hand buttons reach in from the game's right edge (view pixels). */
    var onClusterWidthChanged: ((Float) -> Unit)? = null

    /** Reports how far the right-hand buttons reach up from the game's bottom edge (view pixels). */
    var onClusterHeightChanged: ((Float) -> Unit)? = null

    /** Reports how far the quick bar reaches up from the game's bottom edge (view pixels). */
    var onBarHeightChanged: ((Float) -> Unit)? = null

    /** Reports how far the d-pad reaches in from the drawing's left edge (view pixels). */
    var onPadRightChanged: ((Float) -> Unit)? = null

    /** Shows a short note over the game. */
    var onNotice: ((String) -> Unit)? = null

    private class Button(var command: Command?, val round: Boolean) {
        val rect = RectF()
        /** Rail buttons only: where a touch presses it, which can be more than is drawn. */
        val touch = RectF()
        var pressed = false
        /** Rail buttons only: whether it means anything just now. */
        var shown = true
    }

    private var gameContext = GameContext()
    /** The highlighted item's actions while an item list is browsed. */
    private var verbs: List<Verb> = emptyList()

    private val railButtons = (ControlLayouts.railTop + ControlLayouts.railBottom).map { Button(it, round = false) }
    private val primaryButton = Button(null, round = true)
    private val quickButtons = List(3) { Button(null, round = true) }
    private val digitButtons = ControlLayouts.digits.map { Button(it, round = false) }
    /** Actions that do not fit the cluster, in a row along the bottom. */
    private var stripButtons: List<Button> = emptyList()
    private val drawerButtons = ControlLayouts.drawer.map { (_, commands) -> commands.map { Button(it, round = false) } }
    private val drawerClose = Button(Command("Done", Action.Drawer), round = false)
    private val drawerTitles = ArrayList<Pair<String, Float>>()
    private var drawerOpen = false
    private var drawerScroll = 0f
    private var drawerContentHeight = 0f

    private val activeButtons = HashMap<Int, Button>()
    /**
     * What each finger's button did when it was pressed: a held key or a repeat
     * belongs to the press, not to whatever the button has become since (Reel
     * turns into Wait under the finger when the fish lands).
     */
    private val pressedActions = HashMap<Int, Action>()
    /** The game's whole drawing, under the rail too (the probe maps game cells with it). */
    private val drawing = RectF()

    /*
     * The quick bar (android/pins.c): rows of four slots at the bottom
     * centre of the map, filled by the player. Tap a slot to use it, hold it
     * to empty it. The small button at its left changes how many rows show:
     * up to three on a tablet, two on a phone (a device preference; the slots
     * themselves belong to the character).
     */
    private var bar: List<BarSlot> = BarSlot.parse("")
    private val barSlots = List(BarSlot.COUNT) { RectF() }
    private val barRowsButton = RectF()
    private val preferences = context.getSharedPreferences("controls", Context.MODE_PRIVATE)
    // Read afresh each time: folding or unfolding a phone changes the screen
    // without starting the activity again.
    private val tablet get() = resources.configuration.smallestScreenWidthDp >= 600
    private val maxBarRows get() = if (tablet) 3 else 2
    /** The rows the player chose (kept as chosen); [shownBarRows] fits the screen. */
    private var barRows = preferences.getInt("barRows", 1).coerceIn(1, 3)
    private val shownBarRows get() = barRows.coerceAtMost(maxBarRows)
    /** The touched slot (or BAR_ROWS for the rows button), and its finger. */
    private var barTouched = -1
    private var barPointer = -1
    private var barHeld = false
    private val barHold = Runnable {
        if (barPointer == -1 || barTouched !in 0 until BarSlot.COUNT) return@Runnable
        barHeld = true
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        if (!bar[barTouched].empty) NativeBridge.removePin(barTouched)
        invalidate()
    }
    private val barShown get() =
        (gameContext.mode == ControlMode.MAP || gameContext.mode == ControlMode.CAVE) && !drawerOpen && !game.isEmpty

    /**
     * How far beyond its ring (in radii) a touch still lands on the d-pad: a
     * little way on the map and in caves, where a touch just missing it is
     * meant for it; elsewhere only the ring, so menu rows beside it (Home's
     * lower entries on a phone) can still be tapped.
     */
    private val padReach get() =
        if (gameContext.mode == ControlMode.MAP || gameContext.mode == ControlMode.CAVE) PAD_REACH else 1f

    private val density = resources.displayMetrics.density
    private fun dp(value: Float) = value * density
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop.toFloat()

    // The game's drawing area and the rail beside it (view pixels).
    private val game = RectF()
    private val rail = RectF()
    private val cutout = RectF()

    // D-pad geometry and state.
    private var padX = 0f
    private var padY = 0f
    private var padRadius = 0f
    private var padPointer = -1
    private var padDirection = 0 // keypad digit, 0 = none
    /** True while the d-pad touch has stayed in the centre: lifting there waits a turn. */
    private var padCentreTap = false
    /** The held centre has repeated (or was stopped): lifting adds nothing. */
    private var centreRepeated = false
    private val centreRepeat = object : Runnable {
        override fun run() {
            if (!padCentreTap || dpad.centre == null) return
            centreRepeated = true
            step(5)
            handler.postDelayed(this, REPEAT_MS)
        }
    }
    private var dpad = Dpad()
    private val handler = Handler(Looper.getMainLooper())
    private val repeat = object : Runnable {
        override fun run() {
            if (padDirection == 0) return
            step(padDirection)
            handler.postDelayed(this, REPEAT_MS)
        }
    }

    /** A held repeating button (Reel). */
    private var repeatingKey: Action.Key? = null
    private val repeatButton = object : Runnable {
        override fun run() {
            val key = repeatingKey ?: return
            NativeBridge.pressKey(key.key, key.mod, key.text)
            handler.postDelayed(this, REPEAT_MS)
        }
    }

    // A touch on the game itself: a tap, a long press, or a drawer scroll.
    private var gamePointer = -1
    private var gameDownX = 0f
    private var gameDownY = 0f
    private var gameMoved = false
    private var longPressed = false
    private val longPress = Runnable {
        if (gamePointer == -1 || gameMoved) return@Runnable
        longPressed = true
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        sendClick(gameDownX, gameDownY, MENU_BUTTON)
    }

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.5f)
        color = Color.argb(110, 255, 255, 255)
    }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(235, 255, 255, 255)
        textAlign = Paint.Align.CENTER
        textSize = dp(13f)
    }
    /** A dark outline drawn under a faint button's label. */
    private val textOutline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(3f)
        strokeJoin = Paint.Join.ROUND
        color = Color.argb(230, 0, 0, 0)
        textAlign = Paint.Align.CENTER
    }
    private val railEdge = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.25f)
        color = Color.argb(120, 255, 255, 255)
    }
    private val title = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(255, 120, 170, 220)
        textSize = dp(13f)
    }

    init {
        applyCluster()
    }

    fun setGameContext(context: GameContext) {
        if (context == gameContext) return
        val old = gameContext
        gameContext = context
        // No run in play: the drawer's commands mean nothing (its button hides).
        if (!context.has(ContextFlags.IN_RUN)) drawerOpen = false
        applyCluster()
        if (!dpad.visible) releasePad()
        // A held Wait stops, as resting does, when a monster comes into view.
        val monsterArrived = context.has(ContextFlags.MONSTER_IN_VIEW) && !old.has(ContextFlags.MONSTER_IN_VIEW)
        if (padCentreTap && (monsterArrived || dpad.centre == null)) {
            handler.removeCallbacks(centreRepeat)
            centreRepeated = true
        }
        // In a cave the bar sits above the cave's status strip.
        if ((old.mode == ControlMode.CAVE) != (context.mode == ControlMode.CAVE)) layoutBar()
        invalidate()
    }

    /** The game's text grid (see [AmmerowActivity.onGameGrid]); 0 rows until it is known. */
    private var gridRows = 0
    private var gridCellHeight = 0
    private var gridOriginY = 0
    private var gridOutputHeight = 0

    fun setTextGrid(rows: Int, cellHeight: Int, originY: Int, outputHeight: Int) {
        if (rows == gridRows && cellHeight == gridCellHeight && originY == gridOriginY &&
            outputHeight == gridOutputHeight
        ) return
        gridRows = rows
        gridCellHeight = cellHeight
        gridOriginY = originY
        gridOutputHeight = outputHeight
        layoutControls()
        invalidate()
    }

    /**
     * The top of the game's hint row (the second from the bottom, where every
     * screen explains its keys), which the strip and the digits stay above;
     * without the grid yet, the bottom of the game.
     */
    private fun hintRowTop(): Float {
        if (gridRows < 3 || gridOutputHeight <= 0) return game.bottom
        val row = gridOriginY + (gridRows - 2) * gridCellHeight
        return game.top + row * game.height() / gridOutputHeight
    }

    fun setBar(slots: List<BarSlot>) {
        if (slots == bar) return
        bar = slots
        invalidate()
    }

    fun setVerbs(list: List<Verb>) {
        if (list == verbs) return
        verbs = list
        applyCluster()
        invalidate()
    }

    /** When the large button last changed what it does (uptime ms). */
    private var primaryChangedAt = 0L

    private fun applyCluster() {
        dpad = ControlLayouts.dpad(gameContext)
        val cluster = ControlLayouts.cluster(gameContext, verbs)
        if (cluster.primary != primaryButton.command && gameContext.mode.guardsPrimary) {
            primaryChangedAt = android.os.SystemClock.uptimeMillis()
        }
        primaryButton.command = cluster.primary
        quickButtons.forEachIndexed { index, button -> button.command = cluster.quick.getOrNull(index) }
        // A held key or a repeat ends as soon as its button does something else.
        for ((pointer, button) in activeButtons) {
            val pressed = pressedActions[pointer] ?: continue
            if (button.command?.action != pressed) endHeld(pressedActions.remove(pointer))
        }
        stripButtons = ControlLayouts.strip(gameContext, verbs).map { Button(it, round = false) }
        layoutStrip()
        var railChanged = false
        for (button in railButtons) {
            val shown = button.command?.let { ControlLayouts.railShows(it, gameContext) } ?: false
            if (shown != button.shown) {
                button.shown = shown
                railChanged = true
            }
        }
        if (railChanged) layoutRail()
    }

    /**
     * Where the buttons go beside the rail, where the rail goes, the camera
     * cutout to keep buttons away from, and the game's whole drawing, which
     * runs on under the rail (all view pixels; [cutoutRect] may be empty).
     */
    fun setFrame(gameRect: RectF, railRect: RectF, cutoutRect: RectF, drawingRect: RectF) {
        game.set(gameRect)
        rail.set(railRect)
        cutout.set(cutoutRect)
        drawing.set(drawingRect)
        layoutControls()
        invalidate()
    }

    val drawerIsOpen get() = drawerOpen

    fun closeDrawer(): Boolean {
        if (!drawerOpen) return false
        drawerOpen = false
        invalidate()
        return true
    }

    private fun layoutControls() {
        if (game.isEmpty) return
        layoutRail()

        // D-pad: bottom-left of the game, fixed, so the rest of the left side stays tappable.
        // Drawn faintly, so the game's hint row and the map show through it. Larger on a
        // tablet, where a thumb covers less of it; held a little off the rail, so a thumb
        // reaching for its left side does not press Log or Char instead.
        val margin = dp(12f)
        padRadius = min(dp(if (tablet) 88f else 72f), game.height() * 0.21f)
        padX = game.left + dp(22f) + padRadius
        padY = game.bottom - margin - padRadius
        onPadRightChanged?.invoke(padX + padRadius + dp(8f) - drawing.left)

        // Cluster: a large button in the corner and three on an arc around it.
        val big = min(dp(40f), game.height() * 0.12f)
        val small = big * 0.68f
        val cx = game.right - margin - big
        // On a screen about as tall as it is wide (a foldable opened out) there is
        // room to keep the hint row's end readable: the cluster sits above it.
        val clusterBottom = if (game.height() > game.width() * 0.85f) {
            min(game.bottom - margin, hintRowTop() - dp(4f))
        } else {
            game.bottom - margin
        }
        val cy = clusterBottom - big
        primaryButton.rect.set(cx - big, cy - big, cx + big, cy + big)
        val arc = big + small + dp(12f)
        // Left, up-left, up: comfortable for a right thumb resting near the corner.
        val angles = doubleArrayOf(180.0, 225.0, 270.0)
        quickButtons.forEachIndexed { index, button ->
            val angle = Math.toRadians(angles[index])
            val x = cx + (arc * cos(angle)).toFloat()
            val y = cy + (arc * sin(angle)).toFloat()
            button.rect.set(x - small, y - small, x + small, y + small)
        }
        val clusterLeft = quickButtons.minOf { it.rect.left }
        onClusterWidthChanged?.invoke(game.right - clusterLeft + dp(4f))
        onClusterHeightChanged?.invoke(game.bottom - quickButtons.minOf { it.rect.top } + dp(4f))

        // Digits: one row along the bottom, left of the cluster (the d-pad is hidden then),
        // above the hint row like the d-pad.
        val digitGap = dp(6f)
        val digitRight = clusterLeft - dp(16f)
        val digitWidth = min(dp(56f), (digitRight - game.left - margin - digitGap * 9) / 10)
        val digitHeight = min(dp(52f), game.height() * 0.13f)
        val digitBottom = min(game.bottom - margin, hintRowTop() - dp(4f))
        digitButtons.forEachIndexed { index, button ->
            val left = game.left + margin + index * (digitWidth + digitGap)
            button.rect.set(left, digitBottom - digitHeight, left + digitWidth, digitBottom)
        }

        layoutStrip()
        layoutBar()
        layoutDrawer()
    }

    /**
     * The top of a side-view cave's status strip, the game's bottom three
     * rows (HP, stamina and air; the place; what climbing costs).
     */
    private fun caveStripTop(): Float {
        if (gridRows < CAVE_STRIP_ROWS + 1 || gridOutputHeight <= 0) return game.bottom
        val row = gridOriginY + (gridRows - CAVE_STRIP_ROWS) * gridCellHeight
        return game.top + row * game.height() / gridOutputHeight
    }

    /**
     * The quick bar: rows of four slots between the d-pad and the cluster,
     * the first along the bottom (in a cave, just above its status strip) and
     * the others above it, with the rows button at the first row's left.
     * Reports how high the shown rows reach, so the map can scroll its bottom
     * edge clear of them.
     */
    private fun layoutBar() {
        if (game.isEmpty) return
        val gap = dp(6f)
        val height = min(dp(44f), game.height() * 0.1f)
        val rowsWidth = dp(40f)
        val left = padX + padRadius + dp(16f)
        val right = quickButtons.minOf { it.rect.left } - dp(16f)
        val slotWidth = min(dp(88f), (right - left - rowsWidth - gap * 4) / 4)
        val total = rowsWidth + gap + slotWidth * 4 + gap * 3
        val x0 = left + max(0f, (right - left - total) / 2)
        val bottom = if (gameContext.mode == ControlMode.CAVE) caveStripTop() - dp(6f) else game.bottom - dp(12f)
        barRowsButton.set(x0, bottom - height, x0 + rowsWidth, bottom)
        barSlots.forEachIndexed { index, rect ->
            val x = x0 + rowsWidth + gap + (index % 4) * (slotWidth + gap)
            val y = bottom - height - (index / 4) * (height + gap)
            rect.set(x, y, x + slotWidth, y + height)
        }
        val top = barSlots[(shownBarRows - 1) * 4].top
        onBarHeightChanged?.invoke(game.bottom - top + dp(4f))
    }

    private fun changeBarRows() {
        barRows = if (shownBarRows >= maxBarRows) 1 else shownBarRows + 1
        preferences.edit().putInt("barRows", barRows).apply()
        layoutBar()
        invalidate()
    }

    private fun tapBar(target: Int) {
        if (target == BAR_ROWS) return changeBarRows()
        val slot = bar.getOrNull(target) ?: return
        if (slot.empty) {
            onNotice?.invoke("To fill the bar: Pin an item or a spell in its list, or hold a command under More.")
        } else {
            NativeBridge.runPin(target)
        }
    }

    /**
     * The strip: one row between the d-pad and the cluster, level with the
     * button left of the large one (Back), which leaves the game's bottom
     * line (its hints) readable. Buttons fit their labels.
     */
    private fun layoutStrip() {
        if (game.isEmpty || stripButtons.isEmpty()) return
        val gap = dp(8f)
        val height = min(dp(44f), game.height() * 0.12f)
        val top = min(quickButtons[0].rect.centerY() - height / 2, hintRowTop() - dp(4f) - height)
        val left = padX + padRadius + dp(16f)
        val right = quickButtons.minOf { it.rect.left } - dp(16f)
        val widths = stripButtons.map { max(dp(72f), text.measureText(it.command?.label ?: "") + dp(24f)) }
        val room = right - left - gap * (widths.size - 1)
        // Too many for the row: they share it (labels shrink to fit when drawn).
        val scale = if (widths.sum() > room) room / widths.sum() else 1f
        var x = left
        stripButtons.forEachIndexed { index, button ->
            val width = widths[index] * scale
            button.rect.set(x, top, x + width, top + height)
            x += width + gap
        }
    }

    /**
     * The rail's top buttons from its top and the rest from its bottom, clear
     * of any cutout; hidden ones leave no gap.
     */
    private fun layoutRail() {
        if (rail.isEmpty) return
        val gap = dp(8f)
        val side = min(rail.width() - dp(10f), dp(48f))
        val x = rail.centerX()
        val top = ControlLayouts.railTop.size
        val bottom = ControlLayouts.railBottom.size
        // Where the two groups go: the top one from start down to splitTop, the
        // other from end up to splitBottom, with the button size that allows.
        class Arrangement(val start: Float, val end: Float, val splitTop: Float, val splitBottom: Float) {
            val size = min((splitTop - start - gap - gap * top) / top, (end - splitBottom - gap - gap * bottom) / bottom)
        }
        val middle = rail.centerY()
        var best = Arrangement(rail.top, rail.bottom, middle, middle)
        // A camera cutout in the rail's strip: the groups go either side of it
        // (a phone held sideways), or both on its longer side when it is near an
        // end (an opened-out foldable's camera, at the top), whichever leaves
        // the larger buttons.
        if (!cutout.isEmpty && cutout.right > rail.left && cutout.left < rail.right) {
            val below = (cutout.bottom + rail.bottom) / 2
            val above = (rail.top + cutout.top) / 2
            best = listOf(
                Arrangement(rail.top, rail.bottom, cutout.top, cutout.bottom),
                Arrangement(cutout.bottom, rail.bottom, below, below),
                Arrangement(rail.top, cutout.top, above, above),
            ).maxBy { it.size }
        }
        // Shorter buttons if the groups are short of room. However they are drawn,
        // each is pressed anywhere in its share of the rail, edge to edge: on a
        // phone held sideways seven buttons and the camera share one short edge.
        val size = max(0f, min(side, best.size))
        val upper = railButtons.take(top).filter { it.shown }
        val lower = railButtons.drop(top).filter { it.shown }
        upper.forEachIndexed { index, button ->
            val y = best.start + gap + index * (size + gap)
            button.rect.set(x - side / 2, y, x + side / 2, y + size)
            button.touch.set(rail.left, if (index == 0) best.start else y - gap / 2, rail.right, y + size + gap / 2)
        }
        lower.forEachIndexed { index, button ->
            val y = best.end - (lower.size - index) * (size + gap)
            button.rect.set(x - side / 2, y, x + side / 2, y + size)
            button.touch.set(rail.left, y - gap / 2, rail.right, if (index == lower.size - 1) best.end else y + size + gap / 2)
        }
    }

    private fun layoutDrawer() {
        drawerTitles.clear()
        val left = game.left + dp(16f)
        val right = game.right - dp(16f)
        val buttonWidth = dp(104f)
        val buttonHeight = dp(40f)
        val gap = dp(8f)
        var y = game.top + dp(56f)
        ControlLayouts.drawer.forEachIndexed { group, (name, _) ->
            drawerTitles.add(name to y)
            y += dp(10f)
            var x = left
            for (button in drawerButtons[group]) {
                if (x + buttonWidth > right) {
                    x = left
                    y += buttonHeight + gap
                }
                button.rect.set(x, y, x + buttonWidth, y + buttonHeight)
                x += buttonWidth + gap
            }
            y += buttonHeight + dp(28f)
        }
        drawerContentHeight = y - game.top
        drawerClose.rect.set(right - dp(96f), game.top + dp(6f), right, game.top + dp(50f))
        drawerScroll = drawerScroll.coerceIn(0f, maxDrawerScroll())
    }

    private fun maxDrawerScroll() = max(0f, drawerContentHeight - game.height())

    override fun onDraw(canvas: Canvas) {
        if (game.isEmpty) return
        // See-through, like the d-pad: a faint back and a thin edge with the map
        // showing through, the label outlined in dark so it reads over the map.
        for (button in railButtons) if (button.shown && button.command != null) {
            fill.color = Color.argb(110, 10, 12, 16)
            canvas.drawRoundRect(button.rect, dp(8f), dp(8f), fill)
            canvas.drawRoundRect(button.rect, dp(8f), dp(8f), railEdge)
            drawButton(canvas, button, faint = true)
        }
        if (drawerOpen) {
            drawDrawer(canvas)
            return
        }
        if (dpad.visible) drawPad(canvas)
        if (ControlLayouts.showsDigits(gameContext)) for (button in digitButtons) drawButton(canvas, button)
        for (button in stripButtons) drawButton(canvas, button)
        if (barShown) drawBar(canvas)
        drawButton(canvas, primaryButton, accent = true)
        for (button in quickButtons) drawButton(canvas, button)
    }

    private val dashed = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.2f)
        color = Color.argb(90, 255, 255, 255)
        pathEffect = android.graphics.DashPathEffect(floatArrayOf(dp(5f), dp(4f)), 0f)
    }
    private val barVerb = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(235, 240, 190, 90)
        textAlign = Paint.Align.CENTER
        textSize = dp(10.5f)
    }

    private fun drawBar(canvas: Canvas) {
        val corner = dp(7f)
        // The rows button: one small bar per row shown.
        fill.color = if (barTouched == BAR_ROWS) Color.argb(220, 240, 150, 60) else Color.argb(200, 44, 48, 58)
        canvas.drawRoundRect(barRowsButton, corner, corner, fill)
        fill.color = Color.argb(200, 255, 255, 255)
        val markHeight = dp(3f)
        for (row in 0 until maxBarRows) {
            val y = barRowsButton.bottom - dp(9f) - row * (markHeight + dp(4f))
            fill.alpha = if (row < shownBarRows) 220 else 60
            canvas.drawRect(barRowsButton.left + dp(8f), y - markHeight, barRowsButton.right - dp(8f), y, fill)
        }
        for (index in 0 until shownBarRows * 4) {
            val rect = barSlots[index]
            val slot = bar[index]
            val pressed = index == barTouched && !barHeld
            // The bar sits over the busy middle of the map (and the game's
            // target card), so its slots are solid enough to read over anything.
            if (slot.empty) {
                fill.color = if (pressed) Color.argb(160, 240, 150, 60) else Color.argb(150, 16, 18, 24)
                canvas.drawRoundRect(rect, corner, corner, fill)
                canvas.drawRoundRect(rect, corner, corner, dashed)
                val alpha = text.alpha
                text.alpha = 110
                canvas.drawText("+", rect.centerX(), rect.centerY() - (text.descent() + text.ascent()) / 2, text)
                text.alpha = alpha
                continue
            }
            fill.color = when {
                pressed -> Color.argb(220, 240, 150, 60)
                !slot.enabled -> Color.argb(200, 22, 24, 30)
                else -> Color.argb(220, 44, 48, 58)
            }
            canvas.drawRoundRect(rect, corner, corner, fill)
            canvas.drawRoundRect(rect, corner, corner, stroke)
            val dim = !slot.enabled
            // The action and, for items, how many are left; then the name, shrunk or cut to fit.
            val verbLine = if (slot.isItem) "${slot.verb} ×${slot.count}" else slot.verb
            val room = rect.width() - dp(6f)
            if (verbLine.isNotEmpty()) {
                barVerb.alpha = if (dim) 110 else 235
                canvas.drawText(fitted(verbLine, barVerb, room), rect.centerX(), rect.top + rect.height() * 0.38f, barVerb)
            }
            val size = text.textSize
            val alpha = text.alpha
            text.textSize = dp(12f)
            if (dim) text.alpha = 110
            val nameWidth = text.measureText(slot.name)
            if (nameWidth > room) text.textSize = max(dp(9.5f), text.textSize * room / nameWidth)
            val nameY = if (verbLine.isEmpty()) rect.centerY() - (text.descent() + text.ascent()) / 2 else rect.bottom - rect.height() * 0.2f
            canvas.drawText(fitted(slot.name, text, room), rect.centerX(), nameY, text)
            text.textSize = size
            text.alpha = alpha
        }
    }

    /** [label] cut short with "…" if it does not fit [room] at [paint]'s size. */
    private fun fitted(label: String, paint: Paint, room: Float): String {
        if (paint.measureText(label) <= room) return label
        var end = label.length
        while (end > 1 && paint.measureText(label, 0, end) + paint.measureText("…") > room) end--
        return label.substring(0, end) + "…"
    }

    // Faint, so what is beneath shows through: a thin ring and its direction marks,
    // a little stronger while a finger is on it.
    private val padStroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.25f)
    }

    private fun drawPad(canvas: Canvas) {
        val touched = padPointer != -1
        fill.color = Color.argb(if (touched) 30 else 14, 255, 255, 255)
        canvas.drawCircle(padX, padY, padRadius, fill)
        padStroke.color = Color.argb(if (touched) 110 else 70, 255, 255, 255)
        canvas.drawCircle(padX, padY, padRadius, padStroke)
        canvas.drawCircle(padX, padY, padRadius * DEAD_ZONE, padStroke)
        val digits = if (dpad.diagonals) intArrayOf(1, 2, 3, 4, 6, 7, 8, 9) else intArrayOf(2, 4, 6, 8)
        for (digit in digits) {
            val (dx, dy) = offsetOf(digit)
            val active = digit == padDirection
            fill.color = if (active) Color.argb(200, 240, 150, 60) else Color.argb(if (touched) 110 else 70, 255, 255, 255)
            val x = padX + dx * padRadius * 0.72f
            val y = padY + dy * padRadius * 0.72f
            val label = dpad.labels[digit]
            if (label == null) {
                canvas.drawCircle(x, y, dp(if (active) 9f else 5f), fill)
            } else {
                // Labelled directions (fishing) say what they do.
                val size = text.textSize
                text.textSize = dp(11f)
                canvas.drawText(label, x, y - (text.descent() + text.ascent()) / 2, text)
                text.textSize = size
            }
        }
        if (padCentreTap) {
            fill.color = Color.argb(120, 240, 150, 60)
            canvas.drawCircle(padX, padY, padRadius * DEAD_ZONE, fill)
        }
        dpad.centre?.let { label ->
            // As faint as the direction marks, brighter while pressed.
            val size = text.textSize
            val colour = text.color
            text.textSize = dp(12f)
            text.color = Color.argb(if (padCentreTap) 230 else if (touched) 150 else 105, 255, 255, 255)
            canvas.drawText(label, padX, padY - (text.descent() + text.ascent()) / 2, text)
            text.textSize = size
            text.color = colour
        }
    }

    private fun drawButton(canvas: Canvas, button: Button, accent: Boolean = false, faint: Boolean = false) {
        val command = button.command ?: return
        fill.color = when {
            !command.enabled -> Color.argb(36, 255, 255, 255)
            button.pressed -> Color.argb(190, 240, 150, 60)
            accent -> Color.argb(150, 240, 150, 60)
            faint -> Color.argb(24, 255, 255, 255)
            else -> Color.argb(72, 255, 255, 255)
        }
        if (button.round) {
            canvas.drawOval(button.rect, fill)
        } else {
            canvas.drawRoundRect(button.rect, dp(8f), dp(8f), fill)
        }
        // Long labels shrink to fit their button; unavailable ones are dimmed.
        val size = text.textSize
        val alpha = text.alpha
        if (!command.enabled) text.alpha = 110
        val lines = labelLines(button)
        if (lines.size == 1) {
            text.textSize = lines[0].second
            val y = button.rect.centerY() - (text.descent() + text.ascent()) / 2
            if (faint) {
                textOutline.textSize = text.textSize
                canvas.drawText(lines[0].first, button.rect.centerX(), y, textOutline)
            }
            canvas.drawText(lines[0].first, button.rect.centerX(), y, text)
        } else {
            // A second, smaller line says what it acts on ("Fire again" / "Seeker Arrows").
            val gap = dp(2f)
            val heights = lines.map { it.second * 1.1f }
            var top = button.rect.centerY() - (heights.sum() + gap * (lines.size - 1)) / 2
            lines.forEachIndexed { index, (line, lineSize) ->
                text.textSize = lineSize
                val baseline = top + heights[index] / 2 - (text.descent() + text.ascent()) / 2
                canvas.drawText(line, button.rect.centerX(), baseline, text)
                top += heights[index] + gap
            }
        }
        text.textSize = size
        text.alpha = alpha
    }

    /** A button's label as drawn: each line, and its text size shrunk to fit the button. */
    private fun labelLines(button: Button): List<Pair<String, Float>> {
        val label = button.command?.label ?: return emptyList()
        val size = text.textSize
        val lines = label.split('\n')
        val result = if (lines.size == 1) {
            val width = text.measureText(label)
            val room = button.rect.width() - dp(6f)
            listOf(label to if (width > room) size * room / width else size)
        } else {
            // A round button is narrower above and below its middle.
            val room = button.rect.width() * (if (button.round) 0.8f else 1f) - dp(6f)
            lines.mapIndexed { index, line ->
                text.textSize = if (index == 0) size else size * 0.8f
                val width = text.measureText(line)
                line to if (width > room) text.textSize * room / width else text.textSize
            }
        }
        text.textSize = size
        return result
    }

    /**
     * Debug builds: where every control is, what it says and how large its
     * label is drawn (in view pixels; sizes also in dp), for
     * automated layout checks. Asked for by AmmerowActivity's probe broadcast.
     */
    fun probeReport(seq: Int): String {
        fun rect(r: RectF) = JSONArray(listOf(r.left, r.top, r.right, r.bottom).map { it.toDouble() })
        val controls = JSONArray()
        fun add(kind: String, button: Button, offsetY: Float = 0f) {
            val command = button.command ?: return
            val shifted = RectF(button.rect).apply { offset(0f, offsetY) }
            controls.put(JSONObject().apply {
                put("kind", kind)
                put("label", command.label.replace('\n', ' '))
                put("rect", rect(shifted))
                if (kind == "rail") put("touch", rect(button.touch))
                put("round", button.round)
                put("enabled", command.enabled)
                put("text_dp", JSONArray(labelLines(button).map { (it.second / density).toDouble() }))
            })
        }
        for (button in railButtons) if (button.shown) add("rail", button)
        if (drawerOpen) {
            for (group in drawerButtons) for (button in group) add("drawer", button, -drawerScroll)
            add("drawer", drawerClose)
        } else {
            if (ControlLayouts.showsDigits(gameContext)) for (button in digitButtons) add("digit", button)
            for (button in stripButtons) add("strip", button)
            add("cluster", primaryButton)
            for (button in quickButtons) add("cluster", button)
            if (barShown) {
                controls.put(JSONObject().apply {
                    put("kind", "bar")
                    put("label", "BarRows")
                    put("rect", rect(barRowsButton))
                    put("round", false)
                    put("enabled", true)
                    put("text_dp", JSONArray())
                })
                for (index in 0 until shownBarRows * 4) {
                    controls.put(JSONObject().apply {
                        put("kind", "bar")
                        put("label", bar[index].let { if (it.empty) "+" else it.title })
                        put("rect", rect(barSlots[index]))
                        put("round", false)
                        put("enabled", !bar[index].empty && bar[index].enabled)
                        put("text_dp", JSONArray())
                    })
                }
            }
        }
        val origin = IntArray(2).also { getLocationOnScreen(it) }
        return JSONObject().apply {
            put("seq", seq)
            put("density", density.toDouble())
            put("view", JSONArray(listOf(width, height)))
            put("screen_origin", JSONArray(origin.toList()))
            put("game", rect(game))
            put("drawing", rect(drawing))
            put("rail", rect(rail))
            put("cutout", rect(cutout))
            put("tablet", tablet)
            put("mode", gameContext.mode.name)
            put("primary", gameContext.primary)
            put("flags", gameContext.flags)
            put("detail", gameContext.detail)
            put("verbs", JSONArray(verbs.map { it.label }))
            put("drawer", drawerOpen)
            put("bar_rows", shownBarRows)
            if (dpad.visible && !drawerOpen) {
                put("dpad", JSONObject().apply {
                    put("x", padX.toDouble())
                    put("y", padY.toDouble())
                    put("r", padRadius.toDouble())
                    put("reach", padReach.toDouble())
                })
            }
            put("controls", controls)
        }.toString(1)
    }

    private fun drawDrawer(canvas: Canvas) {
        fill.color = Color.argb(235, 12, 14, 18)
        canvas.drawRect(game, fill)
        canvas.save()
        canvas.clipRect(game)
        canvas.translate(0f, -drawerScroll)
        val hintColor = title.color
        title.color = Color.argb(170, 255, 255, 255)
        canvas.drawText("Tap a command to use it. Hold it to put it on the quick bar.",
            game.left + dp(16f), game.top + dp(28f), title)
        title.color = hintColor
        for ((name, y) in drawerTitles) canvas.drawText(name, game.left + dp(16f), y, title)
        for (group in drawerButtons) for (button in group) drawButton(canvas, button)
        canvas.restore()
        drawButton(canvas, drawerClose, accent = true)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val index = event.actionIndex
        val id = event.getPointerId(index)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> touchDown(id, event.getX(index), event.getY(index))
            MotionEvent.ACTION_MOVE -> touchMove(event)
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> touchUp(id, event.getX(index), event.getY(index))
            MotionEvent.ACTION_CANCEL -> {
                for (pointer in activeButtons.keys.toList()) releaseButton(pointer)
                releasePad()
                cancelGameTouch()
                releaseBar()
            }
        }
        return true
    }

    private fun touchDown(id: Int, x: Float, y: Float) {
        railButtons.firstOrNull { it.shown && it.touch.contains(x, y) }?.let { return pressButton(id, it) }
        // The map runs on under the rail, but a touch there, between its buttons
        // or on the camera, is a finger that missed one, not a walk to the edge.
        if (rail.contains(x, y)) return
        if (drawerOpen) {
            if (drawerClose.rect.contains(x, y)) return pressButton(id, drawerClose)
            startGameTouch(id, x, y)
            return
        }
        if (barShown && barPointer == -1) {
            val target = if (barRowsButton.contains(x, y)) BAR_ROWS else
                (0 until shownBarRows * 4).firstOrNull { barSlots[it].contains(x, y) }
            if (target != null) {
                barPointer = id
                barTouched = target
                barHeld = false
                performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                if (target != BAR_ROWS && !bar[target].empty) {
                    handler.postDelayed(barHold, ViewConfiguration.getLongPressTimeout().toLong())
                }
                invalidate()
                return
            }
        }
        val clusterButton = (quickButtons + primaryButton).firstOrNull { it.command != null && it.rect.contains(x, y) }
        if (ControlLayouts.showsDigits(gameContext)) {
            digitButtons.firstOrNull { it.rect.contains(x, y) }?.let { return pressButton(id, it) }
        }
        stripButtons.firstOrNull { it.command != null && it.rect.contains(x, y) }?.let { return pressButton(id, it) }
        if (clusterButton != null) {
            // A tap meant for the old action (Wait, just as a fish bites) must not
            // become the new one (Strike): the large button ignores taps briefly
            // after it changes.
            if (clusterButton === primaryButton &&
                android.os.SystemClock.uptimeMillis() - primaryChangedAt < PRIMARY_CHANGE_GUARD_MS) return
            return pressButton(id, clusterButton)
        }
        // A finger that lands a little outside the ring still means the d-pad: on the
        // map, a near miss would otherwise be a tap that walks somewhere.
        if (dpad.visible && padPointer == -1 && hypot(x - padX, y - padY) <= padRadius * padReach) {
            padPointer = id
            padCentreTap = hypot(x - padX, y - padY) < padRadius * DEAD_ZONE
            centreRepeated = false
            if (padCentreTap && dpad.centre != null) handler.postDelayed(centreRepeat, FIRST_REPEAT_MS)
            updatePad(x, y)
            invalidate()
            return
        }
        if (game.contains(x, y)) {
            // A second finger on the map turns the touch into a pinch.
            if (gamePointer != -1 && pinchPointer == -1 && !drawerOpen && gameContext.mode.pinchZooms) {
                startPinch(id, x, y)
            } else {
                startGameTouch(id, x, y)
            }
        }
    }

    // Pinch zoom: two fingers on the map scale it continuously with their
    // spread (src/sdl3/zoom.c and frontend-events.c: a scaled snapshot while the fingers move,
    // the real zoom on release); updates are sent at most every PINCH_INTERVAL_MS,
    // and the zoom is saved when the pinch ends.
    private var pinchPointer = -1
    private var pinchStartSpread = 0f
    private var pinchFactor = 1000
    private var pinchSentAt = 0L

    private fun startPinch(id: Int, x: Float, y: Float) {
        pinchPointer = id
        pinchStartSpread = hypot(x - gameDownX, y - gameDownY).coerceAtLeast(1f)
        pinchFactor = 1000
        pinchSentAt = 0L
        NativeBridge.pinchZoom(PINCH_BEGIN, pinchFactor)
        // Neither finger is a tap or a long press any more.
        gameMoved = true
        handler.removeCallbacks(longPress)
    }

    private fun updatePinch(event: MotionEvent) {
        val a = event.findPointerIndex(gamePointer)
        val b = event.findPointerIndex(pinchPointer)
        if (a < 0 || b < 0) return
        val spread = hypot(event.getX(a) - event.getX(b), event.getY(a) - event.getY(b))
        val factor = (spread / pinchStartSpread * 1000).toInt().coerceIn(50, 16000)
        val now = android.os.SystemClock.uptimeMillis()
        if (factor != pinchFactor && now - pinchSentAt >= PINCH_INTERVAL_MS) {
            pinchFactor = factor
            pinchSentAt = now
            NativeBridge.pinchZoom(PINCH_UPDATE, factor)
        }
    }

    private fun endPinch() {
        if (pinchPointer == -1) return
        pinchPointer = -1
        NativeBridge.pinchZoom(PINCH_END, pinchFactor)
    }

    private fun touchMove(event: MotionEvent) {
        if (padPointer != -1) {
            val padIndex = event.findPointerIndex(padPointer)
            if (padIndex >= 0) updatePad(event.getX(padIndex), event.getY(padIndex))
        }
        if (pinchPointer != -1) {
            updatePinch(event)
            return
        }
        if (gamePointer != -1) {
            val gameIndex = event.findPointerIndex(gamePointer)
            if (gameIndex < 0) return
            val x = event.getX(gameIndex)
            val y = event.getY(gameIndex)
            if (!gameMoved && hypot(x - gameDownX, y - gameDownY) > touchSlop) {
                gameMoved = true
                handler.removeCallbacks(longPress)
                handler.removeCallbacks(drawerHold)
            }
            if (drawerOpen && gameMoved) {
                drawerScroll = (drawerScroll - (y - gameDownY)).coerceIn(0f, maxDrawerScroll())
                gameDownY = y
                invalidate()
            }
        }
    }

    private fun touchUp(id: Int, x: Float, y: Float) {
        if (id == padPointer) {
            // A tap on the centre waits (or stays still) for a turn.
            if (padCentreTap && !centreRepeated) step(5)
            releasePad()
        }
        releaseButton(id)
        if (id == barPointer) {
            val target = barTouched
            val held = barHeld
            releaseBar()
            // A slot acts when the finger lifts, so that holding it can empty it instead.
            val stillOver = if (target == BAR_ROWS) barRowsButton.contains(x, y) else barSlots[target].contains(x, y)
            if (!held && stillOver) tapBar(target)
            return
        }
        if (id == pinchPointer || (id == gamePointer && pinchPointer != -1)) {
            // Lifting either finger ends the pinch; the other is not a tap.
            endPinch()
            gamePointer = -1
            return
        }
        if (id != gamePointer) return
        handler.removeCallbacks(longPress)
        handler.removeCallbacks(drawerHold)
        val tapped = !gameMoved && !longPressed
        gamePointer = -1
        if (!tapped) return
        if (drawerOpen) {
            val scrolledY = y + drawerScroll
            val button = drawerButtons.flatten().firstOrNull { it.rect.contains(x, scrolledY) } ?: return
            drawerOpen = false
            invalidate()
            button.command?.let { run(it.action) }
            return
        }
        if (gameContext.mode.sendsTaps) sendClick(x, y, TAP_BUTTON)
    }

    private fun startGameTouch(id: Int, x: Float, y: Float) {
        if (gamePointer != -1) return
        gamePointer = id
        gameDownX = x
        gameDownY = y
        gameMoved = false
        longPressed = false
        if (drawerOpen) {
            handler.postDelayed(drawerHold, ViewConfiguration.getLongPressTimeout().toLong())
        } else if (gameContext.mode.hasContextMenus) {
            handler.postDelayed(longPress, ViewConfiguration.getLongPressTimeout().toLong())
        }
    }

    /** Holding a More command puts it on the quick bar (android/pins.c says so, or why not). */
    private val drawerHold = Runnable {
        if (gamePointer == -1 || gameMoved || !drawerOpen) return@Runnable
        val button = drawerButtons.flatten().firstOrNull { it.rect.contains(gameDownX, gameDownY + drawerScroll) }
        val command = button?.command ?: return@Runnable
        longPressed = true
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        val keys = ControlLayouts.pinKeys(command)
        if (keys == null) {
            onNotice?.invoke("${command.label} cannot go on the quick bar.")
        } else {
            NativeBridge.pinCommand(command.label, keys)
        }
    }

    private fun cancelGameTouch() {
        handler.removeCallbacks(longPress)
        handler.removeCallbacks(drawerHold)
        gamePointer = -1
        endPinch()
    }

    private fun releaseBar() {
        handler.removeCallbacks(barHold)
        barPointer = -1
        barTouched = -1
        barHeld = false
        invalidate()
    }

    private fun sendClick(x: Float, y: Float, button: Int) {
        val point = toGamePoint?.invoke(x, y) ?: return
        NativeBridge.click(point.first, point.second, button)
    }

    private fun pressButton(pointer: Int, button: Button) {
        val command = button.command ?: return
        if (!command.enabled) return
        button.pressed = true
        activeButtons[pointer] = button
        pressedActions[pointer] = command.action
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        when (val action = command.action) {
            is Action.Hold -> NativeBridge.holdKey(action.key, true)
            is Action.Repeat -> {
                run(action.key)
                repeatingKey = action.key
                handler.removeCallbacks(repeatButton)
                handler.postDelayed(repeatButton, FIRST_REPEAT_MS)
            }
            else -> run(action)
        }
        invalidate()
    }

    private fun run(action: Action) {
        when (action) {
            is Action.Key -> NativeBridge.pressKey(action.key, action.mod, action.text)
            is Action.Keys -> action.keys.forEach { NativeBridge.pressKey(it.key, it.mod, it.text) }
            is Action.MapCommand -> NativeBridge.runCommand(action.key.key, action.key.mod, action.key.text)
            is Action.Verb -> NativeBridge.runVerb(action.value)
            is Action.PinItem -> NativeBridge.pinItem(action.value)
            Action.PinSpell -> NativeBridge.pinSpell()
            is Action.Hold, is Action.Repeat -> Unit
            Action.Keyboard -> onToggleKeyboard?.invoke()
            Action.Drawer -> {
                drawerOpen = !drawerOpen
                releasePad()
                invalidate()
            }
        }
    }

    private fun releaseButton(pointer: Int) {
        val button = activeButtons.remove(pointer) ?: return
        button.pressed = false
        endHeld(pressedActions.remove(pointer))
        invalidate()
    }

    /** Lets go of a held key, or stops a repeat. */
    private fun endHeld(action: Action?) {
        when (action) {
            is Action.Hold -> NativeBridge.holdKey(action.key, false)
            is Action.Repeat -> {
                repeatingKey = null
                handler.removeCallbacks(repeatButton)
            }
            else -> Unit
        }
    }

    private fun releasePad() {
        handler.removeCallbacks(centreRepeat)
        padPointer = -1
        padCentreTap = false
        centreRepeated = false
        setPadDirection(0)
        invalidate()
    }

    private fun updatePad(x: Float, y: Float) {
        val dx = x - padX
        val dy = y - padY
        if (hypot(dx, dy) < padRadius * DEAD_ZONE) {
            setPadDirection(0)
            return
        }
        if (padCentreTap) handler.removeCallbacks(centreRepeat)
        padCentreTap = false
        val angle = Math.toDegrees(atan2(dy, dx).toDouble()) + 360
        if (dpad.diagonals) {
            // Eight 45-degree sectors, clockwise from east.
            setPadDirection(SECTOR_DIGITS[((angle + 22.5) / 45).toInt() % 8])
        } else {
            // Four 90-degree sectors, clockwise from east.
            setPadDirection(intArrayOf(6, 2, 4, 8)[((angle + 45) / 90).toInt() % 4])
        }
    }

    private fun setPadDirection(digit: Int) {
        if (digit == padDirection) return
        padDirection = digit
        handler.removeCallbacks(repeat)
        if (digit != 0) {
            step(digit)
            handler.postDelayed(repeat, FIRST_REPEAT_MS)
        }
        invalidate()
    }

    private fun step(digit: Int) {
        performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
        // Straight directions are arrow keys, which every screen understands
        // (tabs, lists, settings); diagonals and waiting need the keypad.
        val key = when (digit) {
            8 -> Keys.UP
            2 -> Keys.DOWN
            4 -> Keys.LEFT
            6 -> Keys.RIGHT
            else -> Keys.keypad(digit)
        }
        NativeBridge.pressKey(key, 0, 0)
    }

    private fun offsetOf(digit: Int): Pair<Float, Float> {
        val dx = when (digit) { 1, 4, 7 -> -1f; 3, 6, 9 -> 1f; else -> 0f }
        val dy = when (digit) { 7, 8, 9 -> -1f; 1, 2, 3 -> 1f; else -> 0f }
        val length = hypot(dx, dy)
        return Pair(dx / length, dy / length)
    }

    private companion object {
        const val DEAD_ZONE = 0.28f
        /** How far beyond its ring (in radii) a touch on the map still lands on the d-pad. */
        const val PAD_REACH = 1.35f
        const val PRIMARY_CHANGE_GUARD_MS = 400L
        const val PINCH_BEGIN = 0
        const val PINCH_UPDATE = 1
        const val PINCH_END = 2
        const val PINCH_INTERVAL_MS = 16L
        const val FIRST_REPEAT_MS = 320L
        /** The game's rows for a side-view cave's status strip (SDL3_LAYOUT_CAVE_STATUS_ROWS). */
        const val CAVE_STRIP_ROWS = 3
        const val REPEAT_MS = 130L
        /** SDL mouse buttons: the game's button 1 walks or selects, its button 2 opens its menus. */
        const val TAP_BUTTON = 1
        const val MENU_BUTTON = 2
        /** [barTouched] for the quick bar's rows button. */
        const val BAR_ROWS = -2
        // atan2 sectors from east, clockwise (screen y grows downwards).
        val SECTOR_DIGITS = intArrayOf(6, 3, 2, 1, 4, 7, 8, 9)
    }
}

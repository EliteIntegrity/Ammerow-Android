/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

package com.ammerow.game

/** What the game is waiting for (keep in step with android/context.c). */
enum class ControlMode(val id: Int) {
    NONE(0), MAP(1), MORE(2), PROMPT(3), CAVE(4), FISHING(5), TEXT(6), CHECK(7), AIM(8), TARGET(9), STORE(10),
    /** An item list opened to browse it (Inventory, Equipment, Quiver): the buttons are the highlighted item's actions. */
    ITEMS(11),
    /** A book's spell list (casting, studying or browsing); detail is a [SpellKind]. */
    SPELLS(12);

    /** A long press on the map opens the game's own menu for that square. */
    val hasContextMenus get() = this == MAP

    /** Two fingers on the map zoom it (the game zooms in these views). */
    val pinchZooms get() = this == MAP || this == CAVE

    /** Taps on the game reach it as clicks; side-view caves take no mouse input. */
    val sendsTaps get() = this != CAVE

    /**
     * The large button ignores taps briefly after the game changes it (a
     * monster appears, a fish bites). In item lists it changes because the
     * player picked another item, and then a quick tap is meant.
     */
    val guardsPrimary get() = this != ITEMS

    companion object {
        fun of(id: Int) = entries.firstOrNull { it.id == id } ?: NONE
    }
}

/** The map's context action (keep in step with android/context.c). */
object Primary {
    /** Nothing in particular here. */
    const val NONE = 0
    const val GET = 1
    const val DOWN = 2
    const val UP = 3
    const val ENTER = 4
    const val SHOP = 5
    const val FISH = 6
    const val OPEN = 7
    // Side-view caves (GET, UP and DOWN apply there too: items and passages).
    const val CAVE_JUMP = 20
    const val CAVE_GRIP = 21
    const val CAVE_LEAVE = 22
    const val CAVE_WAIT = 23
}

/** A shop's list, as bits of [GameContext.detail] (keep in step with android/context.c). */
object StoreFlags {
    /** The home: Take and Stash rather than Buy and Sell. */
    const val HOME = 1
    /** Takes a fishing catch for the village larder (Donate). */
    const val DONATIONS = 2
}

/** Looking or targeting, as bits of [GameContext.detail] (keep in step with android/context.c). */
object TargetFlags {
    /** The cursor is on a monster. */
    const val ON_MONSTER = 1
    /** The cursor is on an item the player knows of. */
    const val ON_OBJECT = 2
    /** Choosing a target (from aiming), not just looking. */
    const val CHOOSING = 4
    /** "g" walks to the cursor. */
    const val CAN_WALK = 8
    /** The cursor is on the player (where Look starts). */
    const val ON_SELF = 16
}

/** Aiming ([ControlMode.AIM]), as bits of [GameContext.detail] (keep in step with android/context.c). */
object AimFlags {
    /** A target is set: At target aims at it. */
    const val TARGET_SET = 1
}

/** What a spell list is for, sent as [GameContext.detail] (keep in step with enum spell_kind). */
object SpellKind {
    const val CAST = 0
    const val STUDY = 1
    const val BROWSE = 2
    const val OTHER = 3
}

/** What a text prompt wants, sent as [GameContext.detail] (keep in step with enum text_kind). */
object TextKind {
    const val GENERAL = 0
    const val NUMBER = 1
    const val NAME = 2
}

/** Outside a run ([ControlMode.NONE]), which screen is up, sent as [GameContext.detail] (keep in step with enum outside_page). */
object OutsidePage {
    /** Home, the run summary and anything else. */
    const val OTHER = 0
    /** Origin, class or attribute method: "=" opens the birth options. */
    const val BIRTH_MENU = 1
    /** Point-based attributes: "r" resets. */
    const val BIRTH_POINTS = 2
    /** Rolled attributes: "r" rerolls... */
    const val BIRTH_ROLLER = 3
    /** ...and "p" brings back the previous roll. */
    const val BIRTH_ROLLER_PREVIOUS = 4
    /** "New character based on previous one": y, n, c, =. */
    const val BIRTH_QUICKSTART = 5
    /** An options page (the birth options): Enter toggles the highlighted option. */
    const val OPTIONS = 6
    /** The end of a run: I, M, X, H and V show its record. */
    const val RUN_SUMMARY = 7
    /** Home's saved runs: Delete removes the highlighted one (after asking). */
    const val HOME_LOAD = 8
    /** "Delete saved run?": y deletes it, n keeps it. */
    const val HOME_DELETE = 9
    /** Home's own menu. */
    const val HOME_MENU = 10
}

/** [ControlMode.PROMPT]'s detail (keep in step with android/context.c). */
object PromptScreen {
    /** The character dossier: "c" renames. */
    const val DOSSIER = 1
    /** An options page: Enter (or a tap on an option) switches the highlighted option. */
    const val OPTIONS = 2
    /** The message log: "=" finds text... */
    const val LOG = 3
    /** ...and once it has, "-" goes to the next match. */
    const val LOG_FOUND = 4
    /** The level map: any key closes it. */
    const val LEVEL_MAP = 5
    /** The field guide: a page of help, or its list of sections. */
    const val HELP = 6
    /** A story card (the opening, a quest briefing, a donation's receipt): OK or Back goes on. */
    const val STORY = 7
}

/**
 * [ControlMode.MAP]'s detail with a monster in view: what the game's "repeat
 * previous command" would do again as it was (keep in step with enum
 * again_kind). Its ammunition, spell or missile comes as the only [Verb].
 */
object Again {
    const val NONE = 0
    const val FIRE = 1
    const val CAST = 2
    const val THROW = 3
}

/** Fishing phases, sent as [GameContext.detail] (keep in step with enum world_fishing_phase). */
object FishingPhase {
    const val WAITING = 1
    const val BITE = 2
    const val WINDING = 3
}

/** Context flags (keep in step with android/context.c). */
object ContextFlags {
    /** Knows a spell and can cast now. */
    const val CAN_CAST = 1
    /** A launcher, and ammunition for it in the quiver. */
    const val CAN_FIRE = 2
    const val ROGUELIKE_KEYS = 4
    /** Carries a throwing weapon or flask. */
    const val CAN_THROW = 8
    /** A monster in view: the game will not explore or rest usefully. */
    const val MONSTER_IN_VIEW = 16
    /** A run is in play (sent with every mode, prompts included). */
    const val IN_RUN = 32
    /** On one of the surface areas, away from stairs: "<" opens known-world travel. */
    const val TRAVEL = 64
}

/**
 * One of the highlighted item's actions, from the game's own menu for it
 * (android/context.c), best first. [value] identifies it to [NativeBridge.runVerb].
 */
data class Verb(val value: Int, val label: String, val enabled: Boolean, val pinnable: Boolean = false) {
    companion object {
        /** Parses onGameVerbs' text: one action per line, "value<TAB>1 or 0<TAB>label<TAB>pinnable". */
        fun parse(text: String): List<Verb> = text.lineSequence().mapNotNull { line ->
            val parts = line.split('\t')
            val value = parts.getOrNull(0)?.toIntOrNull() ?: return@mapNotNull null
            if (parts.size < 3) return@mapNotNull null
            Verb(value, parts[2], parts[1] == "1", parts.getOrNull(3) == "1")
        }.toList()
    }
}

/**
 * One quick-bar slot as the game describes it (android/pins.c): an item action
 * (with how many the character has), a spell, or a More command; [kind] 0 is
 * an empty slot.
 */
data class BarSlot(val kind: Int, val verb: String, val name: String, val count: Int, val enabled: Boolean) {
    val empty get() = kind == EMPTY
    val isItem get() = kind == ITEM

    /** "Quaff Cure Light Wounds", "Use Magic Missile", "Rest". */
    val title get() = if (verb.isEmpty()) name else "$verb $name"

    companion object {
        const val EMPTY = 0
        const val ITEM = 1
        const val SPELL = 2
        const val COMMAND = 3
        const val COUNT = 12

        /** Parses onGameBar's text: one slot per line, "kind<TAB>verb<TAB>name<TAB>count<TAB>1 or 0". */
        fun parse(text: String): List<BarSlot> {
            val slots = text.lineSequence().filter { it.isNotEmpty() }.map { line ->
                val parts = line.split('\t')
                BarSlot(
                    parts.getOrNull(0)?.toIntOrNull() ?: EMPTY,
                    parts.getOrNull(1) ?: "",
                    parts.getOrNull(2) ?: "",
                    parts.getOrNull(3)?.toIntOrNull() ?: 0,
                    parts.getOrNull(4) == "1",
                )
            }.toList()
            return List(COUNT) { slots.getOrNull(it) ?: BarSlot(EMPTY, "", "", 0, false) }
        }
    }
}

data class GameContext(
    val mode: ControlMode = ControlMode.NONE,
    val primary: Int = Primary.NONE,
    val flags: Int = 0,
    /** The fishing phase while fishing, or a [TextKind] at a text prompt, else 0. */
    val detail: Int = 0,
) {
    fun has(flag: Int) = flags and flag != 0
}

sealed interface Action {
    /** A key press; [text] is the character it types (0 for none). */
    data class Key(val key: Int, val mod: Int = 0, val text: Int = 0) : Action
    /** Several key presses in a row, e.g. a command and its default answer. */
    data class Keys(val keys: List<Key>) : Action
    /** Down while the finger is held, up when it lifts (cave Peek). */
    data class Hold(val key: Int) : Action
    /** A map command from the rail: from inside another screen, Back is pressed until the map is reached. */
    data class MapCommand(val key: Key) : Action
    /** Pressed when the finger lands, then again and again while it stays (reeling in). */
    data class Repeat(val key: Key) : Action
    /** One of the highlighted item's actions. */
    data class Verb(val value: Int) : Action
    /** Puts the highlighted item's action on the quick bar. */
    data class PinItem(val value: Int) : Action
    /** Puts the highlighted spell on the quick bar. */
    data object PinSpell : Action
    data object Keyboard : Action
    data object Drawer : Action
}

/** A button. A disabled one is shown (the game lists it, e.g. aiming an empty wand) but does nothing. */
data class Command(val label: String, val action: Action, val enabled: Boolean = true)

/** The key that types [c] in the original keyset. */
fun typed(c: Char): Action.Key = when {
    c.isUpperCase() -> Action.Key(c.lowercaseChar().code, Keys.MOD_SHIFT, c.code)
    else -> Action.Key(c.code, 0, c.code)
}

fun ctrl(c: Char) = Action.Key(c.lowercaseChar().code, Keys.MOD_CTRL)

private fun cmd(label: String, c: Char) = Command(label, typed(c))

/** The right-hand cluster: one large button and up to three around it, in fixed places. */
data class Cluster(val primary: Command?, val quick: List<Command?>)

/**
 * The d-pad for a mode: whether it shows, whether diagonals count, what each
 * direction does where that is not plain movement (keypad digit to label),
 * and what its centre does where that is worth naming (Wait): the name is
 * drawn faintly there, and holding the centre repeats it.
 */
data class Dpad(
    val visible: Boolean = true,
    val diagonals: Boolean = true,
    val labels: Map<Int, String> = emptyMap(),
    val centre: String? = null,
)

object ControlLayouts {
    private val ok = Command("OK", Action.Key(Keys.RETURN))
    private val back = Command("Back", Action.Key(Keys.ESCAPE))
    // The game's auto-explore walks only to the nearest unexplored spot, so it is under More rather than on the map's buttons.
    private val explore = cmd("Explore", 'p')
    // Rest asks how long; Return takes the default, "as needed".
    private val rest = Command("Rest", Action.Keys(listOf(typed('R'), Action.Key(Keys.RETURN))))
    private val look = cmd("Look", 'l')
    // Waits a turn: the game's "stay still" (on a shop entrance it goes in).
    private val hold = cmd("Wait", ',')

    /** Shift+/ is what the game and its Home and menu screens take as "?". */
    private val questionMark = Action.Key('/'.code, Keys.MOD_SHIFT, '?'.code)

    /** The rail beside the game: system buttons, the same in every mode. */
    private fun mapCmd(label: String, c: Char) = Command(label, Action.MapCommand(typed(c)))

    private val railMenu = Command("Menu", Action.Key(Keys.ESCAPE))
    // "?" opens the field guide (on Home too).
    private val railHelp = Command("Help", Action.MapCommand(questionMark))

    val railTop = listOf(railMenu, mapCmd("Inv", 'i'), Command("More", Action.Drawer), railHelp)
    val railBottom = listOf(
        mapCmd("Map", 'M'),
        Command("Log", Action.MapCommand(ctrl('p'))),
        mapCmd("Char", 'C'),
    )

    /**
     * Without a run in play (Home and the screens reached from it, character
     * creation and its prompts, the end of a run) only Menu and Help mean
     * anything.
     */
    fun railShows(command: Command, context: GameContext) =
        context.has(ContextFlags.IN_RUN) || command == railMenu || command == railHelp

    /**
     * The on-screen keyboard is a last resort: prompts get buttons (OK, Random,
     * digits, Yes/No), and Type opens the keyboard only when a word must be typed.
     */
    private val type = Command("Type…", Action.Keyboard)
    private val delete = Command("Del", Action.Key(Keys.BACKSPACE))

    /** Digit buttons shown at number prompts. */
    val digits = (1..9).map { cmd("$it", '0' + it) } + cmd("0", '0')

    fun showsDigits(context: GameContext) = context.mode == ControlMode.TEXT && context.detail == TextKind.NUMBER

    private val wait = Command("Wait", typed(' '))
    private val leave = Command("Leave", Action.Key(Keys.ESCAPE))
    private val grip = cmd("Grip", 'h')
    private val piton = cmd("Piton", '\'')
    private val rope = cmd("Rope", ';')
    private val peek = Command("Peek", Action.Hold(Keys.P))
    private val jump = cmd("Jump", 'j')
    private val fish = Command("Fish", ctrl('c'))

    fun dpad(context: GameContext): Dpad = when (context.mode) {
        ControlMode.MORE, ControlMode.TEXT, ControlMode.CHECK -> Dpad(visible = false)
        // While a fish bites or is being wound in, only the big button applies.
        ControlMode.FISHING -> if (context.detail == FishingPhase.WAITING) {
            Dpad(diagonals = false, labels = mapOf(4 to "Out", 6 to "In", 2 to "Deeper", 8 to "Shallower"))
        } else {
            Dpad(visible = false)
        }
        // Up and down move the highlight, left and right change tab; diagonals mean nothing in a list.
        ControlMode.ITEMS, ControlMode.STORE, ControlMode.SPELLS -> Dpad(diagonals = false)
        // "Delete saved run?" takes only Yes or No. Home's menu is a short list to tap,
        // whose last entries lie under the d-pad's corner on a phone.
        ControlMode.NONE -> Dpad(visible = context.detail != OutsidePage.HOME_DELETE &&
            context.detail != OutsidePage.HOME_MENU)
        // A story card does not scroll; the pad would only cover its last line.
        ControlMode.PROMPT -> Dpad(visible = context.detail != PromptScreen.STORY)
        // The centre is the game's keypad 5: stay still a turn (in caves too, where
        // waiting on stable ground is how stamina comes back).
        ControlMode.MAP, ControlMode.CAVE -> Dpad(centre = "Wait")
        else -> Dpad()
    }

    private fun primaryCommand(primary: Int) = when (primary) {
        Primary.GET -> cmd("Get", 'g')
        Primary.DOWN -> cmd("Down", '>')
        Primary.UP -> cmd("Up", '<')
        Primary.ENTER -> cmd("Enter", '>')
        // Standing still on a shop entrance goes in.
        Primary.SHOP -> cmd("Shop", ',')
        Primary.FISH -> fish
        Primary.OPEN -> cmd("Open", 'o')
        else -> hold
    }

    // Each asks the game's own questions: which ammunition, magic or missile,
    // then where (Nearest, Choose, At target, the d-pad or a tap). Fire
    // nearest, which skips them, is under More and can go on the quick bar.
    // Magic is the game's "Use learned magic".
    private val fire = cmd("Fire", 'f')
    private val cast = cmd("Use magic", 'm')
    private val throwIt = cmd("Throw", 'v')
    private val travel = cmd("Travel", '<')

    /** Ranged attacks the character has now, best first. */
    private fun attacks(context: GameContext) = listOfNotNull(
        fire.takeIf { context.has(ContextFlags.CAN_FIRE) },
        cast.takeIf { context.has(ContextFlags.CAN_CAST) },
        throwIt.takeIf { context.has(ContextFlags.CAN_THROW) },
    )

    /**
     * The last shot, magic or throw again: the game's "repeat previous
     * command", with the same ammunition, magic or missile at the same target
     * (if that has gone, it asks where). Its second line names what it uses;
     * [attack] is the same attack from the start, for anything different.
     */
    private class AgainButton(val command: Command, val attack: Command)

    private fun again(context: GameContext, verbs: List<Verb>): AgainButton? {
        val (attack, verb) = when (context.detail) {
            Again.FIRE -> fire.takeIf { context.has(ContextFlags.CAN_FIRE) } to "Fire"
            Again.CAST -> cast.takeIf { context.has(ContextFlags.CAN_CAST) } to "Use"
            Again.THROW -> throwIt to "Throw"
            else -> null to ""
        }
        if (attack == null) return null
        val name = verbs.firstOrNull()?.label.orEmpty()
        val label = if (name.isEmpty()) "$verb again" else "$verb again\n$name"
        return AgainButton(Command(label, typed('n')), attack)
    }

    /**
     * The map: the large button is what this square offers (Get, Down, Shop,
     * ...) or Wait. With a monster in view it is the best ranged attack
     * instead, and what the square offers (or Wait) moves beside it. Once
     * the character has shot, cast or thrown at it, the large button does
     * that again, with the attack from the start beside it. On the surface
     * with nothing in view, Travel (the known world) takes the attack's place.
     */
    private fun mapCluster(context: GameContext, verbs: List<Verb>): Cluster {
        val attacks = attacks(context)
        val place = primaryCommand(context.primary)
        val placeHere = context.primary != Primary.NONE
        if (!context.has(ContextFlags.MONSTER_IN_VIEW)) {
            val second = if (context.has(ContextFlags.TRAVEL)) travel else attacks.firstOrNull()
            // Wait stays one tap away when the big button is busy with something else.
            return Cluster(place, listOf(rest, second, if (placeHere) hold else look))
        }
        again(context, verbs)?.let { again ->
            // Then what the square offers (stairs to escape by), another attack, or Wait.
            val third = if (placeHere) place else attacks.firstOrNull { it != again.attack } ?: hold
            return Cluster(again.command, listOf(look, again.attack, third))
        }
        // Resting stops for a monster in view, so it is not offered; without a ranged attack, Wait lets it come.
        val big = attacks.firstOrNull() ?: place
        val third = when {
            placeHere && big != place -> place
            big != hold -> hold
            else -> null
        }
        return Cluster(big, listOf(look, attacks.getOrNull(1), third))
    }

    // Look starts with a free cursor, where the game ignores Next and Prev;
    // they cycle only in its "interesting things" mode, which "m" enters at
    // the thing nearest the cursor (and does nothing once in it). So "m" first.
    private val next = Command("Next", Action.Keys(listOf(typed('m'), typed(' '))))
    private val prev = Command("Prev", Action.Keys(listOf(typed('m'), typed('-'))))
    private val target = cmd("Target", 't')
    private val recall = cmd("Recall", 'r')
    private val walkTo = cmd("Walk to", 'g')

    /**
     * Looking or targeting: the thing under the cursor (moved by the d-pad, a
     * tap, or Next and Prev) decides the large button: Target for a monster,
     * Walk to for an item or a place; choosing a target, always Target. No
     * row beside the d-pad: the look card fills that side of the screen.
     */
    private fun targetCluster(context: GameContext): Cluster {
        val onMonster = context.detail and TargetFlags.ON_MONSTER != 0
        val onSelf = context.detail and TargetFlags.ON_SELF != 0
        val canWalk = context.detail and TargetFlags.CAN_WALK != 0
        return when {
            context.detail and TargetFlags.CHOOSING != 0 -> Cluster(target, listOf(back, next, prev))
            onMonster -> Cluster(target, listOf(back, next, recall))
            // Look starts on the player: Next goes to the nearest thing of interest.
            onSelf || !canWalk -> Cluster(next, listOf(back, prev, null))
            else -> Cluster(walkTo, listOf(back, next, prev))
        }
    }

    /** A spell list: "?" shows or hides what each one does. */
    private val describe = Command("Details", questionMark)

    // Enter uses or learns the highlighted magic; the game ignores it on one
    // that cannot be (any can be highlighted, to read about it).
    private fun spellCluster(context: GameContext): Cluster = when (context.detail) {
        SpellKind.BROWSE -> Cluster(describe, listOf(back, null, null))
        else -> Cluster(
            Command(
                when (context.detail) {
                    SpellKind.CAST -> "Use"
                    SpellKind.STUDY -> "Learn"
                    else -> "OK"
                },
                Action.Key(Keys.RETURN),
            ),
            listOf(back, describe, null),
        )
    }

    /**
     * A shop's list. A tap highlights an item; Buy acts on it through the
     * game's own menu for that item (Enter opens it, as a tap on the
     * highlighted item does, and p buys or takes), and Examine (l, in either
     * keyset) shows it. Sell offers your own items. In the home: Take, Stash,
     * Examine.
     */
    private fun storeCluster(context: GameContext): Cluster {
        val home = context.detail and StoreFlags.HOME != 0
        val itemMenu = Action.Key(Keys.RETURN)
        return Cluster(
            Command(if (home) "Take" else "Buy", Action.Keys(listOf(itemMenu, typed('p')))),
            listOf(back, cmd(if (home) "Stash" else "Sell", 'd'), cmd("Examine", 'l')),
        )
    }

    private fun verbCommands(verbs: List<Verb>) = verbs.map { Command(it.label, Action.Verb(it.value), it.enabled) }

    /** Opens the birth options during character creation. */
    private val birthOptions = cmd("Options", '=')

    /** Switches the highlighted option on an options page. */
    private val toggle = Command("Toggle", Action.Key(Keys.RETURN))

    /** Actions that do not fit around the large button, for a row beside the d-pad. */
    fun strip(context: GameContext, verbs: List<Verb>): List<Command> = when {
        // Pin puts the large button's action (Quaff, Wield, Fire, ...) on the quick bar.
        context.mode == ControlMode.ITEMS -> verbCommands(verbs).drop(3) +
            listOfNotNull(verbs.firstOrNull()?.takeIf { it.pinnable }?.let { Command("Pin", Action.PinItem(it.value)) })
        // Not while learning: the magic is not known yet.
        context.mode == ControlMode.SPELLS && context.detail != SpellKind.STUDY -> listOf(Command("Pin", Action.PinSpell))
        context.mode == ControlMode.NONE && context.detail == OutsidePage.BIRTH_MENU -> listOf(birthOptions)
        // Beside the d-pad, clear of the attribute's description (a phone's corner reaches it).
        context.mode == ControlMode.NONE && context.detail == OutsidePage.BIRTH_POINTS -> listOf(cmd("Reset", 'r'))
        // The run's record (the character dump and spoiler files are of no use on a phone).
        context.mode == ControlMode.NONE && context.detail == OutsidePage.RUN_SUMMARY -> listOf(
            cmd("Character", 'I'), cmd("Messages", 'M'), cmd("Items", 'X'), cmd("History", 'H'), cmd("Scores", 'V'),
        )
        // Find asks for the text to look for (the keyboard opens for it).
        context.mode == ControlMode.PROMPT && context.detail == PromptScreen.LOG -> listOf(cmd("Find", '='))
        context.mode == ControlMode.PROMPT && context.detail == PromptScreen.LOG_FOUND ->
            listOf(cmd("Next match", '-'), cmd("Find", '='))
        // In the General Store: give a fishing catch to the village larder.
        context.mode == ControlMode.STORE && context.detail and StoreFlags.DONATIONS != 0 -> listOf(cmd("Donate", 'D'))
        else -> emptyList()
    }

    fun cluster(context: GameContext, verbs: List<Verb> = emptyList()): Cluster = when (context.mode) {
        ControlMode.MAP -> mapCluster(context, verbs)
        // The highlighted item's best action, then Inspect and the next (android/context.c ranks them).
        // Until they are known, OK picks the item, which opens the game's own menu for it.
        ControlMode.ITEMS -> verbCommands(verbs).let { commands ->
            Cluster(commands.firstOrNull() ?: ok, listOf(back, commands.getOrNull(1), commands.getOrNull(2)))
        }
        ControlMode.MORE -> Cluster(Command("Next ▸", Action.Key(Keys.RETURN)), listOf(Command("Skip", Action.Key(Keys.ESCAPE)), null, null))
        ControlMode.CAVE -> {
            val primary = when (context.primary) {
                Primary.GET -> cmd("Get", 'g')
                Primary.UP -> cmd("Up", '<')
                Primary.DOWN -> cmd("Down", '>')
                // Up on the way out leaves the cave.
                Primary.CAVE_LEAVE -> cmd("Leave", '<')
                Primary.CAVE_GRIP -> grip
                Primary.CAVE_WAIT -> wait
                // Standing beside water the cave can be fished from.
                Primary.FISH -> fish
                else -> jump
            }
            // Grip is the big button while climbing or falling; then a piton is the useful extra.
            // Fishing from the bank, Jump keeps its place beside it (Grip does nothing standing).
            val first = when (primary) {
                grip -> piton
                fish -> jump
                else -> grip
            }
            Cluster(primary, listOf(first, rope, peek))
        }
        ControlMode.FISHING -> when (context.detail) {
            // Leave keeps Back's place in every phase.
            FishingPhase.BITE -> Cluster(Command("Strike!", Action.Key(Keys.RETURN)), listOf(leave, wait, null))
            FishingPhase.WINDING -> Cluster(Command("Reel", Action.Repeat(Action.Key(Keys.UP))), listOf(leave, null, null))
            else -> Cluster(wait, listOf(leave, null, null))
        }
        // Back is always the button left of the large one.
        ControlMode.TEXT -> when (context.detail) {
            // "*" asks the game for a random name.
            TextKind.NAME -> Cluster(ok, listOf(back, cmd("Random", '*'), type))
            // At a quantity prompt the first digit replaces the default; "*" means all.
            TextKind.NUMBER -> Cluster(ok, listOf(back, cmd("All", '*'), delete))
            else -> Cluster(ok, listOf(back, type, delete))
        }
        // At [y/n] only y means yes; anything else, including Enter, means no.
        ControlMode.CHECK -> Cluster(cmd("Yes", 'y'), listOf(cmd("No", 'n'), null, null))
        // Taps on a monster or square target it; the d-pad fires in a direction.
        // At target only once there is one (Look's Target, or an earlier shot).
        ControlMode.AIM -> Cluster(
            Command("Nearest", typed('\'')),
            listOf(back, cmd("Choose", '*'), cmd("At target", 't').takeIf { context.detail and AimFlags.TARGET_SET != 0 }),
        )
        ControlMode.TARGET -> targetCluster(context)
        ControlMode.SPELLS -> spellCluster(context)
        ControlMode.NONE -> when (context.detail) {
            // "*" picks a random choice, "@" finishes randomly; Options sits beside the d-pad.
            OutsidePage.BIRTH_MENU -> Cluster(ok, listOf(back, cmd("Random", '*'), cmd("Auto", '@')))
            // The d-pad's left and right change the highlighted attribute.
            OutsidePage.BIRTH_POINTS -> Cluster(ok, listOf(back, null, null))
            OutsidePage.BIRTH_ROLLER -> Cluster(ok, listOf(back, cmd("Reroll", 'r'), null))
            OutsidePage.BIRTH_ROLLER_PREVIOUS -> Cluster(ok, listOf(back, cmd("Reroll", 'r'), cmd("Previous", 'p')))
            // Back does nothing on this page, so it is not offered.
            OutsidePage.BIRTH_QUICKSTART ->
                Cluster(cmd("Use as is", 'y'), listOf(cmd("Redo", 'n'), cmd("Change name", 'c'), birthOptions))
            // Enter (or a tap on an option) switches the highlighted option.
            OutsidePage.OPTIONS -> Cluster(toggle, listOf(back, null, null))
            // Delete asks first, on a page of its own.
            OutsidePage.HOME_LOAD -> Cluster(ok, listOf(back, Command("Delete", Action.Key(Keys.DELETE)), null))
            OutsidePage.HOME_DELETE -> Cluster(cmd("Yes", 'y'), listOf(cmd("No", 'n'), null, null))
            else -> Cluster(ok, listOf(back, null, null))
        }
        ControlMode.STORE -> storeCluster(context)
        ControlMode.PROMPT -> when (context.detail) {
            // Any key closes the level map. It has no pages, and is drawn out to the
            // screen's edges: paging buttons would only hide its corner.
            PromptScreen.LEVEL_MAP -> Cluster(ok, listOf(back, null, null))
            // The dossier has no use for Enter or paging keys; the d-pad's left and
            // right also turn its pages. Rename sits clear of its long left column.
            PromptScreen.DOSSIER -> Cluster(Command("Next page", Action.Key(Keys.RIGHT)), listOf(back, cmd("Rename", 'c'), null))
            // Reading the field guide: the next page is the common step (OK would only
            // move a line). Find asks for the text to look for.
            PromptScreen.HELP -> Cluster(
                Command("PgDn", Action.Key(Keys.PAGE_DOWN)),
                listOf(back, Command("PgUp", Action.Key(Keys.PAGE_UP)), cmd("Find", '/')),
            )
            // A card waits for OK or Back; paging keys do nothing on it.
            PromptScreen.STORY -> Cluster(ok, listOf(back, null, null))
            else -> Cluster(
                if (context.detail == PromptScreen.OPTIONS) toggle else ok,
                listOf(back, Command("PgUp", Action.Key(Keys.PAGE_UP)), Command("PgDn", Action.Key(Keys.PAGE_DOWN))),
            )
        }
    }

    /**
     * The keys a More command presses, as key, modifiers, text triplets for
     * the quick bar, or null if it cannot go there (Keyboard, Done).
     */
    fun pinKeys(command: Command): IntArray? = when (val action = command.action) {
        is Action.Key -> intArrayOf(action.key, action.mod, action.text)
        is Action.Keys -> action.keys.flatMap { listOf(it.key, it.mod, it.text) }.toIntArray()
        else -> null
    }

    /** Every command, for the More drawer (original keyset). */
    val drawer: List<Pair<String, List<Command>>> = listOf(
        "Move and act" to listOf(
            explore, cmd("Run", '.'), cmd("Stay", ','), rest, cmd("Open door", 'o'), cmd("Close door", 'c'),
            cmd("Tunnel", 'T'), cmd("Disarm", 'D'), cmd("Alter", '+'), cmd("Walk into", 'W'),
            cmd("Repeat", 'n'), Command("Fish", ctrl('c')),
            // Away from stairs these walk to the nearest known one. On the surface
            // away from stairs, the game's '<' opens known-world travel instead.
            cmd("Up", '<'), cmd("Down", '>'), travel,
        ),
        "Items" to listOf(
            cmd("Inventory", 'i'), cmd("Equipment", 'e'), cmd("Quiver", '|'), cmd("Pick up", 'g'),
            cmd("Drop", 'd'), cmd("Wield", 'w'), cmd("Take off", 't'), cmd("Examine", 'I'),
            cmd("Inscribe", '{'), cmd("Ignore", 'k'), cmd("Show ignored", 'K'),
            // In the General Store: give fish to the village larder.
            cmd("Donate", 'D'),
        ),
        "Use" to listOf(
            cmd("Quaff", 'q'), cmd("Read", 'r'), cmd("Eat", 'E'), cmd("Use", 'U'), cmd("Aim wand", 'a'),
            cmd("Use staff", 'u'), cmd("Zap rod", 'z'), cmd("Activate", 'A'), cmd("Fuel", 'F'), cmd("Throw", 'v'),
        ),
        "Fight and magic" to listOf(
            cmd("Use magic", 'm'), cmd("Browse", 'b'), cmd("Learn magic", 'G'), cmd("Fire", 'f'), cmd("Fire nearest", 'h'),
            cmd("Target", '*'), cmd("Target closest", '\''), look,
        ),
        "Knowledge" to listOf(
            cmd("Character", 'C'), cmd("Abilities", 'S'), cmd("Knowledge", '~'), Command("Messages", ctrl('p')),
            cmd("Map", 'M'), cmd("Monsters", '['), cmd("Items seen", ']'), cmd("Locate", 'L'),
            cmd("Symbol", '/'), Command("Feeling", ctrl('f')),
        ),
        "Caves" to listOf(
            cmd("Jump", 'j'), grip, piton, rope, cmd("Get", 'g'), cmd("Drop", 'd'),
            cmd("Passage up", '<'), cmd("Passage down", '>'), wait,
        ),
        "Game" to listOf(
            Command("Zoom out", Action.Key(Keys.MINUS, Keys.MOD_CTRL)), Command("Zoom in", Action.Key(Keys.EQUALS, Keys.MOD_CTRL)),
            Command("Centre", ctrl('l')), cmd("Options", '='), cmd("Help", '?'), Command("Save", ctrl('s')),
            Command("Keyboard", Action.Keyboard),
            // The game's own menu of every command, by category.
            Command("Commands", Action.Key(Keys.RETURN)),
        ),
    )
}

// Copyright (c) 2026 John Horton
// SPDX-License-Identifier: GPL-2.0-only

import java.security.MessageDigest
import java.util.Properties
import java.util.zip.ZipFile

plugins {
    id("com.android.application")
}

val localProperties = Properties().apply {
    val file = rootProject.file("local.properties")
    if (file.isFile) file.inputStream().use { load(it) }
}

// Release signing: keystore.properties (not tracked) names a key kept outside
// the repository. Without it, release builds are left unsigned.
val keystoreProperties = Properties().apply {
    val file = rootProject.file("keystore.properties")
    if (file.isFile) file.inputStream().use { load(it) }
}

// The official artwork, audio and launcher icon are separately licensed (see
// ASSET-LICENSE.md) and are not in this repository. Official builds name a
// folder holding them as ammerow.assetPack in local.properties or with
// -Pammerow.assetPack=...; without one, the game uses its plain glyphs and
// synthesised sounds, and the launcher shows a generic icon.
val assetPack: File? = ((project.findProperty("ammerow.assetPack") as String?)
    ?: localProperties.getProperty("ammerow.assetPack"))
    ?.let { rootProject.file(it) }
    ?.also { require(it.isDirectory) { "ammerow.assetPack is not a folder: $it" } }

// Official releases carry their own source: the archive written by
// tools/check_source.py --export from the release commit, named with
// -Pammerow.sourceArchive=... and packaged as assets/source/Ammerow-Android-source.zip.
val releaseSource: File? = (project.findProperty("ammerow.sourceArchive") as String?)
    ?.let { rootProject.file(it) }
    ?.also { require(it.isFile) { "ammerow.sourceArchive is not a file: $it" } }

android {
    namespace = "com.ammerow.game"
    compileSdk = 36
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "com.ammerow.game"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0-android.2"

        ndk {
            // -Pammerow.abis=x86_64 builds only what the emulator needs.
            val abis = (project.findProperty("ammerow.abis") as String?)?.split(",") ?: listOf("arm64-v8a", "x86_64")
            abiFilters += abis
        }

        val officialIcon = assetPack?.resolve("res")?.isDirectory == true
        manifestPlaceholders["appIcon"] = if (officialIcon) "@mipmap/ammerow_icon" else "@mipmap/ic_launcher"
        manifestPlaceholders["appRoundIcon"] = if (officialIcon) "@mipmap/ammerow_icon_round" else "@mipmap/ic_launcher_round"
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    sourceSets {
        getByName("main") {
            // SDL's Java glue must match the SDL version being built, so it is used in place.
            java.directories.add("../third_party/SDL/android-project/app/src/main/java")
            assetPack?.resolve("res")?.takeIf { it.isDirectory }?.let { res.directories.add(it.path) }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    signingConfigs {
        if (keystoreProperties.containsKey("storeFile")) {
            create("release") {
                storeFile = file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release")
            // The APK then depends only on the source, not on the checkout's Git
            // history, so a build from the source archive matches the release.
            vcsInfo.include = false
        }
    }

    lint {
        abortOnError = false
    }
}

/**
 * Lays out the game's runtime files as APK assets: everything the release
 * manifests (game/packaging/runtime*.txt) list, under lib/, with an index that
 * android-main.c reads to install them on first run. Separately licensed files
 * (.art plates, .png sprite sheets, sounds) come from the asset pack when there
 * is one, sounds as .ogg; otherwise they are left out. The licence texts go in
 * legal/ and the source archive, when there is one, in source/: packaged with
 * the game but not installed.
 */
abstract class StageGameAssets : DefaultTask() {
    @get:InputDirectory
    abstract val gameDir: DirectoryProperty

    @get:Optional
    @get:InputDirectory
    abstract val assetPackDir: DirectoryProperty

    @get:InputFiles
    abstract val legalFiles: ConfigurableFileCollection

    @get:Optional
    @get:InputFile
    abstract val sourceArchive: RegularFileProperty

    @get:InputFiles
    abstract val sourceRevision: ConfigurableFileCollection

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun stage() {
        val game = gameDir.get().asFile
        val pack = assetPackDir.orNull?.asFile
        val out = outputDir.get().asFile
        out.deleteRecursively()
        out.mkdirs()

        val digest = MessageDigest.getInstance("SHA-256")
        val index = mutableListOf<String>()
        var left = 0
        for (manifest in listOf("runtime.txt", "runtime-art.txt", "runtime-audio.txt")) {
            for (raw in game.resolve("packaging/$manifest").readLines()) {
                val line = raw.trim()
                if (line.isEmpty() || line.startsWith("#")) continue
                val (component, source, installedPath) = line.split("|")
                require(component == "data" || component == "config") { "$manifest: unknown component $component" }
                var installed = installedPath
                val separate = Regex("""\.(art|png|wav)$""").containsMatchIn(source)
                val from = if (separate) {
                    if (source.endsWith(".wav")) installed = installed.removeSuffix(".wav") + ".ogg"
                    pack?.resolve("lib/$installed")
                } else {
                    game.resolve(source).also { require(it.isFile) { "Missing game file: $source" } }
                }
                if (from == null || !from.isFile) {
                    left++
                    continue
                }
                val target = "lib/$installed"
                val bytes = from.readBytes()
                out.resolve(target).apply { parentFile.mkdirs() }.writeBytes(bytes)
                digest.update(target.toByteArray())
                digest.update(MessageDigest.getInstance("SHA-256").digest(bytes))
                index += "${bytes.size} $target"
            }
        }
        val stamp = digest.digest().joinToString("") { "%02x".format(it) }.take(16)
        out.resolve("ammerow-assets.txt").writeText(
            "ammerow-assets 1\nstamp $stamp\n" + index.joinToString("\n") + "\n")

        val legal = out.resolve("legal").apply { mkdirs() }
        legalFiles.files.forEach { it.copyTo(legal.resolve(it.name), overwrite = true) }

        sourceArchive.orNull?.asFile?.let { archive ->
            // A build from an extracted archive must package that same archive.
            val inside = ZipFile(archive).use { zip ->
                zip.entries().asSequence().firstOrNull { it.name.endsWith("/SOURCE-REVISION.txt") }
                    ?.let { zip.getInputStream(it).readBytes().decodeToString() }
            } ?: throw GradleException("$archive has no SOURCE-REVISION.txt")
            sourceRevision.files.firstOrNull { it.isFile }?.let { here ->
                if (here.readText() != inside) throw GradleException("$archive is not the source of this tree")
            }
            val target = out.resolve("source/Ammerow-Android-source.zip").apply { parentFile.mkdirs() }
            archive.copyTo(target, overwrite = true)
            val hash = MessageDigest.getInstance("SHA-256").digest(target.readBytes())
                .joinToString("") { "%02x".format(it) }
            target.resolveSibling("Ammerow-Android-source.zip.sha256")
                .writeText("$hash  Ammerow-Android-source.zip\n")
        }

        logger.lifecycle("Staged ${index.size} game files, stamp $stamp" +
            if (left > 0) "; $left separately licensed files left out (no asset pack)" else "")
    }
}

val stageGameAssets = tasks.register<StageGameAssets>("stageGameAssets") {
    gameDir.set(rootProject.layout.projectDirectory.dir("game"))
    assetPack?.let { assetPackDir.set(it) }
    legalFiles.from(
        rootProject.files("COPYING", "NOTICE.md", "ASSET-LICENSE.md", "THIRD_PARTY_NOTICES.md", "AUTHORS.md",
            "docs/copying.rst", "docs/thanks.rst"),
        rootProject.fileTree("licenses"))
    releaseSource?.let { sourceArchive.set(it) }
    sourceRevision.from(rootProject.file("SOURCE-REVISION.txt"))
    outputDir.set(layout.buildDirectory.dir("generated/gameAssets"))
}

// An official release (one with the separately licensed assets) must carry its source.
tasks.matching { it.name == "preReleaseBuild" }.configureEach {
    doFirst {
        if (assetPack != null && releaseSource == null) {
            throw GradleException("An official release needs -Pammerow.sourceArchive=... (see README.md)")
        }
    }
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(stageGameAssets, StageGameAssets::outputDir)
    }
}

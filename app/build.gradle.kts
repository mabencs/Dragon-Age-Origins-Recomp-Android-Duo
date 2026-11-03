import java.util.Properties

plugins {
    id("com.android.application")
}

// PLAN 11.3: the version, one place (gradle.properties' warden.version,
// major.minor.patch with an optional "-label", 0.1.0-alpha); the code follows from the
// numbers, so a later release always installs over an earlier one. warden.releaseName
// names the release for the About page ("Open Alpha").
val wardenVersion = (findProperty("warden.version") as String?) ?: "0.0.0"
val wardenVersionCode = wardenVersion.substringBefore("-").split(".").map { it.toInt() }
    .let { (major, minor, patch) -> major * 1_000_000 + minor * 1_000 + patch }
val wardenReleaseName = (findProperty("warden.releaseName") as String?) ?: ""

// PLAN 11.3: the release key, never in the repository. Its properties file (storeFile,
// storePassword, keyAlias, keyPassword) is ~/.android/dao-release.properties unless
// -Pwarden.releaseSigning=<file> names another; without one a release build is left
// unsigned and says so. The committed debug key never signs a release (CLAUDE.md).
val releaseSigning = file((findProperty("warden.releaseSigning") as String?)
    ?: "${System.getProperty("user.home")}/.android/dao-release.properties")

android {
    namespace = "dev.daorecompandroid.duo"
    compileSdk = 36
    buildToolsVersion = "36.1.0"

    // Pinned to the NDK the rest of the project already builds with
    // (tools/compile_runtime.py, docs/TOOLCHAIN.md). A mismatch here would mean the
    // app's native library and the differentially-tested runtime were built by
    // different compilers, which is exactly the kind of difference that produces a
    // bug nobody can reproduce.
    ndkVersion = "28.2.13676358"

    defaultConfig {
        // The owner (2026-11-02): the ids name the project, not a person - the debug build
        // (the owner's own, run-as and the test tools) dev.daorecompandroid.duo, the
        // release prod.daorecompandroid.duo (set below, androidComponents). Until then both
        // had one id with a person's name in it.
        applicationId = "dev.daorecompandroid.duo"
        minSdk = 33

        // The AYN Thor runs Android 13 / API 33 (device/thor-profile.md). Targeting
        // higher would opt into behaviour changes - edge-to-edge enforcement, stricter
        // foreground rules - that this device cannot even exercise, in exchange for
        // nothing. Raise this only if the handheld's OS is updated.
        targetSdk = 33

        versionCode = wardenVersionCode
        versionName = wardenVersion

        // PLAN 11.3: where the app's source is published (GPL-3.0 - About this app says
        // it), from gradle.properties' warden.sourceUrl.
        buildConfigField("String", "RELEASE_NAME", "\"$wardenReleaseName\"")
        buildConfigField("String", "SOURCE_URL",
            "\"${(findProperty("warden.sourceUrl") as String?) ?: ""}\"")

        // The launcher's name, and the deck's task affinity (the package's own, so a
        // second install - 11.4's clean copy - never shares a task with this one).
        manifestPlaceholders["appLabel"] = "@string/app_name"

        // PLAN 11.1: the commit a build came from, for Debugging Mode's reports ("+changes"
        // when the working tree had uncommitted ones).
        val commit = providers.exec {
            commandLine("git", "rev-parse", "--short=12", "HEAD")
            isIgnoreExitValue = true
        }.standardOutput.asText.get().trim().ifEmpty { "unknown" }
        val changed = providers.exec {
            commandLine("git", "status", "--porcelain", "--untracked-files=no")
            isIgnoreExitValue = true
        }.standardOutput.asText.get().isNotBlank()
        buildConfigField("String", "GIT_COMMIT", "\"$commit${if (changed) "+changes" else ""}\"")

        ndk {
            // The Thor is arm64 and this is a personal build for it. Every other ABI
            // would double the build time of a very large native library to produce
            // something that will never be installed.
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    // Xenia uses both. Neither can be disabled - see
                    // tools/compile_runtime.py.
                    "-DANDROID_CPP_FEATURES=rtti exceptions",
                )
                cppFlags += "-std=c++20"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            // SDK CMake, not the 4.x on PATH: CMake 4 dropped compatibility with
            // cmake_minimum_required(VERSION < 3.5), which third-party CMake files
            // still declare. 3.31.6 is the pairing installed with the NDK.
            version = "3.31.6"
        }
    }

    buildFeatures {
        // On for PLAN 11.1's commit (BuildConfig.GIT_COMMIT, Debugging Mode's reports).
        buildConfig = true
        resValues = false
        // On in 4.7, when Swappy arrives as a prefab AAR.
        prefab = false
    }

    signingConfigs {
        if (releaseSigning.isFile) {
            create("release") {
                val keys = Properties().apply { releaseSigning.inputStream().use { load(it) } }
                storeFile = releaseSigning.resolveSibling(keys.getProperty("storeFile"))
                storePassword = keys.getProperty("storePassword")
                keyAlias = keys.getProperty("keyAlias")
                keyPassword = keys.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        debug {
            isJniDebuggable = true
            isMinifyEnabled = false
            // The owner's own build: debug-signed, run-as works (tools/route.sh and the
            // test files), and it installs over the Thor's copy.
            versionNameSuffix = "-debug"
            // Its own name on the home screen: the owner plays on the published app and
            // fixes bugs on this one, side by side.
            manifestPlaceholders["appLabel"] = "DAO Debug"
        }
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            // PLAN 11.3: the release key (above). A release build is a different signer
            // from the Thor's debug-signed install, so it never installs over that one -
            // 11.4's clean copy is a second app (-Pwarden.clean, below).
            signingConfig = signingConfigs.findByName("release")
            if (signingConfig == null) {
                logger.warn("warden: no release key at $releaseSigning - the release APK is unsigned")
            }
            // PLAN 11.4: -Pwarden.clean makes the release build a second app beside the
            // owner's (its own package, data and saves), for the first run on a clean
            // install without touching the real one.
            if (project.hasProperty("warden.clean")) {
                versionNameSuffix = "-clean"
                manifestPlaceholders["appLabel"] = "DAO clean test"
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs {
            // Symbols kept in every build type, the release too (PLAN 11.3): a native
            // crash's tombstone then names its functions. tools/release.py also keeps
            // each release's libwarden.so with its build id.
            keepDebugSymbols += "**/arm64-v8a/*.so"
            // PLAN 12.2: the game's code is never in the APK. The `daogame` target builds
            // it here for development (tools/push_gamecode.py puts it on the device); the
            // published app makes it on the device from the player's own disc.
            excludes += "**/libdaogame.so"
            // PLAN 12.5: the libraries are extracted at install, so the compiler and
            // XenonRecomp (libllvm.so, libxenonrecomp.so - programs, named as libraries)
            // can run from the app's library folder, the one place an app may run a
            // program it carries.
            useLegacyPackaging = true
        }
    }

    // PLAN 12.5: what the app makes the game's code with, staged by
    // tools/stage_gamecode_toolchain.py (after tools/build_device_toolchain.py). A build
    // without it still works and can be given the game's code from the computer
    // (tools/push_gamecode.py); a release must have it (tools/release.py checks).
    sourceSets {
        getByName("main") {
            jniLibs.srcDir(rootProject.file("build/gamecode-stage/jniLibs"))
            assets.srcDir(rootProject.file("build/gamecode-stage/assets"))
        }
    }
}

// The release's own id (a build type can only add a suffix to defaultConfig's): the
// release prod.daorecompandroid.duo, and 11.4's clean copy prod.daorecompandroid.duo.clean.
androidComponents {
    onVariants(selector().withBuildType("release")) { variant ->
        // PLAN 12.6: the published APK leaves out the Khronos validation layer, a
        // developer's Vulkan debugging tool (bootstrap --validation-layer puts it in jniLibs).
        variant.packaging.jniLibs.excludes.add("**/libVkLayer_khronos_validation.so")
        variant.applicationId.set(
            "prod.daorecompandroid.duo" + if (project.hasProperty("warden.clean")) ".clean" else "")
    }
}

dependencies {
    // Deliberately minimal. Added as the phases that need them land:
    //   4.7  androidx.games:games-frame-pacing   (Swappy, via prefab)
    //   8.x  whatever the second screen needs
    // Nothing here is required to bring up a Vulkan device on a SurfaceView.
}

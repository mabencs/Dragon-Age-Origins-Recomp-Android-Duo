// Phase 4.1. One module: the app. The native runtime is built by CMake inside it
// rather than as a separate Gradle module, because its source list is generated from
// tools/compile_runtime.py and has nothing Gradle needs to understand.

pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "DragonAgeOriginsRecompAndroidDuo"
include(":app")

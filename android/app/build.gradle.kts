// The Android head of the C++ port (src/Mdk.Android in C#): the Java activities and the C++
// game, libmain.so, built by the root CMakeLists with SDL inside (third_party/SDL); SDL's Java
// activity comes from the same commit (third_party/SDL/android-project). -PsharedSdl=true uses
// the prebuilt libSDL3.so (third_party/sdl-bin) instead.
//
//   SetupActivity (Java) ─JNI─► Mdk.Game, Mdk.Formats (libmain.so)
//        └─files─► GameActivity (SDLActivity) ─SDL_main─► Game ─► SDL (libmain.so)
plugins {
    id("com.android.application")
}

// The C++ port's root: CMake, the prebuilt SDL, SDL's Java, the icon.
val repo: File = rootDir.resolve("..").canonicalFile
val generated: File = layout.buildDirectory.dir("generated/mdk").get().asFile
// The prebuilt SDL by ABI: its folders are named by .NET runtime identifier.
val sdlFolders = mapOf("arm64-v8a" to "android-arm64", "x86_64" to "android-x64")

// A store build (-PstoreBuild=true, C#'s -p:StoreBuild=true): no All files access in the
// manifest (stores reject it for a game); the game files are always copied into the app
// (DataSetup). It selects the "store" flavour instead of "standard", so a build makes one APK
// (each flavour builds the whole game for both ABIs).
val storeBuild = (findProperty("storeBuild") as String?)?.toBoolean() == true
val sharedSdl = (findProperty("sharedSdl") as String?)?.toBoolean() == true

val copySdl by tasks.registering(Sync::class) {
    into(generated.resolve("jniLibs"))
    sdlFolders.filter { sharedSdl }.forEach { (abi, folder) ->
        from(repo.resolve("third_party/sdl-bin/$folder")) {
            include("libSDL3.so")
            into(abi)
        }
    }
}

// The launcher icon (tools/gen_icon.py), shared with the desktop.
val copyIcon by tasks.registering(Sync::class) {
    into(generated.resolve("res"))
    from(repo.resolve("assets")) {
        include("icon.png")
        into("drawable")
    }
}

android {
    namespace = "io.github.nemo22.mdk"
    compileSdk = 36
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "io.github.nemo22.mdk"
        minSdk = 26
        targetSdk = 36
        versionCode = 21
        versionName = "2.0.0"
        ndk { abiFilters += sdlFolders.keys }
        externalNativeBuild {
            cmake { arguments += listOf("-DMDK_BUILD_TESTS=OFF", "-DMDK_STATIC_SDL=${!sharedSdl}") }
        }
    }

    flavorDimensions += "distribution"
    productFlavors {
        create(if (storeBuild) "store" else "standard") {
            dimension = "distribution"
            buildConfigField("boolean", "STORE_BUILD", storeBuild.toString())
            buildConfigField("boolean", "SHARED_SDL", sharedSdl.toString())
        }
    }

    buildFeatures { buildConfig = true }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets["main"].apply {
        java.srcDir(repo.resolve("third_party/SDL/android-project/app/src/main/java"))
        res.srcDir(generated.resolve("res"))
        jniLibs.srcDir(generated.resolve("jniLibs"))
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint { checkReleaseBuilds = false }
}

tasks.named("preBuild") {
    dependsOn(copySdl, copyIcon)
}

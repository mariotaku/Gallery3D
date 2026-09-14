import javax.inject.Inject

plugins {
    id("com.android.application")
}

// The drawables as Android resources. assets/drawable-*dpi goes to
// res/drawable-*dpi under the same names, so Android picks the bucket for the
// display and scales the art to its density. assets/drawable goes to
// res/drawable-nodpi with "nodpi_" in front: that folder's art is drawn at its
// own pixel size, and several of its names are different art in a bucket.
// DrawableBridge reads both.
abstract class DrawableResources : DefaultTask() {
    @get:InputDirectory
    abstract val art: DirectoryProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @get:Inject
    abstract val files: FileSystemOperations

    @TaskAction
    fun copy() {
        files.sync {
            into(outputDir)
            from(art) {
                include("drawable-*dpi/**")
            }
            from(art.dir("drawable")) {
                into("drawable-nodpi")
                rename { "nodpi_$it" }
            }
        }
    }
}

// Everything in assets apart from the drawables, which are resources.
abstract class PlainAssets : DefaultTask() {
    @get:InputDirectory
    abstract val art: DirectoryProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @get:Inject
    abstract val files: FileSystemOperations

    @TaskAction
    fun copy() {
        files.sync {
            into(outputDir)
            from(art) {
                exclude("drawable/**", "drawable-*/**")
            }
        }
    }
}

val drawableResources = tasks.register<DrawableResources>("drawableResources") {
    art.set(rootProject.file("../assets"))
    outputDir.set(layout.buildDirectory.dir("generated/drawables/res"))
}

val plainAssets = tasks.register<PlainAssets>("plainAssets") {
    art.set(rootProject.file("../assets"))
    outputDir.set(layout.buildDirectory.dir("generated/drawables/assets"))
}

android {
    namespace = "me.mariotaku.gallery3d"
    compileSdk = 35
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "me.mariotaku.gallery3d"
        // MediaStore covers every version from here, so the wall needs no
        // filesystem scan and no legacy storage path.
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }
        ndk {
            // arm64 is what phones run. x86_64 is what the emulators run, and
            // without it an emulator installs the apk and then finds no library
            // to load, which looks like the app starting to a black screen
            // rather than like a missing build.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    buildFeatures {
        // How CMake finds SDL inside the aar archives in libs.
        prefab = true
    }

    externalNativeBuild {
        cmake {
            // The same file the desktop builds, which reads ANDROID itself.
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            // Signed with the debug key so a release build can be installed and
            // measured without a keystore. The debug variant builds the native
            // code at -O0, which makes any timing taken from it meaningless.
            // Replace this before the apk goes anywhere.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint {
        abortOnError = false
    }
}

androidComponents {
    onVariants { variant ->
        variant.sources.res?.addGeneratedSourceDirectory(drawableResources, DrawableResources::outputDir)
        // The fonts, packed straight into the apk. Its assets folder is what
        // the asset manager reads from, which is where App::ASSET_ROOT points
        // on Android.
        variant.sources.assets?.addGeneratedSourceDirectory(plainAssets, PlainAssets::outputDir)
    }
}

dependencies {
    // Not on Maven Central. android/fetch-deps.sh puts them here.
    implementation(fileTree("libs") { include("*.aar") })
}

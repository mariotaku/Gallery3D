plugins {
    id("com.android.application")
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

    sourceSets {
        getByName("main") {
            // The wall's art and fonts, packed straight into the apk. Its
            // assets folder is what the asset manager reads from, which is
            // where App::ASSET_ROOT points on Android.
            assets.srcDir(rootProject.file("../assets"))
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

dependencies {
    // Not on Maven Central. android/fetch-deps.sh puts them here.
    implementation(fileTree("libs") { include("*.aar") })
}

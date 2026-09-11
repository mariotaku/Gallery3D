plugins {
    id("com.android.application")
}

// The apk flattens an asset source directory into its own root, which would put
// the art one level above where App::ASSET_ROOT looks for it. Staging it under
// an assets/ folder first keeps one asset path across every platform.
val assetStage = layout.buildDirectory.dir("generated/gallery3d-assets")

val stageAssets = tasks.register<Copy>("stageAssets") {
    from(rootProject.file("../assets"))
    into(assetStage.map { it.dir("assets") })
}

tasks.withType<com.android.build.gradle.tasks.MergeSourceSetFolders>().configureEach {
    dependsOn(stageAssets)
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
            // The phone this is tested on, and what current devices run.
            abiFilters += listOf("arm64-v8a")
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
            assets.srcDir(assetStage)
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
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

// Calls the app's Java helpers. Android only; nothing else compiles this.
//
// The Java side does the cursor walking and returns JSON, so what crosses here
// is a handful of static calls rather than a parallel implementation in JNI.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace AndroidBridge {

// Finds the Java helper and holds on to it. Call this from the thread SDL calls
// main() on, before any loader thread starts: that thread is the only one that
// can look an app class up by name, because the ones made later attach to the
// vm without the app's class loader.
void init();

// Whether the library can be read yet. False until the user answers the
// permission request.
bool hasMediaPermission();

// Asks for the photo permission and waits for the answer. Call it from a loader
// thread: the dialog needs the main thread to run.
bool requestMediaPermission();

// JSON: one object per folder of photos, with id, name, count and dateTaken.
std::string queryBuckets();

// JSON: one object per photo in the folder, newest first.
std::string queryBucket(const std::string &bucketId);

// The encoded bytes of one photo, read through the content resolver rather
// than a file path.
bool readImage(int64_t id, std::vector<uint8_t> *bytes);

}  // namespace AndroidBridge

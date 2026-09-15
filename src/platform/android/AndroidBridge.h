// Calls the app's Java helpers. Android only; nothing else compiles this.
//
// The Java side does the cursor walking and returns JSON, so what crosses here
// is a handful of static calls rather than a parallel implementation in JNI.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL_stdinc.h>

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

// The space behind the status bar, the navigation bar and any cutout, in
// pixels. The window draws under all three, so the wall fills the screen while
// its controls stay clear of them. False when the insets cannot be read.
bool systemBarInsets(int *left, int *top, int *right, int *bottom);

// JSON: one object per folder of photos, with id, name, count and dateTaken.
std::string queryBuckets();

// JSON: one object per photo in the folder, newest first.
std::string queryBucket(const std::string &bucketId);

// The encoded bytes of one photo, read through the content resolver rather
// than a file path.
bool readImage(int64_t id, std::vector<uint8_t> *bytes);

// JSON from StorageBridge.sources: what the home pill offers, each with kind
// (library, volume, tree or picker), id and name.
std::string storageSources();

// Opens the system folder picker, at the root of this storage volume when the
// id is not empty. The answer arrives later as a sourceChosenEvent. False when
// the picker could not be opened.
bool pickFolder(const std::string &volume);

// The SDL event type a chosen source arrives as, from the source menu or the
// folder picker. data1 is a std::string* with "library" or a tree uri, which
// the receiver deletes, or null when the user backed out of the picker.
Uint32 sourceChosenEvent();

// The source the wall showed last: "library", or a tree uri.
std::string chosenSource();
void setChosenSource(const std::string &id);

// The display name of a tree's folder.
std::string treeName(const std::string &tree);

// StorageBridge.listFolder, readExif and readDocument, for
// AndroidDocumentTreeClient.
std::string listFolder(const std::string &folder);
std::string readExif(const std::string &uri, const std::string &mime);
bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes);

}  // namespace AndroidBridge

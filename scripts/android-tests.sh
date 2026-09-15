#!/bin/sh
# Runs the tests on an Android device or emulator. Installs the app's
# conformance build type, grants it the photo permission, starts it with an
# optional test name filter, waits for the report and prints it. Exits non-zero
# when a test fails or the run does not finish.
#
#   ANDROID_SERIAL=emulator-5554 scripts/android-tests.sh [filter]
#
# ANDROID_SERIAL is required, so a phone that happens to be plugged in is never
# the one written to.
set -eu

if [ -z "${ANDROID_SERIAL:-}" ]; then
    echo "Set ANDROID_SERIAL to the device to run the tests on." >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd)
package=me.mariotaku.gallery3d.conformance
filter=${1:-}

gradle="$root/android/gradlew"
if [ -f "$gradle.bat" ] && [ "${OS:-}" = "Windows_NT" ]; then
    gradle="$gradle.bat"
fi
"$gradle" -p "$root/android" installConformance

adb shell pm grant "$package" android.permission.READ_MEDIA_IMAGES 2>/dev/null || true
adb shell am force-stop "$package"
adb shell run-as "$package" rm -f files/test-results.txt files/test-done
adb shell am start -n "$package/me.mariotaku.gallery3d.MainActivity" --es filter "'$filter'" >/dev/null

# The host writes files/test-done, holding the number of failed checks, once
# the last test has run. Ten minutes covers a slow emulator.
waited=0
until adb shell run-as "$package" test -f files/test-done; do
    waited=$((waited + 1))
    if [ "$waited" -gt 600 ]; then
        echo "The tests did not finish. The report so far:" >&2
        adb shell run-as "$package" cat files/test-results.txt >&2 || true
        exit 3
    fi
    sleep 1
done

adb shell run-as "$package" cat files/test-results.txt
failures=$(adb shell run-as "$package" cat files/test-done | tr -d '\r\n ')
[ "$failures" = "0" ]

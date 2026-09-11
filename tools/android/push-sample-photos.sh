#!/bin/sh
# Pushes a folder of photos to a device and gets MediaStore to index them.
#
# Usage: tools/android/push-sample-photos.sh <folder>
# The folder's subdirectories become albums, one per bucket.
#
# The scan has two requirements that are easy to get wrong. The path in the
# file:// url has to be the real one, /storage/emulated/0, and not the /sdcard
# symlink. And the broadcast needs --receiver-include-background, because
# Android 8 stopped delivering implicit broadcasts to manifest receivers that
# are not already running, which silently drops the scan.
set -e

source="$1"
if [ -z "$source" ] || [ ! -d "$source" ]; then
    echo "usage: $0 <folder of albums>" >&2
    exit 2
fi

adb="${ADB:-adb}"
remote=/storage/emulated/0/Pictures

for album in "$source"/*/; do
    [ -d "$album" ] || continue
    name="$(basename "$album")"
    echo "pushing $name"
    "$adb" shell "mkdir -p '$remote/$name'"
    "$adb" push "$album." "$remote/$name/" > /dev/null
done

echo "scanning"
"$adb" shell "for f in $remote/*/*.jpg $remote/*/*.png; do \
    [ -f \"\$f\" ] || continue; \
    am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE \
        --receiver-include-background -d file://\$f > /dev/null 2>&1; \
done"

"$adb" shell "content query --uri content://media/external/images/media \
    --projection bucket_display_name" | sed 's/.*bucket_display_name=//' | sort | uniq -c

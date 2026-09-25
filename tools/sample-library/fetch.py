#!/usr/bin/env python3
"""Builds a photo library from Pixabay for sample screenshots.

    python tools/sample-library/fetch.py pick     choose photos, write albums.json
    python tools/sample-library/fetch.py fetch    download what albums.json names

pick searches once per album and records the chosen ids, so fetch gets the
same photos every time. fetch writes one folder per album, by default
~/Pictures/gallery3d-samples, and a CREDITS.md with each photo's author and
page. The photos stay out of the repository: the Pixabay Content License does
not allow handing them out as a set of their own.

Most photos are the 640px webformat size, which is all a thumbnail needs. The
first FULL_SIZE_PER_ALBUM of each album, and the ids in an album's "full" list,
are the largest size the key allows: 1920px with full API access, otherwise
1280px.

The key comes from PIXABAY_API_KEY, or from a .env file at the repository root.
"""
import argparse
import datetime
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ALBUMS_FILE = HERE / "albums.json"
API = "https://pixabay.com/api/"
# Pixabay answers 403 to urllib's default User-Agent.
USER_AGENT = "gallery3d-sample-library/1.0"

FULL_SIZE_PER_ALBUM = 2
# The API allows 100 requests a minute.
REQUEST_INTERVAL = 0.7

# Folder name, search terms, the orientation to ask for, and how many photos.
# The counts differ so the stacks on the wall differ, and one album is large
# enough to fill several screens of the grid.
ALBUMS = [
    ("Animals", "wildlife animal", "all", 36),
    ("Travel", "travel landmark", "horizontal", 14),
    ("Food", "food dish", "all", 12),
    ("City", "city skyline night", "horizontal", 12),
    ("Mountains", "mountain landscape", "horizontal", 12),
    ("Flowers", "flower macro", "all", 14),
    ("Beach", "beach sea", "horizontal", 12),
    ("Forest", "forest trees", "all", 10),
    ("Autumn", "autumn leaves", "all", 12),
    ("Winter", "winter snow", "horizontal", 11),
    ("Cats", "cat", "all", 13),
    ("Dogs", "dog", "all", 12),
    ("Birds", "bird", "all", 15),
    ("Coffee", "coffee cup", "all", 10),
    ("Architecture", "architecture building", "all", 12),
    ("Sunsets", "sunset", "horizontal", 11),
    ("Cars", "vintage car", "horizontal", 10),
    ("Desserts", "dessert cake", "all", 12),
    ("Lakes", "lake reflection", "horizontal", 10),
    ("Japan", "japan temple", "all", 13),
    ("Fruit", "fruit", "all", 10),
    ("Night Sky", "starry sky milky way", "horizontal", 10),
    ("Bridges", "bridge", "horizontal", 10),
    ("Waterfalls", "waterfall", "all", 10),
]


def api_key():
    key = os.environ.get("PIXABAY_API_KEY")
    if key:
        return key
    env = ROOT / ".env"
    if env.exists():
        for line in env.read_text(encoding="utf-8").splitlines():
            # Accepts KEY=value, "KEY=value" and KEY="value".
            line = line.strip().strip("'\"")
            name, sep, value = line.partition("=")
            if sep and name.strip().strip("'\"") == "PIXABAY_API_KEY":
                return value.strip().strip("'\"")
    sys.exit("Set PIXABAY_API_KEY, or put it in .env at the repository root.")


def get(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def search(key, **params):
    time.sleep(REQUEST_INTERVAL)
    query = urllib.parse.urlencode({"key": key, "image_type": "photo", "safesearch": "true", **params})
    try:
        return json.loads(get(API + "?" + query))
    except urllib.error.HTTPError as e:
        # The error body can echo the request, key included.
        sys.exit(f"Pixabay answered HTTP {e.code}")


def pick(key):
    albums = []
    # A photo goes in the first album whose search finds it, so a cat from
    # the Animals search is not in Cats as well.
    taken = set()
    for name, terms, orientation, count in ALBUMS:
        ids = []
        # Editors' choice first, for the look of the screenshots. Any photo
        # fills the rest when a search has too few of those.
        for editors_choice in ("true", "false"):
            reply = search(key, q=terms, orientation=orientation, editors_choice=editors_choice,
                           order="popular", per_page=min(200, count * 3))
            for hit in reply["hits"]:
                if len(ids) < count and hit["id"] not in taken:
                    ids.append(hit["id"])
                    taken.add(hit["id"])
            if len(ids) == count:
                break
        if len(ids) < count:
            sys.exit(f"{name}: only {len(ids)} photos for {terms!r}")
        albums.append({"name": name, "query": terms, "ids": ids})
        print(f"{name}: {len(ids)} photos")
    ALBUMS_FILE.write_text(json.dumps({"albums": albums}, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {ALBUMS_FILE.relative_to(ROOT)}")


def fetch(key, out):
    albums = json.loads(ALBUMS_FILE.read_text(encoding="utf-8"))["albums"]
    credits = ["# Credits", "", "Photos from [Pixabay](https://pixabay.com/), under the Pixabay Content License.", ""]
    # Spread the photos over two years, one album a month apart, so the wall's
    # time bar has dates to show. Pixabay strips EXIF, so the file time is the
    # only date a photo has.
    start = datetime.datetime(2024, 9, 1, 10, 0)
    for index, album in enumerate(albums):
        folder = out / album["name"]
        folder.mkdir(parents=True, exist_ok=True)
        credits += [f"## {album['name']}", ""]
        for position, photo_id in enumerate(album["ids"]):
            hit = search(key, id=photo_id)["hits"][0]
            full = position < FULL_SIZE_PER_ALBUM or photo_id in album.get("full", [])
            url = (hit.get("fullHDURL") or hit["largeImageURL"]) if full else hit["webformatURL"]
            path = folder / f"{photo_id}.jpg"
            # Full size ones download every time, so a photo albums.json moves to
            # full size replaces its small copy.
            if full or not path.exists():
                path.write_bytes(get(url))
            taken = start + datetime.timedelta(days=30 * index + position, hours=position)
            os.utime(path, (taken.timestamp(), taken.timestamp()))
            credits.append(f"- `{path.name}`: [{hit['user']}]({hit['pageURL']})")
            print(f"{album['name']}/{path.name} {'full' if full else 'small'}")
        credits.append("")
    (out / "CREDITS.md").write_text("\n".join(credits), encoding="utf-8")
    print(f"wrote {out}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["pick", "fetch"])
    parser.add_argument("--out", type=Path, default=Path.home() / "Pictures" / "gallery3d-samples")
    args = parser.parse_args()
    key = api_key()
    if args.command == "pick":
        pick(key)
    else:
        fetch(key, args.out)


if __name__ == "__main__":
    main()

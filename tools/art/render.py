#!/usr/bin/env python3
"""Renders the SVG sources in art/ to the PNG drawables under assets/.

Each SVG names what it renders on its root element, in the urn:gallery3d:art
namespace:

  g3d:outputs    Space-separated bucket=WIDTHxHEIGHT pairs, such as
                 "drawable-mdpi=40x40 drawable-hdpi=60x60". A drawable= pair is
                 the plain folder, where the wall's textures and anything drawn
                 at a fixed size come from. For chrome, the mdpi and hdpi pairs
                 are not written as given: they say the SVG was traced at 1x or
                 1.5x, and the density buckets below render from the SVG traced
                 nearest to them.
  g3d:name       The drawable's name, when it is not the file's. A nine-patch
                 keeps its .9, as in popup.9.
  g3d:frames     A range such as "1..8". The file renders once for each value,
                 with {{frame}} replaced throughout, the name included.
  g3d:ninepatch  "stretch-x=A-B stretch-y=A-B pad-x=A-B pad-y=A-B", in viewBox
                 units, inclusive. The art renders one pixel in from every edge
                 and the guides go on that border, as a .9.png wants them.

The chrome in CHROME is rendered into every bucket in BUCKETS, mdpi through
xxxhdpi, at the size its code draws it at. The app takes the bucket for the
display's density, or the one above it and scales down, so chrome is never
enlarged.

Needs resvg_py and Pillow: pip install resvg_py pillow

  python tools/art/render.py                   renders every drawable
  python tools/art/render.py --only a,b        renders just these names
  python tools/art/render.py --sheet out.png --original DIR
                                               also draws old beside new
"""

import argparse
import copy
import io
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

import resvg_py
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART = os.path.join(ROOT, "art")
ASSETS = os.path.join(ROOT, "assets")

SVG_NS = "http://www.w3.org/2000/svg"
ART_NS = "urn:gallery3d:art"
ET.register_namespace("", SVG_NS)
ET.register_namespace("xlink", "http://www.w3.org/1999/xlink")
ET.register_namespace("g3d", ART_NS)

# Android's density buckets, as App.cpp loads them.
BUCKETS = {
    "drawable-mdpi": 1.0,
    "drawable-hdpi": 1.5,
    "drawable-xhdpi": 2.0,
    "drawable-xxhdpi": 3.0,
    "drawable-xxxhdpi": 4.0,
}

FIT = "fit"
STRETCH = "stretch"

# Chrome, in density-independent pixels: the box the code draws each drawable
# into. A nine-patch's size leaves out its one pixel guide border. A STRETCH
# drawable fills its box exactly, as the bars that use it stretch it anyway; a
# FIT one keeps its shape, centred in the box.
CHROME = {
    # PathBarLayer: ART_HEIGHT, JOIN_WIDTH and CAP_WIDTH, and ICON_SIZE for the
    # crumbs' icons.
    "pathbar_bg": (1, 39, STRETCH),
    "pathbar_cap": (22, 39, STRETCH),
    "pathbar_join": (21, 39, STRETCH),
    "icon_home_small": (39, 39, FIT),
    "icon_folder_small": (39, 39, FIT),
    "icon_camera_small": (39, 39, FIT),
    "icon_picasa_small": (39, 39, FIT),
    "icon_location_small": (39, 39, FIT),
    "ic_fs_details": (39, 39, FIT),
    # MenuBar: ART_HEIGHT and HIGHLIGHT_EDGE_WIDTH, and ICON_SIZE for the
    # buttons' icons, which PopupMenu's rows share.
    "selection_menu_bg": (1, 58, STRETCH),
    "selection_menu_divider": (1, 2, STRETCH),
    "selection_menu_bg_pressed": (1, 58, STRETCH),
    "selection_menu_bg_pressed_left": (21, 58, STRETCH),
    "selection_menu_bg_pressed_right": (21, 58, STRETCH),
    "icon_delete": (34, 34, FIT),
    "icon_cancel": (34, 34, FIT),
    "icon_more": (34, 34, FIT),
    "icon_play": (34, 34, FIT),
    # PopupMenu: ICON_SIZE for the rows' icons, TRIANGLE_WIDTH and
    # TRIANGLE_HEIGHT, and the panel and highlight nine-patches at the sizes
    # they were traced at. TimeBar's date popup uses the same panel.
    "ic_menu_rotate_left": (34, 34, FIT),
    "ic_menu_rotate_right": (34, 34, FIT),
    "ic_menu_view_details": (34, 34, FIT),
    "popup_triangle_bottom": (43, 28, STRETCH),
    "popup.9": (62, 67, STRETCH),
    "popup_option_selected.9": (18, 40, STRETCH),
    # Drawn at their own size: the zoom and mode buttons by ImageButton, the
    # knob by TimeBar.
    "gallery_zoom_in": (66, 42, FIT),
    "gallery_zoom_in_touch": (66, 42, FIT),
    "gallery_zoom_out": (66, 42, FIT),
    "gallery_zoom_out_touch": (66, 42, FIT),
    "mode_grid": (100, 93, FIT),
    "mode_stack": (100, 93, FIT),
    "scroller_new": (163, 48, FIT),
    "scroller_pressed_new": (163, 48, FIT),
}


def pixels(dp, density):
    """A length in whole pixels, rounded as App::uiPixels rounds it."""
    return int(math.floor(dp * density + 0.5))


def art_attribute(root, key):
    return root.get("{%s}%s" % (ART_NS, key))


def sources():
    """Every drawable an SVG describes, as (path, name, root element)."""
    for file in sorted(os.listdir(ART)):
        if not file.endswith(".svg"):
            continue
        path = os.path.join(ART, file)
        with open(path, encoding="utf-8") as handle:
            template = handle.read()
        frames = art_attribute(ET.fromstring(template), "frames")
        values = [None]
        if frames:
            first, last = frames.split("..")
            values = [str(value) for value in range(int(first), int(last) + 1)]
        for value in values:
            text = template if value is None else template.replace("{{frame}}", value)
            root = ET.fromstring(text)
            yield path, art_attribute(root, "name") or file[: -len(".svg")], root


def view_box(root):
    box = root.get("viewBox")
    if box:
        return [float(value) for value in re.split(r"[\s,]+", box.strip())]
    return [0.0, 0.0, float(root.get("width")), float(root.get("height"))]


def rasterize(path, root, width, height, stretch):
    sized = copy.deepcopy(root)
    sized.set("width", str(width))
    sized.set("height", str(height))
    if stretch:
        sized.set("preserveAspectRatio", "none")
    data = resvg_py.svg_to_bytes(svg_string=ET.tostring(sized, encoding="unicode"))
    image = Image.open(io.BytesIO(bytes(data))).convert("RGBA")
    if image.size != (width, height):
        sys.exit(f"{path}: rendered {image.size[0]}x{image.size[1]}, wanted {width}x{height}")
    return image


def with_guides(path, root, width, height, spec, stretch):
    """The art one pixel in from each edge, with nine-patch guides around it."""
    content_width = width - 2
    content_height = height - 2
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    image.paste(rasterize(path, root, content_width, content_height, stretch), (1, 1))

    left, top, box_width, box_height = view_box(root)
    scale_x = content_width / box_width
    scale_y = content_height / box_height
    guides = dict(item.split("=") for item in spec.split())

    def guide_pixels(key, origin, scale, limit):
        first, last = (float(value) for value in guides[key].split("-"))
        start = max(0, int(round((first - origin) * scale)))
        end = min(limit, int(round((last + 1 - origin) * scale)))
        return range(start + 1, end + 1)

    pixel = image.load()
    black = (0, 0, 0, 255)
    for x in guide_pixels("stretch-x", left, scale_x, content_width):
        pixel[x, 0] = black
    for y in guide_pixels("stretch-y", top, scale_y, content_height):
        pixel[0, y] = black
    for x in guide_pixels("pad-x", left, scale_x, content_width):
        pixel[x, height - 1] = black
    for y in guide_pixels("pad-y", top, scale_y, content_height):
        pixel[width - 1, y] = black
    return image


def render(path, root, width, height, stretch):
    spec = art_attribute(root, "ninepatch")
    if spec:
        return with_guides(path, root, width, height, spec, stretch)
    return rasterize(path, root, width, height, stretch)


def write_sheet(rendered, target, original):
    """Pages of old and new side by side, on mid grey and on near black."""
    cell = 300
    per_page = 12
    columns = 4
    backgrounds = [(128, 128, 128, 255), (24, 24, 24, 255)]
    base, extension = os.path.splitext(target)
    for page_index in range(0, len(rendered), per_page):
        page = rendered[page_index : page_index + per_page]
        rows = (len(page) + 1) // 2
        sheet = Image.new("RGBA", (cell * columns * 2, cell * rows), (60, 0, 60, 255))
        draw = ImageDraw.Draw(sheet)
        for index, (bucket, name, path) in enumerate(page):
            x0 = (index % 2) * cell * columns
            y0 = (index // 2) * cell
            old_path = os.path.join(original, bucket, name + ".png") if original else None
            images = [Image.open(old_path).convert("RGBA") if old_path and os.path.exists(old_path) else None,
                      Image.open(path).convert("RGBA")]
            for column in range(columns):
                background = backgrounds[column // 2]
                picture = images[column % 2]
                x = x0 + column * cell
                draw.rectangle([x, y0, x + cell - 1, y0 + cell - 1], fill=background)
                if picture is None:
                    continue
                scale = max(1, min(6, (cell - 24) // max(picture.width, picture.height)))
                if max(picture.width, picture.height) * scale > cell - 24:
                    picture = picture.resize(((cell - 24), max(1, picture.height * (cell - 24) // picture.width)))
                else:
                    picture = picture.resize((picture.width * scale, picture.height * scale), Image.NEAREST)
                sheet.alpha_composite(picture, (x + 4, y0 + 4))
            draw.text((x0 + 4, y0 + cell - 16), f"{bucket}/{name}  old | new | old | new", fill=(255, 0, 255, 255))
        page_path = f"{base}-{page_index // per_page + 1}{extension}"
        sheet.save(page_path)
        print(f"sheet {page_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", help="comma-separated drawable names")
    parser.add_argument("--sheet", help="where to write comparison sheets")
    parser.add_argument("--original", help="a copy of assets/ to compare against")
    args = parser.parse_args()

    wanted = set(args.only.split(",")) if args.only else None
    rendered = []

    def write(bucket, name, image):
        target = os.path.join(ASSETS, bucket, name + ".png")
        os.makedirs(os.path.dirname(target), exist_ok=True)
        image.save(target, optimize=True)
        rendered.append((bucket, name, target))
        print(f"{bucket}/{name}.png {image.width}x{image.height}")

    # Which SVGs can draw each chrome name, and the density each was traced
    # at. Preferred where two are equally near: one traced for a density bucket
    # over one drawn for the plain folder.
    traced = {}
    for path, name, root in sources():
        outputs = art_attribute(root, "outputs")
        if not outputs:
            sys.exit(f"{path}: no g3d:outputs")
        for output in outputs.split():
            bucket, size = output.split("=")
            width, height = (int(value) for value in size.split("x"))
            if bucket != "drawable" and bucket not in BUCKETS:
                sys.exit(f"{path}: {bucket} is not a bucket this tool knows")
            density = BUCKETS.get(bucket, 1.0)
            traced.setdefault(name, []).append((density, bucket != "drawable", path, root))
            if name in CHROME and bucket != "drawable":
                continue
            if wanted is None or name in wanted:
                write(bucket, name, render(path, root, width, height, False))

    for name, (dp_width, dp_height, fit) in CHROME.items():
        if wanted is not None and name not in wanted:
            continue
        options = traced.get(name)
        if not options:
            sys.exit(f"no SVG draws the chrome drawable {name}")
        for bucket, density in BUCKETS.items():
            _, _, path, root = min(options, key=lambda option: (abs(option[0] - density), not option[1], -option[0]))
            border = 2 if art_attribute(root, "ninepatch") else 0
            width = pixels(dp_width, density) + border
            height = pixels(dp_height, density) + border
            write(bucket, name, render(path, root, width, height, fit == STRETCH))

    if wanted is not None:
        missing = wanted - {name for _, name, _ in rendered}
        if missing:
            sys.exit("no SVG renders " + ", ".join(sorted(missing)))
    if args.sheet:
        write_sheet(rendered, args.sheet, args.original)


if __name__ == "__main__":
    main()

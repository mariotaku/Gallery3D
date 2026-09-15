#!/usr/bin/env python3
"""Writes the image decoding fixtures and their expected results.

Every platform's decoder reads these same files, and the decode tests check
what it returns against tests/fixtures/decode/manifest.json. Pillow, with
libjpeg-turbo and LittleCMS behind it, is the reference decoder and colour
engine the expected pixels come from.

    python tools/fixtures/make_decode_fixtures.py

Needs Pillow built with JPEG, WebP, libtiff and LittleCMS support.

The pictures are flat patches of colour, so decoders that differ in their
IDCT, chroma upsampling or scaler still agree at the middle of each patch,
which is where the probes are. Lossless formats also have a golden of every
pixel.
"""

import io
import json
import os
import struct

from PIL import Image, ImageCms

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "tests", "fixtures", "decode"))

# Top left, top right, bottom left, bottom right. Kept away from the gamut's
# edges, so a profile conversion moves them without clipping.
PATCHES = [(200, 60, 50), (60, 180, 80), (50, 80, 200), (220, 200, 70)]
# What an embedded thumbnail shows instead, so a test can tell which one a
# decoder used.
THUMBNAIL_PATCHES = [(240, 240, 240), (20, 20, 20), (150, 40, 160), (40, 150, 150)]

# How far a decoded pixel may be from the expected one. JPEG decoders differ
# by a few levels even at the middle of a flat patch, colour engines by a few
# more. Lossless decodes differ only by premultiplication rounding.
LOSSY = 6
COLOUR = 8
LOSSLESS = 1


# ICC profiles, the same way tests/test_colorprofile.cpp builds them.

def fixed16(value):
    return struct.pack(">i", int(round(value * 65536.0)))


def padded(data):
    return data + b"\0" * (-len(data) % 4)


def xyz_tag(x, y, z):
    return b"XYZ \0\0\0\0" + fixed16(x) + fixed16(y) + fixed16(z)


def curve_tag():
    # A 2.2 gamma, as u8.8.
    return padded(b"curv\0\0\0\0" + struct.pack(">IH", 1, 0x0233))


def desc_tag(text):
    ascii = text.encode("ascii") + b"\0"
    return padded(b"desc\0\0\0\0" + struct.pack(">I", len(ascii)) + ascii + b"\0" * (4 + 4 + 2 + 1 + 67))


def text_tag(text):
    return padded(b"text\0\0\0\0" + text.encode("ascii") + b"\0")


def matrix_profile(description, red, green, blue):
    """An ICC v2 RGB display profile from D50 colorants and a 2.2 gamma."""
    tags = [
        (b"desc", desc_tag(description)),
        (b"cprt", text_tag("No copyright")),
        (b"wtpt", xyz_tag(0.9642, 1.0, 0.8249)),
        (b"rXYZ", xyz_tag(*red)),
        (b"gXYZ", xyz_tag(*green)),
        (b"bXYZ", xyz_tag(*blue)),
        (b"rTRC", curve_tag()),
        (b"gTRC", curve_tag()),
        (b"bTRC", curve_tag()),
    ]
    table = struct.pack(">I", len(tags))
    data = b""
    data_start = 128 + 4 + 12 * len(tags)
    for signature, tag in tags:
        table += signature + struct.pack(">II", data_start + len(data), len(tag))
        data += tag
    header = struct.pack(">II", 128 + len(table) + len(data), 0) + struct.pack(">I", 0x02100000)
    header += b"mntrRGB XYZ " + struct.pack(">6H", 2026, 9, 15, 0, 0, 0) + b"acspMSFT"
    header += b"\0" * (4 + 4 + 4 + 8 + 4)
    header += fixed16(0.9642) + fixed16(1.0) + fixed16(0.8249)
    header += b"\0" * (4 + 16 + 28)
    assert len(header) == 128
    return header + table + data


SRGB_RED = (0.4361, 0.2225, 0.0139)
SRGB_GREEN = (0.3851, 0.7169, 0.0971)
SRGB_BLUE = (0.1431, 0.0606, 0.7141)
# sRGB with its red and green primaries exchanged: stored red shows as green.
SWAPPED = matrix_profile("Red and green exchanged", SRGB_GREEN, SRGB_RED, SRGB_BLUE)
# Adobe RGB (1998), for a photo that says so through EXIF ColorSpace alone.
ADOBE_RGB = matrix_profile("Adobe RGB (1998)", (0.6097, 0.3111, 0.0195), (0.2053, 0.6257, 0.0609),
                           (0.1492, 0.0632, 0.7446))


def to_srgb(image, profile_bytes):
    """The picture's colours converted from the profile to sRGB, as LittleCMS does it."""
    source = ImageCms.ImageCmsProfile(io.BytesIO(profile_bytes))
    target = ImageCms.createProfile("sRGB")
    mode = image.mode
    transform = ImageCms.buildTransform(source, target, mode, mode,
                                        renderingIntent=ImageCms.Intent.RELATIVE_COLORIMETRIC)
    return ImageCms.applyTransform(image, transform)


# EXIF, written by hand, since Pillow cannot write an IFD1 thumbnail.

def exif_segment(orientation=None, color_space=None, thumbnail=None):
    """A JPEG APP1 segment holding a little-endian TIFF with the given tags."""
    def ifd_size(count):
        return 2 + 12 * count + 4

    ifd0 = []
    if orientation is not None:
        ifd0.append((0x0112, 3, 1, struct.pack("<HH", orientation, 0)))
    exif = []
    if color_space is not None:
        exif.append((0xA001, 3, 1, struct.pack("<HH", color_space, 0)))

    offset = 8
    ifd0_at = offset
    offset += ifd_size(len(ifd0) + (1 if exif else 0))
    exif_at = 0
    if exif:
        exif_at = offset
        offset += ifd_size(len(exif))
        ifd0.append((0x8769, 4, 1, struct.pack("<I", exif_at)))
    ifd1_at = 0
    thumbnail_at = 0
    if thumbnail is not None:
        ifd1_at = offset
        offset += ifd_size(3)
        thumbnail_at = offset

    def ifd(entries, next_at):
        entries = sorted(entries)
        data = struct.pack("<H", len(entries))
        for tag, kind, count, value in entries:
            data += struct.pack("<HHI", tag, kind, count) + value
        return data + struct.pack("<I", next_at)

    tiff = b"II*\0" + struct.pack("<I", ifd0_at) + ifd(ifd0, ifd1_at)
    if exif:
        tiff += ifd(exif, 0)
    if thumbnail is not None:
        tiff += ifd([(0x0103, 3, 1, struct.pack("<HH", 6, 0)),
                     (0x0201, 4, 1, struct.pack("<I", thumbnail_at)),
                     (0x0202, 4, 1, struct.pack("<I", len(thumbnail)))], 0)
        tiff += thumbnail
    payload = b"Exif\0\0" + tiff
    return b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload


def after_soi(jpeg, segment):
    assert jpeg[:2] == b"\xff\xd8"
    return jpeg[:2] + segment + jpeg[2:]


# Pictures.

def quartered(width, height, patches=PATCHES, alphas=None):
    mode = "RGBA" if alphas else "RGB"
    image = Image.new(mode, (width, height))
    boxes = [(0, 0, width // 2, height // 2), (width // 2, 0, width, height // 2),
             (0, height // 2, width // 2, height), (width // 2, height // 2, width, height)]
    for index, box in enumerate(boxes):
        colour = patches[index] + ((alphas[index],) if alphas else ())
        image.paste(colour, box)
    return image


def ramps(width, height):
    """Red rising and falling across the picture and green down it, eight
    levels a pixel, so a region taken one pixel off is further from its
    golden than a decoder's rounding."""
    image = Image.new("RGB", (width, height))
    image.putdata([(abs((x * 8) % 510 - 255), abs((y * 8) % 510 - 255), 100)
                   for y in range(height) for x in range(width)])
    return image


def probe_points(width, height):
    return [(width // 4, height // 4), (3 * width // 4, height // 4),
            (width // 4, 3 * height // 4), (3 * width // 4, 3 * height // 4)]


def rgba(image):
    return image.convert("RGBA")


def probes(expected, width=None, height=None):
    """Straight RGBA at the middle of each patch of the expected picture."""
    image = rgba(expected)
    width = width or image.width
    height = height or image.height
    if (width, height) != image.size:
        image = image.resize((width, height), Image.NEAREST)
    return [[x, y] + list(image.getpixel((x, y))) for x, y in probe_points(width, height)]


def fit_within(width, height, max_edge):
    """The size rule every decoder follows: the long edge becomes max_edge and
    the short edge is rounded to the nearest pixel, never below 1. A picture
    that already fits keeps its size."""
    if max_edge <= 0 or max(width, height) <= max_edge:
        return width, height
    long_edge, short_edge = max(width, height), min(width, height)
    scaled = max(1, (short_edge * max_edge + long_edge // 2) // long_edge)
    return (max_edge, scaled) if width >= height else (scaled, max_edge)


def encode(image, format, **options):
    buffer = io.BytesIO()
    image.save(buffer, format, **options)
    return buffer.getvalue()


def decoded(data):
    return Image.open(io.BytesIO(data))


FIXTURES = []


def write(name, data):
    with open(os.path.join(OUT, name), "wb") as handle:
        handle.write(data)


def fixture(name, data, expected, *, alpha, lossless, tolerance, scaled=(), golden=None, notes="",
            pattern="quarters"):
    """pattern is "quarters" for four flat patches, whose middles any scaler
    agrees on, or "ramps" for a picture that changes every pixel."""
    write(name, data)
    image = rgba(expected)
    entry = {
        "file": name,
        "width": image.width,
        "height": image.height,
        "pattern": pattern,
        "alpha": alpha,
        "lossless": lossless,
        "tolerance": tolerance,
        "probes": probes(image),
        "scaled": [],
    }
    for max_edge in scaled:
        width, height = fit_within(image.width, image.height, max_edge)
        # A point of a reduced ramp depends on the scaler, so ramps are checked
        # against their golden averaged instead.
        entry["scaled"].append({"maxEdge": max_edge, "width": width, "height": height,
                                "probes": probes(image, width, height) if pattern == "quarters" else []})
    if golden is None:
        golden = lossless and image.width * image.height <= 96 * 64
    if golden:
        golden_name = os.path.splitext(name)[0] + ".rgba"
        write(golden_name, image.tobytes())
        entry["golden"] = golden_name
    if notes:
        entry["notes"] = notes
    FIXTURES.append(entry)


def invalid(name, data, notes):
    write(name, data)
    FIXTURES.append({"file": name, "invalid": True, "notes": notes})


def main():
    os.makedirs(OUT, exist_ok=True)
    for old in os.listdir(OUT):
        os.remove(os.path.join(OUT, old))

    picture = quartered(96, 64)

    # JPEG, as cameras and editors write it.
    baseline = encode(picture, "JPEG", quality=95, subsampling=0)
    fixture("baseline.jpg", baseline, decoded(baseline), alpha=False, lossless=False, tolerance=LOSSY,
            scaled=(48, 24))
    subsampled = encode(picture, "JPEG", quality=95, subsampling=2)
    fixture("subsampled.jpg", subsampled, decoded(subsampled), alpha=False, lossless=False, tolerance=LOSSY,
            scaled=(48,))
    progressive = encode(picture, "JPEG", quality=95, subsampling=0, progressive=True)
    fixture("progressive.jpg", progressive, decoded(progressive), alpha=False, lossless=False,
            tolerance=LOSSY, scaled=(48,))
    gray = encode(picture.convert("L"), "JPEG", quality=95)
    fixture("gray.jpg", gray, decoded(gray), alpha=False, lossless=False, tolerance=LOSSY, scaled=(48,))
    cmyk = encode(picture.convert("CMYK"), "JPEG", quality=95)
    fixture("cmyk.jpg", cmyk, decoded(cmyk).convert("RGB"), alpha=False, lossless=False, tolerance=COLOUR,
            notes="Four channel JPEG with no profile, converted without colour management.")

    # Region decoders crop at whole pixels. The size is off the 8x8 block grid
    # on both edges, so the last row and column of blocks are partial.
    gradient = encode(ramps(203, 157), "JPEG", quality=95, subsampling=0)
    fixture("gradient.jpg", gradient, decoded(gradient), alpha=False, lossless=False, tolerance=LOSSY,
            scaled=(101, 50, 25), golden=True, pattern="ramps",
            notes="Red and green ramps of eight levels a pixel. The golden is Pillow's decode.")

    # Orientation is a tag, not the pixels: every decoder hands out the pixels
    # as stored.
    oriented = after_soi(baseline, exif_segment(orientation=6))
    fixture("orientation6.jpg", oriented, decoded(baseline), alpha=False, lossless=False, tolerance=LOSSY,
            notes="EXIF orientation 6. Decoded pixels stay as stored.")
    # One file for each EXIF orientation, big enough for a platform thumbnail.
    # Four different patches tell all eight apart.
    stored = encode(quartered(320, 240), "JPEG", quality=95, subsampling=0)
    for orientation in range(1, 9):
        fixture("orientation_%d.jpg" % orientation, after_soi(stored, exif_segment(orientation=orientation)),
                decoded(stored), alpha=False, lossless=False, tolerance=LOSSY, scaled=(160,),
                notes="EXIF orientation %d over the same stored pixels." % orientation)

    # A camera's embedded thumbnail, in the photo's shape. A decode reduced to
    # no more than the thumbnail's long edge may answer from it, and every
    # platform does.
    large = quartered(1600, 1200)
    large_jpeg = encode(large, "JPEG", quality=90)
    thumbnail_picture = quartered(160, 120, THUMBNAIL_PATCHES)
    thumbnail_jpeg = encode(thumbnail_picture, "JPEG", quality=90)
    with_thumbnail = after_soi(large_jpeg, exif_segment(thumbnail=thumbnail_jpeg))
    write("thumbnail.jpg", with_thumbnail)
    main_image = rgba(decoded(large_jpeg))
    thumbnail_image = rgba(decoded(thumbnail_jpeg))
    FIXTURES.append({
        "file": "thumbnail.jpg",
        "width": 1600,
        "height": 1200,
        "pattern": "quarters",
        "alpha": False,
        "lossless": False,
        "tolerance": LOSSY,
        "probes": probes(main_image),
        "scaled": [
            {"maxEdge": 160, "width": 160, "height": 120, "probes": probes(thumbnail_image),
             "source": "thumbnail"},
            {"maxEdge": 100, "width": 100, "height": 75, "probes": probes(thumbnail_image, 100, 75),
             "source": "thumbnail"},
            {"maxEdge": 161, "width": 161, "height": 121, "probes": probes(main_image, 161, 121),
             "source": "frame"},
            {"maxEdge": 400, "width": 400, "height": 300, "probes": probes(main_image, 400, 300),
             "source": "frame"},
        ],
        "notes": "1600x1200 frame with a 160x120 IFD1 thumbnail of different colours.",
    })

    # Lossless formats.
    fixture("opaque.png", encode(picture, "PNG"), picture, alpha=False, lossless=True, tolerance=LOSSLESS,
            scaled=(48,))
    translucent = quartered(96, 64, alphas=(255, 128, 64, 0))
    fixture("translucent.png", encode(translucent, "PNG"), translucent, alpha=True, lossless=True,
            tolerance=LOSSLESS, notes="Alpha 255, 128, 64 and 0 by patch. Probes are straight alpha.")
    palette = picture.quantize(colors=4, dither=Image.Dither.NONE)
    fixture("palette.png", encode(palette, "PNG"), palette.convert("RGB"), alpha=False, lossless=True,
            tolerance=LOSSLESS)
    gray_alpha = picture.convert("L")
    gray_alpha.putalpha(quartered(96, 64, [(255, 255, 255), (128, 128, 128), (64, 64, 64), (0, 0, 0)])
                        .convert("L"))
    fixture("gray_alpha.png", encode(gray_alpha, "PNG"), gray_alpha, alpha=True, lossless=True,
            tolerance=LOSSLESS)
    fixture("opaque.bmp", encode(picture, "BMP"), picture, alpha=False, lossless=True, tolerance=LOSSLESS)
    fixture("lossless.webp", encode(picture, "WEBP", lossless=True), picture, alpha=False, lossless=True,
            tolerance=LOSSLESS)
    fixture("opaque.tif", encode(picture, "TIFF", compression="raw"), picture, alpha=False, lossless=True,
            tolerance=LOSSLESS)

    # Colour profiles. Stored red shows as green through the swapped profile,
    # which is also kept on its own so a test can compare what a file carries.
    write("swapped.icc", SWAPPED)
    swapped_expected = to_srgb(picture, SWAPPED)
    swapped_jpeg = encode(picture, "JPEG", quality=95, subsampling=0, icc_profile=SWAPPED)
    fixture("icc.jpg", swapped_jpeg, to_srgb(decoded(swapped_jpeg).convert("RGB"), SWAPPED), alpha=False,
            lossless=False, tolerance=COLOUR, scaled=(48,))
    fixture("icc.png", encode(picture, "PNG", icc_profile=SWAPPED), swapped_expected, alpha=False,
            lossless=True, tolerance=COLOUR, scaled=(48,), golden=True)
    fixture("icc.webp", encode(picture, "WEBP", lossless=True, icc_profile=SWAPPED), swapped_expected,
            alpha=False, lossless=True, tolerance=COLOUR, golden=True)
    fixture("icc.tif", encode(picture, "TIFF", compression="raw", icc_profile=SWAPPED), swapped_expected,
            alpha=False, lossless=True, tolerance=COLOUR, golden=True)
    translucent_icc = to_srgb(translucent, SWAPPED)
    fixture("icc_translucent.png", encode(translucent, "PNG", icc_profile=SWAPPED), translucent_icc,
            alpha=True, lossless=True, tolerance=COLOUR, golden=True,
            notes="Converted while straight, then premultiplied.")
    adobe_jpeg = after_soi(encode(picture, "JPEG", quality=95, subsampling=0), exif_segment(color_space=2))
    fixture("adobe_exif.jpg", adobe_jpeg, to_srgb(decoded(adobe_jpeg).convert("RGB"), ADOBE_RGB), alpha=False,
            lossless=False, tolerance=COLOUR,
            notes="No ICC profile. EXIF ColorSpace 2 says Adobe RGB (1998).")

    # Sizes, for the rule in fit_within. One flat colour each, so any scaler
    # agrees on the colour.
    for width, height, format, extension, max_edges in [
            (282, 100, "PNG", "png", (256,)),
            (1000, 333, "JPEG", "jpg", (500,)),
            (2048, 1536, "PNG", "png", (256,)),
            (300, 200, "JPEG", "jpg", (512, 0, -1)),
            (1000, 1, "PNG", "png", (10,)),
            (1, 1000, "PNG", "png", (10,))]:
        flat = Image.new("RGB", (width, height), PATCHES[0])
        options = {"quality": 95, "subsampling": 0} if format == "JPEG" else {}
        data = encode(flat, format, **options)
        fixture("size_%dx%d.%s" % (width, height, extension), data, decoded(data).convert("RGB"),
                alpha=False, lossless=format != "JPEG", tolerance=LOSSY, scaled=max_edges, golden=False)

    # What must not decode.
    invalid("truncated.jpg", baseline[:len(baseline) * 6 // 10], "Cut before the scan.")
    invalid("truncated_scan.jpg", gradient[:len(gradient) * 6 // 10],
            "gradient.jpg cut inside the scan, so its header is whole.")
    opaque_png = encode(picture, "PNG")
    invalid("truncated.png", opaque_png[:len(opaque_png) * 6 // 10], "Cut inside IDAT.")
    invalid("rubbish.jpg", bytes((index * 37) % 256 for index in range(512)), "Not an image.")
    invalid("empty.png", b"", "No bytes.")

    with open(os.path.join(OUT, "manifest.json"), "w", newline="\n") as handle:
        json.dump({"generator": "tools/fixtures/make_decode_fixtures.py",
                   "patches": "top left, top right, bottom left, bottom right",
                   "fixtures": FIXTURES}, handle, indent=1)
        handle.write("\n")
    print("Wrote %d fixtures to %s" % (len(FIXTURES), OUT))


if __name__ == "__main__":
    main()

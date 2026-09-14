# Art

Every PNG under `assets/drawable`, `assets/drawable-mdpi` and
`assets/drawable-hdpi` is rendered from an SVG here. Edit the SVG, never the
PNG, then render:

```sh
pip install resvg_py pillow
python tools/art/render.py                  # every drawable
python tools/art/render.py --only icon_play # one of them
```

The attributes that name a file's outputs, frames and nine-patch guides are
described at the top of `tools/art/render.py`. `--sheet out.png --original DIR`
also writes pages of the old PNGs beside the new ones, on grey and on black.

## Sizes

An output keeps the pixel size of the PNG it replaced, because the code lays
out around those sizes. Draw in mdpi pixels where a drawable has an mdpi size,
and in the fallback drawable's pixels where it has only that one. Some
drawables are textures rather than pictures, and their geometry is part of
how they are drawn:

- `stack_frame`, `stack_frame_focus`, `stack_frame_gold` and `grid_frame` are
  cut into quarters by `GridQuadFrame`, which stretches the middle row and
  column along the photo's edges. The border keeps its original insets, and
  the middle stays clear.
- `pathbar_*`, `selection_menu_*` and `selection_lower_bg` are stretched or
  tiled into bars. Their height, and where the highlight and shadow rows sit
  in it, stay the same.
- Nine-patches keep their stretch regions, so their caps are as wide as
  before.

## The look

Android 2.3 and the Cooliris gallery it came from, around 2010: glyphs with
depth, soft gradients, and shadows rather than flat shapes and hairlines.
Numbers are in mdpi pixels.

- **Shadows.** A glyph on the chrome sits in a dark halo that reaches about two
  pixels on every side and a little further below. It is two blurred black
  copies of the shape laid over each other: blur about 1.5, one offset about
  0.5 down and one about 1 down, together close to opaque at the glyph's edge.
  The halo shows through every hole in the glyph. Anything on the wall, a
  frame or a badge, casts a softer and longer shadow: a tight one and a wide
  one, blur about 2 and 4, offset 2 to 4 down. Leave room in the canvas for a
  shadow to fade out.
- **Light glyphs.** Flat white, and only white: the path bar crumbs, the album
  source icons and the like. Detail inside a glyph, such as the photo inside
  a frame or a camera's lens, is see-through, and the glyph's own halo of
  shadow shows there. Never fill it with grey.
- **Grey controls.** Buttons and badges on the chrome are a grey gradient,
  `#e8e8e8` to `#9a9a9a`, with a one-pixel lighter edge along the top.
- **Menu icons.** The `ic_menu_*` set is a lighter grey bevel with a soft white
  glow around it, as Android 2.3 menus drew them.
- **Pressed and selected.** Gold, `#fcdc71` fading to `#e9a001`, with a
  `#ffc000` glow outside it.
- **Focus.** Orange, `#ef750b`, glowing.
- **Checked.** Green, `#00c000` to `#007a20`, with a light `#66ee66` rim and a
  white check that casts the chrome's drop shadow.
- **Bars.** Translucent black, darker toward the bottom, with a highlight row of
  `#747474` at about 47% near the top edge.
- **Corners.** Small radii, 0.5 to 3. Nothing is fully round unless the old art
  was.

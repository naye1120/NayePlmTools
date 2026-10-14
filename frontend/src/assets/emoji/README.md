These sprites are derived from Twemoji v15.1.0 graphics by Twitter, Inc. and
other contributors, licensed under CC BY 4.0 (see LICENSE-GRAPHICS).

Source: https://github.com/jdecked/twemoji/tree/v15.1.0
Archive: https://codeload.github.com/jdecked/twemoji/tar.gz/refs/tags/v15.1.0
Archive SHA-256: ffa39215bb14fbedf29743c5e7b897c0a2f06070f27009c6ec716ff080032d19

Modifications: original 72x72 PNG graphics resized to 20x20 pixels and assembled
into a sprite sheet with a shared 256-color RGBA palette. Identifier/CSS files are generated alongside it. All 3,838
upstream assets are included, not just emoji seen in a particular README.

Regenerate with Pillow installed:

    python3 tools/build_emoji_sprites.py scratch/twemoji-v15.1.0.tar.gz

The generated files are committed. Ordinary builds need no archive, Pillow or
network access. Vite embeds the PNG in the single HTML file. Browser decoding
uses about 6 MiB for the 1280x1200 RGBA sheet; the asset is shared by all emoji
and does not duplicate its base64 contents per occurrence.

Sprite position rules are generated lazily for the emoji actually rendered.
The smaller raster resolution keeps normal inline glyphs compact; large headings
may look softer than with the earlier 36px candidate.

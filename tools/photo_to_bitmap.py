#!/usr/bin/env python3
"""
Converts a real photo into a raw RGB888 C++ header for embedding in
the kernel and drawing via Framebuffer::draw_rgb_bitmap().

Requires: pip install Pillow

Usage:
    python3 photo_to_bitmap.py photo.jpg wallpaper.hpp 160 100

IMPORTANT -- resolution tradeoff, read before picking numbers:
There's no disk driver in this kernel yet, so the image data gets
embedded directly in the compiled binary as a byte array. Size grows
fast: width * height * 3 bytes, uncompressed.
    160x100  ->   48,000 bytes  (reasonable)
    320x200  ->  192,000 bytes  (still fine)
    800x600  -> 1,440,000 bytes (very large for embedded source data)
Pick something modest unless you have a real reason to go bigger.
"""
import sys
from PIL import Image

def main():
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(1)

    photo_path, out_path, width, height = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])

    img = Image.open(photo_path).convert("RGB")
    img = img.resize((width, height))

    with open(out_path, "w") as f:
        f.write("#pragma once\n")
        f.write("#include <cstdint>\n\n")
        f.write(f"// Auto-generated from {photo_path} by photo_to_bitmap.py.\n")
        f.write("// Do not hand-edit -- regenerate from the source photo instead.\n\n")
        f.write("namespace wallpaper {\n\n")
        f.write(f"constexpr std::uint32_t WIDTH = {width};\n")
        f.write(f"constexpr std::uint32_t HEIGHT = {height};\n\n")
        f.write("inline const std::uint8_t DATA[] = {\n")

        pixels = list(img.getdata())
        line = "    "
        for i, (r, g, b) in enumerate(pixels):
            line += f"{r},{g},{b},"
            if (i + 1) % 10 == 0:
                f.write(line + "\n")
                line = "    "
        if line.strip():
            f.write(line + "\n")

        f.write("};\n\n")
        f.write("} // namespace wallpaper\n")

    total_bytes = width * height * 3
    print(f"Wrote {out_path} ({total_bytes:,} bytes of pixel data)")

if __name__ == "__main__":
    main()
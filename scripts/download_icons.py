#!/usr/bin/env python3
"""Download Lucide icons as PNG with transparency for SDL3_image"""

import os
import urllib.request
import cairosvg

ICON_DIR = "assets/icons"
SIZE = 24
LUCIDE_BASE = "https://unpkg.com/lucide-static@latest/icons"

ICONS = [
    "chevron-left", "chevron-right", "chevron-up", "chevron-down",
    "arrow-left", "arrow-right", "menu", "x",
    "check", "plus", "minus", "edit", "trash", "copy", "search", "settings",
    "refresh-cw", "file", "folder", "folder-open", "download", "upload", "save",
    "eye", "eye-off", "info", "alert-circle", "check-circle", "x-circle",
    "user", "home", "star", "play", "pause", "external-link", "link", "calendar", "clock"
]

os.makedirs(ICON_DIR, exist_ok=True)

print(f"Downloading {len(ICONS)} Lucide icons...")

for icon in ICONS:
    svg_url = f"{LUCIDE_BASE}/{icon}.svg"
    png_file = f"{ICON_DIR}/{icon}.png"

    print(f"  Fetching {icon}...")

    # Download SVG
    with urllib.request.urlopen(svg_url) as response:
        svg_data = response.read().decode('utf-8')

    # Change stroke color to white for dark theme
    svg_data = svg_data.replace('stroke="currentColor"', 'stroke="white"')

    # Convert to PNG with transparency
    cairosvg.svg2png(bytestring=svg_data.encode(), write_to=png_file,
                     output_width=SIZE, output_height=SIZE)

# Rename refresh-cw to refresh
refresh_src = f"{ICON_DIR}/refresh-cw.png"
refresh_dst = f"{ICON_DIR}/refresh.png"
if os.path.exists(refresh_src):
    os.rename(refresh_src, refresh_dst)

print(f"Done! {len(ICONS)} icons saved to {ICON_DIR}")

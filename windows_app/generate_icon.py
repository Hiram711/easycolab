#!/usr/bin/env python3
"""
Generate Windows AppIcon.ico and AppIcon.png for EasyColab.exe packaging.
Uses PySide6 QPainter + Pillow to produce multi-size ICO (16, 24, 32, 48, 64, 128, 256).
"""
from __future__ import annotations

import io
import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import QBuffer, QIODevice
from PySide6.QtGui import QGuiApplication

# Ensure package import works whether run from repo root or windows_app/
REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from windows_app.theme import draw_easycolab_icon_pixmap  # noqa: E402


def generate_icons(out_dir: Path | None = None) -> tuple[Path, Path]:
    app = QGuiApplication.instance() or QGuiApplication([])
    _ = app
    assets_dir = out_dir or (Path(__file__).resolve().parent / "assets")
    assets_dir.mkdir(parents=True, exist_ok=True)

    png_path = assets_dir / "AppIcon.png"
    ico_path = assets_dir / "AppIcon.ico"

    pix_512 = draw_easycolab_icon_pixmap(512)
    pix_512.save(str(png_path), "PNG")

    try:
        from PIL import Image

        buf = QBuffer()
        buf.open(QIODevice.OpenModeFlag.WriteOnly)
        pix_512.save(buf, "PNG")
        raw_bytes = bytes(buf.data())
        buf.close()

        img = Image.open(io.BytesIO(raw_bytes)).convert("RGBA")
        img.save(
            str(ico_path),
            format="ICO",
            sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
        )
    except ImportError:
        # Fallback if Pillow is absent: Qt single-frame ICO/PNG
        pix_256 = draw_easycolab_icon_pixmap(256)
        pix_256.save(str(ico_path), "ICO")

    print(f"Generated {ico_path} and {png_path}")
    return ico_path, png_path


if __name__ == "__main__":
    generate_icons()

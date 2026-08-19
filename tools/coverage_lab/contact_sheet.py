"""Combine coverage-lab comparison plots into one shareable PNG."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps


_DEFAULT_SCENARIOS = (
    ("Empty rectangle - vertex 0", "empty_rectangle"),
    ("Large circle - vertex 0", "large_circle"),
    ("Large polygon - vertex 0", "large_polygon"),
    ("Edge obstacle - vertex 0", "edge_obstacle"),
    ("L field - vertex 0", "l_field"),
    ("Two obstacles - vertex 0", "two_obstacles"),
    ("Narrow corridor - vertex 0", "narrow_corridor"),
    ("Seeded stress - vertex 0", "seeded_stress"),
)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Combine coverage-lab plots into a 3 x 3 PNG.")
    parser.add_argument("--default-dir", type=Path, required=True)
    parser.add_argument("--alternate-polygon-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser


def _image_items(default_dir: Path, alternate_polygon_dir: Path) -> tuple[tuple[str, Path], ...]:
    default_items = tuple(
        (title, default_dir / scenario / "comparison.png")
        for title, scenario in _DEFAULT_SCENARIOS
    )
    alternate_item = (
        "Large polygon - vertex 1",
        alternate_polygon_dir / "large_polygon" / "comparison.png",
    )
    return (*default_items[:3], alternate_item, *default_items[3:])


def main() -> int:
    arguments = _parser().parse_args()
    items = _image_items(arguments.default_dir, arguments.alternate_polygon_dir)
    missing = [path for _, path in items if not path.is_file()]
    if missing:
        raise SystemExit(f"Missing comparison images: {', '.join(str(path) for path in missing)}")

    columns = 3
    rows = 3
    cell_width = 1200
    cell_height = 850
    title_height = 52
    page_title_height = 84
    margin = 24
    gap = 24
    canvas_width = 2 * margin + columns * cell_width + (columns - 1) * gap
    canvas_height = (
        page_title_height
        + 2 * margin
        + rows * cell_height
        + (rows - 1) * gap
    )
    canvas = Image.new("RGB", (canvas_width, canvas_height), color=(226, 232, 240))
    draw = ImageDraw.Draw(canvas)
    page_font = ImageFont.load_default(size=42)
    cell_font = ImageFont.load_default(size=30)
    draw.text(
        (canvas_width / 2, page_title_height / 2),
        "Agricultural Coverage Lab - 8 m swath - selected field edge",
        fill=(15, 23, 42),
        font=page_font,
        anchor="mm",
    )

    for item_index, (title, image_path) in enumerate(items):
        row, column = divmod(item_index, columns)
        left = margin + column * (cell_width + gap)
        top = page_title_height + margin + row * (cell_height + gap)
        canvas.paste((255, 255, 255), (left, top, left + cell_width, top + cell_height))
        draw.text(
            (left + cell_width / 2, top + title_height / 2),
            title,
            fill=(30, 41, 59),
            font=cell_font,
            anchor="mm",
        )
        with Image.open(image_path) as source:
            thumbnail = ImageOps.contain(
                source.convert("RGB"),
                (cell_width - 2 * margin, cell_height - title_height - 2 * margin),
                Image.Resampling.LANCZOS,
            )
        image_left = left + (cell_width - thumbnail.width) // 2
        image_top = top + title_height + (cell_height - title_height - thumbnail.height) // 2
        canvas.paste(thumbnail, (image_left, image_top))
        draw.rectangle(
            (left, top, left + cell_width - 1, top + cell_height - 1),
            outline=(148, 163, 184),
            width=2,
        )

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(arguments.output, format="PNG", optimize=True)
    print(f"contact_sheet={arguments.output}")
    print(f"image_count={len(items)}")
    print(f"resolution={canvas_width}x{canvas_height}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

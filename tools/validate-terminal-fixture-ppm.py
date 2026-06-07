#!/usr/bin/env python3
"""Validate semantic regions in a UIShell terminal fixture PPM artifact."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import tempfile


def read_token(data: bytes, offset: int) -> tuple[bytes, int]:
    while offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    if offset < len(data) and data[offset] == ord("#"):
        while offset < len(data) and data[offset] not in b"\r\n":
            offset += 1
        return read_token(data, offset)
    start = offset
    while offset < len(data) and data[offset] not in b" \t\r\n":
        offset += 1
    return data[start:offset], offset


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    magic, offset = read_token(data, 0)
    if magic != b"P6":
        raise ValueError(f"{path}: expected P6 magic, got {magic!r}")
    width_token, offset = read_token(data, offset)
    height_token, offset = read_token(data, offset)
    max_token, offset = read_token(data, offset)
    if max_token != b"255":
        raise ValueError(f"{path}: expected max value 255, got {max_token!r}")
    if offset >= len(data) or data[offset] not in b" \t\r\n":
        raise ValueError(f"{path}: missing whitespace after PPM header")
    while offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    width = int(width_token)
    height = int(height_token)
    if width <= 0 or height <= 0:
        raise ValueError(f"{path}: invalid dimensions {width}x{height}")
    pixels = data[offset:]
    expected_size = width * height * 3
    if len(pixels) != expected_size:
        raise ValueError(f"{path}: expected {expected_size} pixel bytes, got {len(pixels)}")
    return width, height, pixels


def write_ppm(path: Path, width: int, height: int, pixels: bytes) -> None:
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + pixels)


@dataclass(frozen=True)
class Rect:
    x0: int
    y0: int
    x1: int
    y1: int

    @property
    def area(self) -> int:
        return max(0, self.x1 - self.x0) * max(0, self.y1 - self.y0)


@dataclass(frozen=True)
class Bounds:
    has_pixels: bool
    rect: Rect


def cell_rect(cell_w: int, cell_h: int, col0: int, row0: int, col1: int, row1: int) -> Rect:
    return Rect(cell_w * col0, cell_h * row0, cell_w * col1, cell_h * row1)


def iter_pixels(width: int, height: int, pixels: bytes, rect: Rect):
    x0 = max(0, min(width, rect.x0))
    x1 = max(0, min(width, rect.x1))
    y0 = max(0, min(height, rect.y0))
    y1 = max(0, min(height, rect.y1))
    for y in range(y0, y1):
        row = y * width * 3
        for x in range(x0, x1):
            idx = row + x * 3
            yield x, y, pixels[idx], pixels[idx + 1], pixels[idx + 2]


def visible_count(width: int, height: int, pixels: bytes, rect: Rect) -> int:
    count = 0
    for _, _, r, g, b in iter_pixels(width, height, pixels, rect):
        if max(r, g, b) > 32:
            count += 1
    return count


def pixel_class_count(width: int, height: int, pixels: bytes, rect: Rect, cls: str) -> int:
    count = 0
    for _, _, r, g, b in iter_pixels(width, height, pixels, rect):
        mx = max(r, g, b)
        if cls == "dark":
            match = mx <= 32
        elif cls == "light":
            match = r >= 96 and g >= 96 and b >= 96
        elif cls == "green":
            match = g >= 96 and g > r + 16 and g > b + 16
        elif cls == "red":
            match = r >= 96 and r > g + 16 and r > b + 16
        elif cls == "chromatic":
            match = mx > 32 and mx > min(r, g, b) + 16
        else:
            raise ValueError(f"unknown pixel class {cls!r}")
        if match:
            count += 1
    return count


def visible_bounds(width: int, height: int, pixels: bytes, rect: Rect) -> Bounds:
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1
    for x, y, r, g, b in iter_pixels(width, height, pixels, rect):
        if max(r, g, b) > 32:
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x + 1)
            max_y = max(max_y, y + 1)
    if max_x < min_x or max_y < min_y:
        return Bounds(False, Rect(0, 0, 0, 0))
    return Bounds(True, Rect(min_x, min_y, max_x, max_y))


def bounds_match(reference: Bounds, candidate: Bounds, tolerance: int = 1) -> bool:
    return (
        reference.has_pixels
        and candidate.has_pixels
        and abs(reference.rect.y0 - candidate.rect.y0) <= tolerance
        and abs(reference.rect.y1 - candidate.rect.y1) <= tolerance
    )


def validate(path: Path, cols: int, rows: int) -> dict[str, int | str]:
    width, height, pixels = read_ppm(path)
    if width % cols != 0 or height % rows != 0:
        raise ValueError(f"{path}: dimensions {width}x{height} are not divisible by fixture grid {cols}x{rows}")
    cell_w = width // cols
    cell_h = height // rows

    whole = Rect(0, 0, width, height)
    title_rect = cell_rect(cell_w, cell_h, 0, 0, 80, 1)
    ascii_rect = cell_rect(cell_w, cell_h, 0, 1, 48, 2)
    empty_bg_rect = cell_rect(cell_w, cell_h, 64, 1, 78, 2)
    blocks_rect = cell_rect(cell_w, cell_h, 8, 9, 31, 10)
    red_text_rect = cell_rect(cell_w, cell_h, 0, 16, 22, 17)
    emoji_rect = cell_rect(cell_w, cell_h, 12, 14, 14, 15)
    align_rects = {
        "normal": cell_rect(cell_w, cell_h, 8, 4, 12, 5),
        "color": cell_rect(cell_w, cell_h, 16, 4, 20, 5),
        "bold": cell_rect(cell_w, cell_h, 24, 4, 28, 5),
        "faint": cell_rect(cell_w, cell_h, 32, 4, 36, 5),
    }

    visible = visible_count(width, height, pixels, whole)
    title_visible = visible_count(width, height, pixels, title_rect)
    ascii_light = pixel_class_count(width, height, pixels, ascii_rect, "light")
    empty_bg_dark = pixel_class_count(width, height, pixels, empty_bg_rect, "dark")
    blocks_green = pixel_class_count(width, height, pixels, blocks_rect, "green")
    red_text = pixel_class_count(width, height, pixels, red_text_rect, "red")
    emoji_visible = visible_count(width, height, pixels, emoji_rect)
    emoji_chromatic = pixel_class_count(width, height, pixels, emoji_rect, "chromatic")
    align_bounds = {name: visible_bounds(width, height, pixels, rect) for name, rect in align_rects.items()}
    align_ok = all(bounds_match(align_bounds["normal"], align_bounds[name]) for name in ("color", "bold", "faint"))

    failures: list[str] = []
    if visible < 128:
        failures.append(f"visible pixel count too low: {visible}")
    if title_visible < 16:
        failures.append(f"title visible pixel count too low: {title_visible}")
    if ascii_light < 32:
        failures.append(f"ASCII light pixel count too low: {ascii_light}")
    if empty_bg_dark < empty_bg_rect.area * 9 // 10:
        failures.append(f"empty background dark pixels too low: {empty_bg_dark}/{empty_bg_rect.area}")
    if blocks_green < 16:
        failures.append(f"green block pixels too low: {blocks_green}")
    if red_text < 16:
        failures.append(f"red text pixels too low: {red_text}")
    if emoji_visible < 4:
        failures.append(f"emoji visible pixels too low: {emoji_visible}")
    if emoji_chromatic < 4:
        failures.append(f"emoji chromatic pixels too low: {emoji_chromatic}")
    if not align_ok:
        rendered = ", ".join(f"{name}={bounds.rect}" for name, bounds in align_bounds.items())
        failures.append(f"styled alignment bounds mismatch: {rendered}")
    if failures:
        raise ValueError(f"{path}: semantic fixture validation failed: " + "; ".join(failures))

    return {
        "path": str(path),
        "width": width,
        "height": height,
        "cell_width": cell_w,
        "cell_height": cell_h,
        "visible": visible,
        "title_visible": title_visible,
        "ascii_light": ascii_light,
        "empty_bg_dark": empty_bg_dark,
        "blocks_green": blocks_green,
        "red_text": red_text,
        "emoji_visible": emoji_visible,
        "emoji_chromatic": emoji_chromatic,
    }


def fill_rect(pixels: bytearray, width: int, rect: Rect, color: tuple[int, int, int]) -> None:
    x0 = max(0, min(width, rect.x0))
    x1 = max(0, min(width, rect.x1))
    height = len(pixels) // (width * 3)
    y0 = max(0, min(height, rect.y0))
    y1 = max(0, min(height, rect.y1))
    r, g, b = color
    for y in range(y0, y1):
        row = y * width * 3
        for x in range(x0, x1):
            idx = row + x * 3
            pixels[idx] = r
            pixels[idx + 1] = g
            pixels[idx + 2] = b


def make_synthetic_fixture(*, misalign_bold: bool = False, nonchromatic_emoji: bool = False, blank: bool = False) -> tuple[int, int, bytes]:
    cols = 80
    rows = 24
    cell_w = 4
    cell_h = 4
    width = cols * cell_w
    height = rows * cell_h
    pixels = bytearray(width * height * 3)
    if not blank:
        fill_rect(pixels, width, cell_rect(cell_w, cell_h, 0, 0, 80, 1), (210, 210, 210))
        fill_rect(pixels, width, cell_rect(cell_w, cell_h, 0, 1, 48, 2), (220, 220, 220))
        fill_rect(pixels, width, cell_rect(cell_w, cell_h, 8, 9, 31, 10), (20, 180, 30))
        fill_rect(pixels, width, cell_rect(cell_w, cell_h, 0, 16, 22, 17), (190, 30, 25))
        fill_rect(pixels, width, cell_rect(cell_w, cell_h, 12, 14, 14, 15), (220, 220, 220) if nonchromatic_emoji else (220, 90, 20))

        align_specs = [
            (8, 1),
            (16, 1),
            (24, 3 if misalign_bold else 1),
            (32, 1),
        ]
        for col, yoff in align_specs:
            rect = cell_rect(cell_w, cell_h, col, 4, col + 4, 5)
            fill_rect(pixels, width, Rect(rect.x0, rect.y0 + yoff, rect.x1, rect.y0 + yoff + 1), (230, 230, 230))
    return width, height, bytes(pixels)


def expect_validation_failure(path: Path, label: str) -> None:
    try:
        validate(path, 80, 24)
    except ValueError:
        return
    raise AssertionError(f"validator self-test expected {label} to fail")


def run_self_tests() -> None:
    with tempfile.TemporaryDirectory(prefix="uishell-fixture-validator-") as temp_dir_name:
        temp_dir = Path(temp_dir_name)

        good_path = temp_dir / "good.ppm"
        width, height, pixels = make_synthetic_fixture()
        write_ppm(good_path, width, height, pixels)
        validate(good_path, 80, 24)

        blank_path = temp_dir / "blank.ppm"
        width, height, pixels = make_synthetic_fixture(blank=True)
        write_ppm(blank_path, width, height, pixels)
        expect_validation_failure(blank_path, "blank fixture")

        nonchromatic_path = temp_dir / "nonchromatic-emoji.ppm"
        width, height, pixels = make_synthetic_fixture(nonchromatic_emoji=True)
        write_ppm(nonchromatic_path, width, height, pixels)
        expect_validation_failure(nonchromatic_path, "nonchromatic emoji fixture")

        misaligned_path = temp_dir / "misaligned.ppm"
        width, height, pixels = make_synthetic_fixture(misalign_bold=True)
        write_ppm(misaligned_path, width, height, pixels)
        expect_validation_failure(misaligned_path, "misaligned styled row fixture")

        truncated_path = temp_dir / "truncated.ppm"
        truncated_path.write_bytes(good_path.read_bytes()[:-1])
        expect_validation_failure(truncated_path, "truncated fixture")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ppm", type=Path, nargs="*")
    parser.add_argument("--cols", type=int, default=80)
    parser.add_argument("--rows", type=int, default=24)
    parser.add_argument("--self-test", action="store_true", help="Run positive and negative validator self-tests.")
    args = parser.parse_args()

    if args.self_test:
        run_self_tests()
        print("fixture semantic validator self-test passed")

    if not args.ppm:
        return 0

    for path in args.ppm:
        metrics = validate(path, args.cols, args.rows)
        print(
            "fixture semantic validation passed: "
            f"{metrics['path']} "
            f"{metrics['width']}x{metrics['height']} "
            f"cell={metrics['cell_width']}x{metrics['cell_height']} "
            f"visible={metrics['visible']} "
            f"emoji_chroma={metrics['emoji_chromatic']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

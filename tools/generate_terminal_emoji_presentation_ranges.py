#!/usr/bin/env python3
import argparse
import re
import sys
import urllib.request
from pathlib import Path

DEFAULT_EMOJI_DATA_URL = "https://www.unicode.org/Public/UCD/latest/ucd/emoji/emoji-data.txt"
DEFAULT_EMOJI_SEQUENCES_URL = "https://www.unicode.org/Public/emoji/latest/emoji-sequences.txt"
DEFAULT_EMOJI_ZWJ_SEQUENCES_URL = "https://www.unicode.org/Public/emoji/latest/emoji-zwj-sequences.txt"
DEFAULT_SOURCE = "src/uishell/uishell_terminal_glyph.c"
FETCH_DATE = "2026-06-07"
SEQUENCE_PROPS = {
    "Basic_Emoji",
    "Emoji_Keycap_Sequence",
    "RGI_Emoji_Flag_Sequence",
    "RGI_Emoji_Tag_Sequence",
    "RGI_Emoji_Modifier_Sequence",
    "RGI_Emoji_ZWJ_Sequence",
}


def read_unicode_data(url, input_path):
    if input_path is not None:
        return Path(input_path).read_text(encoding="utf-8")
    with urllib.request.urlopen(url) as response:
        return response.read().decode("utf-8")


def parse_emoji_presentation_ranges(text):
    ranges = []
    for line in text.splitlines():
        if "; Emoji_Presentation" not in line:
            continue
        field = line.split(";", 1)[0].strip()
        if ".." in field:
            first, last = field.split("..", 1)
        else:
            first = last = field
        ranges.append((int(first, 16), int(last, 16)))
    return ranges


def parse_emoji_sequences(text):
    sequences = []
    for raw_line in text.splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line or ";" not in line:
            continue
        fields = [field.strip() for field in line.split(";")]
        if len(fields) < 2:
            continue
        code_field = fields[0]
        prop = fields[1]
        if prop not in SEQUENCE_PROPS:
            continue
        parts = code_field.split()
        if any(".." in part for part in parts):
            # Scalar Basic_Emoji ranges are covered by the Emoji_Presentation
            # scalar table and explicit variation-selector policy.
            if len(parts) == 1:
                continue
            raise RuntimeError("unexpected ranged emoji sequence: %s" % raw_line)
        codepoints = tuple(int(part, 16) for part in parts)
        if len(codepoints) > 1:
            sequences.append(codepoints)
    return sequences


def hash_from_codepoints(codepoints):
    result = 5381
    for codepoint in codepoints:
        result = (((result << 5) + result) + codepoint) & 0xFFFFFFFFFFFFFFFF
    return result


def unique_sorted_sequences(sequences):
    unique = sorted(set(sequences), key=lambda seq: (hash_from_codepoints(seq), len(seq), seq))
    return unique


def generated_range_table_text(ranges, url):
    lines = [
        "// Generated from Unicode UCD latest emoji-data.txt Emoji_Presentation property, fetched %s." % FETCH_DATE,
        "// Source: %s" % url,
        "read_only global UIShell_TerminalCodepointRange uishell_terminal_emoji_presentation_ranges[] =",
        "{",
    ]
    for first, last in ranges:
        lines.append("  {0x%04X, 0x%04X}," % (first, last))
    lines.append("};")
    return "\n".join(lines)


def generated_sequence_table_text(sequences, sequence_url, zwj_url):
    codepoint_values = []
    entries = []
    for sequence in sequences:
        offset = len(codepoint_values)
        codepoint_values.extend(sequence)
        entries.append((hash_from_codepoints(sequence), offset, len(sequence)))

    lines = [
        "// Generated from Unicode emoji sequence data, fetched %s." % FETCH_DATE,
        "// Sources: %s" % sequence_url,
        "//          %s" % zwj_url,
        "read_only global U32 uishell_terminal_emoji_sequence_codepoints[] =",
        "{",
    ]
    for idx in range(0, len(codepoint_values), 8):
        chunk = codepoint_values[idx:idx + 8]
        lines.append("  " + ", ".join("0x%04X" % codepoint for codepoint in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("read_only global UIShell_TerminalEmojiSequence uishell_terminal_emoji_sequences[] =")
    lines.append("{")
    for sequence_hash, offset, count in entries:
        lines.append("  {0x%016X, %u, %u}," % (sequence_hash, offset, count))
    lines.append("};")
    return "\n".join(lines)


def parse_source_ranges(source_text):
    pattern = re.compile(
        r"uishell_terminal_emoji_presentation_ranges\[\]\s*=\s*\{(?P<body>.*?)\};",
        re.DOTALL,
    )
    match = pattern.search(source_text)
    if match is None:
        raise RuntimeError("could not find uishell_terminal_emoji_presentation_ranges[] in source")
    result = []
    for first, last in re.findall(r"\{\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*\}", match.group("body")):
        result.append((int(first, 16), int(last, 16)))
    return result


def parse_source_sequence_tables(source_text):
    codepoint_match = re.search(
        r"uishell_terminal_emoji_sequence_codepoints\[\]\s*=\s*\{(?P<body>.*?)\};",
        source_text,
        re.DOTALL,
    )
    sequence_match = re.search(
        r"uishell_terminal_emoji_sequences\[\]\s*=\s*\{(?P<body>.*?)\};",
        source_text,
        re.DOTALL,
    )
    if codepoint_match is None or sequence_match is None:
        raise RuntimeError("could not find emoji sequence tables in source")

    codepoints = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)", codepoint_match.group("body"))]
    sequences = []
    for sequence_hash, offset, count in re.findall(
        r"\{\s*0x([0-9A-Fa-f]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*\}",
        sequence_match.group("body"),
    ):
        offset = int(offset)
        count = int(count)
        sequence = tuple(codepoints[offset:offset + count])
        actual_hash = hash_from_codepoints(sequence)
        if actual_hash != int(sequence_hash, 16):
            raise RuntimeError("emoji sequence hash mismatch at offset %d" % offset)
        sequences.append(sequence)
    return sequences


def replace_block(source_text, pattern, replacement):
    match = re.search(pattern, source_text, re.DOTALL)
    if match is None:
        raise RuntimeError("could not find generated block to replace")
    return source_text[:match.start()] + replacement + source_text[match.end():]


def main():
    parser = argparse.ArgumentParser(
        description="Generate, update, or validate the UIShell terminal emoji presentation tables."
    )
    parser.add_argument("--url", default=DEFAULT_EMOJI_DATA_URL, help="Unicode emoji-data.txt URL to fetch")
    parser.add_argument("--emoji-sequences-url", default=DEFAULT_EMOJI_SEQUENCES_URL, help="Unicode emoji-sequences.txt URL to fetch")
    parser.add_argument("--emoji-zwj-sequences-url", default=DEFAULT_EMOJI_ZWJ_SEQUENCES_URL, help="Unicode emoji-zwj-sequences.txt URL to fetch")
    parser.add_argument("--input", help="Read emoji-data.txt from this path instead of fetching --url")
    parser.add_argument("--emoji-sequences-input", help="Read emoji-sequences.txt from this path instead of fetching --emoji-sequences-url")
    parser.add_argument("--emoji-zwj-sequences-input", help="Read emoji-zwj-sequences.txt from this path instead of fetching --emoji-zwj-sequences-url")
    parser.add_argument("--source", default=DEFAULT_SOURCE, help="UIShell source file to validate in --check mode")
    parser.add_argument("--check", action="store_true", help="Compare generated ranges with the embedded source table")
    parser.add_argument("--write", action="store_true", help="Update generated tables in --source")
    args = parser.parse_args()

    data = read_unicode_data(args.url, args.input)
    expected = parse_emoji_presentation_ranges(data)
    if not expected:
        print("no Emoji_Presentation ranges found", file=sys.stderr)
        return 1
    sequence_data = read_unicode_data(args.emoji_sequences_url, args.emoji_sequences_input)
    zwj_sequence_data = read_unicode_data(args.emoji_zwj_sequences_url, args.emoji_zwj_sequences_input)
    expected_sequences = unique_sorted_sequences(parse_emoji_sequences(sequence_data) + parse_emoji_sequences(zwj_sequence_data))
    if not expected_sequences:
        print("no emoji sequences found", file=sys.stderr)
        return 1

    range_table = generated_range_table_text(expected, args.url)
    sequence_table = generated_sequence_table_text(expected_sequences, args.emoji_sequences_url, args.emoji_zwj_sequences_url)

    if args.write:
        source_path = Path(args.source)
        source_text = source_path.read_text(encoding="utf-8")
        source_text = replace_block(
            source_text,
            r"// Generated from Unicode UCD latest emoji-data\.txt Emoji_Presentation property.*?uishell_terminal_emoji_presentation_ranges\[\]\s*=\s*\{.*?\};",
            range_table,
        )
        sequence_pattern = r"// Generated from Unicode emoji sequence data.*?uishell_terminal_emoji_sequences\[\]\s*=\s*\{.*?\};"
        if re.search(sequence_pattern, source_text, re.DOTALL):
            source_text = replace_block(source_text, sequence_pattern, sequence_table)
        else:
            source_text = source_text.replace(range_table, range_table + "\n\n" + sequence_table, 1)
        source_path.write_text(source_text, encoding="utf-8")
        print(
            "updated %s with %d Emoji_Presentation ranges and %d emoji sequences"
            % (args.source, len(expected), len(expected_sequences))
        )
        return 0

    if args.check:
        source_text = Path(args.source).read_text(encoding="utf-8")
        actual = parse_source_ranges(source_text)
        if actual != expected:
            print(
                "embedded Emoji_Presentation table is out of date: expected %d ranges, found %d ranges"
                % (len(expected), len(actual)),
                file=sys.stderr,
            )
            mismatch_count = min(len(expected), len(actual))
            for idx in range(mismatch_count):
                if expected[idx] != actual[idx]:
                    print(
                        "first mismatch at index %d: expected {0x%04X, 0x%04X}, found {0x%04X, 0x%04X}"
                        % (idx, expected[idx][0], expected[idx][1], actual[idx][0], actual[idx][1]),
                        file=sys.stderr,
                    )
                    break
            return 1
        actual_sequences = parse_source_sequence_tables(source_text)
        if actual_sequences != expected_sequences:
            print(
                "embedded emoji sequence table is out of date: expected %d sequences, found %d sequences"
                % (len(expected_sequences), len(actual_sequences)),
                file=sys.stderr,
            )
            mismatch_count = min(len(expected_sequences), len(actual_sequences))
            for idx in range(mismatch_count):
                if expected_sequences[idx] != actual_sequences[idx]:
                    print(
                        "first sequence mismatch at index %d: expected %s, found %s"
                        % (
                            idx,
                            " ".join("0x%X" % cp for cp in expected_sequences[idx]),
                            " ".join("0x%X" % cp for cp in actual_sequences[idx]),
                        ),
                        file=sys.stderr,
                    )
                    break
            return 1
        print(
            "Emoji_Presentation table matches %d ranges from %s; emoji sequence table matches %d sequences from %s and %s"
            % (
                len(expected),
                args.input or args.url,
                len(expected_sequences),
                args.emoji_sequences_input or args.emoji_sequences_url,
                args.emoji_zwj_sequences_input or args.emoji_zwj_sequences_url,
            )
        )
        return 0

    print(range_table)
    print()
    print(sequence_table)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

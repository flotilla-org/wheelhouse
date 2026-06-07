Terminal Unicode Data
=====================

These files are checked-in inputs for terminal emoji presentation table
generation. They keep the normal verification path independent of network
access and independent of Unicode's `latest` URLs changing over time.

Current snapshot:

- `emoji-data.txt`: Unicode Emoji 17.0
- `emoji-sequences.txt`: Unicode Emoji 17.0
- `emoji-zwj-sequences.txt`: Unicode Emoji 17.0

To validate the embedded UIShell tables against these snapshots:

```sh
python3 tools/generate_terminal_emoji_presentation_ranges.py --check \
  --input data/unicode/emoji-data.txt \
  --emoji-sequences-input data/unicode/emoji-sequences.txt \
  --emoji-zwj-sequences-input data/unicode/emoji-zwj-sequences.txt
```

To update to a newer Unicode release, refresh the three snapshot files, then
run the same command with `--write` and review the generated source diff.

The Unicode data files are from https://www.unicode.org/Public/ and are subject
to the Unicode terms of use noted in each file.

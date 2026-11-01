# Claude Code transcripts

Synthetic Claude Code sessions for `tools/claude-to-events.py`, one case per
directory, laid out as Claude Code lays out `~/.claude/projects/<dir>/`. The
repository is public: never add a real transcript here.

`<case>.events.jsonl` is each case's golden output. After a deliberate change
to the translator, regenerate them and review the diff:

```sh
python3 tools/test-claude-to-events.py --update
```

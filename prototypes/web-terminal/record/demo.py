#!/usr/bin/env python3
"""PROTOTYPE - throwaway. Terminal output exercising the renderer: colour
(16, 256, truecolor), attributes, Unicode (CJK, emoji, combining, box
drawing), a fast scroll, and a full-screen animated redraw in the
alternate screen."""
import sys, time, math, os

w = sys.stdout.write
def flush(): sys.stdout.flush()

w("\x1b[1mPalette\x1b[0m\n")
for i in range(16):
    w(f"\x1b[48;5;{i}m  {i:2} \x1b[0m")
w("\n")
for i in range(16, 232):
    w(f"\x1b[48;5;{i}m \x1b[0m")
    if (i - 15) % 72 == 0: w("\n")
w("\n\x1b[1mTruecolor\x1b[0m\n")
for row in range(4):
    for c in range(110):
        r = int(127 + 127 * math.sin(c / 9 + row)); g = int(127 + 127 * math.sin(c / 11 + 2)); b = int(127 + 127 * math.sin(c / 7 + 4))
        w(f"\x1b[48;2;{r};{g};{b}m ")
    w("\x1b[0m\n")
w("\x1b[1mbold\x1b[0m \x1b[2mdim\x1b[0m \x1b[3mitalic\x1b[0m \x1b[4munderline\x1b[0m \x1b[4:3mcurly\x1b[0m \x1b[7minverse\x1b[0m \x1b[9mstrike\x1b[0m "
  "\x1b[38;5;208morange\x1b[0m \x1b]8;;https://example.com\x1b\\a link\x1b]8;;\x1b\\\n")
w("Unicode: 日本語のテキスト 한국어 中文 — emoji 🚀🔥✨👩‍💻 combining é ä — arrows ←↑→↓ math ∑∫√∞\n")
w("┌──────────┬──────────┐\n│ box      │ drawing  │\n├──────────┼──────────┤\n│ ░▒▓█     │ ▁▂▃▄▅▆▇█ │\n└──────────┴──────────┘\n")
flush(); time.sleep(1.0)

# A fast scroll: a few thousand coloured log lines, written in bursts.
levels = [("INFO", 32), ("WARN", 33), ("ERROR", 31), ("DEBUG", 36)]
for i in range(4000):
    lv, col = levels[(i * 7) % 4 if i % 13 else 2]
    w(f"\x1b[90m2026-10-23T10:{i // 60 % 60:02}:{i % 60:02}\x1b[0m \x1b[{col}m{lv:5}\x1b[0m worker-{i % 9} processed item {i} in {(i * 37) % 900} ms {'█' * (i % 30)}\n")
    if i % 40 == 0:
        flush(); time.sleep(0.004)
flush(); time.sleep(0.8)

# Full-screen animation in the alternate screen, with synchronized output.
cols, rows = os.get_terminal_size()
w("\x1b[?1049h\x1b[?25l")
start = time.time()
frame = 0
while time.time() - start < 6:
    t = time.time() - start
    w("\x1b[?2026h\x1b[H")
    w(f"\x1b[1;37;44m{' top-like view  frame ' + str(frame):<{cols}}\x1b[0m\n")
    for r in range(1, rows - 1):
        if r < 12:
            cpu = (math.sin(t * 2 + r) + 1) / 2
            bar = int(cpu * 40)
            w(f" proc-{r:02} \x1b[32m{'|' * bar}\x1b[90m{'·' * (40 - bar)}\x1b[0m {cpu * 100:5.1f}%".ljust(cols)[:cols + 20] + "\x1b[K\n")
        else:
            line = []
            for c in range(cols):
                v = math.sin(c / 6 + t * 3) + math.cos(r / 3 - t * 2)
                h = int((v + 2) * 63)
                line.append(f"\x1b[48;2;{h};{40 + h // 3};{255 - h}m ")
            w("".join(line) + "\x1b[0m\n")
    w(f"\x1b[7m q quit  ←→ select  時間 {t:4.1f}s \x1b[0m\x1b[K")
    w("\x1b[?2026l")
    flush()
    frame += 1
    time.sleep(1 / 30)
w("\x1b[?25h\x1b[?1049l")
w("back on the normal screen\n")
flush()

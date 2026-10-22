#!/usr/bin/env python3
"""PROTOTYPE - throwaway. Run a command in a pty at a fixed size and write
an asciicast v3 recording (the format Cleat records)."""
import json, os, pty, select, struct, fcntl, termios, sys, time

def main(out, cols, rows, argv):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"; os.environ["COLUMNS"] = str(cols); os.environ["LINES"] = str(rows)
        os.execvp(argv[0], argv)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    with open(out, "w") as f:
        f.write(json.dumps({"version": 3, "term": {"cols": cols, "rows": rows, "type": "xterm-256color"}}) + "\n")
        last = start = time.monotonic()
        while True:
            r, _, _ = select.select([fd], [], [], 0.5)
            if not r:
                continue
            try:
                data = os.read(fd, 65536)
            except OSError:
                break
            if not data:
                break
            now = time.monotonic()
            f.write(json.dumps([round(now - last, 6), "o", data.decode("utf-8", "replace")]) + "\n")
            last = now
    os.waitpid(pid, 0)

if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4:])

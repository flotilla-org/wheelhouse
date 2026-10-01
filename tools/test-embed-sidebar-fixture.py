#!/usr/bin/env python3
"""The embedded sidebar must be identical across checkout locales and newlines."""
import runpy
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class FixtureGenerationTests(unittest.TestCase):
    def test_windows_locale_and_crlf_match_committed_fixture(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(ROOT / 'data/sidebar', root / 'data/sidebar')
            (root / 'tools').mkdir()
            shutil.copy(ROOT / 'tools/embed-sidebar-fixture.py', root / 'tools')
            output = root / 'src/uishell/generated/sidebar_fixture.h'
            output.parent.mkdir(parents=True)
            for source in (root / 'data/sidebar').iterdir():
                if source.is_file():
                    source.write_bytes(source.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
            read_text = Path.read_text

            def windows_read(path, *args, **kwargs):
                kwargs.setdefault('encoding', 'cp1252')
                return read_text(path, *args, **kwargs)

            with patch.object(Path, 'read_text', windows_read):
                runpy.run_path(str(root / 'tools/embed-sidebar-fixture.py'))
            self.assertEqual(output.read_bytes(), (ROOT / 'src/uishell/generated/sidebar_fixture.h').read_bytes())


if __name__ == '__main__':
    unittest.main()

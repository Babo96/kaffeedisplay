"""Tests for the standby image generator's build-output handling."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("generate_standby_cup.py")
SPEC = importlib.util.spec_from_file_location("generate_standby_cup", SCRIPT)
GENERATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(GENERATOR)


class GeneratorOutputTests(unittest.TestCase):
    def test_writes_generated_header_to_requested_build_path(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "generated" / "standby.hpp"
            with (
                patch.object(GENERATOR, "generate", return_value="// fixture\n"),
                patch.object(sys, "argv", [str(SCRIPT), "--output", str(output)]),
            ):
                GENERATOR.main()

            self.assertEqual(output.read_text(), "// fixture\n")


if __name__ == "__main__":
    unittest.main()

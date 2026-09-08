# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Guard the complete byte-faithful Foundry language utility registration producer."""
import hashlib
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LanguageUtilityProducerTest(unittest.TestCase):
    def test_exact_pinned_registration_surface(self):
        # Producer: git show c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6:
        # modules/foundry_script/fs_utility_functions.cpp, lines 501-511 (all registrations).
        with (ROOT / "tests/fixtures/foundry_utility_registrations.txt").open("rb") as stream:
            fixture = stream.read()
        self.assertEqual(hashlib.sha256(fixture).hexdigest(),
                         "34dcafc39a87152378fec513f919c6d94d072b4ca7dbb67dda75c9759ffeaced")
        with (ROOT / "src/bs_utility_functions.cpp").open("rb") as stream:
            lines = stream.read().splitlines(keepends=True)
        registrations = b"".join(line for line in lines if line.startswith(b"\tREGISTER_FUNC("))
        self.assertEqual(registrations, fixture)
        self.assertEqual(len(fixture.splitlines()), 11)
        # The port supports only the producer's empty defaults; never silently ignore a new one.
        for registration in fixture.splitlines():
            with self.subTest(registration=registration):
                self.assertRegex(registration, rb"varray\(\s*\)\);$")


if __name__ == "__main__":
    unittest.main()

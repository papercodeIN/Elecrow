#!/usr/bin/env python3
"""Static checks for the Bluetooth Stream Deck action map.

Verifies that every tile key in src/main.cpp has a matching handler in
send_action(), that the tile count matches the static_assert, and that there
are no orphan handlers. No device or firmware build is required.
"""

import os
import re
import sys
import unittest

MAIN_CPP = os.path.join(
    os.path.dirname(__file__), "..", "src", "main.cpp"
)


def read_main():
    with open(MAIN_CPP, encoding="utf-8") as f:
        return f.read()


def parse_tile_keys(src):
    """Extract the key field from each entry in the TILES[] array."""
    match = re.search(r"static\s+const\s+Tile\s+TILES\[\s*\]\s*=\s*\{(.+?)\};",
                      src, re.DOTALL)
    assert match, "Could not find TILES[] array definition"
    entries = re.findall(r'\{\s*"([^"]+)"', match.group(1))
    assert entries, "No tile entries found in TILES[] array"
    return entries


def parse_send_action_keys(src):
    """Extract the key strings compared against in send_action()."""
    match = re.search(r"static\s+ActionStatus\s+send_action\(.+?\)\s*\{(.+?)\n\}",
                      src, re.DOTALL)
    assert match, "Could not find send_action() function"
    body = match.group(1)
    return set(re.findall(r'strcmp\(key,\s*"([^"]+)"', body))


def parse_static_assert(src):
    """Verify the static_assert for TILE_COUNT == 16 exists."""
    return bool(re.search(r"static_assert\(TILE_COUNT\s*==\s*16", src))


class TestActionMap(unittest.TestCase):

    def setUp(self):
        self.src = read_main()
        self.tile_keys = parse_tile_keys(self.src)
        self.action_keys = parse_send_action_keys(self.src)

    def test_tile_count_is_16(self):
        self.assertEqual(len(self.tile_keys), 16)

    def test_no_duplicate_tile_keys(self):
        self.assertEqual(len(self.tile_keys), len(set(self.tile_keys)))

    def test_static_assert_for_16(self):
        self.assertTrue(parse_static_assert(self.src),
                        "static_assert(TILE_COUNT == 16) not found")

    def test_every_tile_has_handler(self):
        for key in self.tile_keys:
            self.assertIn(key, self.action_keys,
                          f"Tile '{key}' has no handler in send_action()")

    def test_no_orphan_handlers(self):
        """Every strcmp in send_action must correspond to a defined tile."""
        for key in self.action_keys:
            self.assertIn(key, self.tile_keys,
                          f"Handler for '{key}' has no matching TILES entry")


if __name__ == "__main__":
    sys.exit(unittest.main(verbosity=2))

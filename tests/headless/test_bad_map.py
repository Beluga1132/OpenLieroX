"""Headless test: a level that fails to load must not crash the server.

Starting a game on a corrupt level while the server's local client
was still connecting made the failed start reset that client's channel,
and CClient::SendPackets then dereferenced the null channel
on the next frame (SIGSEGV; found by fuzzing level files).
"""

import os

from harness import CONTROL_DIR, GAMEDIR, OlxInstance

BAD_MAP_CONTROL = os.path.join(CONTROL_DIR, "bad_map_control.py")


def _write_corrupt_level(home):
    """Copy a shipped level and corrupt its compressed image data."""
    with open(os.path.join(GAMEDIR, "levels", "Dirt Level.lxl"), "rb") as fh:
        data = bytearray(fh.read())
    # The zlib stream starts after the 156-byte header; break it.
    for i in range(160, 200):
        data[i] ^= 0xFF
    levels = os.path.join(home, ".OpenLieroX", "levels")
    os.makedirs(levels)
    with open(os.path.join(levels, "broken.lxl"), "wb") as fh:
        fh.write(data)


def test_corrupt_level_does_not_crash_server(olx_binary, tmp_path):
    home = str(tmp_path / "server")
    _write_corrupt_level(home)
    server = OlxInstance(
        olx_binary, "server", home, BAD_MAP_CONTROL,
        env={"OLX_PORT": "23400", "OLX_MAP": "broken.lxl"},
    ).start()
    try:
        assert server.wait_for("SERVER_STARTGAME", timeout=30), server.read_log()
        assert server.wait_for("Could not load the level", timeout=20), (
            "the corrupt level was not rejected, "
            "so the failure path was not exercised:\n" + server.read_log()
        )
        assert server.wait_for("SERVER_ALIVE", timeout=20), (
            "server died after a level failed to load:\n" + server.read_log()
        )
        assert "SIGSEGV" not in server.read_log(), server.read_log()
    finally:
        server.stop()

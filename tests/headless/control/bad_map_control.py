#!/usr/bin/env python3
"""Dedicated-server control script for the corrupt-level test.

Opens a lobby and at once starts a game on the level in ``OLX_MAP``,
while the server's local client is still connecting;
that is when a failed level load left the client without a channel.
Emits ``SERVER_STARTGAME``, then ``SERVER_ALIVE`` once a few frames later.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _olx_pipe import command, emit  # noqa: E402


def main():
    for key, value in (
        ("GameOptions.Network.RegisterServer", 0),
        ("GameOptions.Network.UseIpToCountry", 0),
        ("GameOptions.GameInfo.AllowEmptyGames", 1),
        ("GameOptions.GameInfo.ImmediateStart", 1),
        ("GameOptions.GameInfo.ModName", "Classic"),
    ):
        command('setvar %s "%s"' % (key, value))
    command("startlobby " + os.environ.get("OLX_PORT", "23400"))
    command('setvar GameOptions.GameInfo.LevelName "%s"' % os.environ["OLX_MAP"])
    command("startGame")
    emit("SERVER_STARTGAME")
    time.sleep(3)
    emit("SERVER_ALIVE")
    command("quit")


main()

"""Headless test: /login is rate limited per address.

The admin password travels as plain chat,
so without a limit a client could guess it at network speed.
After 5 failed attempts from one address,
further attempts are refused for a while,
even with the right password.
"""

import re

PASSWORD = "s3cret"


def test_login_locks_out_after_repeated_failures(network_game):
    """A client that fails /login 5 times can't log in, even with the password."""
    server = network_game.start_server(OLX_SERVER_PASSWORD=PASSWORD)
    assert server.wait_for("SERVER_LOBBY", timeout=30), server.read_log()

    lines = (
        # Control: the right password works,
        # so the chat command path itself is exercised.
        ["/login " + PASSWORD]
        + ["/login wrong%d" % i for i in range(5)]
        # Locked out now, so the right password must be refused.
        + ["/login " + PASSWORD]
    )
    c1 = network_game.add_client("c1", env={"OLX_CHAT_LINES": "\n".join(lines)})
    assert c1.wait_for("CLIENT[c1] CHAT_DONE", timeout=60), c1.read_log()

    assert server.wait_for("login refused, too many failures", timeout=20), (
        "the locked-out login was not refused:\n" + server.read_log()
    )
    log = server.read_log()
    assert "failed login 5/5" in log, log
    logins = re.findall(r"worm \d+:.* logged in$", log, re.MULTILINE)
    assert len(logins) == 1, (
        "expected only the first (control) login to succeed:\n" + log
    )

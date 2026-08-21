---
name: cpp-dev
description: Use BEFORE modifying any KTPAmxxCurl C++ source (asio/libcurl bridge module) — the exception-boundary rules, socket-lifecycle ownership, handle-reuse contract, and build/verify workflow. Also use when planning a change, to know which invariants it touches.
---

# KTPAmxxCurl Development

This is the non-blocking HTTP module (asio + libcurl) loaded by KTPAMXX in
**extension mode** (no Metamod) on a 24-instance production fleet. It is the
transport for Discord embeds, HLStatsX, and the HLTV API. The rules below each
encode a real production incident (several the CHI1 shutdown-crash family);
follow them even when they feel paranoid.

Unlike the bigger forks in this stack, this module is small enough that the
**whole tree is in scope** — including shapes inherited from upstream
AmxxCurl (branch ladders, vestigial guards). Don't apply a fork-delta-only
filter here.

## Hard safety rules
- **NEVER restart game servers** without the operator's explicit permission in
  the current conversation.
- Binaries deploy as `.new` files (`modules/amxxcurl_ktp_i386.so.new`) and swap
  at the 03:00 ET nightly restart.
- Vendored third-party dirs (`deps/asio`, `deps/cares`, `deps/curl`,
  `deps/openssl`, `deps/zlib`, `deps/halflife`, `deps/metamod`) are off-limits —
  never edit vendored code to fix a KTP bug; wrap/guard at the call site.
- Commit source at or before ship. Version, CHANGELOG, and shipped binary must
  describe the same code.

## No-throw discipline at every C boundary (the headline rule)
Exceptions are **enabled** in this module (`-std=c++14`, no `-fno-exceptions`)
— but KTPAMXX/ReHLDS on the other side of every callback boundary is not, and
libcurl's C frames can't unwind through a C++ exception either way. Any throw
that reaches one of these boundaries is `std::terminate()` — a mid-match
SIGABRT, not a caught error:
- **The four libcurl C-callback trampolines** (`CurlSocketCallbackStatic`,
  `CurlTimerCallbackStatic`, `CurlOpenSocketCallbackStatic`,
  `CurlCloseSocketCallbackStatic`). Only the socket one has a try/catch today
  (1.3.11). Treat that guard as the pattern, not a one-off: any new or edited
  libcurl callback needs the same wrapper, because the callback bodies use
  throwing asio overloads (`timer.cancel()`, `expires_from_now()`,
  `release()`) and allocating calls (`async_wait`, `emplace`) that can throw
  under memory pressure.
- **`AsioPoller::Poll()`**, the per-frame entry point
  (`MF_RegModuleFrameFunc(CurlFrameCallback)`). It currently calls the
  throwing `io_context::poll()` overload with nothing between asio handler
  bodies and the engine frame loop — any exception from a handler (including
  one rethrown out of a poorly-guarded C callback above) unwinds straight into
  ReHLDS. Any change here should move to the non-throwing overload or wrap the
  call.
- **The Pawn-native boundary** (`curl_easy_setopt`, `curl_easy_getinfo`,
  and anything else that calls into `SetupAmxCallback` or similar). These
  natives catch only two named exception types — a `std::runtime_error` or
  anything else escapes into `-fno-exceptions` KTPAMXX core. If you add a
  `throw` anywhere reachable from a native, check that native's catch list;
  don't assume the general C-boundary pattern covers it.

## Socket lifecycle: libcurl owns the fd, not `socket_map_`
`CURL_POLL_REMOVE` means "stop polling this fd," not "the fd is dead." libcurl
keeps reused connections alive in its own keep-alive cache and only tells you
an fd is really gone via `CURLOPT_CLOSESOCKETFUNCTION`.
Closing a socket on `CURL_POLL_REMOVE` (the pre-1.3.14 bug) silently kills
libcurl's connection cache and, worse, can hand a recycled fd number to the
*next* connection — delayed, unattributable crossfire.
- On `CURL_POLL_REMOVE` for a curl-owned socket: cancel pending asio waits,
  **leave the fd open** in `socket_map_`. `CurlCloseSocketCallback` is the
  only place a curl-owned fd is ever closed.
- c-ares sockets are different — c-ares owns those fds and never issues a
  close callback, so they still get `release()`d and erased immediately.
  Don't unify this path with the curl-owned path.
- A `[CURL] WARNING stale socket_map_ entry` in the logs means the contract
  above was violated somewhere (a missed close callback) — **investigate
  immediately**, it has never fired in the field. Do not silence it.
- **Clean logs are not evidence this contract is being honoured.** libcurl
  checks a cached connection for liveness before reusing it, and when it finds
  an fd that was closed behind its back it discards the connection and re-dials
  in silence — no error, no socket callback. Surfacing a real `EBADF` needs the
  racy variant, where the fd number has already been reissued to a *concurrent*
  transfer, and KTP's sequential match traffic almost never forces that. The
  pre-1.3.14 bug therefore ran for months looking healthy. Reason about this
  path from the code, never from the absence of a symptom.
- Member order matters: `asio_poller_` must stay declared before
  `curl_manager_` in the controller — C++ destructs in reverse declaration
  order, and kept-open sockets need a live `io_context` to destruct against.

## Handle-reuse and `AddCurl` failure are both open contract gaps
- `curl.inc` documents that a handle may be reused across sequential
  `curl_easy_perform` calls. Nothing on the reperform path clears the
  auto-buffered `response_body_` — a second transfer's data appends onto the
  first's. `CurlCallbackAmx::ClearResponseBody()` already exists for this and
  is currently unused; if you touch `Perform` or the reuse path, wire it in
  (call it before arming the transfer, not in the completion callback — the
  body must survive into the callback).
- `CurlMulti::AddCurl`'s `curl_multi_add_handle` failure path returns `void`,
  logs only via `DEBUG_LOG` (compiled out in release), and never resets the
  caller's `is_transfer_in_progress_` flag — a permanent zombie handle that
  the shutdown drain loop burns its full timeout waiting on. If you touch
  `AddCurl` or `Perform`, propagate the failure (return a `CURLMcode`/bool)
  instead of adding a fourth silent-failure site next to the existing ones.

## Threading model
Single-threaded on the game thread. There is no `std::thread`/`std::mutex`
anywhere — `MF_RegModuleFrameFunc(CurlFrameCallback)` runs
`AsioPoller::Poll()` + `SweepDeferredCleanups()` every frame, and every
curl/asio callback (socket, timer, completion) executes inline on that call.
Don't introduce a second thread without re-deriving every one of the
guards above; they all assume single-threaded execution.

## Teardown ordering
Extension mode historically never called `OnAmxxDetach` at all (Metamod-only
path) — the real teardown was an `atexit`/`__cxa_atexit` guard flag
(1.3.12/1.3.13). Since KTPAMXX 2.7.21 + ReHLDS `.928`'s
`KTP_ExtensionShutdown` callback, `OnAmxxDetach` is a real, frequently
exercised path again. Any teardown change must preserve the ordering
invariant documented in `amx_curl_controller_class.h`: `curl_multi_cleanup`
must run (or the multi must be torn down) **before** `curl_global_cleanup()` —
never call libcurl API after global cleanup has run.

## Never run a destructive simulation inside the working tree
Verifying a fix often means simulating the failure — writing a fake `build.sh`, a
fake artifact, a fake staging dir. Do it in a **verified** scratch dir, never in
the repo:

```bash
T="$(mktemp -d)" || exit 1
[ -n "$T" ] && [ -d "$T" ] || exit 1   # verify BEFORE you cd — this is the whole rule
cd "$T" || exit 1
```

`cd "$T"` with an empty `$T` **silently succeeds and leaves you where you were** —
in the repo. A simulation that then writes `build.sh` overwrites the real one. On
2026-07-16 exactly that truncated a tracked 60-line upstream file to 2 lines and
dropped a junk `.so` into `build/`, where a `find | head -1` could have staged it.
It was caught only because `git status` showed a modification nobody made.

So: verify the scratch dir before `cd`, and **run `git status` after any test that
touches the filesystem** — an unexpected change is the tell. Prefer copying inputs
out to the scratch dir over running tools "in place".

## Workflow
1. **Build**: `wsl bash -c "cd '/mnt/n/Nein_/KTP Git Projects/KTPAmxxCurl' && bash build_linux.sh"`
   (CMake → `build/amxxcurl_ktp_i386.so`, auto-stages to the KTP DoD Server
   test tree).
2. **Review**: `ktp-code-review` agent before staging anything nontrivial —
   this module's incident history is almost entirely exception-boundary and
   socket-lifecycle bugs the reviewer is tuned to catch.
3. **Test**: prefer a tier-2 red→green contract test for crash-class or
   lifecycle fixes (see PR #40's shape — a test that fails on the old code
   and passes on the fix, not just a smoke run).
   ⚠️ **A test that never exercises connection reuse cannot see the whole
   class of bug this module is prone to, and it passes either way.** Two
   properties of the mock decide that, and both were wrong once: it must
   speak **HTTP/1.1**, because against an HTTP/1.0 responder libcurl never
   caches a connection at all — the original socket-lifecycle bug and any
   regression from its fix both ran green; and it must **drain the request
   body on every path, including the ones it rejects**, because unread bytes
   left in a kept-alive stream get parsed as the next request line, and the
   mock answers its own legitimate reuse with a 501 and `Connection: close`.
   Assert on reuse happening, not just on the response.
4. **Fleet stage**: `.new` via paramiko to all 24 active instances;
   md5-verify every staged file.
5. **Post-activation verify**: 24/24 on the new md5, no leftover `.new`, zero
   new cores — check `/tmp`, not the game trees
   (`find /tmp -maxdepth 1 -name 'core.*' -mtime -1`; a game-tree search only
   ever matches `core.so`/`core.ini`/`core.wav` and looks clean either way).
   Verify by md5, never by console banner.

## Versioning
Version string lives in `src/sdk/moduleconfig.h` (`MODULE_VERSION`). Bump it
for every shipped change, write the CHANGELOG.md entry with what/why + the
md5 of the shipped binary, and update README's version header.

## Docs check (not just the version line)
If the change touched a build path, install path, or a diagnostic an operator is
expected to act on, **verify the README still works from a clean clone's
perspective — not from this tree**, and grep the whole stack for any old path
string. The 2026-07-19 audit found "Building from Source" still documenting the
Premake5 workflow this repo replaced with CMake in 1.3.7 — plausible enough to
attempt, since a vestigial `premake5.lua` is still tracked. New `[CURL] WARNING`
lines that mean "investigate immediately" belong in the README, not only in this
skill. Full checklist: root `CLAUDE.md` → "Module / Engine Release Checklist".

## Comments
Short, why-not-what, no ticket/finding IDs. Never delete a tripwire fact
("never fired in the field," "must not touch curl again") while trimming a
comment — shorten the prose, keep the warning.

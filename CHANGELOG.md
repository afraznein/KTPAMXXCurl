# Changelog

All notable changes to KTP CURL AMXX will be documented in this file.

## [1.3.17-ktp] - 2026-08-10

Shipped binary `amxxcurl_ktp_i386.so` md5 **6de9919edc2aa3a0abf39e0868fe580c** — verified 24/24 on the live fleet 2026-08-25 (`md5sum .../dod/addons/ktpamx/modules/amxxcurl_ktp_i386.so` on every instance; matches root `CLAUDE.md`'s version table).

⚠️ **This line originally pinned `214ec285d706855909962e3e40d47d1a`**, the build taken right after the CU-02 fix (`bd5841d`) landed. Commit `7fa37d3`, a same-day source refactor ("pass the socket into ArmWaits instead of looking it up again"), changed `SocketData` after that pin was written but before the binary that actually shipped was built — so a later build legitimately carries a different md5 under the same `1.3.17-ktp` label with no version bump. `214ec285d706855909962e3e40d47d1a` is that superseded pre-refactor build: **retired, do not stage it.**

### Fixed
- **Readiness events were silently dropped, stalling transfers to `CURLOPT_TIMEOUT`
  (CU-02).** `SocketData::previous_action` was doing two jobs: recording what libcurl
  last asked for, *and* standing in for which asio waits were actually pending. Those
  diverge. On `CURL_POLL_INOUT` both a read and a write wait were armed, each bound
  with the composite action; when the write fired first libcurl could drop the socket
  to `CURL_POLL_IN`, and the guard

      if (error || action == previous_action || previous_action == CURL_POLL_INOUT)

  then matched none of its arms for the still-pending read wait. The event was
  discarded, libcurl was never told the socket was readable, and nothing re-armed —
  the transfer hung until `CURLOPT_TIMEOUT`. The same handler also reported `INOUT`
  to `curl_multi_socket_action`, claiming both directions were ready when one was.
  Arming state is now tracked per direction (`read_armed` / `write_armed`), handlers
  bind the single direction they were armed for and report only that, and the guard
  is gone — a completed wait is always reported. Also fixes CU-13.

  Measured on the new harness (`tests/build_harness.sh`, added 1.3.16→1.3.17
  development): before, 16,296 of 524,288 bytes and `CURLE_OPERATION_TIMEDOUT` after
  20,001 ms; after, the full 524,288 bytes and `CURLE_OK` in 37 ms. The harness is
  RED on the old code and GREEN on the new under the same gate.

## [1.3.16-ktp] - 2026-08-09

### Fixed
- `curl_easy_getinfo` on a `CURLINFO_STRING` passed libcurl's `char*` straight to
  `MF_SetAmxString` with no NULL check, and the local was uninitialized. libcurl
  returns `CURLE_OK` with a null pointer for an absent header — a response with
  no `Content-Type` is enough — so a remote server could segfault the game thread
  by omitting a header. Now initialized and substituted with an empty string.
- The LONG/SOCKET, DOUBLE and SLIST branches of the same function had the same
  uninitialized local and were **worse**: they wrote it through to Pawn with no
  success check at all, so a non-OK `curl_easy_getinfo` returned an uninitialized
  stack value as the answer. The SLIST case handed Pawn an uninitialized
  *pointer*, and the module exposes `curl_slist_free_all`, which would free it —
  a wild free on the game thread. All four are now initialized and the writeback
  is gated on `CURLE_OK`. Not reachable from any current fleet plugin — every live
  call site uses `CURLINFO_RESPONSE_CODE`, which always returns OK — but it
  is the same defect class in the same function. On a non-OK result the cell is
  now left as the plugin passed it in, which for a Pawn `new` local is a
  deterministic 0 rather than stack garbage — so the two call sites that branch on
  the code now fail closed instead of possibly reading garbage inside 200-299 and
  reporting success.
- A reused easy handle's `curl_get_response_body` returned the *previous*
  transfer's body concatenated with the new one. `ClearResponseBody()` existed but
  had no callers; it is now called from `Perform()`, after the
  `MF_AmxFindPublic` throw so a failed perform preserves the prior body. Clearing
  in the completion path instead would be wrong — the body has to survive into
  `curl_get_response_body`.
- `SetupAmxCallback()` unregistered the old SP forward but kept its id in
  `registered_callbacks_` when re-registration failed (bad function name, or the
  unsupported-option throw). The data callbacks then dispatched through a freed
  forward id that AMXX is free to recycle for an unrelated forward. The entry is
  now erased with the unregister, so a failed re-registration leaves the option
  with no entry — which is the state every data callback already treats as
  "fall back gracefully".

### Documentation

README's "Building from Source" still described the Premake5 workflow this repo
replaced with CMake in **1.3.7** — `premake5 vs2017` / `premake5 gmake` /
`premake5 clean`, and output in `bin/<config>/`. None of that works: the build is
`build_linux.sh` (`cmake .. && make`), and the artifact is
`build/amxxcurl_ktp_i386.so`. Rewritten, with a direct no-staging invocation for
anyone who doesn't want the local test-tree copy.

Noted while fixing: `premake5.lua` is still tracked despite being unreferenced by
any build path. Left in place — removing it is a separate call — but it means the
old instructions looked plausible enough to attempt. It now carries a
legacy/unsupported header comment so it can't be mistaken for the build again.

**Added a `## Diagnostics` section to the README.** The module emits nine distinct
`[CURL] WARNING` lines and two `FATAL ERROR` boundary lines; the README documented
none of them, describing `[CURL]` output only as "registration success/failure".
The one that mattered: `stale socket_map_ entry for fd N on open` means libcurl
violated its own close-callback contract and has never fired in the field — its
"investigate immediately" severity was recorded only in `.claude/skills/`, which
no server operator reads. It now sits in an operator-facing table alongside the
genuinely benign warnings it was indistinguishable from, and the two
in-flight-handle *refusals* (1.3.14) are documented as the contract change plugin
authors can hit, not just as log lines.

**"Verify Installation" told operators to look for a string the module never
prints** — `[CURL] Module loaded successfully`. The real lines are
`[CURL] Module loaded (extension mode, using frame callbacks)` and, when the
frame-callback API is absent, a bare `[CURL] Module loaded`. That distinction is
the whole point of the check: the bare form means the module loaded but async
will never run.

**Three of fifteen registered natives were missing from the API Reference** —
`curl_get_response_body` (described in prose but never given a signature) plus
`curl_formadd` / `curl_formfree`, added under a Forms heading that carries their
upstream `#pragma deprecated` status rather than presenting them as recommended.

**README header said 1.3.15 while the first section said "What's New in v1.3.5"**,
followed by a seven-release "Previous:" chain that skipped 1.3.2 and stopped ten
releases short. Collapsed to a single 1.3.15 block plus the CHANGELOG link; the
still-current behavioral facts live in Features / API Reference / Diagnostics as
current behavior instead of as release history.

**`modules.ini` snippets** now show the bare `amxxcurl` the deployed config
actually uses, rather than a platform-split `.dll`/`.so` pair.

**`.gitignore` asserted the opposite of the tracked state** — it listed
`build_linux.sh` under "not committed" while that file is tracked and is *the*
documented build command. Removed. Added `.claude/settings.local.json` (per-machine
state, and this repo is public) as a targeted entry — deliberately not a blanket
`.claude/` ignore, since `.claude/skills/` is intentionally tracked.

**`CLAUDE.md`** listed `bin/ReleaseDLL/` as the build output — a Premake-era path
no current build produces.

## [1.3.15-ktp] - 2026-07-18

Crash-safety + resource-leak hardening from the 2026-07-15 stack review (CU-01/03/05/07, all P2/CONFIRMED). Binary md5 `fd6160057bf5d997d7d20b6fa7f8c1df`. Module-internal only — no plugin recompile (no `.inc` / native-signature change).

### Fixed
- **CU-03 — the outermost game-thread boundary (`AsioPoller::Poll`) was unguarded.** `io_context::poll()` rethrows anything an asio handler let escape (`bad_alloc`, a throwing `timer.cancel()`, an uncaught `std::runtime_error` from a completion callback), so it could unwind through `CurlFrameCallback` / the `OnAmxxDetach` drain loop into ReHLDS's `-fno-exceptions` frame loop and `std::terminate` the server mid-match. Now wrapped in the same `std::exception`/`...` catch (gated on `g_amxxcurl_detached`) the module already uses at the C-callback boundary; `io_context` stays valid after a handler throws so the `stopped()`/`restart()` check still runs. Covers both the frame-callback and shutdown-drain Poll paths.
- **CU-07 — an unsupported `FUNCTIONPOINT` curl option crashed the server at the native boundary.** `curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, ...)` (and the other exported-but-unsupported callback options) makes `SetupAmxCallback` / `GetMethodPointerForCallbackOption`'s default case throw `std::runtime_error`, which the native only caught for two typed exceptions — anything else escaped into `-fno-exceptions` core = `std::terminate` (SIGABRT, the 1.3.11 CHI1 class, at the native boundary instead of the C-callback one). Added a trailing `catch (const std::exception&)` that logs via `MF_LogError` and returns -1.
- **CU-01 — `curl_global_cleanup()` ran while the `CURLM*` was still alive.** On every clean fleet shutdown since `.928`+2.7.21 woke `OnAmxxDetach`, the multi was cleaned up only later, in the `~CurlMulti` destructor cascade during `dlclose` — i.e. `curl_multi_cleanup` after global teardown, UB per libcurl's contract (benign only on the vendored curl 7.63/OpenSSL 1.1, one bump from the CHI1 shutdown-crash class). `CurlMulti` now has an idempotent `Shutdown()` (`curl_multi_cleanup` + null); `OnAmxxDetach` calls it between `RemoveAllTasks()` and `curl_global_cleanup()`, and `~CurlMulti` calls it too.
- **CU-05 — a failed `curl_multi_add_handle` left a permanent zombie handle.** `AmxCurl::Perform` set `is_transfer_in_progress_=true` before `AddCurl`, but `AddCurl`'s failure path (OOM-class) silently erased the completion callback (`DEBUG_LOG`, stripped in release) with no signal to the caller — so `OnPerformComplete` never fired, `IsAllTransfersCompleted()` stayed false forever (every subsequent shutdown drain burned the full 5s), and the `amx_callback_data_` buffer leaked. `AddCurl` now returns its `CURLMcode` and logs the failure via `MF_PrintSrvConsole` (release-visible); `Perform` resets the flag and frees/nulls the buffer on a non-`CURLM_OK` return.

### Deferred (not in this cut)
- CU-02 / CU-04 / CU-08 (PLAUSIBLE — no current fleet consumer exercises the path) and CU-06 / CU-09 (handle-reuse body concatenation — no fleet consumer reuses a handle) stay post-LAN.

## [1.3.14-ktp] - 2026-07-13

### Fixed
- **Keep-alive sockets were being closed under libcurl on `CURL_POLL_REMOVE` — the generator of the 1.3.11 EBADF class.** `CurlSocketCallback` unconditionally erased the fd from `socket_map_`, and since the map owns the `asio::ip::tcp::socket`, that erase **closed the fd**. But `CURL_POLL_REMOVE` means only "stop polling this fd" — libcurl keeps the connection in its keep-alive cache and signals real fd death exclusively through `CURLOPT_CLOSESOCKETFUNCTION`. So we closed live sockets underneath libcurl, and on connection reuse the socket callback arrived for an fd no longer in `socket_map_`, which the code then misclassified as a c-ares socket and fed to `WrapTcpSocket` — on a closed fd. That is exactly the EBADF that 1.3.11 had to defuse at the C-callback boundary; 1.3.11 handled the symptom, this fixes the cause. Worse, once the kernel recycled the fd number, the old connection's eventual close callback would erase and close the *new* connection's socket — delayed, unattributable fd crossfire. Now: c-ares sockets still `release()`+erase (c-ares owns those fds and never issues a close callback); curl-owned sockets get their pending asio waits `cancel()`ed but **stay open in the map**, leaving `CurlCloseSocketCallback` as the single place an fd is ever closed. Verified against the vendored libcurl 7.63.0 contract: `Curl_closesocket` fires `Curl_multi_closed` (→ our `CURL_POLL_REMOVE`) *before* closing, and `curl_multi_cleanup` → `Curl_conncache_close_all_connections` honors the close callback, so shutdown drains cleanly. **Note:** with eager close gone, 1.3.13's controller member order (`asio_poller_` declared before `curl_manager_`, so kept sockets destruct against a live `io_context`) is now load-bearing rather than belt-and-suspenders — do not reorder it.
- **Spurious `CURL_CSELECT_ERR` and ghost handlers in `AsioSocketActionCallback`** — three guards, the first of which is *required* by the change above: `operation_aborted` now early-returns (the new `cancel()` fires pending handlers with it, and without the guard every keep-alive REMOVE would report a socket error to libcurl and kill the cached connection); a destroyed-multi guard; and a staleness identity check that the handler's bound `SocketData` is still the one `socket_data_map_` holds for that fd (keep-alive reuse mints a fresh `SocketData` for the same fd, so a handler from the previous generation must not act on the new one). The handler's own captured `shared_ptr` keeps the old object alive, so the identity check is ABA-safe.
- **Stale-entry hardening in `CurlOpenSocketCallback`** — now that idle keep-alive sockets legitimately live in `socket_map_`, a missed close callback would make `emplace` collide on an existing key: the emplace silently no-ops, the moved-from temporary's destructor closes the **brand-new** fd, and we hand libcurl a closed socket. The kernel having just issued that fd number proves any existing entry is a corpse, so it is `release()`d (never closed — the fd is live and ours) and evicted, with a one-line warning. Converts a catastrophic contract violation into a log line.
- **UB in `CheckMultiInfo`: the completion callback destroyed itself mid-call.** `curl_map_[easy](res)` invoked the `std::function` *through the map slot*, and its first act (`OnPerformComplete` → `RemoveCurl` → `curl_map_.erase(easy)`) destroys that very `std::function` while its `operator()` is on the stack. Copied out before invoking.
- **`amx_callback_data_` leaked on any task destroyed in flight** — the `new[]` cell buffer was freed only on the `OnPerformComplete` path, so tasks dropped by `RemoveAllTasks` (shutdown/detach) stranded it. `AmxCurl` now owns the buffer properly: a destructor frees it, the move constructor nulls the source, `OnPerformComplete` takes-and-nulls before freeing, and `Perform` releases any prior buffer. (Getting only the destructor would have turned a leak into a double-free — the map stores `AmxCurl` by value and move-constructs on emplace.)
- **`curl_easy_reset` on an in-flight handle** is UB per libcurl (it rips out the options — including our socket callbacks — mid-transfer). Now refused with a console warning, matching the guard `RemoveTask` already had.
- **`curl_easy_perform` on an in-flight handle silently killed the running transfer.** `curl_multi_add_handle` returns `CURLM_ADDED_ALREADY`, and `AddCurl`'s error path then erased the live completion callback — so the in-flight transfer completed with nobody to call, `is_transfer_in_progress_` stayed true forever, and the handle became a zombie that `RemoveTask` defers on and the sweeper never collects. Now refused (and the caller's buffer freed), like `curl_easy_reset`.
- **64-bit `CURLOPTTYPE_OFF_T` options were corrupted whenever the low word had bit 31 set.** Pawn passes the value as two *signed* 32-bit cells `{high, low}`; the old `val |= p[1]` sign-extended the low word to `0xFFFFFFFF_xxxxxxxx`, smearing ones across the high word (and `val <<= 32` on the signed high word was UB besides). Now assembled through `uint64_t`. No consumers today — `*_LARGE` options (`INFILESIZE_LARGE`, `RESUME_FROM_LARGE`, `MAXFILESIZE_LARGE`, `POSTFIELDSIZE_LARGE`, `MAX_SEND/RECV_SPEED_LARGE`) are unused by KTP plugins — but it was a live landmine for the first plugin to touch one.

### Changed
- `AmxCurl` constructor init-lists reordered to match declaration order (silences `-Wreorder`; members initialize in declaration order regardless of list order, so a mismatched list is a bug waiting for the first initializer that reads another member).

---

## [1.3.13-ktp] - 2026-07-03

### Fixed
- **libcurl teardown-ordering UB on the skipped-detach exit path** — follow-up to 1.3.12 after the root-cause investigation landed the key fact: **extension-mode engine shutdown NEVER calls `OnAmxxDetach`** (`ReleaseEntityDlls` calls only the single `pfnGameShutdown` slot — which KTPAMXX doesn't register — then dlcloses ktpamx with no module-detach pass; `Meta_Detach` is Metamod-only). So on every fleet shutdown the whole `OnAmxxDetach` body (drain loop, `RemoveAllTasks`, `curl_global_cleanup`) is dead code, and the 1.3.12 atexit guard is the *real* teardown path — but 1.3.12 only armed the flag, leaving `~Curl` to run `curl_easy_cleanup` on easies still attached to the multi (UB per libcurl docs; `~AsioPoller` also destructs before `~AmxCurlManager`, reverse member order). The guard handler now does the teardown properly: `g_amxxcurl_detached.exchange(true)`, and if the flag was previously clear (detach never ran), calls `RemoveAllTasks()` — detaching in-flight easies from the multi while libcurl and asio are fully alive — before the destructor cascade runs. When a real detach did run, the exchange gate makes the handler a no-op (must not touch curl after `curl_global_cleanup`). Belt-and-suspenders: the three `MF_PrintSrvConsole` sites reachable from multi callbacks (two C-callback-boundary catches + the WrapTcpSocket EBADF recovery, `curl_multi_class.cc`) are now gated on `g_amxxcurl_detached` so a pathological callback during exit-time removal can't print through a stale MF pointer. Root-cause detail in memory `chi1-shutdown-segfault-amxxcurl-detach-skipped`; the engine/KTPAMXX-side fix (per-extension shutdown export so detach actually runs) is tracked separately in TODO.md.
- **Controller member order: `asio_poller_` now declared before `curl_manager_`** (ktp-code-review finding, same teardown pass) — destruction runs in reverse declaration order, and `~CurlMulti` (inside the manager) owns `asio::ip::tcp::socket` objects bound to the poller's `io_context`; the old order destroyed the poller first, leaving any socket libcurl's connection cache still held (outside the in-progress set `RemoveAllTasks` drains) to destruct against a dead `io_context`. Pre-existing latent condition, usually masked by the eager close-on-`CURL_POLL_REMOVE` behavior; the swap also makes the constructor's `curl_manager_(asio_poller_)` reference a fully-constructed member.

---

## [1.3.12-ktp] - 2026-07-03

### Fixed
- **Shutdown SIGSEGV in `~CurlCallbackAmx()` when `OnAmxxDetach` never runs** — the CHI1 27015 recurring shutdown segfault (May 13/18, Jun 7 03:00, Jun 23 09:07 cores) survived the 1.3.10/1.3.11 guard because the guard never armed: `g_amxxcurl_detached` read `0x00` in both analyzed cores, proving the engine unloaded KTPAMXX core **without ever calling `OnAmxxDetach`** on that shutdown path. The `AmxCurlController` singleton's exit-time destructor cascade (manager → tasks → `~CurlCallbackAmx`) then reached `IsAmxValid()` → `MF_FindScriptByAmx` through a stale `g_fn` pointer into unmapped core — both cores fault at identical relative offsets (`amxxcurl+0x96a0c` in the destructor, EIP at `base+0x673070` in the ex-KTPAMXX region, with engine/ktpamx/dod already absent from the core's mapping list while amxxcurl was still resident). Fix: `AmxCurlController::Instance()` now registers an `atexit` handler **immediately after** the singleton is constructed that sets `g_amxxcurl_detached = true`. Because `atexit` and static destructors share one LIFO list, the handler is guaranteed to run before the singleton's destructor — so the exit-time cascade always sees `detached=true` and takes the safe no-`MF_*` path, whether or not `OnAmxxDetach` ever ran. A normal detach still sets the flag earlier (drain loop → flag → `RemoveAllTasks`), making the atexit store a no-op; skipping `MF_UnregisterSPForward` at exit is correct since the forward table owner is already gone. No behavior change at gameplay time; single-header diff (`src/amx_curl_controller_class.h`). Root-cause analysis in memory `chi1-shutdown-segfault-amxxcurl-detach-skipped`; the separate question of WHY that shutdown path skips module detach remains open on the KTPAMXX side.

---

## [1.3.11-ktp] - 2026-05-13

### Fixed
- **SIGABRT in `WrapTcpSocket` from throw across C-callback boundary** — CHI1 27015 crashed at 2026-05-13 00:26 ET with a `std::system_error("assign: Bad file descriptor")` propagating out of `AsioPoller::WrapTcpSocket` through `CurlSocketCallbackStatic`, hitting the C-callback boundary from libcurl with no handler, and tripping `std::terminate()`. Distinct failure class from the 1.3.9 / 1.3.10 shutdown race (`~CurlCallbackAmx()` at module-detach time) — this one is mid-operation during normal traffic when libcurl's multi-handle dispatch hands us a stale fd that a sibling event closed between socket-callback issuance and our dispatch. The prior `WrapTcpSocket` used the throwing `basic_socket(io_context, protocol, native_socket)` constructor, which calls `asio::detail::throw_error(ec, "assign")` (`basic_socket.hpp:164`) on EBADF. Fix: `WrapTcpSocket` now takes an `asio::error_code& ec` out-param and uses the non-throwing `socket.assign(protocol, native_socket, ec)` overload (`basic_socket.hpp:389`); on EBADF the returned socket is default-constructed (not open) and `ec` is populated. Caller in `CurlMulti::CurlSocketCallback` handles the failure by calling `curl_multi_assign(curl_multi_, s, nullptr)` to clear libcurl's stale `socketp` for this fd, erasing our `socket_data_map_` entry, and returning 0 so libcurl can continue dispatching other transfers. Belt-and-suspenders try/catch at the `CurlSocketCallbackStatic` boundary returns `-1` on any future escaping exception (libcurl interprets as transfer abort, not process crash). ktp-code-review round 1 caught the missing `curl_multi_assign` clear (without it libcurl retained the stale socketp pointer across our recovery); round 2 caught the thread-safety note about `MF_PrintSrvConsole` (safe in current single-threaded `Poll()` model; would need a thread-safe log queue if io_context ever moves to worker threads). See memory `amxxcurl_asio_throw_assign.md` for the full investigation. 
  
  **Deployment timeline:** binary md5 `b1932ed0c74efe6eff1cf1c68b6ddd0a` SCP'd as `.so.new` to all 24/24 active fleet instances + tier2 runner 2026-05-13; auto-swapped 2026-05-14 03:00 EDT via the established `ktp-scheduled-restart.sh` glob; soak window 2026-05-14 → 2026-05-21.

---

## [1.3.10-ktp] - 2026-05-05

### Changed
- **Flag-store ordering inverted in `OnAmxxDetach`** — `g_amxxcurl_detached.store(true)` now happens AFTER the in-flight transfer drain loop exits but BEFORE `manager.RemoveAllTasks()` and `curl_global_cleanup()`. The 1.3.9 order (flag stored at the very end) closed the confirmed crash path but left a theoretical window: if a `shared_ptr<CurlCallbackAmx>` escaped `RemoveAllTasks` via a late asio handler that captured one, the destructor would have observed `detached=false` and dereferenced `g_fn_FindAmxScriptByAmx` after KTPAMXX core was already unmapped — exactly the failure mode 1.3.9 was meant to prevent. The new order eliminates that window: every `~CurlCallbackAmx()` fired during or after teardown atomically sees `detached=true` and takes the safe `registered_callbacks_.clear()` path. Skipping `MF_UnregisterSPForward` during teardown is correct — KTPAMXX is about to free its forward table anyway. The drain loop runs first so legitimate in-flight completion callbacks still fire with valid MF_* function pointers; only after the loop returns does the no-op path activate. Defensive-only: no current code path triggers the previous failure mode (1.3.9 already covers the confirmed `0x965d6` crash signature). 5-line diff (one block move + comment refresh in `src/callbacks.cc`; declaration comment refresh in `src/amx_curl_callback_class.h`). See `docs/INVESTIGATION_shutdown_race_2026-05-04.md`.

---

## [1.3.9-ktp] - 2026-05-05

### Fixed
- **Shutdown SIGSEGV in `~CurlCallbackAmx()` after KTPAMXX core unmap** — When a curl request was still in flight at engine `quit` and the 5-second drain in `OnAmxxDetach` couldn't reach a clean state (or an asio handler held a `shared_ptr<CurlCallbackAmx>` past `RemoveAllTasks`), the callback object survived into the `AmxCurlController` Meyers singleton's static-destructor phase. Its destructor calls `IsAmxValid()`, which calls `MF_FindScriptByAmx(amx_)` — an indirect call through `g_fn_FindAmxScriptByAmx`. By that point KTPAMXX core's `.text` is already unmapped, so the call jumps into a freed page and segfaults. Hit on ATL1 27015 (2026-05-04 03:00:08 EDT) and DEN5 27019 (2026-05-05 03:00:13 EDT) at the scheduled-restart window, with byte-identical relative offsets in the amxxcurl module — confirmed root cause via `objdump -d` at offset `0x965d6`. Trigger correlated with HLTV proxy's 03:00:01-03 reconnect cron priming a Discord/HLStatsX POST that landed in the unsafe window. Fix: module-level `std::atomic<bool> g_amxxcurl_detached`, set at the very end of `OnAmxxDetach`. `IsAmxValid()` and `OnPerformComplete()` now short-circuit on it before any `MF_*` call, so late destructors take their safe `registered_callbacks_.clear()` branch and never dereference the stale function pointer. No behavior change at gameplay time. See `docs/INVESTIGATION_shutdown_race_2026-05-04.md`.

---

## [1.3.8-ktp] - 2026-04-19

### Fixed
- **`CURLM_RECURSIVE_API_CALL` when `timeout_ms == 0`** (PR #1 by JimmyLockhart65616) — `CurlTimerCallback` fires synchronously from inside libcurl callbacks (notably `curl_multi_add_handle`). Calling `curl_multi_socket_action` directly in that context is rejected by libcurl as a recursive API call (rc=8), so transfers never progressed and user callbacks never fired on high-RPS streams. Fix posts the socket-action to the asio `io_context` so it runs on the next `Poll()` outside any libcurl callback. Observed while wiring the DoD HUD Observer ingest path at ~50 events/sec.
- **UAF guard on `this`-captured asio handlers** (PR #2) — The lambda posted in the `timeout_ms == 0` branch and the existing `async_wait(std::bind(&CurlMulti::AsioTimerCallback, this, _1))` in the `timeout_ms > 0` branch both capture `this`. If either runs after `~CurlMulti()` (e.g. if any `Poll()` fires between the drain loop exit and `RemoveAllTasks` during `OnAmxxDetach`), the `curl_multi_` member would already be cleaned up. `~CurlMulti()` now sets `curl_multi_ = nullptr` and both handlers early-return on null. Defensive — no current code path reaches this, but it removes a footgun for future shutdown-path changes.
- **`moduleconfig.h` `MODULE_VERSION` was frozen at `1.3.6-ktp`** — missed during the 1.3.7 release; the module was self-identifying as `1.3.6-ktp` in logs and plugin-info messages despite the binary containing 1.3.7 code. Now tracks the real version.

---

## [1.3.7-ktp] - 2026-04-02

### Changed
- **Build system migrated to CMake** — Replaced Premake5 + generated Makefiles with a single `CMakeLists.txt`. Consistent with KTP-ReHLDS and KTP-ReAPI build systems.
- **Compiler optimizations** — `-O3 -march=native -mtune=native -flto -fno-math-errno` for CPU-specific instructions and link-time optimization.

### Fixed
- **Buffer overflow in `amx_curl_formadd`** — `strcpy` replaced with `strncpy` + null terminator. `MF_GetAmxString` could return strings larger than the 16384-byte buffer, causing heap overflow.
- **Memory leak in `amx_curl_easy_perform`** — Added catch-all exception handler to prevent `data` array leak on unexpected exceptions during `CurlPerformTask`.
- **Exception in libcurl callback (`SetSock`)** — Replaced `throw std::runtime_error` with graceful return + debug log. Throwing from a libcurl socket callback is undefined behavior and can crash the server.
- **Exception safety in `AddCurl`** — Reordered operations to set curl options before inserting into `curl_map_`. If `SetOption` throws, the handle is no longer orphaned in the map.
- **CPU busy-spin during detach** — Added 10ms sleep in the poll loop during module unload. Without sleep, `io_context::poll()` returns immediately when no I/O is ready, causing the loop to spin at 100% CPU for up to 5 seconds.

---

## [1.3.6-ktp] - 2026-03-24

### Fixed
- **`curl_global_cleanup` added to `OnAmxxDetach`** — `curl_global_init` was called on attach but `curl_global_cleanup` was never called on detach. On a long-running server with frequent map changes, the unpaired init/cleanup calls leaked SSL/OpenSSL state and OS resolver threads.
- **`curl_formadd` params array bounds check** — The `CURLFORM_END` sentinel scan had no upper bound on the params array index. Malformed plugin calls without a terminating `CURLFORM_END` could read past the end of the params array. Now checks against the actual param count.
- **`OnAmxxDetach` timeout uses wall-clock** — The interrupt-and-drain loop used an iteration counter (~5000 polls) as a proxy for 5 seconds, but `io_context_.poll()` returns immediately when no I/O is ready, making the counter exhaust in microseconds. Now uses `std::chrono::steady_clock` for a real 5-second wall-clock deadline.
- **`CurlReset` re-binds WriteCallback** — `curl_easy_reset` removes all options including `CURLOPT_WRITEFUNCTION`. After reset, the auto-buffering WriteCallback (needed for `curl_get_response_body`) was lost. Now re-binds it after every reset.

---

## [1.3.5-ktp] - 2026-03-14

### Async Safety + POSTFIELDS Fix

**Fixed:**
- **`CURLOPT_POSTFIELDS` used stale pointer during async perform** — `MF_GetAmxString` returns a pointer to a static internal buffer that gets overwritten on the next call. For async `Perform`, libcurl reads the POST data later when the buffer is stale, sending corrupted or unrelated data. Now auto-upgrades `CURLOPT_POSTFIELDS` to `CURLOPT_COPYPOSTFIELDS`, which makes libcurl copy the data immediately.
- **`RemoveAllTasks` left handles attached to curl_multi** — `curl_easy_cleanup` ran while handles were still in the multi, which is undefined behavior per libcurl docs. Now removes all in-flight handles from curl_multi before destroying them.
- **IOCTL interrupt code incorrect** — `CURLIOE_UNKNOWNCMD` tells libcurl the command is unknown; `CURLIOE_FAILRESTART` correctly signals a failed restart, which triggers proper abort handling.
- **`curl_multi_add_handle` failures silent** — Return code was unchecked. Now logs error and cleans up the curl map entry on failure.
- **`curl_formadd` static aliasing risk** — Changed from `static char[14][16384]` to heap allocation (`new`/`delete`). Static storage with `CURLFORM_PTRCONTENTS` could alias across concurrent calls; heap allocation ensures each invocation gets its own buffers.

---

## [1.3.4-ktp] - 2026-03-12

### In-Flight Callback Safety

**Fixed:**
- **Segfault from stale AMX in mid-transfer callbacks** — `WriteCallback`, `HeaderCallback`, `ReadCallback`, and all other libcurl callbacks called `MF_ExecuteForward` without checking if the plugin was still loaded. If a map change unloaded a plugin during a slow HTTP response, the next callback would dereference a stale AMX pointer. All 10 callback methods now check `IsAmxValid()` before calling into Pawn, aborting the transfer cleanly via the interrupt mechanism if the plugin is gone.
- **Move constructor omitted `is_transfer_in_progress_`** — The `AmxCurl` move constructor did not copy `is_transfer_in_progress_`, leaving it uninitialized (undefined behavior). The primary constructor set it to `false`, but after move-construction into the handle map, the value was garbage. This could cause `IsAllTransfersCompleted()` to return false indefinitely, hanging `OnAmxxDetach`. Now properly copied in the move initializer list.
- **`OnAmxxDetach` spin-wait could hang indefinitely** — The detach cleanup loop polled `IsAllTransfersCompleted()` without first interrupting in-flight transfers. A stuck or slow transfer (DNS timeout, hung upstream) would block server shutdown forever. Now calls `TryInterruptAllTransfers()` before the loop, with a 5000-poll timeout as a safety bound.
- **`RemoveTask` on in-flight handle caused use-after-free** — `curl_easy_cleanup` from Pawn during an active transfer destroyed the `AmxCurl` object while libcurl still held a reference to the easy handle. The next `CheckMultiInfo` call would fire the completion callback on freed memory. Now checks `is_transfer_in_progress` and interrupts instead of destroying, deferring cleanup to the completion callback.
- **Destructor called `MF_UnregisterSPForward` on stale forwards** — `~CurlCallbackAmx` unconditionally unregistered all Pawn forwards, but after plugin unload the forward IDs reference freed function tables. Now checks `IsAmxValid()` first — if invalid, clears the map without calling AMXX.
- **Response body grew unbounded** — Auto-buffered response bodies (`response_body_`) had no size limit. A misbehaving endpoint returning megabytes would accumulate it all in heap memory. Now capped at 64KB — sufficient for Discord API responses while preventing memory exhaustion.
- **`curl_formadd` 224KB stack allocation** — `char strings[14][16384]` allocated 224KB on the stack. Changed to `static` storage to move out of the stack frame while keeping the correct 16384 buffer size (matching KTPAMXX's `MAX_BUFFER_LENGTH`).
- **Deferred cleanup for in-flight handles** — `RemoveTask` now marks handles with `cleanup_deferred_` instead of silently leaking them. `SweepDeferredCleanups()` runs each frame and erases completed deferred handles, preventing unbounded growth of `amx_curl_` on long-running servers.
- **Detach timeout warning logged actual poll count** — Previously printed hardcoded `0` instead of the actual poll count reached.

---

## [1.3.3-ktp] - 2026-03-10

### Stale AMX Pointer Validation

**Fixed:**
- **Segfault when plugin unloaded during async transfer** -- When an async HTTP request completes (`OnPerformComplete`), the module calls `MF_RegisterSPForward(amx, func)` to invoke the Pawn callback. If the plugin that started the request was unloaded during the async operation (e.g., during a map change), the stored AMX pointer is stale and `amx->base` points to freed memory, causing a segfault in `amx_GetPublic`. Now validates the AMX pointer via `MF_FindScriptByAmx()` before registering the forward. If the plugin is no longer loaded, the callback is skipped with a warning logged to the server console.

---

## [1.3.2-ktp] - 2026-02-25

### Auto-Buffering Fix

**Fixed:**
- **`curl_get_response_body()` always returned empty string** — The v1.3.0 auto-buffering feature was broken because `CURLOPT_WRITEFUNCTION` was never bound to the curl handle by default. The C++ `WriteCallback` (which buffers into `response_body_`) only fires if explicitly installed on the handle via `BindCallback()`. Without a Pawn `WRITEFUNCTION` set, `BindCallback` was never called, so libcurl used its default writer (stdout), bypassing the C++ callback entirely. Fixed by calling `BindCallback(CURLOPT_WRITEFUNCTION)` in `Curl::InitCurl()` so the C++ WriteCallback is always installed. Discovered via Discord embed message IDs never being captured (empty response body → `DISCORD_MSG_ID_NOT_FOUND` → all embed updates skipped with `no_msg_id`).

---

## [1.3.1-ktp] - 2026-02-25

### Bug Fixes

**Fixed:**
- **`curl_easy_unescape` native called escape instead of unescape** — Copy-paste bug in `curl_natives.cc`: `amx_curl_easy_unescape` called `manager.CurlEscapeUrl()` instead of `manager.CurlUnescapeUrl()`. The unescape native was actually escaping URLs.
- **Server crash when callback function not found** — `curl_easy_perform` only caught `CurlAmxManagerInvalidHandleException` but not `CurlTaskCallbackNotFoundException`. If a plugin passed a non-existent callback function name, the unhandled exception crashed the server and leaked the `data[]` array.
- **Potential infinite loop on module detach** — `AmxCurl` constructor didn't initialize `is_transfer_in_progress_`, leaving it as garbage. `OnAmxxDetach()` loops `while(!IsAllTransfersCompleted())` — an uninitialized `true` value would cause an infinite hang on server shutdown or map change. All `AmxCurl` members now properly initialized.
- **Corrupted error messages in `BindCallback`** — `"failture with code " + code` performed pointer arithmetic on the string literal instead of string concatenation. For `CURLcode` values >= 20, this read past the null terminator (undefined behavior). Replaced with `curl_easy_strerror(code)` for proper human-readable error messages.

---

## [1.3.0-ktp] - 2026-02-23

### Built-in Response Body Capture

**Added:**
- **`curl_get_response_body()` native** - Retrieves the response body captured during an async transfer. When no `CURLOPT_WRITEFUNCTION` Pawn callback is set, the module automatically buffers response data in C++ (`std::string`). Call this native in your completion callback before `curl_easy_cleanup` to read the captured body.

**Why:**
- The Pawn-level `WRITEFUNCTION` callback path (`MF_ExecuteForward` → `amx_Allot`) can fail silently under memory pressure, writing response data to an uninitialized pointer and corrupting the heap — causing segfaults traced to `discord_curl_write` on production servers (ATL3, ATL4, NY1). Capturing in C++ eliminates the dangerous Pawn callback entirely.

**Technical Details:**
- `CurlCallbackAmx::WriteCallback()` now appends to `response_body_` when no Pawn callback is registered (previously discarded data)
- `CurlCallbackAmx::ResetAmxCallbacks()` clears the response body buffer
- New `AmxCurlManager::CurlGetResponseBody()` accessor method
- Native copies response body to Pawn buffer via `MF_SetAmxString`

---

## [1.2.1-ktp] - 2026-01-31

### Forward Registration Validation & Diagnostics

**Fixed:**
- **Silent callback registration failures** - Forward registration could fail silently, causing curl to abort transfers
  - `MF_RegisterSPForwardByName` returns -1 when function not found, but this was stored anyway
  - Invalid forward IDs caused `MF_ExecuteForward` to return 0
  - Curl interpreted 0 from WriteCallback as "abort transfer", preventing completion callback from firing
  - Result: Discord embeds created but no response captured, HLTV recording commands not processed

**Added:**
- **Forward registration validation** - Now checks return value and logs error if registration fails
- **Detailed callback logging** - Logs successful registrations with forward ID and option type
- **WriteCallback diagnostics** - Logs if forward ID is invalid or if callback returns unexpected value
- **Graceful fallback** - If write callback registration fails, accepts data silently instead of aborting

**Technical Details:**
- `SetupAmxCallback()` now validates forward ID before storing
- `WriteCallback()` double-checks forward ID validity before execution
- Error messages printed to server console with `[CURL]` prefix for easy grep

---

## [1.2.0-ktp] - 2026-01-10

### Critical Segfault Fixes

**Fixed:**
- **Use-after-free in async socket callbacks** - Raw `SocketData*` pointers passed to ASIO async callbacks could be deleted before callback executed
  - Changed to `shared_ptr<SocketData>` with `socket_data_map_` tracking
- **Handle allocation bug** - `count() > 1` always false (count returns 0 or 1), causing handle collisions
  - Fixed to `count() != 0`
- **Stale socket map entries** - Non-ARES sockets weren't erased from `socket_map_` on CURL_POLL_REMOVE
  - Now always erases from socket_map_ regardless of socket type
- **Unvalidated callback execution** - `MF_ExecuteForward` called without checking callback registration
  - Added `.count()` validation before all 10 callback functions

**Changed:**
- `curl_multi_class.h` - Added `SocketDataPtr` typedef and `socket_data_map_` for lifetime management
- `curl_multi_class.cc` - Refactored socket handling to use shared_ptr
- `amx_curl_callback_class.cc` - All callback functions now validate registration before execution
- `amx_curl_manager_class.h` - Fixed handle allocation loop condition

---

## [1.1.1-ktp] - 2025-12-04

### KTP Fork - KTPAMXX Integration

**Breaking Changes:**
- **Requires KTPAMXX** - Standard AMX Mod X not supported (module loads but async transfers won't process)

**Removed:**
- Metamod dependency - No longer requires Metamod to run

**Added:**
- KTPAMXX frame callback integration via `MF_RegModuleFrameFunc()` API
- Console logging for module load events
- Graceful fallback when frame callback API not available

**Changed:**
- `callbacks.cc` - Replaced Metamod StartFrame hook with KTPAMXX frame callback
- `moduleconfig.h` - Disabled USE_METAMOD, updated branding to KTP
- Binary renamed to `amxxcurl_ktp_i386` (from `amxxcurl_amxx_i386`)

**Technical Details:**
- Frame callbacks registered in `OnAmxxAttach()`, unregistered in `OnAmxxDetach()`
- `CurlFrameCallback()` processes pending curl_multi transfers each server frame
- All native functions unchanged - full API compatibility with original AmxxCurl

---

## Upstream Releases (Polarhigh/AmxxCurl)

### [1.1.1] - Upstream

- Fixed error "Failed to send data to host"
- Linux issues fixed

### [1.1.0] - Upstream

- Replaced threading with curl_multi interface + ASIO polling
- Non-blocking transfers without spawning threads
- Improved stability and performance

### [1.0.x] - Upstream

- Full libcurl easy interface wrapper
- SSL/TLS support via OpenSSL
- Callback support (write, read, progress, header, debug)
- URL encoding/decoding
- slist support for custom headers
- Cross-platform Windows/Linux support

---

## Credits

**KTP Fork:**
- **Nein_** ([@afraznein](https://github.com/afraznein)) - KTPAMXX integration, Metamod removal

**Upstream AmxxCurl:**
- **Polarhigh** (Igor Minin) - Original module development

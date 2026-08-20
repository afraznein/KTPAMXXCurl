# KTP CURL AMXX

**Version 1.3.17-ktp** - libcurl wrapper module for AMX Mod X with non-blocking async HTTP/FTP support

A fork of [AmxxCurl](https://github.com/Polarhigh/AmxxCurl) modified to work without Metamod by using KTPAMXX's module frame callback API. Provides full libcurl easy interface functionality with SSL support for making HTTP requests, FTP uploads, and other network operations from AMX plugins.

Part of the [KTP Competitive Infrastructure](https://github.com/afraznein).

---

## What's New in v1.3.17-ktp

Transfers no longer stall to `CURLOPT_TIMEOUT` on a dropped readiness event.
Internally the module tracked "which socket waits are pending" using the same
field that recorded "what libcurl last asked for" — and those two diverge. When
libcurl wanted both directions at once and then narrowed to one, a still-pending
wait for the other direction fired against a mismatched state and was silently
discarded: libcurl was never told the socket was readable, nothing re-armed, and
the transfer hung until it timed out. Pending waits are now tracked per direction,
each handler knows which direction it was armed for and reports only that one, and
the discard path is gone. In practice this was reachable by any transfer large
enough to want a read and a write in the same moment — Discord embeds, HLStatsX
posts and the HLTV API all qualify — and it presents as a `CURLE_OPERATION_TIMEDOUT`
with a partial body, not as an error the plugin can see.

This release also adds the module's first test. `tests/build_harness.sh` drives the
real socket layer against a loopback HTTP server with no AMXX and no HLDS; it is
RED on the previous build and GREEN on this one. It builds out-of-tree and stages
nothing. Module-internal only — no plugin recompile (no `.inc` or
native-signature change).

See [CHANGELOG.md](CHANGELOG.md) for the full history. Behavior worth knowing
about that arrived in earlier releases is documented as current behavior under
[Features](#features), [API Reference](#api-reference) and
[Diagnostics](#diagnostics) rather than repeated here.

---

## Features

### Core Capabilities

- **Full libcurl easy interface** - All curl_easy_* functions available as natives
- **Non-blocking transfers** - Async HTTP/FTP operations using curl multi + ASIO
- **SSL/TLS support** - HTTPS and FTPS with OpenSSL
- **No Metamod dependency** - Runs as pure AMXX module via KTPAMXX

### For Plugin Developers

- **Familiar API** - Same behavior as [libcurl C functions](https://curl.haxx.se/libcurl/c/)
- **Callback support** - Write, read, progress, header, and debug callbacks
- **URL encoding** - Built-in escape/unescape functions
- **slist support** - For custom headers and other list options

### For Server Operators

- **Simple deployment** - Single module, no Metamod setup required
- **Cross-platform** - Windows and Linux support
- **Reliable networking** - Production-tested libcurl library

---

## Installation

### Prerequisites

- **KTPAMXX** - Modified AMX Mod X with module frame callback API
- **NOT compatible** with standard AMX Mod X (module will load but async won't work)

### Step 1: Download Module

Download the latest release from [Releases](https://github.com/afraznein/KTPAmxxCurl/releases).

### Step 2: Install Module

```bash
# Windows
copy amxxcurl_ktp_i386.dll "<game>/addons/ktpamx/modules/"

# Linux
cp amxxcurl_ktp_i386.so "<game>/addons/ktpamx/modules/"
```

### Step 3: Enable in modules.ini

Edit `<game>/addons/ktpamx/configs/modules.ini`:
```ini
; KTP CURL module — the bare name is platform-independent;
; KTPAMXX appends the _ktp_i386.so / .dll suffix itself.
amxxcurl
```

### Step 4: Install Include Files

Copy `curl.inc` and `curl_consts.inc` to your scripting include directory:
```bash
cp amx_includes/*.inc "<amxmodx>/scripting/include/"
```

### Step 5: Verify Installation

Check server console on startup:
```
[CURL] Module loaded (extension mode, using frame callbacks)
```

That is the line to expect on KTPAMXX. If you instead see the bare
`[CURL] Module loaded`, the frame-callback API was not found — the module is
loaded but async transfers will never be processed. See [Diagnostics](#diagnostics).

---

## API Reference

All natives mirror the [libcurl C API](https://curl.haxx.se/libcurl/c/) with AMXX-specific adaptations.

### Core Functions

```pawn
// Initialize a curl handle
native CURL:curl_easy_init();

// Perform the transfer (async, calls callback on completion)
native curl_easy_perform(const CURL:handle, const callback[], const data[] = {}, const len = 0);

// Set options
native CURLcode:curl_easy_setopt(const CURL:handle, const CURLoption:option, any:...);

// Get info after transfer
native CURLcode:curl_easy_getinfo(const CURL:handle, const CURLINFO:info, any:...);

// Cleanup handle
native curl_easy_cleanup(const CURL:handle);

// Reset handle to defaults
native curl_easy_reset(const CURL:handle);
```

### Caller contracts

Three ways a plugin can use this module correctly by the compiler's reckoning and
still be wrong. Each one has shipped to the fleet.

**A `CURLcode` of `CURLE_OK` says nothing about the HTTP status.** It reports that
the transfer completed, so 401, 403, 429 and 500 all arrive looking like success —
a callback that checks only the `CURLcode` logs rejected auth and rate limits as
sends. Always follow up with `curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, ...)`.
Check that call's own return too: on anything other than `CURLE_OK` the out-param is
**not written**, so a freshly declared Pawn local keeps its zero-init value and reads
as a legitimate response code of 0.

**Give every callback its own Pawn function.** A function registered both as a menu
callback and as a curl completion callback can resolve to the wrong forward, because
the registration dedup has matched on plugin plus function name without regard to
parameter types — at which point a menu selection integer is handed to native code
expecting a string pointer. Cheap to avoid, expensive to diagnose.

**A header `curl_slist` shared by async transfers cannot be freed.** Freeing one
while an in-flight handle still points at it takes the engine down, so the usual
shape is a list built once at `plugin_init` and deliberately never released. The
consequence to plan for: those header values are then fixed for the process. Re-reading
a config file updates the plugin's own globals while the headers on the wire stay
exactly as they were at init, which looks like the reload silently doing nothing.

### URL Encoding

```pawn
// URL encode a string
native curl_easy_escape(const CURL:handle, const url[], buffer[], const maxlen);

// URL decode a string
native curl_easy_unescape(const CURL:handle, const url[], buffer[], const maxlen);
```

### String Lists

```pawn
// Append to slist (for headers, etc.)
native curl_slist:curl_slist_append(curl_slist:list, string[]);

// Free slist
native curl_slist_free_all(curl_slist:list);
```

### Utility

```pawn
// Get error message for code
native curl_easy_strerror(const CURLcode:code, buffer[], const maxlen);

// Get libcurl version string
native curl_version(buffer[], const maxlen);

// Read the captured response body — no WRITEFUNCTION callback needed.
// Captured up to a 64KB cap; beyond that the body is truncated and a
// [CURL] WARNING is printed.
native curl_get_response_body(const CURL:handle, buffer[], const maxlen);
```

### Forms (deprecated)

Both are registered and functional, but carry libcurl's upstream deprecation —
`#pragma deprecated` in `curl.inc`. Prefer building the body yourself.

```pawn
native CURLFORMcode: curl_formadd(&curl_httppost: first, &curl_httppost: last, any: ...);
native curl_formfree(&curl_httppost: first);
```

### Callback Signatures

**Completion callback:**
```pawn
// With user data
public myCallback(CURL:curl, CURLcode:code, data[])

// Without user data
public myCallback(CURL:curl, CURLcode:code)
```

---

## Usage Examples

### HTTP GET Request

```pawn
#include <amxmodx>
#include <curl>

public plugin_init() {
    register_plugin("HTTP Example", "1.0", "KTP")
}

public test_http_get() {
    new CURL:curl = curl_easy_init()

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://example.com/api/data")
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, "write_callback")
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0)

        curl_easy_perform(curl, "on_complete")
    }
}

public write_callback(data[], size, nmemb, userdata) {
    // Process received data
    server_print("Received: %s", data)
    return size * nmemb
}

public on_complete(CURL:curl, CURLcode:code) {
    if (code == CURLE_OK) {
        server_print("Request completed successfully")
    } else {
        new error[128]
        curl_easy_strerror(code, error, charsmax(error))
        server_print("Request failed: %s", error)
    }

    curl_easy_cleanup(curl)
}
```

### HTTP POST with JSON

```pawn
public send_json_post() {
    new CURL:curl = curl_easy_init()

    if (curl) {
        new curl_slist:headers = curl_slist_append(SList_Empty, "Content-Type: application/json")

        new json[256]
        formatex(json, charsmax(json), "{\"player\": \"TestPlayer\", \"score\": 100}")

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.example.com/scores")
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0)

        curl_easy_perform(curl, "on_post_complete")

        curl_slist_free_all(headers)
    }
}
```

### FTP Upload

```pawn
public upload_file_ftp() {
    new CURL:curl = curl_easy_init()

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "ftp://ftp.example.com/uploads/file.txt")
        curl_easy_setopt(curl, CURLOPT_USERPWD, "username:password")
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1)
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, "read_callback")

        curl_easy_perform(curl, "on_upload_complete")
    }
}
```

---

## Building from Source

### Prerequisites

- **CMake** >= 3.1
- **GCC** with 32-bit multilib (`sudo apt-get install gcc-multilib g++-multilib`)

### Build

```bash
bash build_linux.sh
```

That is the canonical entry point: it runs `cmake .. && make -j$(nproc)` in a
clean `build/` dir, then auto-stages the module into the local
`KTP DoD Server/serverfiles/` test tree if that tree exists.

To build without staging, do it directly:

```bash
mkdir -p build && cd build && cmake .. && make -j$(nproc)
```

### Build Output

`build/amxxcurl_ktp_i386.so`

> **Note:** the build migrated from Premake5 to CMake in 1.3.7. A vestigial
> `premake5.lua` is still in the tree but is not used by any build path.

---

## Architecture

### How Non-Blocking Works

```
┌─────────────────────────────────────────────────┐
│  AMX Plugin                                     │
│  - Calls curl_easy_perform() with callback      │
│  - Continues execution immediately              │
└────────────────┬────────────────────────────────┘
                 │ Native call
                 ↓
┌─────────────────────────────────────────────────┐
│  KTP CURL AMXX Module                           │
│  - Queues transfer with curl_multi              │
│  - ASIO polls for socket activity               │
│  - CurlFrameCallback() called each frame        │
└────────────────┬────────────────────────────────┘
                 │ Frame callback (KTPAMXX API)
                 ↓
┌─────────────────────────────────────────────────┐
│  KTPAMXX                                        │
│  - Calls registered frame callbacks each frame  │
│  - Provides MF_RegModuleFrameFunc() API         │
└────────────────┬────────────────────────────────┘
                 │ On transfer complete
                 ↓
┌─────────────────────────────────────────────────┐
│  AMX Plugin Callback                            │
│  - Receives CURL handle and result code         │
│  - Can query CURLINFO_* for response data       │
└─────────────────────────────────────────────────┘
```

### Key Components

| Component | Purpose |
|-----------|---------|
| `curl_multi_class` | Manages multiple concurrent transfers |
| `asio_poller` | Non-blocking socket polling via ASIO |
| `amx_curl_callback_class` | Bridges libcurl callbacks to AMX forwards |
| `callbacks.cc` | Module lifecycle and frame callback registration |

---

## Compatibility

### Requirements

| Component | Required |
|-----------|----------|
| KTPAMXX | Yes (for frame callback API) |
| Standard AMX Mod X | Partial (module loads, async won't work) |
| Metamod | Not needed |

### Known Limitations

- **Requires KTPAMXX** for proper async operation
- Standard AMX Mod X lacks `MF_RegModuleFrameFunc()` API
- Module gracefully handles missing API but transfers won't process

---

## Diagnostics

Every console line the module emits is prefixed `[CURL]`. Most are informational;
one is not.

### Startup

| Line | Meaning |
|------|---------|
| `[CURL] Module loaded (extension mode, using frame callbacks)` | Expected on KTPAMXX. Async transfers will be processed. |
| `[CURL] Module loaded` | `MF_RegModuleFrameFunc()` was not found. The module loaded, but **nothing drives transfers to completion** — async will not run. |

### Warnings

| Line | Action |
|------|--------|
| `WARNING: stale socket_map_ entry for fd N on open — close callback was missed` | **Investigate immediately.** It means libcurl violated its own close-callback contract. It has never fired in the field. Do not silence it. |
| `WARNING: Detach timeout after 5s, forcing cleanup` | A transfer did not wind down during shutdown. Cleanup proceeds; worth noting if it recurs. |
| `WARNING: detach-guard atexit registration failed (N)` | The shutdown guard could not register. Raises the risk of a segfault at process exit. |
| `WARNING: Plugin unloaded during async transfer (AMX ... no longer valid)` | Benign. The completion callback is skipped because its owner is gone. |
| `WARNING: Response body reached NB cap — truncating further data` | Benign. The response exceeded the 64KB `curl_get_response_body` buffer. |
| `WARNING: curl_easy_cleanup called on handle N while transfer is in progress — deferring cleanup` | Benign. Cleanup runs when the transfer finishes. |
| `WARNING: curl_easy_perform called on handle N while a transfer is already in progress — ignoring` | Plugin bug. The call is **refused** (1.3.14+) rather than corrupting the in-flight transfer. |
| `WARNING: curl_easy_reset called on handle N while transfer is in progress — ignoring` | Plugin bug, same contract as above. |

### Transfer timeouts (curl code 28)

A code 28 on a request the plugin fires during map load is almost always the caller's
budget, not the network: the map-load stall on a busy host can outlast a short
per-request timeout on its own, and the request that trips it is usually one a plugin
re-fires on every map. It is cosmetic where the payload does not matter — hosts have
logged bursts of these on days with no player traffic at all.

**Raise the caller's latch, not the timeout.** A longer timeout buries the one signal
that distinguishes a slow host from a hung transfer, and this class of 28 is the
cheapest evidence there is that a host's map-load stall is growing.

### Fatal

`[CURL] FATAL ERROR caught at asio-poll boundary` / `at C-callback boundary` — an
exception reached a C callback boundary and was contained there. Escaping it would
unwind through C and take the server down, so this line means the crash was
prevented, not that it happened. Report it.

---

## Version History

See [CHANGELOG.md](CHANGELOG.md) for full version history.

---

## Related Projects

### KTP Competitive Infrastructure

**Engine Layer:**
- **[KTP-ReHLDS](https://github.com/afraznein/KTPReHLDS)** - Modified ReHLDS engine
- **[KTP-ReAPI](https://github.com/afraznein/KTPReAPI)** - ReAPI without Metamod

**Module Layer:**
- **[KTPAMXX](https://github.com/afraznein/KTPAMXX)** - AMX Mod X with frame callbacks
- **[KTP CURL AMXX](https://github.com/afraznein/KTPAmxxCurl)** - This project

**Plugin Layer:**
- **[KTP Cvar Checker](https://github.com/afraznein/KTPCvarChecker)** - Uses curl for Discord webhooks
- **[KTP Match Handler](https://github.com/afraznein/KTPMatchHandler)** - Match management

### Upstream

- **[AmxxCurl](https://github.com/Polarhigh/AmxxCurl)** - Original project by Polarhigh
- **[libcurl](https://curl.haxx.se/libcurl/)** - Underlying HTTP library
- **[AMX Mod X](https://github.com/alliedmodders/amxmodx)** - Plugin platform

---

## License

**MIT License**

Based on [AmxxCurl](https://github.com/Polarhigh/AmxxCurl) by Igor Minin (Polarhigh).

See [LICENSE.txt](LICENSE.txt) for full text.

---

## Credits

**KTP Fork:**
- **Nein_** ([@afraznein](https://github.com/afraznein)) - KTPAMXX integration, Metamod removal

**Upstream AmxxCurl:**
- **Polarhigh** (Igor Minin) - Original module development
- **libcurl Team** - HTTP library
- **ASIO** - Async I/O library

---

## Links

- **Repository**: https://github.com/afraznein/KTPAmxxCurl
- **Upstream**: https://github.com/Polarhigh/AmxxCurl
- **Issues**: https://github.com/afraznein/KTPAmxxCurl/issues
- **libcurl Docs**: https://curl.haxx.se/libcurl/c/

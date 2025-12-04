# KTP CURL AMXX

**libcurl wrapper module for AMX Mod X with non-blocking async HTTP/FTP support**

A fork of [AmxxCurl](https://github.com/Polarhigh/AmxxCurl) modified to work without Metamod by using KTPAMXX's module frame callback API. Provides full libcurl easy interface functionality with SSL support for making HTTP requests, FTP uploads, and other network operations from AMX plugins.

Part of the [KTP Competitive Infrastructure](https://github.com/afraznein).

---

## What's New in v1.1.1-ktp

### No Metamod Required

KTP CURL AMXX operates as a pure AMXX module using KTPAMXX's frame callback API:

| Feature | Original AmxxCurl | KTP CURL AMXX |
|---------|------------------|---------------|
| Dependencies | AMX Mod X + Metamod | KTPAMXX only |
| Frame processing | Metamod StartFrame hook | Module frame callback |
| Binary | `amxxcurl_amxx_i386.dll` | Same |
| Performance | Standard | Identical |
| Compatibility | Requires Metamod | No Metamod needed |

### KTP Integration

- Uses `MF_RegModuleFrameFunc()` API for async processing
- Graceful fallback if API not available
- Compatible with KTP competitive infrastructure stack

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
copy amxxcurl_amxx_i386.dll "<game>/addons/ktpamx/modules/"

# Linux
cp amxxcurl_amxx_i386.so "<game>/addons/ktpamx/modules/"
```

### Step 3: Enable in modules.ini

Edit `<game>/addons/ktpamx/configs/modules.ini`:
```ini
; KTP CURL module
amxxcurl_amxx_i386.dll  ; Windows
; OR
amxxcurl_amxx_i386.so   ; Linux
```

### Step 4: Install Include Files

Copy `curl.inc` and `curl_consts.inc` to your scripting include directory:
```bash
cp amx_includes/*.inc "<amxmodx>/scripting/include/"
```

### Step 5: Verify Installation

Check server console on startup:
```
[CURL] Module loaded successfully
```

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

- **Premake5** - [Get it here](https://github.com/premake/premake-core)
- **Windows**: Visual Studio 2017+
- **Linux**: GCC with multilib support

### Windows Build

```bash
# Generate Visual Studio project
premake5 vs2017

# Open solution and build
# Or use MSBuild from command line
```

### Linux Build

```bash
# Generate Makefile
premake5 gmake

# Build
cd build/gmake
make
```

### Build Output

Binaries are placed in `bin/<config>/`:
- `amxxcurl_amxx_i386.dll` (Windows)
- `amxxcurl_amxx_i386.so` (Linux)

### Clean Build

```bash
premake5 clean
```

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

## Version History

### v1.1.1-ktp (2025-12) - KTPAMXX Integration

- **REMOVED: Metamod dependency** - Now uses KTPAMXX frame callback API
- **ADDED: MF_RegModuleFrameFunc()** - Module frame callback registration
- **ADDED: MF_UnregModuleFrameFunc()** - Module frame callback cleanup
- **MODIFIED: callbacks.cc** - Replaced StartFrame hook with frame callback
- **MODIFIED: moduleconfig.h** - Disabled USE_METAMOD, updated branding
- **COMPATIBLE: Original API unchanged** - All natives work identically

### Upstream (Polarhigh/AmxxCurl)

- Full libcurl easy interface wrapper
- Non-blocking transfers via curl_multi + ASIO
- SSL/TLS support via OpenSSL
- Cross-platform Windows/Linux

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

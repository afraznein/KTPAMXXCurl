# Changelog

All notable changes to KTP CURL AMXX will be documented in this file.

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

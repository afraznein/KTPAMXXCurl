# KTPAmxxCurl - Claude Code Context

**REQUIRED: Before modifying any C++ source in this repo, invoke the `cpp-dev` skill** (`.claude/skills/cpp-dev/SKILL.md`). It carries the exception-boundary rules, socket-lifecycle ownership, the handle-reuse contract, and the build/verify workflow; do not edit source without it loaded.

**A design that ships as a document is NOT done** — every proposal in a docs-only PR becomes a
tracked board item in the same act (operator ruling 2026-09-14). See `DESIGN_DOCS_ARE_NOT_DONE.md`.

## Build Command
To build this project, use:
```bash
wsl bash -c "cd '/mnt/n/Nein_/KTP Git Projects/KTPAmxxCurl' && bash build_linux.sh"
```

This will:
1. Build using CMake
2. Output to `build/amxxcurl_ktp_i386.so`
3. Auto-stage to `N:\Nein_\KTP Git Projects\KTP DoD Server\serverfiles\dod\addons\ktpamx\modules\`

## Project Structure
- `build_linux.sh` - WSL build script
- `CMakeLists.txt` - CMake build configuration
- `src/` - C++ source files
- `build/` - Build output (`amxxcurl_ktp_i386.so`)
- `amx_includes/` - AMX include files for plugins using this module

## Purpose
Non-blocking HTTP module for AMXX plugins. Enables async HTTP requests (GET/POST) with callbacks, used for Discord webhooks and HLStatsX integration.

## Build Output
| File | Destination |
|------|-------------|
| `amxxcurl_ktp_i386.so` | `addons/ktpamx/modules/` |

## Dependencies
- CMake
- libcurl (linked statically from deps/)
- GCC with 32-bit support

## Server Deployment

This project contains example Python/Paramiko scripts for SSH operations. Use these as templates for server management tasks.

**Key Scripts** (in `scripts/` folder, gitignored):
- `scripts/deploy_curl.py` - Deploy module to all server instances via SFTP
- `scripts/check_logs.py` - Check console logs across servers
- `scripts/restart_servers.py` - Restart game servers (requires permission)
- `scripts/claim_netdata.py` / `unclaim_netdata.py` - Netdata Cloud management
- `scripts/check_hlstatsx.py` - Check HLStatsX daemon status

**Server Credentials:** never in this repo (it is PUBLIC). The old table here
listed the pre-2026-05-31 rotated values. Current credentials and the full
paramiko SSH documentation live in the private root context:
`N:\Nein_\KTP Git Projects\CLAUDE.md` § Server Credentials.

## Related Projects
- `N:\Nein_\KTP Git Projects\KTPAMXX` - AMX Mod X fork (provides module API)
- `N:\Nein_\KTP Git Projects\KTPMatchHandler` - Primary consumer (Discord integration)
- `N:\Nein_\KTP Git Projects\KTP DoD Server` - Test server with staged modules

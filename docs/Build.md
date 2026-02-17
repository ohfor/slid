# Build Setup

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| Visual Studio 2022 | Latest | C++ desktop development workload |
| CMake | 3.21+ | Included with VS, or install separately |
| VCPKG | Latest | Package manager for C++ dependencies |
| Git | Latest | For FetchContent (CommonLibSSE-NG) |

## VCPKG Setup

```cmd
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

Set environment variable:
```
VCPKG_ROOT=C:\vcpkg
```

## Environment Paths

Fill in your local paths:

| Variable | Your Path | Example |
|----------|-----------|---------|
| `VCPKG_ROOT` | | `C:\vcpkg` |
| `PROJECT_ROOT` | | `D:\git\ohfor\slid-private` |
| `SKYRIM_DIR` | | `D:\SteamLibrary\steamapps\common\Skyrim Special Edition` |

## Configure

First-time setup (generates build files):

```cmd
cmake --preset release
```

Available presets: `debug`, `release`, `relwithdebinfo`

To reconfigure from scratch (after CMakeLists.txt changes):
```cmd
rmdir /s /q build 2>nul && cmake --preset release
```

## Build

```cmd
cmake --build build\release --config Release
```

Or with VsDevCmd for explicit VCPKG integration:
```cmd
cmd.exe /c "set VCPKG_ROOT=C:\vcpkg && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 && cd /d {PROJECT_ROOT} && cmake --build build\release --config Release"
```

Output: `build\release\Release\SLID.dll`

## Deploy

The recommended way to build and deploy is the all-in-one script:

```powershell
# C++ only — build, copy DLL, verify timestamps
powershell -ExecutionPolicy Bypass -File scripts/deploy.ps1

# C++ + all Papyrus scripts
powershell -ExecutionPolicy Bypass -File scripts/deploy.ps1 -Papyrus

# Papyrus only (skip C++ build)
powershell -ExecutionPolicy Bypass -File scripts/deploy.ps1 -PapyrusOnly
```

The script handles: cmake build, DLL copy to game folder, Papyrus compilation, `.pex` copy back to repo, and timestamp verification.

### Manual Deploy (alternative)

Copy the DLL to your game's SKSE plugins folder:
```cmd
copy /Y "build\release\Release\SLID.dll" "{SKYRIM_DIR}\Data\SKSE\Plugins\"
```

Or set `SKYRIM_MODS_PATH` environment variable to auto-deploy via CMake post-build.

## Log Location

```
%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\SLID.log
```

## Papyrus Compilation

> Full workflow guide: `.private/reference/papyrus-workflow.md`

Papyrus sources live in `scripts/papyrus/source/`. The compile loop:

1. Edit `.psc` in `scripts/papyrus/source/`
2. Compile to game directory: `scripts\papyrus\compile.bat`
3. Test in-game (requires game restart for modified scripts)
4. Copy compiled `.pex` back to repo: `copy /Y "{SKYRIM_DIR}\Data\Scripts\SLID_*.pex" scripts\papyrus\compiled\`
5. Commit both `.psc` and `.pex`

### Prerequisites

- **PapyrusCompiler.exe**: `{SKYRIM_DIR}\Papyrus Compiler\PapyrusCompiler.exe` (from Creation Kit)
- **Flags file**: `{SKYRIM_DIR}\Data\source\Scripts\TESV_Papyrus_Flags.flg`
- **SkyUI SDK** (if using MCM): Download `SKI_*.psc` files from [SkyUI GitHub](https://github.com/schlangster/skyui) (`dist/Data/Scripts/Source/`) to `{SKYRIM_DIR}\Data\Scripts\Source\`

### Manual Compilation

```cmd
"{SKYRIM_DIR}\Papyrus Compiler\PapyrusCompiler.exe" "scripts\papyrus\source\{ScriptName}.psc" ^
    -f="{SKYRIM_DIR}\Data\source\Scripts\TESV_Papyrus_Flags.flg" ^
    -i="{SKYRIM_DIR}\Data\Scripts\Source;{SKYRIM_DIR}\Data\source\Scripts;scripts\papyrus\source" ^
    -o="{SKYRIM_DIR}\Data\Scripts"
```

## ESP Files

> Full workflow guide: `.private/reference/esp-workflow.md`

ESP files are created and edited in [SSEEdit](https://www.nexusmods.com/skyrimspecialedition/mods/164). The authoritative copy lives in the game's `Data/` folder. The repo copy is at `dist/SLID.esp`. After editing in xEdit, copy the updated ESP to `dist/` and commit.

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `VCPKG_ROOT not set` | Set environment variable to your vcpkg install path |
| CommonLibSSE fetch fails | Check internet connection; FetchContent needs Git |
| `xbyak not found` | Run `vcpkg install xbyak:x64-windows-static-md` |
| DLL doesn't load | Check SKSE log for errors; verify Address Library installed |
| Forms return null on VR | Use `TESDataHandler::LookupForm`, not `LookupByEditorID` (see `.private/reference/vr-compatibility.md`) |
| `unknown type ski_configbase` | SkyUI SDK `.psc` files not on import path (see Papyrus section above) |
| `.pex` timestamps didn't update | Compilation silently failed — usually an import path issue. Check compiler output for errors |
| Script changes not taking effect | Game restart required for modified scripts (VM caches per session) |

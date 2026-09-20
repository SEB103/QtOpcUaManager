<!--
SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Release packaging

This folder turns a Release build of OPC UA Manager into ready-to-distribute
Windows artifacts. One build produces all three at once:

```
Build -> Canonical Deployment -> { Setup.exe (installer), *-win64.zip (portable), repository/ (updates) }
```

One script does everything: [`release.ps1`](release.ps1). All names and the
version come from a single file, [`product.json`](product.json).

---

## 1. Quick start

Open a terminal, go to the **project root** (`…\OpcUaManager\project`, the folder
that contains `packaging/`), and run the script with `-File`.

**Windows PowerShell / PowerShell 7 (`pwsh`):**

```powershell
cd E:\AI\OpcUaManager\project
powershell -ExecutionPolicy Bypass -File .\packaging\release.ps1
```

**cmd.exe:**

```bat
cd /d E:\AI\OpcUaManager\project
powershell -ExecutionPolicy Bypass -File .\packaging\release.ps1
```

**Git Bash (use forward slashes):**

```bash
cd /e/AI/OpcUaManager/project
powershell -ExecutionPolicy Bypass -File ./packaging/release.ps1
```

That's it. When it finishes you'll have the installer, the portable ZIP and the
update repository under `release/` (see section 3).

> The script finds its own location, so it also works with an absolute path from
> anywhere, e.g. `powershell -ExecutionPolicy Bypass -File "E:/AI/OpcUaManager/project/packaging/release.ps1"`.
> Always pass the script with `-File`; `powershell release.ps1` (without `-File`)
> does **not** work.

---

## 2. Prerequisites

Install these once:

- **Visual Studio 2022 / MSVC** (x64 tools). You do *not* need a "Developer"
  prompt — if `cl.exe` is not on `PATH`, `release.ps1` loads the VS environment
  itself.
- **Qt 6.11.1**, kit `msvc2022_64`, with the **`qtopcua`** module. Default path
  `C:/Qt/6.11.1/msvc2022_64` (override with `-QtPrefix`).
- **Qt Installer Framework** (`binarycreator.exe`, `repogen.exe`). Default path
  `C:\Qt\Tools\QtInstallerFramework\4.10` (override with `-QtIfwRoot` or the
  `QTIFW_ROOT` environment variable).

---

## 3. What you get

Everything lands in `release/` (git-ignored):

```
release/
  OPC-UA-Manager-<version>/           # the app folder, ready to run
  OPC-UA-Manager-<version>-Setup.exe  # the installer
  OPC-UA-Manager-<version>-win64.zip  # portable package
  repository/                         # update repository (Updates.xml + data)
```

The `<version>` is taken from `product.json`.

---

## 4. Options and single stages

The pipeline runs six stages in order. To run just one, use `-Stage` (earlier
stages must already have produced their output):

| Stage | What it does |
|-------|--------------|
| `Build` | Configure + build Release, then generate the offline documentation site (`docs` target -> `build/release/doc/site`). |
| `Deploy` | `cmake --install` -> `release/<name>-<version>/` (the app folder with all Qt runtime, and `doc/site/` for in-app Help). |
| `Verify` | Checks the deployment (exe, plugins, OpenSSL, licenses, the documentation site and the Qt WebView runtime; no debug/dev files). |
| `PackageInstaller` | Builds `…-Setup.exe`. |
| `PackagePortable` | Builds `…-win64.zip` (adds the `portable.ini` marker). |
| `GenerateRepository` | Builds `repository/`. |

Common examples (from the project root):

```powershell
# Only rebuild the installer (reuses the existing deployment - fast)
powershell -ExecutionPolicy Bypass -File .\packaging\release.ps1 -Stage PackageInstaller

# Dark-themed installer wizard
powershell -ExecutionPolicy Bypass -File .\packaging\release.ps1 -Theme dark

# Override the version (otherwise taken from product.json)
powershell -ExecutionPolicy Bypass -File .\packaging\release.ps1 -Version 0.2.0

# Point at a different Qt IFW / Qt
powershell -ExecutionPolicy Bypass -File .\packaging\release.ps1 `
  -QtIfwRoot "C:/Qt/Tools/QtInstallerFramework/4.11" `
  -QtPrefix  "C:/Qt/6.11.1/msvc2022_64"
```

---

## 5. Building just the application (development)

For day-to-day development you don't need the packaging script — build the app
directly with the CMake presets (see the top-level [`README.md`](../README.md)):

```powershell
cd E:\AI\OpcUaManager\project
cmake --preset windows-msvc2022-release
cmake --build --preset windows-msvc2022-release
```

Or simply open `CMakeLists.txt` in **Qt Creator** and build/run as usual.

To run the whole release from Qt Creator, add it as an external tool:
**Tools → External → Configure… → Add Tool**, then set
Executable `powershell`,
Arguments `-NoProfile -ExecutionPolicy Bypass -File "%{ActiveProject:Path}/packaging/release.ps1"`,
Working directory `%{ActiveProject:Path}`.

---

## 6. Installing the result

**Graphical install:** double-click `OPC-UA-Manager-<version>-Setup.exe` and follow
the wizard (choose the folder, accept the licence, optional desktop shortcut).
Default location `C:\Program Files\OPC UA Manager`. Because the installer is not
code-signed yet, Windows SmartScreen shows a warning: click *More info → Run anyway*.

**Silent install (command line):**

```bat
OPC-UA-Manager-0.1.0-Setup.exe install --root "C:\Program Files\OPC UA Manager" --accept-licenses --default-answer --confirm-command
```
(installing into `Program Files` needs administrator rights; a folder inside your
user profile does not).

**Update or uninstall:** use the Maintenance Tool created in the install folder
(`OpcUaManagerMaintenanceTool.exe`), from its Start-Menu shortcut or:

```bat
"C:\Program Files\OPC UA Manager\OpcUaManagerMaintenanceTool.exe" purge --confirm-command --accept-licenses --default-answer
```

**Portable (no install):** unzip `OPC-UA-Manager-<version>-win64.zip` anywhere and
run `appOpcUaManager.exe`. All data stays next to it (`config/`, `data/`, `logs/`),
so you can move the folder freely.

---

## 7. Where the app stores data (installed vs portable)

The same executable picks its mode at runtime (`src/core/apppaths.*`):

- **Installed** (no `portable.ini`): program files stay in the install folder;
  user data (settings, logs, PKI, database) go to `%LOCALAPPDATA%\OpcUaManager`,
  projects default to `Documents\OpcUaManager`.
- **Portable** (the ZIP ships a `portable.ini` marker next to the exe): all user
  data lives inside the app folder (`config/`, `data/`, `logs/`).

Uninstalling removes the program but **keeps user data**. Updates replace the
program files and preserve user data.

---

## 8. Themes

`-Theme light` (default) or `-Theme dark` selects the wizard style
(`installer/styles/*.qss`). It matches the app's accent; the Modern wizard layout
itself is fixed by Qt IFW, so this is an accent/branding pass, not a full re-skin.

---

## 9. Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| `… wird nicht als Name einer Skriptdatei erkannt` / *is not recognized as the name of a script file* | Wrong path or you're already inside `packaging/`. Run from the project root, or drop the `packaging/` prefix. Always use `-File`. |
| `… cannot be loaded because running scripts is disabled` | Add `-ExecutionPolicy Bypass` (as in all examples above). |
| `PermissionDenied … Setup.exe … Remove-Item` | A previously built `Setup.exe` is still open. **Close the running installer window** before rebuilding — a running exe locks its file. |
| `binarycreator not found` / `repogen not found` | Qt IFW isn't at the default path. Pass `-QtIfwRoot "…"` or set `QTIFW_ROOT`. |
| `CMake configure/build failed` | Qt kit not found or wrong. Pass `-QtPrefix "C:/Qt/6.11.1/msvc2022_64"`; make sure the `qtopcua` module is installed. |
| Git Bash mangles a path argument | Use forward slashes and quotes; if needed prefix the command with `MSYS_NO_PATHCONV=1`. |

---

## 10. Notes for maintainers

**Single source of truth.** `product.json` holds the product name, version,
publisher, URLs and installer identity. Change the version there (or pass
`-Version`) — CMake and the packaging scripts both read it.

**Licensing.** The app ships Qt as LGPL-3.0 shared libraries (see
`THIRD_PARTY_NOTICES.md`). The installer and Maintenance Tool are themselves Qt
Installer Framework programs by The Qt Company, so their distribution carries the
IFW/Qt attribution obligation — add that attribution before a public release.
Code signing is not set up yet; unsigned installers trigger SmartScreen until a
certificate is added (a signing step can be inserted after the build/package
stages).

**Local update test (already verified).** Install version A, publish version B to
`release/repository/`, then update in place. Note the IFW CLI: repository options
use dashed names placed **before** the `update` command, and `--default-answer`
is mutually exclusive with `--accept-messages`:

```bat
"C:\Temp\opcua\OpcUaManagerMaintenanceTool.exe" --set-temp-repository "file:///E:/AI/OpcUaManager/project/release/repository" --accept-licenses --default-answer --confirm-command update
```

**Offline documentation (Help).** The `Build` stage generates a self-contained
QDoc site (`build/release/doc/site`: a per-language user + developer guide plus the
C++/QML API reference) and the install ships it to `doc/site` next to the exe. The
in-app **Help → Documentation** entry opens it locally through Qt WebView, which on
Windows renders with the Microsoft Edge WebView2 control; `windeployqt` deploys
`Qt6WebView.dll`, the `QtWebView` QML module and `WebView2Loader.dll`, and `Verify`
checks all three. The WebView2 runtime itself is a Microsoft system component (see
`THIRD_PARTY_NOTICES.md`). Any code change requires re-running `release.ps1` (the
`Build` stage regenerates the docs so the shipped Help matches the shipped app).

**Future rename / server (not decided yet).** The design keeps the internal
identity separate from the display name, so *if* the product is ever renamed
(e.g. to "OPC UA Management Studio") it can ship as an ordinary update rather
than a new installer. In that case change only `displayName` and `installDirName`
in `product.json`; keep `identifier`, `orgDomain`, `exeName` and `componentId`
unchanged. A second executable (e.g. an OPC UA server) can later be added as a
`server/` subfolder and its own component. None of this is required today.

---

## 11. Configure release commands in Qt Creator

Qt Creator can expose the complete release pipeline and each individual stage
under **Tools -> External**. These entries are IDE shortcuts for
`packaging/release.ps1`; they do not replace or modify the script.

Open the project-level `CMakeLists.txt` in Qt Creator so that
`%{ActiveProject:Path}` resolves to the project root (the directory containing
`packaging/`). Then open **Tools -> External -> Configure...**, select
**Add -> Add Category**, and name the category `OPC UA Manager Release`.

### Common settings

For every tool listed below, select **Add -> Add Tool** and use these common
values:

| Field | Value |
|-------|-------|
| Executable | `powershell.exe` |
| Working directory | `%{ActiveProject:Path}` |
| Output | `Show in General Messages` |
| Error output | `Show in General Messages` |
| Base environment | `Active Build Environment of the Active Project` |
| Environment | No changes |
| Modifies current document | Disabled |
| Input | Empty |

Use the active project's build environment because the commands also use
`%{ActiveProject:Path}`. This keeps the script path and inherited toolchain,
Qt kit, `PATH`, `INCLUDE`, and `LIB` variables tied to the same project. By
contrast, `Current Build Environment` follows the project that owns the file
currently open in the editor and can therefore select a different environment
in a multi-project session. Both choices normally resolve to the same values
when only one project is open.

The **Description** field is the command name shown in the
**Tools -> External -> OPC UA Manager Release** menu. The **Input** field is
standard input (`stdin`) passed to the process; it is not a comment field.
`release.ps1` does not read standard input, so leave it empty. Qt Creator does
not provide a separate long-comment field for an external tool; use the
descriptive command names below and keep detailed explanations in this README.

After adding or changing a tool, select **Apply**. The command then appears
under **Tools -> External -> OPC UA Manager Release**.

### Complete release commands

The two complete-release commands run every stage in this order:

```text
Build -> Deploy -> Verify -> PackageInstaller -> PackagePortable -> GenerateRepository
```

They are the normal commands for producing a distributable release. The theme
changes only the Qt Installer Framework wizard; it does not change the
application theme.

#### 01 Full Release - Light

**Arguments:**

```text
-NoProfile -ExecutionPolicy Bypass -File "%{ActiveProject:Path}/packaging/release.ps1" -Stage All -Theme light
```

Builds Release binaries and offline documentation, creates and verifies the
canonical deployment, and produces the light-themed offline installer, portable
ZIP, and update repository.

#### 02 Full Release - Dark

**Arguments:**

```text
-NoProfile -ExecutionPolicy Bypass -File "%{ActiveProject:Path}/packaging/release.ps1" -Stage All -Theme dark
```

Produces the same artifacts as `01 Full Release - Light`, but uses the dark
installer wizard style.

### Individual stage commands

The commands below are optional shortcuts for rerunning one stage without
repeating the whole pipeline. A single-stage command does not run its
prerequisites automatically. Use `01 Full Release - Light` or
`02 Full Release - Dark` when a complete, fresh release is required.

#### 10 Build Release + Documentation

**Arguments:**

```text
-NoProfile -ExecutionPolicy Bypass -File "%{ActiveProject:Path}/packaging/release.ps1" -Stage Build
```

Configures and builds the Release application and server runtime under
`build/release`, then builds the `docs` target. The generated offline site is
placed under `build/release/doc/site`. This stage does not create the canonical
deployment, installer, ZIP, or update repository.

#### 20 Deploy - CMake Install

**Arguments:**

```text
-NoProfile -ExecutionPolicy Bypass -File "%{ActiveProject:Path}/packaging/release.ps1" -Stage Deploy
```

Runs `cmake --install` on the existing `build/release` tree and creates the
self-contained canonical deployment under
`release/<artifact-name>-<version>/`. This is a distributable application
folder, not an installation into `Program Files`. Run `10 Build Release +
Documentation` first whenever binaries or documentation have changed.

#### 30 Verify Deployment

**Arguments:**

```text
-NoProfile -ExecutionPolicy Bypass -File "%{ActiveProject:Path}/packaging/release.ps1" -Stage Verify
```

Checks the existing canonical deployment for the application and server
executables, Qt plugins, OpenSSL, licensing files, offline documentation, and
Qt WebView runtime, and rejects unwanted debug or development files. Run
`20 Deploy - CMake Install` first whenever the deployment has changed.

#### 40 Repack Offline Installer - Light

**Arguments:**

```text
-NoProfile -ExecutionPolicy Bypass -File "%{ActiveProject:Path}/packaging/release.ps1" -Stage PackageInstaller -Theme light
```

Reuses the existing canonical deployment and creates the light-themed
`<artifact-name>-<version>-Setup.exe`. It does not rebuild or redeploy the
application. Use it after installer metadata, scripts, images, or styling have
changed while the deployment itself remains current.

#### 41 Repack Offline Installer - Dark

**Arguments:**

```text
-NoProfile -ExecutionPolicy Bypass -File "%{ActiveProject:Path}/packaging/release.ps1" -Stage PackageInstaller -Theme dark
```

Works like `40 Repack Offline Installer - Light`, but uses the dark installer
wizard style.

#### 50 Repack Portable ZIP

**Arguments:**

```text
-NoProfile -ExecutionPolicy Bypass -File "%{ActiveProject:Path}/packaging/release.ps1" -Stage PackagePortable
```

Reuses the existing canonical deployment, adds the `portable.ini` marker, and
creates `<artifact-name>-<version>-win64.zip`. It does not rebuild or redeploy
the application.

#### 60 Regenerate Update Repository

**Arguments:**

```text
-NoProfile -ExecutionPolicy Bypass -File "%{ActiveProject:Path}/packaging/release.ps1" -Stage GenerateRepository
```

Reuses the existing canonical deployment and regenerates
`release/repository/`, including `Updates.xml`. This creates local repository
files only; it does not upload or publish them. The product's update support is
currently disabled in `product.json`, and the configured channel URLs must be
replaced before publishing a real update repository.

### Recommended menu

The resulting Qt Creator menu can be organized as follows:

```text
Tools
  External
    OPC UA Manager Release
      01 Full Release - Light
      02 Full Release - Dark
      10 Build Release + Documentation
      20 Deploy - CMake Install
      30 Verify Deployment
      40 Repack Offline Installer - Light
      41 Repack Offline Installer - Dark
      50 Repack Portable ZIP
      60 Regenerate Update Repository
```

For an ordinary release, run `01 Full Release - Light` (or the dark variant).
Use the numbered stage commands only when their prerequisite output is already
current and only that stage needs to be repeated. All commands read the default
version from `packaging/product.json`; if the version changes, run a complete
release or at least repeat `Build` and `Deploy` before repackaging artifacts.

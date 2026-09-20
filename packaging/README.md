<!--
SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Release packaging

This folder turns a Release build of OPC UA Manager into ready-to-distribute
Windows artifacts. One build produces all three at once:

```
Build -> Canonical Deployment -> { Setup.exe (hybrid installer), *-win64.zip (portable), repository/ (updates) }
```

One script does everything: [`release.ps1`](release.ps1). All names and the
version come from a single file, [`product.json`](product.json). The same script
runs unchanged in GitHub Actions, which publishes the result as a GitHub Release
and the update repository as a GitHub Pages site (see section 12).

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
  OPC-UA-Manager-<version>-Setup.exe  # the hybrid installer
  OPC-UA-Manager-<version>-win64.zip  # portable package
  repository/                         # update repository (Updates.xml + data)
```

The `<version>` is taken from `product.json` — it is the only place the version
is defined, and it is the same value CMake compiles into the executable, so the
artifact names, the installer metadata and the running program can never
disagree. There is deliberately no command-line override.

---

## 4. Options and single stages

The pipeline runs six stages in order. To run just one, use `-Stage` (earlier
stages must already have produced their output):

| Stage | What it does |
|-------|--------------|
| `Build` | Configure + build Release, then generate the offline documentation site (`docs` target -> `build/release/doc/site`). |
| `Deploy` | `cmake --install` -> `release/<name>-<version>/` (the app folder with all Qt runtime, and `doc/site/` for in-app Help). |
| `Verify` | Checks the deployment (exe, plugins, OpenSSL, licenses, the documentation site and the Qt WebView runtime; no debug/dev files). |
| `PackageInstaller` | Builds the **hybrid** `…-Setup.exe` (`binarycreator --hybrid`: full offline payload plus the stable update repository URL), then dumps it with `devtool` and fails unless the binary really is hybrid. |
| `PackagePortable` | Builds `…-win64.zip` (adds the `portable.ini` marker). |
| `GenerateRepository` | Builds `repository/` with `repogen` (`Updates.xml` + per-component archives). |

Common examples (from the project root):

```powershell
# Only rebuild the installer (reuses the existing deployment - fast)
powershell -ExecutionPolicy Bypass -File .\packaging\release.ps1 -Stage PackageInstaller

# Dark-themed installer wizard
powershell -ExecutionPolicy Bypass -File .\packaging\release.ps1 -Theme dark

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

**Update (installed):** in the application choose **Info → Check for updates…**.
When a newer version exists the dialog offers **Install update**: the application
closes (unsaved project changes are prompted for first) and starts the Maintenance
Tool in updater mode (`OpcUaManagerMaintenanceTool.exe --start-updater`), which
downloads the new version from the stable update repository and replaces the
program files. User data is preserved. The same update can be started from the
Maintenance Tool's Start-Menu shortcut.

**Uninstall:** use the Maintenance Tool created in the install folder
(`OpcUaManagerMaintenanceTool.exe`), from its Start-Menu shortcut or:

```bat
"C:\Program Files\OPC UA Manager\OpcUaManagerMaintenanceTool.exe" purge --confirm-command --accept-licenses --default-answer
```

**Portable (no install):** unzip `OPC-UA-Manager-<version>-win64.zip` anywhere and
run `appOpcUaManager.exe`. All data stays next to it (`config/`, `data/`, `logs/`),
so you can move the folder freely.

**Update (portable):** a portable copy is never updated in place. **Info → Check
for updates…** offers **Open download page** instead, which opens the GitHub
Release of the new version; download the new `…-win64.zip`, unzip it and copy
your `config/`, `data/` and `logs/` folders over (or unzip on top of the old
folder). A development build started from Qt Creator (installed mode but no
Maintenance Tool next to the executable) gets the same download-page fallback.

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
publisher, URLs, installer identity and the update endpoints. Change the version
only there — CMake, the packaging script and the release workflow all read it,
and the workflow refuses a tag that does not match it.

**Hybrid installer.** `Setup.exe` is built with `binarycreator --hybrid`: it
carries the complete payload (installs offline, like before) *and* keeps the
`<RemoteRepositories>` from `installer/config/config.xml.in`, whose URL is
`update.channels.stable` in `product.json`. The Maintenance Tool it creates
therefore checks that repository for newer versions. `release.ps1` verifies the
result with `devtool dump` (the binary's `config-internal.ini` must record
`hybridInstaller=true`). A plain `--offline-only` installer would never update.

**Update repository.** `repogen` turns the same canonical deployment into
`release/repository/`: `Updates.xml` plus 7z archives per component. `Updates.xml`
is generated — never edit it by hand; regenerate with `-Stage GenerateRepository`.
The published copy lives at `https://seb103.github.io/QtOpcUaManager/updates/stable/`
(GitHub Pages, section 12). The update path stays valid across versions because the
component id `com.opcuamanager.app` never changes. The channel URL ends with `/`;
the IFW 4.10 Maintenance Tool then requests `…/updates/stable//Updates.xml`, which
GitHub Pages serves normally (verified), so keep the URL exactly as configured.

**In-app update check.** `update.enabled` in `product.json` gates the feature;
it is `true`. The application queries the GitHub Releases API
(`update.releasesApiUrl`), compares the latest tag with its own version and, when
newer, offers *Install update* (installed copy: starts the Maintenance Tool with
`--start-updater` and quits) or *Open download page* (portable copy, or no
Maintenance Tool present). The automatic startup check only reports; it never
starts the updater.

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
files only; it does not upload or publish them — publishing is done by the
release workflow (section 12).

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

---

## 12. Publishing: GitHub Actions, Releases and Pages

`.github/workflows/release.yml` runs the same `packaging/release.ps1 -Stage All`
on a `windows-2022` runner with Qt 6.11.1 (`jurplel/install-qt-action`) and Qt
Installer Framework 4.10 (`tools_ifw,qt.tools.ifw.410`, verified by file version
before use). Nothing is published unless every earlier job succeeds.

| Trigger | What happens |
|---------|--------------|
| push of a tag `vX.Y.Z` | `check` (tag must be `vX.Y.Z` **and** equal `product.json` `version`) → `test` (configure, build, `ctest`, `qmllint`) ∥ `build` (release pipeline, artifact/version consistency check) → `pages` (publish the update repository) → `release` (GitHub Release with the installer and the ZIP). |
| `workflow_dispatch` (Actions → Release → Run workflow) | Dry run: `check`, `test`, `build` only. Artifacts are attached to the workflow run; no Release, no Pages deployment, no version change. |

Three kinds of output:

- **Workflow artifacts** (`installer`, `portable`, `repository`) — attached to
  every run, including dry runs; useful to inspect what a tag build would publish.
- **GitHub Release assets** — `…-Setup.exe` and `…-win64.zip` on
  `https://github.com/SEB103/QtOpcUaManager/releases/latest`. This is what the
  in-app check reads (via the Releases API) and what the download page offers.
- **GitHub Pages site** — `index.html` (from `packaging/pages/index.html`) plus
  the *complete* `repogen` output under `updates/stable/`
  (`Updates.xml`, `com.opcuamanager.app/*.7z`, metadata). The Maintenance Tool of
  every installed copy fetches updates from there. The site is deployed with
  `configure-pages` / `upload-pages-artifact` / `deploy-pages` from the workflow
  artifact; no `gh-pages` branch, no generated file is ever committed.

Job permissions are minimal: `contents: read` for check/test/build,
`pages: write` + `id-token: write` for the Pages job only, `contents: write` for
the Release job only. Pages is deployed *before* the Release is created so that
the update repository is already online when the API starts reporting the new
version.

### Releasing a new version

1. Set the new version in `packaging/product.json` (`"version": "X.Y.Z"`, nothing
   else; do not bump it anywhere else — there is no other source).
2. Commit: `git commit -am "release: X.Y.Z"`.
3. Tag the commit: `git tag vX.Y.Z`.
4. Push commit and tag: `git push origin master` then `git push origin vX.Y.Z`
   (or `git push origin master vX.Y.Z`).
5. Watch **Actions → Release**: all five jobs must be green. If `check` fails,
   the tag and `product.json` disagree — fix `product.json`, commit, and move or
   recreate the tag (never edit `Updates.xml` or the Release by hand).
6. Verify the **Release** page shows `…-Setup.exe` and `…-win64.zip`, and that
   `https://seb103.github.io/QtOpcUaManager/updates/stable/Updates.xml` shows
   `<Version>X.Y.Z</Version>`.
7. Optional end-to-end check on a machine with the previous version installed:
   **Info → Check for updates… → Install update** (or Maintenance Tool →
   *Update components*).

### Manual GitHub settings (once)

- **Settings → Pages → Build and deployment → Source: GitHub Actions** (the
  workflow deploys an artifact; no branch is used).
- **Settings → Actions → General → Workflow permissions**: the workflow declares
  per-job permissions (`contents: write` only in the Release job, `pages` /
  `id-token` only in the Pages job), so the default *Read repository contents
  and packages permissions* is sufficient; only make sure Actions are allowed
  to run for the repository.
- **Settings → Environments → `github-pages`** is created automatically by the
  first deployment; if deployment branch/tag protection is enabled there, allow
  tags `v*` (the Pages job runs on tag refs).
- Nothing else: no secrets, no certificates (the installer is not code-signed).


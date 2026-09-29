# OpcUaManager

OpcUaManager is a Qt Quick desktop application for discovering OPC UA servers, selecting endpoints, connecting with supported authentication modes, and browsing the server address space.

The project is currently in the stabilization phase before further product development.

**Download latest release:** <https://github.com/SEB103/QtOpcUaManager/releases/latest>
(Windows installer, portable ZIP and the source code archives). See
[Installation](#installation) for the options and for the Windows warnings about
unsigned downloads. How releases are built and published is documented in
[`packaging/README.md`](packaging/README.md).

**User manual:** <https://github.com/SEB103/QtOpcUaManager/wiki> (English,
German, French, Italian, Russian, Ukrainian). The same manual ships with the
application (**Info → Documentation**) together with the API reference; both are
generated from `doc/` by QDoc, the wiki pages by `doc/qdoc2wiki.py`.

## Installation

Every [release](https://github.com/SEB103/QtOpcUaManager/releases/latest) offers
three ways to get OpcUaManager on Windows x64:

1. **Installer** — `OPC-UA-Manager-<version>-Setup.exe`. Installs the application
   (default `C:\Program Files\OPC UA Manager`), creates Start-menu shortcuts and the
   Maintenance Tool, and receives updates through **Info → Check for updates…**.
   Uninstall with the Maintenance Tool; user data is kept.
2. **Portable ZIP** — `OPC-UA-Manager-<version>-win64.zip`. Extract the archive
   into any writable folder and run `appOpcUaManager.exe`. Settings, projects and
   logs stay next to the executable; update by replacing the folder.
3. **Build from source** — download *Source code (zip)* or *Source code (tar.gz)*
   from the same release page, or clone the repository:

   ```powershell
   git clone https://github.com/SEB103/QtOpcUaManager.git
   ```

   Then build it with CMake or Qt Creator
   ([Configure and build with CMake presets](#configure-and-build-with-cmake-presets)),
   or produce your own installer and portable ZIP with the release script
   ([Build your own installer](#build-your-own-installer)).

GitHub shows a SHA-256 digest next to each downloaded file on the release page.
To check a download, compare it with:

```powershell
Get-FileHash .\OPC-UA-Manager-<version>-Setup.exe -Algorithm SHA256
```

### Windows installation notice

OPC UA Manager is a young open-source project and is currently distributed
without a digital code-signing certificate.

As a result, Microsoft Edge, Microsoft Defender SmartScreen, or Windows may
display a warning about an unknown publisher or an unrecognized file when
downloading or running the installer. This is expected for the current releases
and does not by itself indicate that the installer contains malicious software.

The project has applied for free code signing through the SignPath Foundation
program, but at its current stage it does not yet meet the program's
requirements regarding public visibility and community adoption.

If Windows blocks the normal download or execution of the installer, the
application can still be installed using the options provided by Windows for
unrecognized applications:

- **Microsoft Edge blocks the download** ("… isn't commonly downloaded"): open
  the Downloads list, choose **… → Keep**, then **Show more → Keep anyway**.
- **"Windows protected your PC"** (Defender SmartScreen) when starting
  `Setup.exe`: choose **More info → Run anyway**.
- **Portable ZIP**: before extracting, right-click the ZIP, choose
  **Properties**, tick **Unblock** and confirm (or run
  `Unblock-File .\OPC-UA-Manager-<version>-win64.zip`). Otherwise every extracted
  file inherits the download mark and SmartScreen warns when it starts.

On Windows 11 with **Smart App Control** turned on, unsigned applications may be
blocked without a *Run anyway* option, because Smart App Control offers no
per-application exceptions. Download releases only from this repository's
[Releases page](https://github.com/SEB103/QtOpcUaManager/releases) and check the
SHA-256 digest if in doubt.

Code signing may be added in the future as the project grows and becomes more
widely adopted.

## Project baseline

- Qt 6.11.x
- C++20
- Microsoft Visual Studio 2022 / MSVC 2022
- CMake 3.21 or newer
- Windows x86_64
- Qt OPC UA with the open62541 backend

Qt 6.11 requires at least C++17. This repository explicitly selects C++20 and uses the MSVC 2022 toolchain for the supported Windows target.

## Required Qt modules

Install the following modules for the selected Qt 6.11 MSVC 2022 kit:

- Qt Core
- Qt GUI
- Qt Network
- Qt QML
- Qt Quick
- Qt Quick Controls 2
- Qt Quick Dialogs 2
- Qt SQL
- Qt SVG
- Qt WebView (documentation viewer)
- Qt OPC UA
- Qt Linguist Tools
- Qt Test and Qt Quick Test for tests

The Qt OPC UA installation must contain a usable backend plugin, normally the open62541 backend.
If the Qt online installer does not offer Qt OPC UA for your kit, build the module from
the `qtopcua` sources tagged for your Qt version and install it into the kit;
`.github/actions/build-qtopcua/action.yml` contains the exact configuration used by CI.

Further build prerequisites:

- Git and internet access during the first configure: the embedded server runtime
  fetches open62541 v1.4.14 with CMake `FetchContent`.
- OpenSSL 3 development files for the server runtime's encryption; the OpenSSL from
  the Qt Maintenance Tool (`C:\Qt\Tools\OpenSSLv3\Win_x64`) is detected
  automatically, otherwise set `OPENSSL_ROOT_DIR`.

## Architecture

```text
QML user interface
    |
    v
OpcUaManager                 GUI-thread facade
    |
    | queued signals and value snapshots
    v
OpcUaService                 dedicated OPC UA worker thread
    |
    v
QOpcUaClient / backend plugin
```

`QOpcUaNode` objects remain in the OPC UA worker thread. Browse results cross the thread boundary as copied `OpcUaNodeData` values and are applied to `OpcUaModel` in the GUI thread.

Main directories:

```text
app/                         application startup and runtime logging
src/core/                    OPC UA service and transferable node data
src/models/                  address-space tree model
src/qmlapi/                  QML-facing facade
qml/                         application and reusable QML modules
resources/                   Qt Quick Controls configuration and SVG resources
pki/                         empty initial OPC UA PKI directory structure
cmake/                       Qt OPC UA and Windows runtime helpers
tests/                       unit, QML smoke, and optional integration tests
translations/                Qt Linguist translation sources
```

## Configure and build with CMake presets

Get the sources first, either as *Source code (zip)* from a
[release](https://github.com/SEB103/QtOpcUaManager/releases) or with
`git clone https://github.com/SEB103/QtOpcUaManager.git`.

Set `QTDIR` to the root of the selected Qt 6.11 MSVC 2022 kit, for example:

```powershell
$env:QTDIR = "C:\Qt\6.11.1\msvc2022_64"
```

Configure and build Debug:

```powershell
cmake --preset windows-msvc2022-debug
cmake --build --preset windows-msvc2022-debug
```

Configure and build Release:

```powershell
cmake --preset windows-msvc2022-release
cmake --build --preset windows-msvc2022-release
```

The presets use the Visual Studio 2022 x64 generator and create build trees below `build/`.
`CMakeUserPresets.json` is ignored and may be used for machine-local overrides.

## Configure in Qt Creator

Open the root `CMakeLists.txt` or import `CMakePresets.json`, then select a Qt 6.11.x MSVC 2022 64-bit kit. Keep the project standard at C++20. Local `.user` and `.qtcreator` data must not be committed.

## Build your own installer

`packaging/release.ps1` builds the same artifacts as a GitHub release — the canonical
deployment folder, the `Setup.exe` installer, the portable ZIP and the update
repository — under `release/`:

```powershell
powershell -ExecutionPolicy Bypass -File .\packaging\release.ps1
```

It additionally needs the Qt Installer Framework (default
`C:\Qt\Tools\QtInstallerFramework\4.10`). Prerequisites, single stages and options
are described in [`packaging/README.md`](packaging/README.md). Self-built installers
are unsigned as well, so the notes of the
[Windows installation notice](#windows-installation-notice) apply to them too.

## Run tests

After building a preset:

```powershell
ctest --preset windows-msvc2022-debug
```

The test set contains:

- `OpcUaModel.Unit` — tree model behavior, browse snapshots, errors, retry, and reset;
- `OpcUaManager.QmlSmoke` — QML component loading smoke test;
- `OpcUa.ExternalIntegration` — optional live-server test.

The live-server test is skipped unless a test server URL is provided:

```powershell
$env:OPCUAMANAGER_TEST_SERVER_URL = "opc.tcp://127.0.0.1:4840"
ctest --preset windows-msvc2022-debug -R OpcUa.ExternalIntegration --output-on-failure
```

## QML lint and formatting

CMake creates QML lint targets for the QML modules:

```powershell
cmake --build --preset windows-msvc2022-debug --target all_qmllint
```

QML context-property settings are stored in `qml/.contextProperties.ini`.

Formatting should be applied separately from functional changes and reviewed before commit.

## OPC UA connection workflow

1. Select an installed OPC UA backend.
2. Enter a discovery URL, for example `opc.tcp://127.0.0.1:4840`.
3. Run **Find Servers**.
4. Select a server and authentication mode.
5. Run **Get Endpoints**.
6. Select a compatible endpoint and connect.

The option to rewrite an advertised endpoint host and port to the discovery URL is disabled by default. Enable it only for servers that advertise an unreachable host name or port.

## PKI and certificate authentication

The repository contains only an empty PKI directory structure. Never commit production private keys.

Expected initial files for certificate authentication:

```text
pki/own/certs/client.der
pki/own/private/client.pem
```

At runtime, the initial PKI tree is copied to the writable application data directory. The exact path is reported by the application log. The application identity used for certificate authentication is loaded from the certificate so the Application URI remains consistent with it.

Server certificates are not trusted automatically. Review an unknown certificate out of band, then place the approved certificate in the runtime PKI `trusted/certs` directory and reconnect. Rejected or unverified certificates must not be accepted merely to make a connection succeed.

See `pki/README.md` for details.

## Runtime logs

Release builds write `app.log` below Qt's writable application-local data directory instead of the installation directory. Log records include timestamp and severity. Debug output remains available through the debugger and standard Qt logging facilities.

## Deployment

The project uses `qt_generate_deploy_qml_app_script()` for installation deployment. On Windows, the development build can additionally copy the Qt OPC UA backend and optional OpenSSL runtime files next to the executable through `OPCUAMANAGER_COPY_WINDOWS_RUNTIME`.

Before distributing binaries, verify all Qt, open62541, OpenSSL, and icon-license obligations. See `THIRD_PARTY_NOTICES.md`.

## Code signing

Release binaries are currently **not code-signed**; see the
[Windows installation notice](#windows-installation-notice). The release workflow
already contains an optional SignPath signing step that stays inactive until the
project has a signing certificate; see
[`packaging/README.md`](packaging/README.md#13-code-signing-signpath-foundation).

## Privacy

This program will not transfer any information to other networked systems unless
specifically requested by the user or the person installing or operating it. The
application connects only to the OPC UA servers the user configures, and — if the
user leaves the optional startup check enabled
(**Settings → Updates → Check for updates automatically on startup**) or triggers
it manually (**Info → Check for updates…**) — to the GitHub Releases API to look up
the latest published version.

## License

OpcUaManager is licensed under the **GNU General Public License v3.0 or later**
(`GPL-3.0-or-later`).

- `LICENSE` — the full GPL-3.0 text that governs the OpcUaManager source code.
- `NOTICE` — copyright and a summary of the license and third-party components.
- `THIRD_PARTY_NOTICES.md` — per-component third-party notices (Qt, open62541,
  OpenSSL, Google Material Symbols).
- `LICENSES/` — full, verbatim license texts named by their SPDX identifiers, for
  both the project's own license and every third-party component.
- `REUSE.toml` — machine-readable ([REUSE](https://reuse.software)) licensing
  metadata for the repository.

Third-party licenses apply only to their respective components and do not change
the license of the OpcUaManager source code. The same license and notice
documents are shown in the application under **Help ▸ About OpcUaManager**.

## Current limitations

- The browser currently loads node hierarchy only; value reading, writing, subscriptions, methods, and history are not yet implemented.
- Certificate errors are reported and rejected. An interactive trust-confirmation dialog is intentionally not implemented yet.
- Username and certificate authentication depend on the selected endpoint and backend capabilities.
- The live OPC UA integration test requires an externally started test server.
- Translation files exist, but complete translations have not yet been supplied.

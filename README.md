# OpcUaManager

OpcUaManager is a Qt Quick desktop application for discovering OPC UA servers, selecting endpoints, connecting with supported authentication modes, and browsing the server address space.

The project is currently in the stabilization phase before further product development.

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
- Qt OPC UA
- Qt Linguist Tools
- Qt Test and Qt Quick Test for tests

The Qt OPC UA installation must contain a usable backend plugin, normally the open62541 backend.

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

## Current limitations

- The browser currently loads node hierarchy only; value reading, writing, subscriptions, methods, and history are not yet implemented.
- Certificate errors are reported and rejected. An interactive trust-confirmation dialog is intentionally not implemented yet.
- Username and certificate authentication depend on the selected endpoint and backend capabilities.
- The live OPC UA integration test requires an externally started test server.
- Translation files exist, but complete translations have not yet been supplied.

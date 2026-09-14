# Third-party notices

OpcUaManager is licensed under **GPL-3.0-or-later** (see `LICENSE` and
`LICENSES/GPL-3.0-or-later.txt`). This file lists the third-party components that
the application uses, bundles, or may distribute. Each component remains governed
by its own license; the licenses below do **not** apply to the OpcUaManager
source code itself.

The exact set of deployed runtime files depends on the selected Qt kit and build
configuration. Full license texts are stored in the `LICENSES/` directory using
their SPDX identifiers as file names.

## Qt 6.11

The application is built against Qt 6.11 and links the following Qt modules
dynamically (shared libraries): Qt Core, Qt GUI, Qt Network, Qt OPC UA, Qt QML,
Qt Quick, Qt Quick Controls, Qt Quick Dialogs, Qt SQL, Qt SVG, and Qt WebView
(used by the offline documentation viewer). Qt Linguist Tools is used at build
time only and is not distributed.

- **License (as used here):** GNU Lesser General Public License, version 3
  (`LICENSES/LGPL-3.0-only.txt`), which incorporates the GNU General Public
  License, version 3 (`LICENSES/GPL-3.0-or-later.txt`) by reference.
- Qt is also available under the GNU General Public License, version 2, and under
  commercial licenses from The Qt Company. OpcUaManager relies on the open-source
  LGPLv3 option.
- **LGPLv3 obligation:** Qt is linked dynamically. Recipients may replace the Qt
  shared libraries with their own compatible builds. To do so, obtain the
  corresponding Qt source for the version in use, build it with a compatible
  configuration, and replace the Qt library files next to the executable.
- **Source:** <https://www.qt.io> — archived sources at
  <https://download.qt.io/archive/qt/>.

## Qt OPC UA — open62541 backend (open62541 1.4.14)

OPC UA connectivity is provided by the Qt OPC UA module through its open62541
backend plugin (`open62541_backend.dll` on Windows), which embeds **open62541
version 1.4.14**.

- **Primary license:** Mozilla Public License 2.0 (`LICENSES/MPL-2.0.txt`).
- **Additional licenses for individual parts of open62541:** Creative Commons
  Zero v1.0 Universal (`LICENSES/CC0-1.0.txt`), Creative Commons
  Attribution-ShareAlike 4.0 International (`LICENSES/CC-BY-SA-4.0.txt`),
  BSD 3-Clause (`LICENSES/BSD-3-Clause.txt`), Apache License 2.0
  (`LICENSES/Apache-2.0.txt`), and MIT (`LICENSES/MIT.txt`).
- **Project homepage:** <https://open62541.org> — upstream version 1.4.14.

MPL-2.0 is a file-level (weak) copyleft license and is compatible with
GPL-3.0-or-later. open62541 is distributed as a separate plugin binary; its MPL
obligations apply to the open62541 files, not to the OpcUaManager source code.

## OpenSSL 3

When OPC UA security features are used, the deployment may include the OpenSSL 3
runtime libraries (for example `libssl-3-x64.dll` and `libcrypto-3-x64.dll` on
Windows). On Windows these are copied next to the executable by the optional
`OPCUAMANAGER_COPY_WINDOWS_RUNTIME` build step.

- **License:** Apache License 2.0 (`LICENSES/Apache-2.0.txt`).
- **Source:** <https://www.openssl.org>.

Retain the license and notices supplied with the OpenSSL binaries you actually
distribute.

## Microsoft Edge WebView2 (Windows)

The offline documentation viewer uses Qt WebView, which on Windows renders through
the Microsoft Edge WebView2 control. The deployment therefore includes the WebView2
loader library (`WebView2Loader.dll`) copied next to the executable by
`windeployqt`. The WebView2 runtime itself is a system component supplied and
updated by Microsoft (the "Evergreen" runtime) and is not bundled by OpcUaManager.

- **License:** the `WebView2Loader.dll` redistributable is governed by the Microsoft
  Edge WebView2 SDK license terms (Microsoft Software License Terms). It is not
  covered by the OpcUaManager or Qt licenses.
- **Source / terms:** <https://developer.microsoft.com/microsoft-edge/webview2/>.

If a deployment does not use the documentation viewer, `WebView2Loader.dll` may be
omitted; the application still runs without the in-app Help window.

## Google Material Symbols (icons)

The SVG icons under `resources/images/svg/` are based on Google Material Symbols
Outlined and are embedded in the application resources.

- **License:** Apache License 2.0 (`LICENSES/Apache-2.0.txt`; a component-specific
  copy is also kept at `resources/images/LICENSE-Material-Symbols.txt`).
- **Source:** <https://github.com/google/material-design-icons>.

The SVG path data was not modified; only the file names were normalized. See
`resources/images/README.md`.

## Country flags (UI-language selector)

The SVG country flags under `resources/images/flags/` (`gb.svg`, `de.svg`, `ru.svg`,
`ua.svg`) are used by the UI-language selector and the active-locale indicator. The
files carry a Creative Commons Public Domain dedication in their source markup.

- **License:** Public Domain (national flags are not subject to copyright; the SVG
  artwork is released into the public domain).

## Before distributing binaries

Verify that the license texts and notices for every component you actually ship
(Qt, open62541, OpenSSL, the Microsoft Edge WebView2 loader, and the icon set) are
included with the distribution and that the LGPLv3 relinking obligation for Qt is
satisfied.

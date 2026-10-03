<!--
SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Changelog

All notable changes to **OPC UA Manager** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
(`MAJOR.MINOR.PATCH`).

The product version is defined in exactly one place — `packaging/product.json`
(`version`). From there it flows automatically to the Help&nbsp;>&nbsp;About dialog,
the Windows executable metadata, the bundled documentation, and the installer and
update repository. To cut a release, bump that single field, then rebuild and run
`packaging/release.ps1`.

## [1.2.0] - 2026-10-03

Feature release for working with monitored values: highlighted values,
drag and drop into the Data View, copyable attributes, and a refreshed look
with the teal accent in both themes.

### Added
- The **Value** column of the Data View is colored like the structured Value
  panel: strings, booleans and numbers each in their own color, following the
  light or dark theme. Array elements are colored one by one. The displayed
  text, tooltip, copy, CSV export and sorting still use the plain value.
- Nodes can be dragged from the address space and from segment trees into the
  Data View. A preview with the node icon and name follows the pointer. On a
  touch screen a long press starts the drag, while a plain swipe still scrolls
  the tree. Nodes already in the Data View are refused.
- Attribute names and values in the **Attributes** panel can be selected with
  the mouse and copied with `Ctrl+C`; `Ctrl+A` selects a whole field.
- The address space and segment trees scroll horizontally, so deep branches
  and long display names are no longer cut off.

### Changed
- The interface uses the teal accent sparingly in both themes. Pane headers,
  the menu bar and dividers get teal-tinted neutral colors instead of plain
  white in the light theme, and pane titles have stronger contrast.
- The status bar shows the connection state: a teal line and tint when
  connected, amber while connecting, neutral when offline.
- The open menu title, highlighted menu items, the selected tree node, the
  highlighted Data View row and the Server Studio selection share one teal
  highlight. Split handles turn teal while hovered.
- The Data View and the Attributes panel no longer shade every other row.

### Fixed
- Dragging a node into the Data View did nothing; the drop was never
  delivered.
- Removing rows from the Data View left the matching tree checkboxes checked,
  so a removed node could not be added again by drag and drop.
- Clicking a row in the Data View did not highlight it.
- After switching to the light theme the Value column kept the pale
  dark-theme colors.
- `Ctrl+C` in the Attributes panel and in the structured value view copied the
  node id instead of the selected text.

### Documentation
- The user manual (all languages) explains how to add variables to the Data
  View: with the tree check box or by drag and drop, including the long press
  on touch screens, and what happens to duplicates and removed rows. It also
  mentions copying text from the Attributes panel.

### Build and release
- Changing the version in `packaging/product.json` now triggers a CMake
  reconfigure, so an incremental build no longer keeps the old version in the
  executable metadata.
- Fixed MSVC warning C4804 in the trend model.

## [1.1.0] - 2026-09-30

Feature release: finding OPC UA servers on the network, and reliable
connections to remote servers that report their own host name.

### Added
- **Scan network…** in the connection form: scans the IPv4 subnets this PC is
  connected to (or a range typed as `10.10.1.0/24`, `10.10.1.1-254`,
  `10.10.1.5-10.10.1.40` or a single address) for OPC UA servers. Only port
  `4840` is checked by default; further ports can be entered as a list or
  range. One scan covers at most 1024 addresses and 16 ports.
- The scan result table shows each server's name, security policies and
  supported logins while the scan runs; open ports that do not answer as an
  OPC UA server are shown dimmed. Double-clicking a server (or **Use**) puts
  its address into the connection form and requests its endpoints
  automatically.
- Scanning is possible while a session is connected; using a scanned server
  requires disconnecting first.
- The **Discovery URL** field lists the ten most recently connected server
  addresses (without user credentials).

### Fixed
- Connecting to remote servers that advertise their own host name (for
  example a PLC panel reached at `opc.tcp://10.10.1.2:4840` that reports
  `opc.tcp://AOPT690:4840`) failed with an empty endpoint list. The client now
  keeps using the address it actually reached, lists duplicated endpoints
  once, and explains when an entered host name cannot be resolved.
- Reopening a project whose stored endpoint differs from the current list only
  in the endpoint host now selects the same endpoint (security policy, mode
  and login) instead of silently falling back to the first one.

### Documentation and translations
- The user manual (all languages) describes the network scanner and the
  host-name case in Troubleshooting.
- All new texts are translated into German, English (UK), French, Italian,
  Russian and Ukrainian.
- Seven messages of the update dialog that were still shown in English in every
  language are now translated.

### Build and release
- The CI and release workflows extract the Qt archives with 7-Zip, because the
  current Python archive library rejects the Qt WebView package.

## [1.0.2] - 2026-09-30

Bug-fix release for the menu icons introduced in 1.0.1.

### User interface
- Submenu entries (OPC UA, Open Recent, Value Format, Toolbars) showed their
  icons at twice the size of the other menu items; all menu icons now have
  the same size.
- The menu bar titles are text-only again; icons are shown only inside the
  dropdown menus and submenus.

## [1.0.1] - 2026-09-30

Maintenance release: menu icons, clearer installer tooling, installation
guidance for unsigned builds, and a more robust release build.

### User interface
- Menu bar titles, menu items and submenus show Material Symbols icons; the
  icons follow the light/dark theme.

### Windows distribution
- The Maintenance Tool and `Setup.exe` use their own icon, so they are no
  longer mistaken for the application.
- `packaging/release.ps1` resolves `git.exe` on every build and passes it to
  CMake, so a Git for Windows update no longer breaks the open62541 download
  with a stale cached path.
- Release binaries are not code-signed yet; the SignPath integration stays
  dormant until the project is accepted, and releases are published unsigned.

### Documentation
- README, download page and user manual (all languages) explain every way to
  install (installer, portable ZIP, build from source), how to verify the
  SHA-256 checksum, and how to handle Windows SmartScreen for unsigned builds.
- The user manual is also published to the GitHub Wiki.

### Project
- Security policy (`SECURITY.md`) and Dependabot updates for GitHub Actions.

## [1.0.0] - 2026-09-15

First stable release. OPC UA Manager is a Qt&nbsp;6 desktop application for
browsing, testing, and simulating OPC UA servers, pairing an OPC UA client with a
built-in server studio.

### OPC UA client
- Connect to an OPC UA server (discovery, endpoint selection, anonymous /
  username-password / certificate identities) with the network work on a dedicated
  worker thread.
- Browse the address space with lazy child fetching; inspect node attributes.
- Read, write, and subscribe to variables; live values in the Data View with
  sorting, filtering, and per-row controls.
- Plot selected variables as trends.
- Decode structured (CODESYS-style) values by browsing the instance subtree.
- Persist monitored nodes in a local SQLite database.

### OPC UA Server Studio
- Design an OPC UA server address space (`.uaserver` projects) and run it as a
  separate headless process embedding open62541.
- Security Test Lab: None and Basic256Sha256 endpoints, a self-signed server
  certificate in an independent PKI, anonymous and username/password access, and
  real server-side certificate trust (trust list + rejected store + UI).
- Live runtime diagnostics over a local channel, with kill/reconnect failure
  testing.
- Value simulation (counter, toggle, random, sine, ramp) writing real data-change
  notifications.
- Custom enumeration data types; model-level NodeSet2 import and export.
- Clone the address space of a connected server into a new server project, carrying
  current client values.
- Open the running local server directly in the built-in client.

### User interface and languages
- Multilingual UI (English, German, Russian, Ukrainian) with live language
  switching and per-user persistence; country-flag indicators in the language
  selector and the top bar.

### Help and documentation
- Integrated, offline, versioned documentation (QDoc): a per-language user +
  developer guide (EN/DE/RU/UK) plus the C++/QML API reference, cross-linked.
- Help&nbsp;>&nbsp;Documentation opens the docs in an embedded viewer, served over a
  local loopback HTTP server; the online Qt reference opens in the same view.
- The documentation follows the application light/dark theme and uses the
  application's Teal accent.

### Licensing and About
- Help&nbsp;>&nbsp;About dialog with version, build, toolchain, and third-party
  license information; GPL-3.0-or-later licensing infrastructure (LICENSES/, NOTICE,
  THIRD-PARTY notices, REUSE).

### Windows distribution
- Reproducible release pipeline (`packaging/release.ps1`) producing a Qt Installer
  Framework installer, a portable ZIP, and an update repository from one canonical
  deployment.
- All product identity and the version come from the single source
  `packaging/product.json`; installed-vs-portable data locations are handled by
  `AppPaths`.

[1.2.0]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.2.0
[1.1.0]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.1.0
[1.0.2]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.0.2
[1.0.1]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.0.1
[1.0.0]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.0.0

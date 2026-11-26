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

## [Unreleased]

### Added
- **Clone to Server Studio**: a progress dialog shows the captured and pending nodes and
  can cancel the clone; a cancelled clone leaves the current server project unchanged.
  The clone dialog offers a node limit of 50 000, 200 000 (default) or 1 000 000 nodes.
- **Server Studio**: structure data types. A structure variable references its
  structure type and has its members as child nodes (the way CODESYS publishes them);
  the runtime serves the type as a `Structure` subtype. Variables may be
  multi-dimensional arrays with `ArrayDimensions` (for example `Int32[2,3]`). The tree and
  the property editor show these types, and NodeSet2 export/import keeps them. Projects
  that use them are saved as `.uaserver` format version 2; all other projects stay
  version 1.

### Fixed
- **Clone to Server Studio**: an incomplete clone is no longer reported as complete. The
  browse no longer stops at 5000 nodes or after a fixed 30 seconds: it is bounded by
  the chosen node limit, a depth of 64 and 30 seconds without a server response (there is
  no overall time limit), and a dialog and the status message name the limit or timeout
  that ended it and the nodes that could not be read or browsed. Members of a structure
  or array are now found even when the structure's own value cannot be read, attribute
  reads are batched, and at most a few requests are sent to the server at a time. The
  value of a structure variable is no longer read, only its members, so a large PLC no
  longer encodes the same nested data once per nesting level.
- **Clone to Server Studio**: variable data types are now resolved through the source
  server's type hierarchy instead of falling back to `Int32` for every type outside the
  standard built-ins. CODESYS IEC aliases such as `STRING` and `UINT`, and standard
  subtypes such as `Duration`, `UtcTime` and `LocaleId`, keep their OPC UA base type
  (`String`, `UInt16`, `Double`, …) and their values. Date/time values and unresolvable
  types become `String` variables, and the status message reports how many variables use
  such a substitute type.
- **Clone to Server Studio**: structures are cloned as structures of their original type
  (named after the server's DataType, for example `ST_HMI_Button`) instead of `String`,
  arrays of structures as arrays of that type, and arrays keep their rank and
  dimensions. One-element arrays (such as `ServerArray`) are no longer split into
  characters, and a value that could not be read stays empty instead of becoming `0`.
- **Server Studio**: the property editor of the selected node scrolls inside its panel;
  in a small window its fields no longer overlap the Security section below.

## [1.4.1] - 2026-11-24

Patch release for the update check.

### Fixed
- **Check for updates dialog**: the dialog showed two *Close* buttons; it now
  shows a single *Close* button next to *Check again*.

## [1.4.0] - 2026-11-10

Feature release for the Data View: rows with the same name can be told apart
at a glance, and the columns can be arranged the way you work.

### Added
- **Context column**: right after Display Name, it shows where each variable
  lives, in IEC notation starting at the GVL or program — for example
  `V_HMI.astTestPrepContainer[1].stRefOffset` — so rows with the same name,
  such as the `value` member of every element of an array of structures, can
  be told apart. A narrow column keeps the end of the path visible, the full
  node path is shown on hover, and sorting treats array indexes as numbers
  (`[2]` before `[10]`).
- **Movable columns**: drag the middle of a column header to the left or
  right to change the column order; dragging near its edge still resizes the
  column. A marker shows where the column will land, and the row number stays
  first. Copying rows and CSV export follow the visible order.
- **Column layout for new projects**: the column order, visibility, and
  widths you arrange are saved with the project and also become the starting
  layout of every new project, so the columns do not have to be arranged
  again after a restart.

### Changed
- **Row numbers and sorting**: `#` is the number of the row in the order the
  variables were added and stays with its row when the table is sorted;
  sorting by `#` restores that order. New rows are always appended at the end,
  and the sort is no longer saved in the project, so a project always opens
  in the order the rows were added.
- **Calmer table**: name, value, and status keep their full strength, while
  the reference columns — row number, context, data type, interval,
  timestamps, node path, node id, and server — are drawn muted, like the
  second column of the Attributes panel. A row still waiting for its first
  value dims its primary columns only.

### Fixed
- **Discard when quitting**: answering *Discard* in the unsaved-changes prompt
  now quits the application instead of showing the prompt again. The same
  applies when quitting to install an update.

## [1.3.1] - 2026-10-08

Patch release that completes the interface zoom.

### Added
- **Zoom with the mouse wheel**: holding `Ctrl` while turning the wheel zooms
  in (away from you) or out (towards you), one step per notch, on every screen
  and over any list, table or tree. High-resolution wheels and touchpads step
  once per notch-equivalent instead of jumping through all levels. The wheel
  without `Ctrl` still scrolls as before.

## [1.3.0] - 2026-10-07

Feature release for a personal workspace: the application now opens the way
you left it — same theme, same zoom, same window and pane layout.

### Added
- **Interface zoom** like the page zoom of a web browser: `Ctrl++` (also
  `Ctrl+=`), `Ctrl+-` and `Ctrl+0` on every screen, plus a **View > Zoom**
  submenu. Steps go from 50% to 200%. Layouts reflow instead of being cut off,
  and menus, dialogs and tooltips scale too. The status bar shows the zoom
  when it is not 100% (click to reset), and a short badge confirms each
  change. The zoom is remembered between sessions.
- **Color theme on the start page**: a theme selector next to the language
  offers *System*, *Light* and *Dark*. *System* follows the Windows light or
  dark mode. The workspace opens in the chosen theme, and the choice is
  remembered between sessions.
- **The window and pane layout is restored at the next start**: the window
  size, position and state (normal, maximized or full screen), the widths of
  Address Space, Data View and Attributes, the split between Address Space and
  the segments, and the split inside the Attributes panel. A window last shown
  on a monitor that is no longer connected opens on the primary screen.
- **Full screen mode** with **View > Full Screen** or `F11`; pressing it again
  returns to the normal or maximized window.

### Changed
- The **View** menu theme switch now stores an explicit light or dark choice,
  so the theme no longer resets to the Windows setting at the next start.
- Dialogs of the main window are centered and sized for the current zoom.

### Documentation
- The user manual (all languages) describes choosing the color theme on the
  start page, the interface zoom, full screen mode, and which window and pane
  settings are remembered between sessions.

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

[1.4.1]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.4.1
[1.4.0]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.4.0
[1.3.1]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.3.1
[1.3.0]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.3.0
[1.2.0]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.2.0
[1.1.0]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.1.0
[1.0.2]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.0.2
[1.0.1]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.0.1
[1.0.0]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.0.0

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

[1.0.0]: https://github.com/SEB103/QtOpcUaManager/releases/tag/v1.0.0

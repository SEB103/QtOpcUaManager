# Security Policy

## Supported versions

Only the latest release published on
<https://github.com/SEB103/QtOpcUaManager/releases/latest> receives security
fixes. Older releases are not maintained; please update before reporting.

| Version | Supported |
|---------|-----------|
| latest release | yes |
| older releases | no |

## Reporting a vulnerability

Please do **not** open a public issue for security problems.

Report vulnerabilities privately through GitHub:
**Security → Report a vulnerability** on
<https://github.com/SEB103/QtOpcUaManager/security/advisories/new>.

Include, where possible:

- the affected version (**Info → About OpcUaManager…**) and whether the
  installed or the portable package is used;
- the component (OPC UA client, Server Studio, `OpcUaServerRuntime`, installer,
  update check) and the steps to reproduce;
- the impact you expect (for example: remote code execution, credential
  disclosure, denial of service against the built-in server).

You will receive an acknowledgement, and the fix is coordinated with you before
the advisory is published. There is no bug bounty.

## Scope notes

- The built-in OPC UA server (`OpcUaServerRuntime`) is a test and simulation
  tool. Running it with the *None* security policy or anonymous access on an
  untrusted network is a configuration choice, not a vulnerability.
- Vulnerabilities in third-party components (Qt, open62541, OpenSSL) should be
  reported to those projects; a report here is still welcome if the bundled
  version is affected, so the dependency can be updated.

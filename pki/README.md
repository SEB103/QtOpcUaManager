# OPC UA PKI directory

The source tree contains only an initial directory layout. On first use, the application mirrors this layout to Qt's writable application-local data location and uses the runtime copy for OPC UA security.

Expected structure:

```text
pki/
├── own/
│   ├── certs/client.der
│   └── private/client.pem
├── trusted/
│   ├── certs/
│   └── crl/
└── issuers/
    ├── certs/
    └── crl/
```

## Certificate authentication

For certificate authentication, provide both:

```text
own/certs/client.der
own/private/client.pem
```

The certificate Application URI is used as the OPC UA application identity. Generate the certificate for this application and verify that its URI, validity period, key usage, and host information are appropriate for the target environment.

If `client.pem` is encrypted, enter its password in the connection form before requesting endpoints or connecting. The password is kept only in process memory and is not written to the repository.

## Trust workflow

The application does not automatically ignore certificate validation errors.

For an unknown server certificate:

1. Stop the connection attempt.
2. Obtain the certificate fingerprint through a trusted channel.
3. Compare the fingerprint and certificate properties with the server administrator.
4. Copy only the approved certificate into the runtime `trusted/certs` directory.
5. Reconnect and confirm that validation succeeds.

Use `issuers/certs` for approved issuer certificates and the corresponding `crl` directories for certificate revocation lists when the deployment requires them.

## Repository safety

Never commit production certificates or private keys. `.gitignore` excludes files below `pki/own/private` and `pki/own/certs`, while retaining the empty directory placeholders.

Do not distribute private keys with source archives. Provision credentials separately for each installation.

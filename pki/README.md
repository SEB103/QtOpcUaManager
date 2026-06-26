# OPC UA PKI directory

The application copies this directory next to the development executable and,
on first use, mirrors it to the writable application data location.

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

The initial project intentionally contains no private key or certificate.
Never commit production private keys to source control.

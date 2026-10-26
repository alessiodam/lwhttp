# Changelog

## 1.0.0-rc1

First release candidate.

- Asynchronous HTTP/1.1 client driven by lwIP-CE socket events, with blocking wrappers.
- HTTPS through lwIP-CE TLS 1.3.
- Keep-alive reuse, automatic retry, redirects, chunked transfer, trailers, `Expect: 100-continue`, upgrades.
- Strict rejection of encoded responses.
- Exported parser, header store, writer and URL layers.

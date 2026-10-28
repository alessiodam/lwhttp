# Changelog

## 1.0.0-rc2

Second release candidate. It breaks the ABI and API of rc1: rebuild every program against the new `LWHTTP.lib`
and `lwhttp.h`. Built and tested against lwIP-CE `v3.0.0-rc3`, which needs the v3 `lwIPCERT` truststore.

Added:

- {c:member}`lwhttp_client_config_t.keep_headers` stores only the listed response headers (plus `Location`) and skips
  over-long lines of the others, so small header stores work with sites that send large headers.
- {c:member}`lwhttp_parser_callbacks_t.skip_field` lets parser users discard header and trailer lines that do not fit
  the line buffer.
- `LWHTTP_ERR_TIMEOUT` for a connection that went silent after it was established.
- `examples/https_get`, a minimal HTTPS GET that prints the status code.

Changed:

- The 512-byte send buffer is only allocated for requests with a `body_reader`, so a client takes 512 bytes less
  heap.
- The library is smaller: 19.8 KB instead of 20.1 KB, despite the additions.
- `Content-Encoding` is checked as the field is parsed instead of read back from the header store.
- {c:member}`lwhttp_parser.flags` and {c:member}`lwhttp_parser.internal` are now `uint8_t`.
- `lwhttp_url_t` no longer has `userinfo`, `userinfo_len` and `has_userinfo`; the userinfo is still part of
  {c:member}`lwhttp_url_t.authority`.

Fixed:

- The inactivity timeout of an open connection never fired while other frames reached the interface, because
  lwIP-CE extends it on any received frame
  ([lwIP-CE #93](https://github.com/cagscalclabs/lwip-ce/issues/93)). The client now tracks each connection itself
  and ends a silent request with the new `LWHTTP_ERR_TIMEOUT`.
- A {c:member}`lwhttp_client_config_t.header_size` close to `SIZE_MAX` wrapped the allocation size and corrupted
  the lwIP-CE heap. {c:func}`lwhttp_client_create` now returns `NULL`.
- A request head longer than `SIZE_MAX` wrapped its size and was sent truncated. {c:member}`lwhttp_buffer_t.len`
  now saturates and the request fails with `LWHTTP_ERR_TOO_LARGE`.

## 1.0.0-rc1

First release candidate.

- Asynchronous HTTP/1.1 client driven by lwIP-CE socket events, with blocking wrappers.
- HTTPS through lwIP-CE TLS 1.3.
- Keep-alive reuse, automatic retry, redirects, chunked transfer, trailers, `Expect: 100-continue`, upgrades.
- Strict rejection of encoded responses.
- Exported parser, header store, writer and URL layers.

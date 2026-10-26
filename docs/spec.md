# HTTP/1.1 coverage

LWHTTP is a client. Message syntax and framing follow [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112),
semantics follow [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110), and URLs follow
[RFC 3986](https://www.rfc-editor.org/rfc/rfc3986).

## Implemented

| Area | Behaviour |
| --- | --- |
| Status line | `HTTP/1.x`, three-digit status, optional reason phrase. Other major versions fail with `LWHTTP_ERR_VERSION`. |
| Field syntax | Token names, OWS trimming, `obs-fold` replaced by a space, bare LF line endings accepted, CR, NUL and other control characters rejected, whitespace before the colon tolerated. |
| Message length | `HEAD`, `1xx`, `204` and `304` have no body. `101` and `2xx` to `CONNECT` switch protocols. `Transfer-Encoding` overrides `Content-Length` and disables reuse. Repeated `Content-Length` values must agree. |
| Chunked transfer | Chunk extensions (including quoted strings) skipped, trailer fields collected, chunk sizes bounds-checked. |
| Interim responses | `100 Continue` releases a held body, other `1xx` are skipped. |
| Persistent connections | Reuse per origin, `Connection: close` and `keep-alive` honoured, one automatic retry for idempotent requests on a dead reused connection. |
| Request bodies | `Content-Length` for known sizes, chunked for streamed bodies, `Content-Length: 0` for empty `POST`, `PUT`, `PATCH`, `Expect: 100-continue`. |
| Early responses | A final response that arrives during the upload stops it and retires the connection. |
| Redirects | `301`, `302`, `303`, `307`, `308`, relative references, method rewriting, credential stripping across origins. |
| Request targets | origin form, authority form for `CONNECT`, asterisk and absolute form through `target`. |
| Content codings | `Accept-Encoding: identity` sent, encoded responses rejected unless the program opts in. |
| Upgrades | `101` and `CONNECT` tunnels hand the socket over to the program. |
| Dates | IMF-fixdate output; IMF-fixdate, RFC 850 and asctime input. |
| HTTPS | TLS 1.3 through lwIP-CE, with SNI and certificate verification. |

## Not implemented

| Area | Why |
| --- | --- |
| Content decoding (`gzip`, `deflate`, `br`) | A streaming inflater needs a 32 KB window, most of the calculator's free RAM. |
| Caching | Belongs in the application on a device without storage to spare. |
| Cookies | A separate specification (RFC 6265) with state that applications manage better themselves. |
| Proxies | lwIP-CE cannot start TLS inside an existing TCP tunnel. |
| Pipelining | Optional in RFC 9112 and poorly supported by servers. |
| Server role | LWHTTP is a client library. |

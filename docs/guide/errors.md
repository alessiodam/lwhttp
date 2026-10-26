# Errors

Every fallible call returns an {c:type}`lwhttp_error_t`, and `on_complete` receives one as the final result.
{c:func}`lwhttp_error_string` turns any of them into a short message.

| Result | Typical cause |
| --- | --- |
| `LWHTTP_OK` | Success. A finished request may still carry any HTTP status, including `404` or `500`. |
| `LWHTTP_ERR_ARG` | `NULL` arguments, a bad method, target or header, or a user `Content-Length` / `Transfer-Encoding`. |
| `LWHTTP_ERR_MEM` | The lwIP-CE heap could not satisfy an allocation. See [memory](memory.md). |
| `LWHTTP_ERR_STATE` | A request was started while the client was still busy. |
| `LWHTTP_ERR_URL` | The URL has no scheme or host, a bad port, or spaces and control characters. |
| `LWHTTP_ERR_DNS` | The host name could not be resolved. |
| `LWHTTP_ERR_CONNECT` | The TCP connection could not be established, or timed out while connecting. |
| `LWHTTP_ERR_TLS` | The TLS handshake or certificate check failed. See [HTTPS](https.md). |
| `LWHTTP_ERR_SEND` | lwIP-CE rejected a write on an open connection. |
| `LWHTTP_ERR_CLOSED` | The connection closed, reset or timed out before the response was complete. |
| `LWHTTP_ERR_PROTOCOL` | The server sent a malformed response: bad status line, bad header, inconsistent lengths, broken chunking. |
| `LWHTTP_ERR_VERSION` | The server answered with a major HTTP version other than 1. |
| `LWHTTP_ERR_UNSUPPORTED` | A scheme other than `http`/`https`, or a compressed or otherwise encoded response body. |
| `LWHTTP_ERR_TOO_LARGE` | A length or chunk size too big to represent, or a URL too long for its buffer. |
| `LWHTTP_ERR_HEADERS_TOO_LARGE` | The response head or a single header line does not fit in the header store. |
| `LWHTTP_ERR_REDIRECT` | A `Location` header that cannot be resolved into a URL. |
| `LWHTTP_ERR_ABORTED` | A callback returned `false`, the request was cancelled, or a body reader ended early. |

## Synchronous and asynchronous errors

Errors found before a request goes on the wire are returned by {c:func}`lwhttp_client_request` and never reach
`on_complete`. Everything after that is reported once, through `on_complete`, and is also available afterwards from
{c:func}`lwhttp_client_result`.

```c
lwhttp_error_t e = lwhttp_client_request(client, &request, &handlers, &state);
if (e) show_error(lwhttp_error_string(e));
```

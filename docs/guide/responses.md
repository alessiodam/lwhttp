# Responses

## Status and headers

`on_response` runs once the final response head has been parsed. From then until the next request, these accessors
describe it:

```c
static bool on_response(lwhttp_client_t *client, void *arg)
{
  uint16_t status = lwhttp_client_status(client);
  const char *type = lwhttp_client_header(client, "content-type");
  return status == 200;
}
```

Header names are matched case-insensitively. When a field appears more than once, {c:func}`lwhttp_client_header`
returns the first; walk all of them with {c:func}`lwhttp_client_headers` and {c:func}`lwhttp_headers_at`.

Returning `false` from `on_response` aborts the request with `LWHTTP_ERR_ABORTED` before any body arrives.

## Bodies

`on_body` receives the body in pieces as it arrives. Chunked bodies are already decoded. The pointer refers to the
client's receive buffer, so copy what you need before returning.

```c
static bool on_body(lwhttp_client_t *client, const uint8_t *data, size_t len, void *arg)
{
  return ti_Write(data, 1, len, *(uint8_t *)arg) == len;
}
```

Responses to `HEAD`, `204 No Content` and `304 Not Modified` never have a body, whatever their headers say.

## Trailers

Fields sent after a chunked body are appended to the same header store as the response head. They are visible
through {c:func}`lwhttp_client_header` once `on_complete` runs.

## Interim responses

`100 Continue`, `103 Early Hints` and other `1xx` responses are consumed silently; `on_response` only ever sees the
final response. `101 Switching Protocols` is final, see [upgrades](connections.md#upgrades).

## Content codings

LWHTTP does not decompress bodies. To make that safe it asks for uncompressed responses with
`Accept-Encoding: identity`, and **rejects** any response whose body is encoded anyway:

- a `Content-Encoding` other than `identity` (`gzip`, `deflate`, `br`, ...)
- a `Transfer-Encoding` other than `chunked`

Both end the request with `LWHTTP_ERR_UNSUPPORTED` before `on_response` or `on_body` run.

:::{tip}
To receive compressed bytes on purpose, send your own `Accept-Encoding` header. LWHTTP then leaves the
`Content-Encoding` check to you and passes the body through untouched.
:::

## Limits

The whole response head and any trailers must fit in {c:member}`lwhttp_client_config_t.header_size` bytes
(4096 by default). Exceeding it ends the request with `LWHTTP_ERR_HEADERS_TOO_LARGE`. See [memory](memory.md).

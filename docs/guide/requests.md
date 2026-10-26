# Requests

A request is described by a {c:type}`lwhttp_request_t`. Zero it, fill in what you need and pass it to
{c:func}`lwhttp_client_request`.

```c
lwhttp_request_t request = {0};
request.url = "http://httpbin.org/get";
```

## Method and target

`method`
: Any HTTP token. `NULL` means `GET`. Names are case-sensitive, as in the protocol.

`url`
: An absolute `http://` or `https://` URL. It is copied, so it may be a temporary buffer.

`target`
: Optional. Overrides the request-target written on the request line, for the asterisk form (`OPTIONS *`) or
  the absolute form used by proxies. Without it, the target is the URL's path and query, or `host:port` for
  `CONNECT`.

## Headers LWHTTP writes for you

| Header | When |
| --- | --- |
| `Host` | Always, unless you supply your own `Host`. |
| `User-Agent` | When {c:member}`lwhttp_client_config_t.user_agent` is set and you don't supply one. |
| `Accept-Encoding: identity` | Unless you supply your own `Accept-Encoding`. See [responses](responses.md#content-codings). |
| `Content-Length` | For in-memory bodies, streamed bodies with a known length, and empty `POST`, `PUT` and `PATCH`. |
| `Transfer-Encoding: chunked` | For streamed bodies of unknown length. |
| `Expect: 100-continue` | When `expect_continue` is set and there is a body. |
| `Connection: close` | When {c:member}`lwhttp_client_config_t.keep_alive` is off. |

## Your own headers

```c
static const lwhttp_header_t headers[] = {
  {"Accept", "application/json"},
  {"Authorization", "Bearer 0123456789"},
};

request.headers = headers;
request.header_count = 2;
```

Names must be HTTP tokens and values may not contain CR, LF or other control characters, so header injection is
impossible. `Content-Length` and `Transfer-Encoding` are managed by LWHTTP and are rejected with
`LWHTTP_ERR_ARG`. The array must stay valid until `on_complete`.

## Bodies

::::{tab-set}
:::{tab-item} In memory
```c
static const char json[] = "{\"x\":1}";

request.method = "POST";
request.body = (const uint8_t *)json;
request.body_length = sizeof(json) - 1;
```
Sent with `Content-Length`. The buffer is written straight from your memory without a copy, so keep it valid until
`on_complete`.
:::
:::{tab-item} Streamed, known length
```c
static size_t read_file(void *arg, uint8_t *buf, size_t size)
{
  return ti_Read(buf, 1, size, *(uint8_t *)arg);
}

request.method = "PUT";
request.body_reader = read_file;
request.body_arg = &handle;
request.body_length = file_size;
```
Sent with `Content-Length`. The reader is called with up to 512 bytes of room and must deliver exactly
`body_length` bytes in total. Returning `0` early, or more than `size`, aborts the request.
:::
:::{tab-item} Streamed, chunked
```c
request.method = "POST";
request.body_reader = next_sample;
request.body_arg = &sensor;
request.body_length = LWHTTP_LENGTH_UNKNOWN;
```
Sent with `Transfer-Encoding: chunked`, one chunk per reader call. Return `0` to end the body.
:::
::::

## Expect: 100-continue

With `expect_continue` set, LWHTTP sends the request head, then holds the body until the server answers
`100 Continue`. If the server stays silent, the body is sent at the next lwIP-CE poll tick, which comes within about two seconds and can come almost immediately.
If the server answers with a final status instead, the body is never sent and the connection is not reused.

## Validation

{c:func}`lwhttp_client_request` checks the method, target, URL and headers before anything goes on the wire, and
returns the problem directly:

| Result | Cause |
| --- | --- |
| `LWHTTP_ERR_ARG` | `NULL` request or URL, bad method, bad target, bad or forbidden header |
| `LWHTTP_ERR_URL` | URL without a scheme or host, or with spaces or control characters |
| `LWHTTP_ERR_UNSUPPORTED` | Scheme other than `http` or `https` |
| `LWHTTP_ERR_STATE` | The client is still busy with another request |
| `LWHTTP_ERR_MEM` | Not enough lwIP-CE heap to build the request |

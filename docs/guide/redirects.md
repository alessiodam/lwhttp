# Redirects

LWHTTP follows `301`, `302`, `303`, `307` and `308` responses that carry a `Location`, up to
{c:member}`lwhttp_client_config_t.max_redirects` times (5 by default). Redirect responses are never shown to your
callbacks; you only see the final response. {c:func}`lwhttp_client_url` returns the URL that was finally fetched.

Set `max_redirects` to `0` to receive every `3xx` yourself.

## Locations

`Location` may be relative. It is resolved against the current URL following RFC 3986, dot segments included, the
same way {c:func}`lwhttp_url_resolve` does.

## Methods and bodies

| Status | Method used for the next request |
| --- | --- |
| `301`, `302` | `POST` becomes `GET` without a body; other methods are kept |
| `303` | Becomes `GET` without a body, except `HEAD`, which stays `HEAD` |
| `307`, `308` | Method and body are kept |

A body produced by a `body_reader` cannot be sent twice. When a redirect would need to resend one, LWHTTP does not
follow it and delivers the `3xx` response instead.

After a redirect of a request that carried a body, the next request always goes out on a fresh connection, because
servers often close a connection whose request body they did not read.

## Credentials across origins

As soon as a redirect leads to a different scheme, host or port, LWHTTP stops sending your `Authorization`,
`Proxy-Authorization` and `Cookie` headers for the rest of that request.

## Failures

A `Location` that cannot be resolved ends the request with `LWHTTP_ERR_REDIRECT`. A redirect to a scheme other
than `http` or `https` ends it with `LWHTTP_ERR_UNSUPPORTED`.

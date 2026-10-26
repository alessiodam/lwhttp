# HTTPS

`https://` URLs are fetched over lwIP-CE's TLS stack. There is nothing to configure: LWHTTP opens a TLS socket,
lwIP-CE sends the host name with SNI and verifies the certificate chain against its truststore.

## Before the first handshake

lwIP-CE waits for DHCP, DNS, the clock (SNTP) and its random number generator before it starts a handshake. The
first HTTPS request after `lwip_network_up()` therefore takes a few seconds longer; later handshakes are faster, and
reused connections skip them entirely.

The truststore is the `lwIPCERT` appvar shipped with lwIP-CE. Keep it on the calculator and in sync with the
installed lwIP-CE version.

## Limits

lwIP-CE implements a focused subset of TLS. Two limits matter for real-world sites:

:::{warning}
**TLS 1.3 only.** Servers that only speak TLS 1.2, including many older AWS load balancers, cannot be reached.

**P-256 only.** Certificate chains that contain an ECDSA P-384 key anywhere, as many Let's Encrypt ECDSA, Google
and Cloudflare chains do, are rejected. RSA chains and pure P-256 chains work.
:::

Both cases end the request with `LWHTTP_ERR_TLS`. You can check a server from a computer with:

```sh
openssl s_client -connect example.com:443 -tls1_3 -showcerts </dev/null
```

## Errors

Certificate problems (expired, wrong host, self-signed, unknown root) and handshake failures all report
`LWHTTP_ERR_TLS`. For more detail, lwIP-CE's `lwip_get_traceback()` lists the internal error sites of the last
failures.

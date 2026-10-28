# Testing

LWHTTP is tested on the host, in an emulator and on real hardware. All of it runs in CI on every push to `main`.

## Host

```sh
make test
```

Compiles the protocol code (parser, header store, writer, URLs, tokens) for the host and runs the shared unit suite.
CI runs it with gcc and clang, with AddressSanitizer and UndefinedBehaviorSanitizer, at `-O2`, and as a 32-bit
build.

```sh
make coverage GCOVR=gcovr
make fuzz FUZZ_SECONDS=60
```

`coverage` reports line and branch coverage of `src/`. `fuzz` runs libFuzzer harnesses for the parser, URL
handling and the remaining utilities, starting from the seed corpora in `tests/fuzz/corpus`.

## HTTPTEST

`HTTPTEST` is a calculator program that runs the same unit suite on the eZ80 and then exercises the client against
real servers: HTTP and HTTPS, keep-alive, chunked bodies, uploads, redirects, aborts, cancellation, parallel
clients, failure cases, TLS limits and WebSocket upgrades.

```sh
make httptest
```

Passing tests are listed on screen in green. Failures appear in red and are written, with the request timings,
connection statistics and lwIP-CE's error trace, to the `HTTPLOG` appvar so they can be pulled from the calculator
for diagnosis.

## Emulator

```sh
make emulator-test AUTOTESTER_ROM=/path/to/rom LWIP_DIST=../lwip-ce/build CLIBS=clibs.8xg
```

Runs HTTPTEST in CEmu's autotester; the result is read back from the screen and from `HTTPLOG`. `HTTPTEST_MODE`
picks the suite:

| Mode | What runs |
| --- | --- |
| `offline` | The unit suite and the offline client checks. The default, needs no network. |
| `full` | Adds the network suite against httpbin, example.com and the failure cases. |
| `extended` | Adds the TLS chain probes against third-party hosts and the WebSocket upgrade. |

The network modes need [CEmu-lwIP-CE](https://github.com/alessiodam/CEmu-lwIP-CE)'s autotester, a TAP interface and
the lwIP-CE truststore, which [cemu-net-action](https://github.com/alessiodam/cemu-net-action) sets up in CI:

```sh
make emulator-test HTTPTEST_MODE=full TRUSTSTORE=lwIPCERT.8xv \
  USB_STEP='usb|eth,cemu0,dhcp=192.168.84.2/24,gw=192.168.84.1,dns=1.1.1.1' ...
```

TLS is slow in the emulator. If the `extended` suite runs out of time, raise `HTTPTEST_TIMEOUT` (milliseconds, 900000
by default).

## Continuous integration

| Workflow | What it does |
| --- | --- |
| CI | Lint, ABI check, host tests, coverage, fuzzing, the CE build with a size budget, and the `full` and `extended` emulator runs |
| Release | Runs CI for a `v*` tag and publishes the release with checksums and build provenance |
| Nightly | Builds against lwIP-CE `master` to catch upstream changes early |
| CodeQL | Static security analysis |
| Docs | Builds this site and checks that every export is documented |

#!/usr/bin/env bash
# Run a command inside a private network namespace laid out like CI: TAP
# cemu0 at 192.168.84.1/24, httpbin on 192.168.84.1:8080 and :8081, and
# internet through slirp4netns. Needs no root: it uses a user namespace.
#
# Requires unshare, slirp4netns, iptables and gunicorn with httpbin
# importable (set GUNICORN to a venv's gunicorn).
#
#   tests/device/netns.sh make emulator-test ...
set -euo pipefail

if [[ -z ${LWHTTP_NETNS:-} ]]; then
	LWHTTP_NETNS=1 unshare --user --map-root-user --net "$0" "$@" &
	ns=$!
	# slirp4netns must join the new namespace, not ours before unshare runs.
	for _ in $(seq 50); do
		[[ $(readlink "/proc/$ns/ns/net") != "$(readlink /proc/self/ns/net)" ]] && break
		sleep 0.1
	done
	slirp4netns --configure --disable-host-loopback "$ns" tap0 >/dev/null 2>&1 &
	slirp=$!
	status=0
	wait "$ns" || status=$?
	kill "$slirp" 2>/dev/null || true
	exit "$status"
fi

for _ in $(seq 50); do ip link show tap0 >/dev/null 2>&1 && break; sleep 0.2; done
ip link show tap0 >/dev/null 2>&1 || { echo "netns.sh: slirp4netns did not attach" >&2; exit 1; }
ip tuntap add dev cemu0 mode tap
ip addr add 192.168.84.1/24 dev cemu0
ip link set cemu0 up
echo 1 > /proc/sys/net/ipv4/ip_forward
iptables -t nat -A POSTROUTING -s 192.168.84.0/24 -o tap0 -j MASQUERADE

pid=$(mktemp -u)
"${GUNICORN:-gunicorn}" -D -p "$pid" -k gthread --threads 8 \
	-b 192.168.84.1:8080 -b 192.168.84.1:8081 httpbin:app
trap 'kill "$(cat "$pid")" 2>/dev/null || true' EXIT
for _ in $(seq 50); do curl -sf -o /dev/null http://192.168.84.1:8080/get && break; sleep 0.2; done

"$@"

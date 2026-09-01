#!/bin/sh
# Stand-in for dnsmasq, used by both host_tests e2e tests (the init script
# in static_resources_init_test and cvdalloc in cvdalloc_test) via a PATH shim.
#
# The real dnsmasq cannot run inside the rootless user namespace the sandbox
# uses. This is because setgroups(2) cannot be called in an unprivileged
# userns.

pidfile=""
for arg in "$@"; do
	case "$arg" in
	--pid-file=*) pidfile="${arg#--pid-file=}" ;;
	esac
done

setsid /bin/sh -c '
	pidfile="$1"
	# Close descriptors above stderr (e.g. the caller control socket) so the
	# daemon does not keep them open for its whole lifetime.
	for fd in 3 4 5 6 7 8 9; do
		eval "exec ${fd}>&-" 2>/dev/null || true
	done
	child=""
	cleanup() {
		[ -n "$pidfile" ] && rm -f "$pidfile"
		[ -n "$child" ] && kill "$child" 2>/dev/null
		exit 0
	}
	trap cleanup TERM INT
	[ -n "$pidfile" ] && printf "%s\n" "$$" >"$pidfile"
	# Sleep until signalled (teardown) or until the sandbox pid namespace is
	# torn down. 2147483647s stands in for "forever" portably.
	sleep 2147483647 &
	child=$!
	wait "$child"
	cleanup
' dnsmasq-shim "$pidfile" </dev/null >/dev/null 2>&1 &

exit 0

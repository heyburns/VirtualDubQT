#!/usr/bin/env bash
# BASH_ENV for trusted release jobs. Keep useful failure details in a GitHub
# annotation as well as the complete log/artifact, including non-compiler errors.
if [[ ${VDQT_RELEASE_SHELL_ACTIVE:-} == 1 ]]; then
    return
fi
export VDQT_RELEASE_SHELL_ACTIVE=1
vdqt_release_log=$(mktemp "${RUNNER_TEMP:-/tmp}/vdqt-release-step-XXXXXXXX.log")
exec 3>&1
exec > >(tee "$vdqt_release_log" >&3) 2>&1
vdqt_release_tee_pid=$!
vdqt_release_failure() {
    local vdqt_status=$?
    trap - ERR
    # Drain the logger before reading it. Otherwise a noisy package install can
    # leave tee behind and hide the short, important startup error at the end.
    exec 1>&3 2>&3
    wait "$vdqt_release_tee_pid" || true
    # GitHub caps annotation messages at 4096 bytes. Preserve the end of
    # the output so a long install log cannot displace the actual error.
    tail -c 3500 "$vdqt_release_log" | python3 -c '
import sys
message = sys.stdin.buffer.read().decode("utf-8", errors="replace")
message = message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
print("::error title=Release build diagnostics::" + message)
'
    exit "$vdqt_status"
}
trap vdqt_release_failure ERR

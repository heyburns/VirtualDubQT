#!/usr/bin/env bash
# BASH_ENV for trusted release jobs. Keep useful failure details in a GitHub
# annotation as well as the complete log/artifact, including non-compiler errors.
if [[ ${VDQT_RELEASE_SHELL_ACTIVE:-} == 1 ]]; then
    return
fi
export VDQT_RELEASE_SHELL_ACTIVE=1
vdqt_release_log=$(mktemp "${RUNNER_TEMP:-/tmp}/vdqt-release-step-XXXXXXXX.log")
exec > >(tee "$vdqt_release_log") 2>&1
vdqt_release_failure() {
    local vdqt_status=$?
    trap - ERR
    tail -n 60 "$vdqt_release_log" | python3 -c '
import sys
message = sys.stdin.read().replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
print("::error title=Release build diagnostics::" + message)
'
    exit "$vdqt_status"
}
trap vdqt_release_failure ERR

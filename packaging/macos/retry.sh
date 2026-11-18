#!/bin/bash
#
# Runs a command again when it fails, with a pause that doubles each time.
#
#   retry.sh ATTEMPTS PAUSE_SECONDS COMMAND [ARGUMENT...]
#
# For the steps that reach Apple's servers: the timestamp server that codesign
# and productbuild ask, and the notary service. The 1.18.0 release failed
# twice on those during an outage, once with notarytool answering 401 and once
# with codesign finding no timestamp, while nothing in the repository or the
# secrets was wrong. Four attempts from 30 seconds wait three and a half
# minutes in all, which is enough for a short fault and fails soon enough on a
# real one.
set -uo pipefail

attempts="${1:?usage: retry.sh ATTEMPTS PAUSE_SECONDS COMMAND [ARGUMENT...]}"
pause="${2:?usage: retry.sh ATTEMPTS PAUSE_SECONDS COMMAND [ARGUMENT...]}"
shift 2

if [ "$#" -eq 0 ]; then
  echo "retry.sh: no command given" >&2
  exit 2
fi

for ((attempt = 1; ; attempt++)); do
  "$@"
  status=$?

  if [ "$status" -eq 0 ]; then
    exit 0
  fi

  if [ "$attempt" -ge "$attempts" ]; then
    echo "retry.sh: $1 failed $attempts times, giving up" >&2
    exit "$status"
  fi

  echo "retry.sh: $1 failed with status $status on attempt $attempt of $attempts, trying again in ${pause}s" >&2
  sleep "$pause"
  pause=$((pause * 2))
done

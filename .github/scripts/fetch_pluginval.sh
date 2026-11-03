#!/usr/bin/env bash
# Fetches the pinned pluginval for the runner's platform into the current
# directory and checks it against the SHA-256 taken from its release. Used by
# the build job on all three platforms and by the sanitized pluginval job, so
# the version and the checksums are written down once.
set -euo pipefail

PLUGINVAL_VERSION=v1.0.4

base="https://github.com/Tracktion/pluginval/releases/download"

# Each archive's SHA-256, taken from the v1.0.4 release. Moving to
# another version means taking all three again.
case "$RUNNER_OS" in
  macOS)
    asset=pluginval_macOS.zip
    expected=3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f ;;
  Windows)
    asset=pluginval_Windows.zip
    expected=c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab ;;
  Linux)
    asset=pluginval_Linux.zip
    expected=c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352 ;;
esac

# --fail so a wrong tag or a renamed asset fails here. Without it
# curl writes the 404 page into the zip and the first complaint
# comes from unzip, about a file that looks like it downloaded.
curl -sSL --fail --retry 3 -o pluginval.zip \
  "$base/$PLUGINVAL_VERSION/$asset"

# A release asset can be replaced after the fact, and this one is run
# against the plugin, so it has to be the file that was checked.
# shasum is the one every macOS has, and sha256sum the one the other
# two have.
if [ "$RUNNER_OS" = "macOS" ]; then
  actual=$(shasum -a 256 pluginval.zip | cut -d ' ' -f 1)
else
  actual=$(sha256sum pluginval.zip | cut -d ' ' -f 1)
fi

if [ "$actual" != "$expected" ]; then
  echo "::error::$asset has SHA-256 $actual, expected $expected"
  exit 1
fi

# The bash on the Windows runners is the one Git ships and it has no
# unzip, the same reason the release workflow archives with pwsh.
if [ "$RUNNER_OS" = "Windows" ]; then
  powershell -Command "Expand-Archive -Force pluginval.zip ."
else
  unzip -q -o pluginval.zip
  # The archive does not reliably carry the executable bit through.
  chmod +x pluginval 2>/dev/null || true
  chmod +x pluginval.app/Contents/MacOS/pluginval 2>/dev/null || true
fi

# Printed so that a layout that differs from the one expected below
# reads as a wrong path rather than a mysteriously missing command.
ls -l

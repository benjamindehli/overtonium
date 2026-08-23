#!/bin/bash
#
# Builds the macOS installer.
#
# One component package per format, so the installer can offer them
# separately, then productbuild to wrap them in a single .pkg with a chooser.
# A plugin is a bundle, and pkgbuild wants each one alone in a staging root
# with the folder it belongs in given as the install location.
set -euo pipefail

version="${1:?usage: build-pkg.sh VERSION ARTEFACTS_DIR OUTPUT_PKG [IDENTITY]}"
artefacts="${2:?}"
output="${3:?}"

# A Developer ID Installer identity, if there is one. Without it the package
# still builds, it just cannot be notarised and the installer will be refused
# by Gatekeeper on a machine that has not been told otherwise.
identity="${4:-}"

here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

component() {
  local format="$1" bundle="$2" identifier="$3" location="$4"
  local src="$artefacts/$format/$bundle"

  if [ ! -e "$src" ]; then
    echo "no $format built, skipping"
    return
  fi

  mkdir -p "$work/root/$format"
  cp -R "$src" "$work/root/$format/"

  pkgbuild \
    --root "$work/root/$format" \
    --identifier "$identifier" \
    --version "$version" \
    --install-location "$location" \
    "$work/$format.pkg"

  echo "built $format.pkg"
}

component VST3 Overtonium.vst3 com.dehlimusikk.overtonium.vst3 \
  /Library/Audio/Plug-Ins/VST3
component AU Overtonium.component com.dehlimusikk.overtonium.au \
  /Library/Audio/Plug-Ins/Components
component Standalone Overtonium.app com.dehlimusikk.overtonium.app \
  /Applications

# The licence the installer shows before it will proceed.
mkdir -p "$work/resources"
cp "$here/../../LICENSE" "$work/resources/LICENSE.txt"

sed "s/__VERSION__/$version/g" "$here/distribution.xml" \
  > "$work/distribution.xml"

if [ -n "$identity" ]; then
  productbuild \
    --distribution "$work/distribution.xml" \
    --package-path "$work" \
    --resources "$work/resources" \
    --sign "$identity" \
    --timestamp \
    "$output"

  echo "built and signed $output"
else
  productbuild \
    --distribution "$work/distribution.xml" \
    --package-path "$work" \
    --resources "$work/resources" \
    "$output"

  echo "built $output, unsigned"
fi

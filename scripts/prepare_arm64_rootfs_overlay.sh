#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <tracked-overlay> <arm64-build-dir> <output-overlay>" >&2
  exit 64
fi

tracked_overlay=$(realpath "$1")
arm64_build=$(realpath "$2")
output_overlay=$3

rm -rf "$output_overlay"
mkdir -p "$output_overlay"
cp -a "$tracked_overlay"/. "$output_overlay"/
mkdir -p "$output_overlay/usr/bin"
install -m 0755 "$arm64_build/vecu_execution_manager" "$output_overlay/usr/bin/"
install -m 0755 "$arm64_build/vecu_faultable_service" "$output_overlay/usr/bin/"
chmod 0755 "$output_overlay/etc/init.d/S99vecu"

file "$output_overlay/usr/bin/vecu_execution_manager"
file "$output_overlay/usr/bin/vecu_faultable_service"


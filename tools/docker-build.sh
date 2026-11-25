#!/usr/bin/env bash
# Run a command inside the ZeroLED build container (docker/Dockerfile).
#
#   tools/docker-build.sh                      # cmake --workflow --preset cp0-cross-package
#   tools/docker-build.sh <cmd> [args...]      # anything else, e.g. a shell: tools/docker-build.sh bash
#
# The repo is mounted at the same absolute path as on the host, and the container
# runs as the host user, so build/, .cache/ and dist/ stay host-owned and the
# CMake caches are valid from either side.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${CP0_BUILD_IMAGE:-zeroled-build:trixie}"

if [[ -n "${CP0_REBUILD_IMAGE:-}" ]] || ! docker image inspect "$image" >/dev/null 2>&1; then
  docker build -t "$image" "$repo/docker"
fi

if (( $# == 0 )); then
  set -- cmake --workflow --preset cp0-cross-package
fi

tty_flags=()
[[ -t 0 && -t 1 ]] && tty_flags=(-it)

exec docker run --rm "${tty_flags[@]}" \
  --user "$(id -u):$(id -g)" \
  -v "$repo:$repo" -w "$repo" \
  "$image" "$@"

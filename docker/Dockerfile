# Build environment for WLED Remote (CardputerZero).
#
# Debian trixie matches the CardputerZero BSP sysroot (GCC 14, glibc 2.41), so the
# cross compiler's libstdc++ headers line up with the runtime shipped in the BSP.
# It also has CMake >= 3.31, which the presets require.
#
# Use via tools/docker-build.sh rather than directly.
FROM debian:trixie

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates git file xz-utils \
      cmake ninja-build pkg-config dpkg-dev \
      crossbuild-essential-arm64 \
      build-essential libpng-dev libjpeg-dev libfmt-dev libsdl2-dev \
      libfreetype-dev zlib1g-dev \
      python3 \
 && rm -rf /var/lib/apt/lists/*

# The container runs as the invoking host user (see tools/docker-build.sh), which
# has no passwd entry or home here; give git a writable HOME for FetchContent.
ENV HOME=/tmp/home
RUN mkdir -p /tmp/home && chmod 1777 /tmp/home \
 && git config --system --add safe.directory '*'

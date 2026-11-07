#!/bin/sh
# Snapshot the tree (so editing can continue) and run tests/linux/run.sh in a Debian container.
# Usage: sh tests/linux/test.sh [linux/arm64|linux/amd64]
cd "$(dirname "$0")/../.." || exit 1
PLAT=${1:-linux/arm64}
SNAP=bench/out/linux-snap-$(echo "$PLAT" | tr / -)
rm -rf "$SNAP" && mkdir -p "$SNAP"
tar --exclude=./build --exclude=./bench/out --exclude=./.xisto --exclude=./third_party/qbe/qbe --exclude='*.o' --exclude='./third_party/aws-lc/aws-lc-[0-9]*[0-9]' -cf - . | (cd "$SNAP" && tar -xf -)
mkdir -p "$SNAP/oracle" && cp build/tests/*.node "$SNAP/oracle/"
docker build -q --platform "$PLAT" -t "xisto-test:$(echo "$PLAT" | tr / -)" tests/linux > /dev/null || exit 1
docker run --rm --platform "$PLAT" -v "$PWD/$SNAP":/snap:ro "xisto-test:$(echo "$PLAT" | tr / -)" sh -c 'cp -r /snap/. /work/ && cd /work/third_party/qbe && make -s clean-gen > /dev/null 2>&1; cd /work && sh tests/linux/run.sh'

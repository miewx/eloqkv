#!/usr/bin/env bash
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"
. sh/pid.sh

if command -v podman &>/dev/null; then
  RUNNER=podman
else
  RUNNER=docker
fi

set -x

git submodule update --init --recursive

exec $RUNNER run --rm \
  -v /tmp/eloqkv:/tmp \
  -v "$DIR":/app \
  -w /app eloqdata/eloqkv-builder:latest \
  ./sh/build_then_test.sh "$@"

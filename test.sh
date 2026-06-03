#!/usr/bin/env bash
set -e
DIR=$(realpath $0) && DIR=${DIR%/*}
cd $DIR

if command -v podman &>/dev/null; then
  RUNNER=podman
else
  RUNNER=docker
fi

set -x

git submodule update --init --recursive

run_test() {
  $RUNNER run --rm \
    -v /tmp/eloqkv:/tmp \
    -v "$DIR":/app \
    -w /app eloqdata/eloqkv-builder:latest \
    ./sh/build_then_test.sh "$@"
}

run_test "$@"
if [ $# -eq 0 ]; then
  run_test -DWITH_DATA_STORE=ROCKSDB
fi


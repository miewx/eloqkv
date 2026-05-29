#!/usr/bin/env bash

set -e
DIR=$(realpath $0) && DIR=${DIR%/*}
cd $DIR
set -x

if [ ! -f "data_substrate/CMakeLists.txt" ]; then
  git submodule update --init --recursive
fi

export CCACHE_DIR="$DIR/.ccache"

mkdir -p build
cd build

cmake -DWITH_LOG_SERVICE=ON ..
NUM_PROCS=1
make -j"$NUM_PROCS"
chmod +x eloqkv

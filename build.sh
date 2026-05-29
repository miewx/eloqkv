#!/usr/bin/env bash

set -e
DIR=$(realpath $0) && DIR=${DIR%/*}
cd $DIR
set -x

if [ ! -f "data_substrate/CMakeLists.txt" ]; then
  git submodule update --init --recursive
fi

# Set ccache directory to be in the workspace to persist it across container runs
export CCACHE_DIR="$DIR/.ccache"

# Determine the number of compilation jobs based on available memory and CPUs to avoid OOM
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
if [ -f /proc/meminfo ]; then
  mem_total_kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
  mem_total_gb=$((mem_total_kb / 1024 / 1024))
  mem_jobs=$((mem_total_gb / 3))
  if [ $mem_jobs -lt 1 ]; then
    mem_jobs=1
  fi
  if [ $mem_jobs -lt $JOBS ]; then
    JOBS=$mem_jobs
  fi
elif [ "$(uname)" = "Darwin" ]; then
  mem_total_bytes=$(sysctl -n hw.memsize 2>/dev/null || echo 0)
  if [ $mem_total_bytes -gt 0 ]; then
    mem_total_gb=$((mem_total_bytes / 1024 / 1024 / 1024))
    mem_jobs=$((mem_total_gb / 3))
    if [ $mem_jobs -lt 1 ]; then
      mem_jobs=1
    fi
    if [ $mem_jobs -lt $JOBS ]; then
      JOBS=$mem_jobs
    fi
  fi
fi

if [ -z "$JOBS" ] || [ "$JOBS" -lt 1 ]; then
  JOBS=1
fi

if [ ! -d "build" ]; then
  mkdir build
fi

cd build

cmake -DWITH_LOG_SERVICE=ON ..
make -j$JOBS

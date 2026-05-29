#!/usr/bin/env bash

set -e
DIR=$(realpath $0) && DIR=${DIR%/*}
cd $DIR

if [ "$(uname)" = "Linux" ]; then
  RUNNER="podman"
else
  RUNNER="docker"
fi

set -x

# 基于 Dockerfile 构建测试镜像
"$RUNNER" build -t eloqkv-test .

# 运行容器，挂载当前目录并执行依赖安装、编译和测试
"$RUNNER" run --rm \
  --privileged \
  --ulimit memlock=-1 \
  -v "$DIR":/app \
  eloqkv-test \
  bash -c "bun install && ./build.sh && bun test ${*:-js/namespace.test.js}"

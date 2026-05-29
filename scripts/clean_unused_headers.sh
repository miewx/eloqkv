#!/usr/bin/env bash
set -euo pipefail

# 确保脚本在项目根目录下执行
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "=== 1. 构建 Docker 镜像 ==="
docker build -t eloqkv-iwyu -f scripts/clean_unused_headers.dockerfile .

echo "=== 2. 运行 Docker 容器进行 IWYU 清理 ==="
# 挂载宿主机项目根目录到 /app，并且将 /tmp 挂载到容器的 /tmp 以进行临时编译
docker run --rm --privileged \
    --ulimit memlock=-1 \
    -v /tmp:/tmp \
    -v "$DIR":/app \
    eloqkv-iwyu /app/scripts/clean_unused_headers_inner.sh

# 使用 eloqdata/eloqkv-builder 基础镜像作为构建底座
FROM eloqdata/eloqkv-builder:latest

# 安装 include-what-you-use (IWYU) 诊断工具
# 并自动将 fix_includes.py 软链接到 /usr/bin/fix_includes.py 方便脚本直接调用
RUN apt-get update && \
    apt-get install -y iwyu && \
    rm -rf /var/lib/apt/lists/* && \
    # 查找 fix_includes.py 的具体安装位置并创建软链接
    FIX_PATH=$(find /usr -name fix_includes.py | head -n 1) && \
    if [ -n "$FIX_PATH" ]; then \
        ln -sf "$FIX_PATH" /usr/bin/fix_includes.py; \
    fi

# 避免容器内工作目录的 Git 安全目录校验冲突
RUN git config --global --add safe.directory /app

# 设置工作目录
WORKDIR /app

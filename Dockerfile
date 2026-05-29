# 使用与构建测试相同的 builder 基础镜像
FROM eloqdata/eloqkv-builder:latest

# 安装 unzip（Bun 安装程序所需）和 git
RUN apt-get update && apt-get install -y unzip git && rm -rf /var/lib/apt/lists/*

# 设置 Bun 的环境变量 PATH
ENV PATH="/root/.bun/bin:${PATH}"

# 安装 Bun
RUN curl -fsSL https://bun.sh/install | bash

# 避免容器内工作目录的 Git 所有权冲突
RUN git config --global --add safe.directory /app

# 设置容器内工作目录
WORKDIR /app

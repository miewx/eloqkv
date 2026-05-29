#!/usr/bin/env bash
set -euo pipefail

# 确保脚本在项目根目录下执行
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "=== 1. 确认 IWYU 和 fix_include 可执行文件 ==="
IWYU_TOOL_BIN="/usr/bin/iwyu_tool"
FIX_INCLUDES_BIN="/usr/bin/fix_include"
echo "使用 iwyu_tool: $IWYU_TOOL_BIN"
echo "使用 fix_include: $FIX_INCLUDES_BIN"

echo "=== 2. 创建临时的构建目录 ==="
# 在 /tmp 目录下创建构建目录，避免污染宿主机挂载的源码目录
IWYU_BUILD_DIR="/tmp/build_iwyu"
rm -rf "$IWYU_BUILD_DIR"
mkdir -p "$IWYU_BUILD_DIR"
cd "$IWYU_BUILD_DIR"

echo "=== 3. 配置 CMake 并生成编译数据库 ==="
# 仅配置并生成 compile_commands.json，这里不直接使用 CMAKE_CXX_INCLUDE_WHAT_YOU_USE 编译以避免交错输出
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DBUILD_WITH_TESTS=ON "$DIR"

echo "=== 4. 运行标准编译以生成必要的头文件 ==="
make -j$(nproc 2>/dev/null || echo 4) eloqkv object_serialize_deserialize_test

echo "=== 5. 使用 iwyu_tool 并行捕获诊断日志 ==="
# 使用 iwyu_tool 在编译数据库上并行运行 IWYU 并生成诊断日志
# iwyu_tool 会自动串行化每个源文件的输出，防止日志交错
"$IWYU_TOOL_BIN" -p "$IWYU_BUILD_DIR" -j$(nproc 2>/dev/null || echo 4) -- -Xiwyu --no_comments -Xiwyu --no_fwd_decls > iwyu.log || true

echo "=== 6. 自动清理 git diff 修改过的文件中的多余头文件 ==="
# 切换回源码根目录获取 git diff 修改的文件列表
cd "$DIR"
FILES=$(git diff main --name-only | grep -E '\.(cpp|h)$' || true)

if [ -n "$FILES" ]; then
    echo "待处理的文件列表："
    echo "$FILES"
    
    # 运行 fix_include 对捕获的日志进行处理，并仅修改指定的文件
    # --nosafe_headers: 允许删除未使用的头文件（默认只添加不删除）
    # --reorder: 对头文件进行重新排序和整理
    python3 "$FIX_INCLUDES_BIN" --nosafe_headers --reorder $FILES < "$IWYU_BUILD_DIR/iwyu.log"
else
    echo "未发现任何相对于 main 分支被修改过的 C++ 文件。"
fi

echo "=== 7. 清理临时构建目录 ==="
rm -rf "$IWYU_BUILD_DIR"

echo "=== 头文件清理完成！ ==="

#!/usr/bin/env bash
set -euo pipefail

# 确保脚本在项目根目录下执行
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "=== 1. 查找 IWYU 和 fix_includes.py 可执行文件 ==="
IWYU_BIN=$(which include-what-you-use 2>/dev/null || echo "")
if [ -z "$IWYU_BIN" ]; then
    # 尝试常见的 macOS Homebrew 路径
    if [ -f "/opt/homebrew/bin/include-what-you-use" ]; then
        IWYU_BIN="/opt/homebrew/bin/include-what-you-use"
    else
        echo "错误：未找到 include-what-you-use 诊断工具。"
        exit 1
    fi
fi
echo "使用 IWYU: $IWYU_BIN"

FIX_INCLUDES_BIN=$(which fix_includes.py 2>/dev/null || echo "")
if [ -z "$FIX_INCLUDES_BIN" ]; then
    # 尝试常见路径
    if [ -f "/opt/homebrew/bin/fix_includes.py" ]; then
        FIX_INCLUDES_BIN="/opt/homebrew/bin/fix_includes.py"
    else
        # 容器内搜索
        FIX_INCLUDES_BIN=$(find /usr -name fix_includes.py 2>/dev/null | head -n 1 || echo "")
    fi
fi

if [ -z "$FIX_INCLUDES_BIN" ] || [ ! -f "$FIX_INCLUDES_BIN" ]; then
    echo "错误：未找到 fix_includes.py 头文件清理脚本。"
    exit 1
fi
echo "使用 fix_includes.py: $FIX_INCLUDES_BIN"

echo "=== 2. 创建临时的 IWYU 构建目录 ==="
rm -rf build_iwyu
mkdir build_iwyu
cd build_iwyu

echo "=== 3. 配置启用 IWYU 的 CMake ==="
# 使用 -DCMAKE_CXX_INCLUDE_WHAT_YOU_USE 在编译时对每个文件运行 IWYU。
# --no_comments: 跳过 IWYU 内部的原因注释，使输出更干净
# --no_fwd_decls: 跳过前向声明建议，只关注头文件的清理
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DCMAKE_CXX_INCLUDE_WHAT_YOU_USE="${IWYU_BIN};-Xiwyu;--no_comments;-Xiwyu;--no_fwd_decls" \
      -DBUILD_WITH_TESTS=ON ..

echo "=== 4. 运行编译并捕获 IWYU 诊断日志 ==="
# 编译 eloqkv 及单元测试，并将 stdout/stderr 重定向到 iwyu.log
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) eloqkv namespace_test object_serialize_deserialize_test > iwyu.log 2>&1 || true

echo "=== 5. 自动清理 git diff 修改过的文件中的多余头文件 ==="
# 获取相对于 main 分支有修改的 C++ 源文件和头文件列表
FILES=$(git diff main --name-only | grep -E '\.(cpp|h)$' || true)

if [ -n "$FILES" ]; then
    echo "待处理的文件列表："
    echo "$FILES"
    
    # 运行 fix_includes.py 对捕获的日志进行处理，并仅修改指定的文件
    # --nosafe_headers: 允许删除未使用的头文件（默认只添加不删除）
    # --reorder: 对头文件进行重新排序和整理
    # --quoted_includes_first: 优先将双引号本地头文件排在前面
    python3 "$FIX_INCLUDES_BIN" --nosafe_headers --reorder --quoted_includes_first $FILES < iwyu.log
else
    echo "未发现任何相对于 main 分支被修改过的 C++ 文件。"
fi

echo "=== 6. 清理临时构建目录 ==="
cd "$DIR"
rm -rf build_iwyu

echo "=== 头文件清理完成！ ==="

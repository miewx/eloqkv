#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

# Ensure we are in the repository root directory
DIR=$(realpath "$0") && DIR=${DIR%/*}
cd "$DIR"

# 1. 获取当前分支名
CURRENT_BRANCH=$(git symbolic-ref --short HEAD 2>/dev/null)
if [ -z "$CURRENT_BRANCH" ]; then
    echo "错误：当前未处于任何 Git 分支（处于分离头指针状态，detached HEAD）。"
    exit 1
fi

if [[ "$CURRENT_BRANCH" == *_pure ]]; then
    echo "错误：您已经处于 _pure 分支上 ($CURRENT_BRANCH)。"
    exit 1
fi

PURE_BRANCH="${CURRENT_BRANCH}_pure"
echo "正在同步 $CURRENT_BRANCH -> $PURE_BRANCH ..."

# 2. 检查未跟踪的 cpp, h, hpp 文件并临时追踪
UNTRACKED_FILES=$(git ls-files --others --exclude-standard -- '*.[ch]' '*.hpp' '*.cpp')
if [ -n "$UNTRACKED_FILES" ]; then
    echo "$UNTRACKED_FILES" | xargs git add -N
fi

# 3. 生成差异 patch
PATCH_FILE=$(mktemp "$DIR/.sync_patch_XXXXXX")
git diff main -- '*.[ch]' '*.hpp' '*.cpp' > "$PATCH_FILE"

# 清除临时意向标记
if [ -n "$UNTRACKED_FILES" ]; then
    echo "$UNTRACKED_FILES" | xargs git reset -q HEAD
fi

# 4. 检查是否有需要应用的修改
if [ ! -s "$PATCH_FILE" ]; then
    rm -f "$PATCH_FILE"
    git branch -f "$PURE_BRANCH" main >/dev/null
    echo "未检测到 cpp/h/hpp 修改，已同步 $PURE_BRANCH 为 main 分支。正在推送..."
    git push -q -f origin "$PURE_BRANCH"
    echo "同步并推送完成！"
    exit 0
fi

# 5. 创建临时工作区以进行后台静默更新
TEMP_DIR=$(mktemp -d "$DIR/.sync_worktree_XXXXXX")

git branch -f "$PURE_BRANCH" main >/dev/null
git worktree add "$TEMP_DIR" "$PURE_BRANCH" >/dev/null 2>&1

if git -C "$TEMP_DIR" apply "$PATCH_FILE" >/dev/null 2>&1; then
    git -C "$TEMP_DIR" add -A
    git -C "$TEMP_DIR" commit -q -m "Sync cpp/h/hpp changes from $CURRENT_BRANCH"
    SUCCESS=true
else
    SUCCESS=false
fi

# 清理临时工作区
git worktree remove --force "$TEMP_DIR" >/dev/null 2>&1
rm -rf "$TEMP_DIR"
rm -f "$PATCH_FILE"

if [ "$SUCCESS" = true ]; then
    echo "正在推送 $PURE_BRANCH 到远程仓库..."
    if git push -q -f origin "$PURE_BRANCH"; then
        echo "同步并推送完成！"
    else
        echo "错误：推送 $PURE_BRANCH 失败。"
        exit 1
    fi
else
    echo "错误：应用补丁到 $PURE_BRANCH 失败。"
    exit 1
fi

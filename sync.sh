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

# 2. 检查 PURE_BRANCH 分支状态
if git rev-parse --verify "$PURE_BRANCH" >/dev/null 2>&1; then
  echo "分支 $PURE_BRANCH 已存在，将直接在其基础上更新。"
elif git rev-parse --verify "origin/$PURE_BRANCH" >/dev/null 2>&1; then
  echo "检测到远程分支 origin/$PURE_BRANCH，正在拉取到本地..."
  git branch "$PURE_BRANCH" "origin/$PURE_BRANCH" >/dev/null
else
  echo "分支 $PURE_BRANCH 不存在，将基于 main 分支创建。"
  git branch "$PURE_BRANCH" main >/dev/null
fi

# 3. 创建临时工作区以进行更新
TEMP_DIR=""
cleanup() {
  if [ -n "$TEMP_DIR" ] && [ -d "$TEMP_DIR" ]; then
    cd "$DIR"
    git worktree remove --force "$TEMP_DIR" >/dev/null 2>&1
    rm -rf "$TEMP_DIR"
  fi
}
trap cleanup EXIT

TEMP_DIR=$(mktemp -d "$DIR/.sync_worktree_XXXXXX")
git worktree add "$TEMP_DIR" "$PURE_BRANCH" >/dev/null 2>&1

# 4. 删除临时工作区中已有的匹配文件（以正确处理当前分支中被删除的文件）
echo "清理临时工作区中的旧文件..."
(
  cd "$TEMP_DIR"
  git -c core.quotePath=false ls-files -z -- '*.[ch]' '*.hpp' '*.cpp' '*.ini' '*.txt' | xargs -0 rm -f
)

# 5. 从当前分支的工作目录复制对应的文件到临时工作区
echo "正在复制最新的代码文件..."
git -c core.quotePath=false ls-files --cached --others --exclude-standard -- '*.[ch]' '*.hpp' '*.cpp' '*.ini' '*.txt' | tar -cf - -T - | tar -xf - -C "$TEMP_DIR"

# 6. 进入临时工作区提交代码
(
  cd "$TEMP_DIR"
  git add -A
  
  if git diff --quiet --cached; then
    echo "没有检测到任何文件变化，无需提交。"
    touch .no_changes
    exit 0
  fi
  
  echo "正在基于代码提交更改..."
  # 优先使用用户的自定义提交命令 gci，若不存在则回退到普通提交
  if command -v gci >/dev/null 2>&1; then
    gci
  elif [ -x "/Users/z/.bin/gci" ]; then
    /Users/z/.bin/gci
  else
    echo "警告：未找到自定义提交命令 gci，回退到普通 git commit。"
    git commit -m "Sync cpp/h/hpp/ini changes from $CURRENT_BRANCH"
  fi
)

# 7. 推送分支
if [ -f "$TEMP_DIR/.no_changes" ]; then
  echo "无需推送，$PURE_BRANCH 已是最新的。"
else
  echo "正在推送 $PURE_BRANCH 到远程仓库..."
  if git push -f origin "$PURE_BRANCH"; then
    echo "同步并推送完成！"
  else
    echo "错误：推送 $PURE_BRANCH 失败。"
    exit 1
  fi
fi


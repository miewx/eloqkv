#!/usr/bin/env bash

set -e

DIR=$(realpath "$0") && DIR=${DIR%/*}
cd "$DIR"

pre-commit run --all-files || true
git add . && git commit -m. && git push || true

# 1. 获取当前分支名称
CURRENT_BRANCH=$(git branch --show-current)
if [ -z "$CURRENT_BRANCH" ]; then
  CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
fi

PURE_BRANCH="${CURRENT_BRANCH}_pure"
TMP_DIR="/tmp/eloqkv_pure"
rm -rf $TMP_DIR

cleanup() {
  echo "正在清理工作区 (worktree)..."
  cd "$DIR"
  if git worktree list | grep -q "$TMP_DIR"; then
    git worktree remove -f "$TMP_DIR" || true
  fi
  if [ -d "$TMP_DIR" ]; then
    rm -rf "$TMP_DIR" || true
  fi
}
trap cleanup EXIT

# 清理使用相同分支或目录的任何过期/残留工作区 (worktree)
git worktree list | grep "\[$PURE_BRANCH\]" | awk '{print $1}' | while read -r wt_path; do
  git worktree remove -f "$wt_path" || true
done
if [ -d "$TMP_DIR" ]; then
  rm -rf "$TMP_DIR" || true
fi
git worktree prune || true

# 2. 使用 git worktree 在临时目录下创建带有 _pure 后缀的新分支
set -x
git worktree add "$TMP_DIR" -B "$PURE_BRANCH"
set +x

# 3. 进入工作区目录并删除 *.sh 和 sh 目录
cd "$TMP_DIR"
rm -f *.sh
rm -rf sh .mise.toml

# 4. 使用 soft reset 将此分支与 main 之间的所有更改压缩为单个提交
git reset --soft main

# 执行 git add
git add -A

# 运行 gci 生成提交
gci

# 5. 强制推送新分支到远程仓库
git push origin "$PURE_BRANCH" --force

# 6. 返回原始目录并删除本地 _pure 分支
cd "$DIR"
# 先移除工作区，以便我们可以删除分支
if git worktree list | grep -q "$TMP_DIR"; then
  git worktree remove -f "$TMP_DIR" || true
fi
if [ -d "$TMP_DIR" ]; then
  rm -rf "$TMP_DIR" || true
fi

git branch -D "$PURE_BRANCH" || true

echo "执行成功！"

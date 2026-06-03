#!/usr/bin/env bash

set -e

DIR=$(realpath "$0") && DIR=${DIR%/*}
cd "$DIR"

pre-commit run --all-files
git add .
git commit -m.
git push

# 1. Get the current branch name
CURRENT_BRANCH=$(git branch --show-current)
if [ -z "$CURRENT_BRANCH" ]; then
  CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
fi

PURE_BRANCH="${CURRENT_BRANCH}_pure"
TMP_DIR="/tmp/eloqkv_pure"
rm -rf $TMP_DIR

cleanup() {
  echo "Cleaning up worktree..."
  cd "$DIR"
  if git worktree list | grep -q "$TMP_DIR"; then
    git worktree remove -f "$TMP_DIR" || true
  fi
  if [ -d "$TMP_DIR" ]; then
    rm -rf "$TMP_DIR" || true
  fi
}
trap cleanup EXIT

# Clean up any stale worktree using the same branch or directory
git worktree list | grep "\[$PURE_BRANCH\]" | awk '{print $1}' | while read -r wt_path; do
  git worktree remove -f "$wt_path" || true
done
if [ -d "$TMP_DIR" ]; then
  rm -rf "$TMP_DIR" || true
fi
git worktree prune || true

# 2. Use git worktree to create a new branch with _pure suffix at the tmp directory
set -x
git worktree add "$TMP_DIR" -B "$PURE_BRANCH"
set +x

# 3. Go to the worktree directory and delete *.sh and sh directory
cd "$TMP_DIR"
rm -f *.sh
rm -rf sh .mise.toml

# 4. Squash all changes between this branch and main into a single commit using soft reset
git reset --soft main

# Delete the remote branch of the same name first. If not exists, ignore error
git push origin --delete "$PURE_BRANCH" || true

# Git add
git add -A

# Run gci to generate the commit
gci

# 5. Force push the new branch to remote
git push origin "$PURE_BRANCH" --force

# 6. Go back to original directory and delete local _pure branch
cd "$DIR"
# Remove worktree first so we can delete the branch
if git worktree list | grep -q "$TMP_DIR"; then
  git worktree remove -f "$TMP_DIR" || true
fi
if [ -d "$TMP_DIR" ]; then
  rm -rf "$TMP_DIR" || true
fi

git branch -D "$PURE_BRANCH" || true

echo "Done successfully!"

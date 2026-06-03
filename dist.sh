#!/usr/bin/env bash

set -e

DIR=$(realpath "$0") && DIR=${DIR%/*}
cd "$DIR"

# 1. Get the current branch name
CURRENT_BRANCH=$(git branch --show-current)
if [ -z "$CURRENT_BRANCH" ]; then
    CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
fi

PURE_BRANCH="${CURRENT_BRANCH}_pure"
TMP_DIR="${DIR}/.tmp/eloqkv_pure"

# Cleanup function to be run on exit or error
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
rm -rf sh

# Commit the deletion of *.sh and sh/
git add -A
git commit -m "chore: remove .sh files and sh directory" || true

# 4. Squash all changes between this branch and main into a single commit
git reset --soft main

# Commit the squashed changes
if git diff --cached --quiet; then
    git commit --allow-empty -m "feat: squashed changes from ${CURRENT_BRANCH} (without sh/sh scripts)"
else
    git commit -m "feat: squashed changes from ${CURRENT_BRANCH} (without sh/sh scripts)"
fi

# 5. Force push the new branch to remote
git push origin "$PURE_BRANCH" --force

# 6. Go back to original directory and delete local _pure branch
cd "$DIR"
# Remove worktree first so we can delete the branch
git worktree remove -f "$TMP_DIR" || true
rm -rf "$TMP_DIR" || true

git branch -D "$PURE_BRANCH" || true

echo "Done successfully!"

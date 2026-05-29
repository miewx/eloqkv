#!/usr/bin/env bun
import { $, cd } from "zx";
import fs from "node:fs";
import path from "node:path";
import { execSync } from "node:child_process";

// 1. Ensure we are in the repository root directory
const repoRoot = import.meta.dirname;
cd(repoRoot);
$.verbose = 1;

// Helper to check if a command exists in PATH
function commandExists(cmd) {
  try {
    execSync(`command -v ${cmd}`, { stdio: "ignore" });
    return true;
  } catch (e) {
    return false;
  }
}

async function run() {
  // 1. 获取当前分支名
  let currentBranch = "";
  try {
    const result = await $`git symbolic-ref --short HEAD`;
    currentBranch = result.stdout.trim();
  } catch (e) {
    console.error("错误：当前未处于任何 Git 分支（处于分离头指针状态，detached HEAD）。");
    process.exit(1);
  }

  if (currentBranch.endsWith("_pure")) {
    console.error(`错误：您已经处于 _pure 分支上 (${currentBranch})。`);
    process.exit(1);
  }

  const pureBranch = `${currentBranch}_pure`;
  console.log(`正在同步 ${currentBranch} -> ${pureBranch} ...`);

  // 2. 检查 PURE_BRANCH 分支状态
  let branchExists = false;
  try {
    await $`git rev-parse --verify ${pureBranch}`;
    branchExists = true;
    console.log(`分支 ${pureBranch} 已存在，将直接在其基础上更新。`);
  } catch (e) {
    // Branch does not exist locally
  }

  if (!branchExists) {
    let originExists = false;
    try {
      await $`git rev-parse --verify origin/${pureBranch}`;
      originExists = true;
    } catch (e) {}

    if (originExists) {
      console.log(`检测到远程分支 origin/${pureBranch}，正在拉取到本地...`);
      await $`git branch ${pureBranch} origin/${pureBranch}`;
    } else {
      console.log(`分支 ${pureBranch} 不存在，将基于 main 分支创建。`);
      await $`git branch ${pureBranch} main`;
    }
  }

  // 3. 创建临时工作区以进行更新
  let tempDir = "";
  try {
    tempDir = fs.mkdtempSync(path.join(repoRoot, ".sync_worktree_"));
    // git worktree add expects the directory to either not exist or be empty.
    // We remove it first so git can create/initialize it.
    fs.rmSync(tempDir, { recursive: true, force: true });
    await $`git worktree add ${tempDir} ${pureBranch}`;

    // 4. 获取当前工作区存在的所有文件
    const workspaceFilesOutput =
      await $`git -c core.quotePath=false ls-files --cached --others --exclude-standard`;
    const workspaceFiles = new Set();
    for (const line of workspaceFilesOutput.stdout.split("\n")) {
      const file = line.trim();
      if (file && fs.existsSync(path.join(repoRoot, file))) {
        workspaceFiles.add(file);
      }
    }

    // 5. 获取 pure 分支中已有的所有文件
    const pureFilesOutput = await $`git -c core.quotePath=false -C ${tempDir} ls-files`;
    const pureFiles = pureFilesOutput.stdout
      .split("\n")
      .map((line) => line.trim())
      .filter((line) => line.length > 0);

    // 6. 删除不存在于当前工作区但存在于 pure 分支的文件
    console.log("清理临时工作区中多余/已删除的文件...");
    for (const file of pureFiles) {
      if (!workspaceFiles.has(file)) {
        const filePath = path.join(tempDir, file);
        if (fs.existsSync(filePath)) {
          console.log(`  删除：${file}`);
          fs.rmSync(filePath, { force: true });
        }
      }
    }

    // 7. 从当前工作目录复制最新匹配文件到临时工作区
    console.log("正在复制最新的代码文件...");
    const filesToCopyOutput =
      await $`git -c core.quotePath=false ls-files --cached --others --exclude-standard -- '*.[ch]' '*.hpp' '*.cpp' '*.ini' '*.txt'`;
    const filesToCopy = filesToCopyOutput.stdout
      .split("\n")
      .map((line) => line.trim())
      .filter((line) => line.length > 0);

    for (const file of filesToCopy) {
      const srcPath = path.join(repoRoot, file);
      const destPath = path.join(tempDir, file);

      const destDir = path.dirname(destPath);
      if (!fs.existsSync(destDir)) {
        fs.mkdirSync(destDir, { recursive: true });
      }

      fs.copyFileSync(srcPath, destPath);
    }

    // 8. 进入临时工作区提交代码
    process.chdir(tempDir);
    await $`git add -A`;

    let hasChanges = true;
    try {
      await $`git diff --quiet --cached`;
      hasChanges = false;
    } catch (e) {
      // Non-zero exit code means there are changes
    }

    if (!hasChanges) {
      console.log("没有检测到任何文件变化，无需提交。");
      fs.writeFileSync(".no_changes", "");
    } else {
      console.log("正在基于代码提交更改...");

      let useGci = false;

      if (useGci) {
        // unreachable
      } else {
        console.warn("警告：回退到普通 git commit。");
        await $`git commit -m "Sync cpp/h/hpp/ini changes from ${currentBranch}"`;
      }
    }

    // 切换回主目录以允许删除工作区
    process.chdir(repoRoot);

    // 9. 推送分支
    const noChangesPath = path.join(tempDir, ".no_changes");
    if (fs.existsSync(noChangesPath)) {
      console.log(`无需推送，${pureBranch} 已是最新的。`);
    } else {
      console.log(`正在推送 ${pureBranch} 到远程仓库...`);
      try {
        await $`git push -f origin ${pureBranch}`;
        console.log("同步并推送完成！");
      } catch (e) {
        console.error(`错误：推送 ${pureBranch} 失败。`);
        process.exit(1);
      }
    }
  } finally {
    // 确保清理临时工作区
    if (tempDir && fs.existsSync(tempDir)) {
      console.log("清理临时工作区...");
      // Change dir back to repo root to avoid busy directory
      process.chdir(repoRoot);
      try {
        await $`git worktree remove --force ${tempDir}`;
      } catch (e) {}
      try {
        fs.rmSync(tempDir, { recursive: true, force: true });
      } catch (e) {}
    }
  }
}

run().catch((err) => {
  console.error("运行过程中发生错误:", err);
  process.exit(1);
});

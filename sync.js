#!/usr/bin/env bun
import { simpleGit } from "simple-git";
import gci from "@3-/gci";
import fs from "node:fs";
import path from "node:path";
import ERR from "@3-/log/ERR.js";

// 1. Ensure we are in the repository root directory
const repo_root = import.meta.dirname,
  git = simpleGit(repo_root),
  run = async () => {
    // 1. 获取当前分支名
    let current_branch = "";
    try {
      current_branch = (await git.revparse(["--abbrev-ref", "HEAD"])).trim();
    } catch {
      ERR("错误：当前未处于任何 Git 分支（处于分离头指针状态，detached HEAD）。");
      process.exit(1);
    }

    if (current_branch.endsWith("_pure")) {
      ERR("错误：您已经处于 _pure 分支上 (" + current_branch + ")。");
      process.exit(1);
    }

    const pure_branch = current_branch + "_pure";
    console.log("正在同步 " + current_branch + " -> " + pure_branch + " ...");

    // 2. 检查 PURE_BRANCH 分支状态
    let branch_exists = false;
    try {
      await git.revparse(["--verify", pure_branch]);
      branch_exists = true;
      console.log("分支 " + pure_branch + " 已存在，将直接在其基础上更新。");
    } catch {
      // Branch does not exist locally
    }

    if (!branch_exists) {
      let origin_exists = false;
      try {
        await git.revparse(["--verify", "origin/" + pure_branch]);
        origin_exists = true;
      } catch {}

      if (origin_exists) {
        console.log("检测到远程分支 origin/" + pure_branch + "，正在拉取到本地...");
        await git.branch([pure_branch, "origin/" + pure_branch]);
      } else {
        console.log("分支 " + pure_branch + " 不存在，将基于 main 分支创建。");
        await git.branch([pure_branch, "main"]);
      }
    }

    // 3. 创建临时工作区以进行更新
    let temp_dir = "";
    try {
      temp_dir = fs.mkdtempSync(path.join(repo_root, ".sync_worktree_"));
      // git worktree add expects the directory to either not exist or be empty.
      // We remove it first so git can create/initialize it.
      fs.rmSync(temp_dir, { recursive: true, force: true });
      await git.raw(["worktree", "add", temp_dir, pure_branch]);

      const temp_git = simpleGit(temp_dir),
        workspace_files_output = await git.raw([
          "-c",
          "core.quotePath=false",
          "ls-files",
          "--cached",
          "--others",
          "--exclude-standard",
        ]),
        workspace_files = new Set();

      for (const line of (workspace_files_output || "").split("\n")) {
        const file = line.trim();
        if (file && fs.existsSync(path.join(repo_root, file))) {
          workspace_files.add(file);
        }
      }

      // 5. 获取 pure 分支中已有的所有文件
      const pure_files_output = await temp_git.raw(["-c", "core.quotePath=false", "ls-files"]),
        pure_files = (pure_files_output || "")
          .split("\n")
          .map((line) => line.trim())
          .filter((line) => line.length > 0);

      // 6. 删除不存在于当前工作区但存在于 pure 分支的文件
      console.log("清理临时工作区中多余/已删除的文件...");
      for (const file of pure_files) {
        if (!workspace_files.has(file)) {
          const file_path = path.join(temp_dir, file);
          if (fs.existsSync(file_path)) {
            console.log("  删除：" + file);
            fs.rmSync(file_path, { force: true });
          }
        }
      }

      // 7. 从当前工作目录复制最新匹配文件到临时工作区
      console.log("正在复制最新的代码文件...");
      const files_to_copy_output = await git.raw([
          "-c",
          "core.quotePath=false",
          "ls-files",
          "--cached",
          "--others",
          "--exclude-standard",
          "--",
          "*.[ch]",
          "*.hpp",
          "*.cpp",
          "*.ini",
          "*.txt",
        ]),
        files_to_copy = (files_to_copy_output || "")
          .split("\n")
          .map((line) => line.trim())
          .filter((line) => line.length > 0);

      for (const file of files_to_copy) {
        const src_path = path.join(repo_root, file),
          dest_path = path.join(temp_dir, file),
          dest_dir = path.dirname(dest_path);

        if (!fs.existsSync(dest_dir)) {
          fs.mkdirSync(dest_dir, { recursive: true });
        }

        fs.copyFileSync(src_path, dest_path);
      }

      // 8. 进入临时工作区提交代码
      await temp_git.add("-A");

      const diff_text = await temp_git.diff(["--cached"]);

      if (!diff_text.trim()) {
        console.log("没有检测到任何文件变化，无需提交。");
        fs.writeFileSync(path.join(temp_dir, ".no_changes"), "");
      } else {
        console.log("正在基于代码提交更改...");
        try {
          const git_url = await git.remote(["get-url", "origin"]).catch(() => "");
          process.env.NO_PUSH = "1";
          await gci(git_url, temp_dir);
        } catch (err) {
          ERR("错误：gci 自动生成提交消息并提交失败，退出同步。", err.message || err);
          throw err;
        }
      }

      // 9. 推送分支
      const no_changes_path = path.join(temp_dir, ".no_changes");
      if (fs.existsSync(no_changes_path)) {
        console.log("无需推送，" + pure_branch + " 已是最新的。");
      } else {
        console.log("正在推送 " + pure_branch + " 到远程仓库...");
        try {
          await temp_git.push(["-f", "origin", pure_branch]);
          console.log("同步并推送完成！");
        } catch (e) {
          ERR("错误：推送 " + pure_branch + " 失败。", e.message || e);
          throw e;
        }
      }
    } finally {
      // 确保清理临时工作区
      if (temp_dir && fs.existsSync(temp_dir)) {
        console.log("清理临时工作区...");
        try {
          await git.raw(["worktree", "remove", "--force", temp_dir]);
        } catch {}
        try {
          fs.rmSync(temp_dir, { recursive: true, force: true });
        } catch {}
      }
    }
  };

try {
  await run();
} catch (err) {
  ERR("同步发生未捕获的错误：", err.message || err);
  process.exit(1);
}

export default run;

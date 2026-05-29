#!/usr/bin/env python3
import os
import subprocess
import sys

def main():
    # 1. Get modified files relative to main branch
    try:
        git_diff = subprocess.check_output(["git", "diff", "--name-only", "main"]).decode().strip()
    except subprocess.CalledProcessError as e:
        print("Error running git diff: ", e)
        return

    modified_files = git_diff.split("\n") if git_diff else []
    
    # Filter for C++ source files
    source_extensions = (".cpp", ".cc", ".cxx")
    cpp_files = [os.path.abspath(f) for f in modified_files if f.endswith(source_extensions) and os.path.exists(f)]
    
    if not cpp_files:
        print("No modified C++ source files found compared to main branch.")
        return
        
    print(f"Found {len(cpp_files)} modified C++ source files relative to main branch:")
    for f in cpp_files:
        print(f"  - {os.path.basename(f)}")
    print("\nStarting IWYU analysis on the host...\n")

    sysroot = subprocess.check_output(["xcrun", "--show-sdk-path"]).decode().strip()

    # Base IWYU tool command
    iwyu_cmd_base = [
        "python3", "/opt/homebrew/bin/iwyu_tool.py", "-p", "build_local"
    ]
    
    # macOS system root & Homebrew libraries include paths
    extra_args = [
        "--",
        "-I/opt/homebrew/opt/brpc/include",
        "-I/opt/homebrew/opt/glog/include",
        "-I/opt/homebrew/opt/gflags/include",
        "-I/opt/homebrew/opt/leveldb/include",
        "-I/opt/homebrew/opt/prometheus-cpp/include",
        "-isysroot", sysroot
    ]

    for filepath in cpp_files:
        print("="*80)
        print(f"Analyzing: {os.path.relpath(filepath)}")
        print("="*80)
        
        cmd = iwyu_cmd_base + [filepath] + extra_args
        res = subprocess.run(cmd, capture_output=True, text=True)
        
        output = res.stdout + "\n" + res.stderr
        
        # Check if there are compilation errors
        if "error:" in output or "fatal error:" in output:
            print("❌ SKIPPED (compilation error on host)")
            print("Note: This usually happens due to Protobuf/BRPC version mismatches on macOS vs Docker.")
            continue
            
        # Parse output for recommendations
        lines = res.stdout.split("\n")
        should_add = []
        should_remove = []
        
        state = 0  # 0: idle, 1: reading should add, 2: reading should remove
        for line in lines:
            if "should add these lines" in line:
                state = 1
                continue
            elif "should remove these lines" in line:
                state = 2
                continue
            elif "The full include-list" in line or line.strip() == "---":
                state = 0
                continue
                
            if state == 1 and line.strip():
                should_add.append(line.strip())
            elif state == 2 and line.strip():
                should_remove.append(line.strip())
                
        if not should_add and not should_remove:
            print("✅ All includes are perfect! No unused or missing headers.")
        else:
            if should_remove:
                print("🚫 Unused headers (safe to remove):")
                for r in should_remove:
                    # Clean up macOS-specific internal headers if any slip through
                    if "_ctype.h" in r or "_stdlib.h" in r or "_string.h" in r:
                        continue
                    print(f"  {r}")
            if should_add:
                print("➕ Missing headers (suggested to add):")
                for a in should_add:
                    # Map macOS system internal headers to standard C++ ones
                    if "_ctype.h" in a:
                        a = '#include <cctype>'
                    elif "_stdlib.h" in a:
                        a = '#include <cstdlib>'
                    elif "_string.h" in a:
                        a = '#include <string>'
                    print(f"  {a}")
        print()

if __name__ == "__main__":
    main()

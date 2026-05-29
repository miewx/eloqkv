import os
import subprocess
import sys

def main():
    # Read files list
    with open("file_list.txt") as f:
        files = f.read().split()

    print(f"Total files to analyze: {len(files)}")
    sysroot = subprocess.check_output(["xcrun", "--show-sdk-path"]).decode().strip()

    iwyu_cmd_base = [
        "python3", "/opt/homebrew/bin/iwyu_tool.py", "-p", "build_local"
    ]
    
    extra_args = [
        "--",
        "-I/opt/homebrew/opt/brpc/include",
        "-I/opt/homebrew/opt/glog/include",
        "-I/opt/homebrew/opt/gflags/include",
        "-I/opt/homebrew/opt/leveldb/include",
        "-I/opt/homebrew/opt/prometheus-cpp/include",
        "-isysroot", sysroot
    ]

    success_count = 0
    skipped_count = 0

    for i, filepath in enumerate(files):
        # We also need to expand -isysroot flag correctly for subprocess
        cmd = iwyu_cmd_base + [filepath] + extra_args
        
        # We run the command
        print(f"[{i+1}/{len(files)}] Analyzing {os.path.basename(filepath)}...", end="", flush=True)
        res = subprocess.run(cmd, capture_output=True, text=True)
        
        # Check if there are compilation errors in output
        output = res.stdout + "\n" + res.stderr
        
        # Simple heuristic for compilation errors
        has_error = "error:" in output or "fatal error:" in output
        
        if has_error:
            print(" SKIPPED (compilation error on host)")
            skipped_count += 1
            continue

        # Save output to a temp file and run fix_includes.py on it
        temp_out = f"temp_iwyu_{success_count}.out"
        with open(temp_out, "w") as tf:
            tf.write(res.stdout)
            
        fix_res = subprocess.run(
            ["python3", "/opt/homebrew/bin/fix_includes.py", "--nosafe_headers"],
            input=res.stdout,
            capture_output=True,
            text=True
        )
        
        if fix_res.returncode == 0 or "Fixing" in fix_res.stdout:
            print(" OPTIMIZED!")
            success_count += 1
        else:
            print(" NO CHANGES NEEDED")
            skipped_count += 1
            
        if os.path.exists(temp_out):
            os.remove(temp_out)

    print(f"\nOptimization Finished!")
    print(f"Optimized: {success_count} files")
    print(f"Skipped: {skipped_count} files")

if __name__ == "__main__":
    main()

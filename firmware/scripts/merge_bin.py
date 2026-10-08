"""
Post-build: merge bootloader + partition table + boot_app0 + app into one binary
for esp-web-tools browser flashing. Output: .pio/build/<env>/firmware-merged.bin
and a copy in ../web-installer/.
"""
Import("env")
import os, subprocess, sys, shutil

def merge_bin(source, target, env):
    build_dir   = env.subst("$BUILD_DIR")
    merged_path = os.path.join(build_dir, "firmware-merged.bin")
    bootloader  = os.path.join(build_dir, "bootloader.bin")
    partitions  = os.path.join(build_dir, "partitions.bin")
    app         = os.path.join(build_dir, "firmware.bin")
    boot_app0   = os.path.join(env.PioPlatform().get_package_dir("framework-arduinoespressif32"),
                               "tools", "partitions", "boot_app0.bin")
    esptool_py  = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    python      = env.subst("$PYTHONEXE") or sys.executable

    flags = env.subst("$UPLOADERFLAGS").split()
    flash_mode, flash_freq = "dio", "80m"
    for i, a in enumerate(flags):
        if a == "--flash_mode" and i + 1 < len(flags): flash_mode = flags[i + 1]
        if a == "--flash_freq" and i + 1 < len(flags): flash_freq = flags[i + 1]

    cmd = [python, esptool_py, "--chip", "esp32s3", "merge_bin",
           "--flash_mode", flash_mode, "--flash_freq", flash_freq, "--flash_size", "4MB",
           "-o", merged_path,
           "0x0000", bootloader, "0x8000", partitions, "0xe000", boot_app0, "0x10000", app]
    print("Merging firmware -> " + merged_path)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("merge_bin failed:", r.stderr); return
    web_dir = os.path.join(env.subst("$PROJECT_DIR"), "..", "web-installer")
    if os.path.isdir(web_dir):
        shutil.copy2(merged_path, os.path.join(web_dir, "firmware-merged.bin"))
        print("Copied to web-installer/firmware-merged.bin")

env.AddPostAction("$BUILD_DIR/firmware.bin", merge_bin)

# Dive GPU Profiler

Dive is a powerful GPU profiler designed for inspecting low-level graphics data. It is primarily used for **OpenXR** applications running on **Android XR devices** featuring **Qualcomm Adreno 7XX series GPUs**.

Dive operates through two main components: a User Interface for graphical analysis and a Command Line Interface for automated scripting and capture. Both components currently support capturing and replaying OpenXR and Vulkan applications on Android.

* **Testing Environment:** Both the **Dive User Interface** and **Dive Command Line Interface** were tested on a clean **gLinux cloudtop**.

---

## Prerequisites

Before using Dive, ensure the following tools are installed and configured on your host machine:

#### 1. Android Debug Bridge (adb)
You must have `adb` installed and accessible in your system's PATH.
* **Installation (Debian/Ubuntu):**
    ```bash
    sudo apt install adb
    ```
* **Alternative:** Get the latest version from the **Android SDK Platform Tools**.

#### 2. Python
Python must be installed, and the `python` command must be in your system's PATH.
* **Recommendation:** It is **highly recommended** to use a virtual environment (e.g., `virtualenv` or `pipenv`) for dependency management.
* **Debian alternative:**
    ```bash
    sudo apt install python-is-python3
    ```

#### 3. Linux Dependencies
* **Non-gLinux Systems:** For environments other than gLinux cloudtops (e.g., standard Linux desktops or laptops), additional `apt` packages may be required for a successful installation and run.

---

## Dive Components

### Dive User Interface (Dive UI)

The UI tool, executed as the `dive` binary, provides a graphical environment for analysis.

### Dive Command Line Interface (Dive CLI)

The CLI tool, executed as the `dive_client_cli` binary, is ideal for scripting, automation, and headless operations.

### Standalone PM4 Capture

Examples:
 - Install the dependencies on device and start the package and do a capture after the applications runs 5 seconds.
 ```
 ./dive_client_cli --device 9A221FFAZ004TL --command capture --package de.saschawillems.vulkanBloom --type vulkan --trigger_capture_after 5 --download_dir "/path/to/save/captures"
 ```

 - Install the dependencies on device and start the package
 ```
 ./dive_client_cli --device 9A221FFAZ004TL --command run --package com.google.bigwheels.project_cube_xr.debug --type openxr --download_dir "/path/to/save/captures"
 ```
Then you can follow the hint output to trigger a capture by press key `t` and `enter` or exit by press key `enter` only.

The capture files will be saved at the path specified with the `--download_dir` option or the current directory if this option not specified.

### GFXR Capture
GFXR capturing can be triggered in the ui or within the cli.

To begin a GFXR capture in the ui, either press key `F6` or click `Capture` at the top left corner and select `GFXR Capture` from the dropdown menu.

To begin a GFXR capture with the cli, first ensure you know the correct architecture for the device you are attempting to capture on. This is required when intiating a GFXR capture.

Examples:
 - Install the dependencies on device, start the package, and initiate a GFXR capture.
 ```
 ./dive_client_cli --device 9A221FFAZ004TL --command gfxr_capture --package com.google.bigwheels.project_cube_xr.debug --type vulkan --device_architecture arm64-v8a --gfxr_capture_file_dir gfxr_bigwheels_capture --download_dir "/path/to/save/captures"
 ```

Then you can follow the hint output to trigger a capture by pressing key `g` and `enter`, stopping it with the same key combination, or exiting by pressing key `enter`.

The capture file directory will be saved at the path specified with the `--download_dir` option or the current directory if this option not specified.

### GFXR Replay

First, push the GFXR capture to the device or find the path where it is located on the device.

If multiple Android Devices are connected, set the enviroment variable `ANDROID_SERIAL` to the device serial in preparation for the GFXR replay script.

Using the `gfxr-replay` command will install the `gfxr-replay.apk` found in the `install` dir, and then replay the specified capture.

Example:
```
./dive_client_cli --device 9A221FFAZ004TL --command gfxr_replay --gfxr_replay_file_path /storage/emulated/0/Download/gfxrFileName.gfxr
```

For a capture that is a single frame, it can be replayed in a loop.

Example:
```
./dive_client_cli --device 9A221FFAZ004TL  --command gfxr_replay --gfxr_replay_file_path /storage/emulated/0/Download/gfxrFileName.gfxr --gfxr_replay_flags "--loop-single-frame-count 300"
```

### Cleanup

The command line tool will clean up the device and application automatically at exit. If somehow it crashed and left the device in a uncleaned state, you can run following command to clean it up

```
./dive_client_cli --command cleanup --package de.saschawillems.vulkanBloom --device 9A221FFAZ004TL
```
This will remove all the libraries installed and the settings that had been setup by Dive for the package.
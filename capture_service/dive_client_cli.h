/*
Copyright 2025 Google Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once

#include "absl/flags/flag.h"
#include "absl/flags/internal/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "android_application.h"
#include "command_utils.h"
#include "constants.h"
#include "device_mgr.h"
#include "network/tcp_client.h"
#include "absl/strings/str_cat.h"

#include <filesystem>
#include <future>
#include <iostream>
#include <ostream>
#include <string>
#include <system_error>
#include <thread>

struct GlobalOptions
{
    std::string serial;
    std::string package;
    std::string vulkan_command;
    std::string vulkan_command_args;
    std::string app_type;
    std::string device_architecture;
    std::string download_dir;
    std::string gfxr_capture_file_dir;
    int         trigger_capture_after;

    Dive::GfxrReplaySettings replay_settings;
};

struct CommandContext
{
    Dive::DeviceManager& mgr;
    const GlobalOptions& options;
};

enum class Command
{
    kListDevice,
    kListPackage,
    kRunPackage,
    kRunAndCapture,
    kGfxrCapture,
    kGfxrReplay,
    kCleanup,
    kNone,
};

struct CommandMetadata
{
    std::string                                        name;
    std::string                                        description;
    std::function<absl::Status(const GlobalOptions&)>  validator;
    std::function<absl::Status(const CommandContext&)> executor;
};

// Helper to validate options common to run/capture commands.
absl::Status ValidateRunOptions(const GlobalOptions& options);

// Helper to validate options for GFXR replay.
absl::Status ValidateGfxrReplayOptions(const GlobalOptions& options);

// Returns the singleton map of available commands and their metadata.
const std::map<Command, CommandMetadata>& GetCommandRegistry();

// Generates a usage string for the available commands.
std::string GenerateUsageString();

// Waits for user input before exiting.
absl::Status WaitForExitConfirmation();

// Abseil Flag Parsing Overloads.

// Overload for parsing the Command enum from command line flags.
bool AbslParseFlag(absl::string_view text, Command* command, std::string* error);

// Overload for converting the Command enum back to a string.
std::string AbslUnparseFlag(Command command);

// Abseil flags parsing uses ADL. GfxrReplayOptions is in the Dive namespace so AbslParseFlag
// and AbslUnparseFlag need to be as well.
namespace Dive
{
// Overload for parsing GfxrReplayOptions.
bool AbslParseFlag(absl::string_view text, GfxrReplayOptions* run_type, std::string* error);

// Overload for converting GfxrReplayOptions back to a string.
std::string AbslUnparseFlag(GfxrReplayOptions run_type);
}  // namespace Dive

// Selects and sets up the target device based on the serial flag.
absl::StatusOr<Dive::AndroidDevice*> GetTargetDevice(Dive::DeviceManager& mgr,
                                                     const std::string&   serial_flag);

// Internal helper to run a package on the device.
absl::Status InternalRunPackage(const CommandContext& ctx, bool enable_gfxr);

// Triggers a capture on the device and downloads the resulting file.
absl::Status TriggerCapture(Dive::DeviceManager& mgr);

// Checks if the capture directory on the device is currently in use.
absl::Status IsCaptureDirectoryBusy(Dive::DeviceManager& mgr,
                                    const std::string&   gfxr_capture_directory);

// Renames the screenshot file locally to match the GFXR capture file name.
absl::Status RenameScreenshotFile(const std::filesystem::path& full_target_download_dir,
                                  const std::filesystem::path& gfxr_capture_file_name);

// Retrieves the GFXR capture file name from a list of files in a directory.
absl::StatusOr<std::filesystem::path> GetGfxrCaptureFileName(
const std::filesystem::path&    full_target_download_dir,
const std::vector<std::string>& file_list);

// Retrieves a GFXR capture from the device and downloads it.
absl::Status RetrieveGfxrCapture(Dive::DeviceManager& mgr,
                                 const std::string&   gfxr_capture_directory);

// Triggers a GFXR capture on the device, allowing for multiple captures and screenshot.
absl::Status TriggerGfxrCapture(Dive::DeviceManager& mgr,
                                const std::string&   gfxr_capture_directory);

// Command Executors.
absl::Status CmdListDevice(const CommandContext& ctx);
absl::Status CmdListPackage(const CommandContext& ctx);
absl::Status CmdRunPackage(const CommandContext& ctx);
absl::Status CmdRunAndCapture(const CommandContext& ctx);
absl::Status CmdGfxrCapture(const CommandContext& ctx);
absl::Status CmdGfxrReplay(const CommandContext& ctx);
absl::Status CmdCleanup(const CommandContext& ctx);
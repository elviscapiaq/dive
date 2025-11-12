/*
Copyright 2023 Google Inc.

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

#include "dive_client_cli.h"

using namespace std::chrono_literals;

absl::Status ValidateRunOptions(const GlobalOptions& options)
{
    if (options.package.empty() && options.vulkan_command.empty())
    {
        return absl::InvalidArgumentError("Missing required flag: --package or --vulkan_command");
    }

    static const std::set<std::string> valid_types = { "openxr", "vulkan", "vulkan_cli" };
    if (valid_types.find(options.app_type) == valid_types.end())
    {
        return absl::InvalidArgumentError(
        absl::StrCat("Invalid --type '",
                     options.app_type,
                     "'. Valid values: openxr, vulkan, vulkan_cli"));
    }

    if (!options.device_architecture.empty())
    {
        static const std::set<std::string> valid_archs = {
            "arm64-v8a", "arm64-v8", "armeabi-v7a", "x86", "x86_64"
        };

        if (valid_archs.find(options.device_architecture) == valid_archs.end())
        {
            return absl::InvalidArgumentError(
            absl::StrCat("Invalid --device_architecture '",
                         options.device_architecture,
                         "'. Valid values: arm64-v8a, arm64-v8, armeabi-v7a, x86, x86_64"));
        }
    }

    return absl::OkStatus();
}

absl::Status ValidateGfxrReplayOptions(const GlobalOptions& options)
{
    if (options.replay_settings.remote_capture_path.empty())
    {
        return absl::InvalidArgumentError("Missing required flag: --gfxr_replay_file_path");
    }
    if (!absl::EndsWith(options.replay_settings.remote_capture_path, ".gfxr"))
    {
        return absl::InvalidArgumentError(absl::StrCat("Invalid --gfxr_replay_file_path '",
                                                       options.replay_settings.remote_capture_path,
                                                       "'. File must have a .gfxr extension."));
    }
    return absl::OkStatus();
}

const std::map<Command, CommandMetadata>& GetCommandRegistry()
{
    static const auto* registry = new std::map<Command, CommandMetadata>{
        { Command::kListDevice,
          { "list_device",
            "List connected Android devices.",
            [](const GlobalOptions&) { return absl::OkStatus(); },
            CmdListDevice } },
        { Command::kListPackage,
          { "list_package",
            "List installable packages on the selected device.",
            [](const GlobalOptions&) { return absl::OkStatus(); },
            CmdListPackage } },
        { Command::kRunPackage,
          { "run",
            "Run an app for manual testing or external capture.",
            ValidateRunOptions,
            CmdRunPackage } },
        { Command::kRunAndCapture,
          { "capture",
            "Run an app and trigger a capture after a delay.",
            ValidateRunOptions,
            CmdRunAndCapture } },
        { Command::kGfxrCapture,
          { "gfxr_capture",
            "Run an app and enable GFXR capture via key-press.",
            ValidateRunOptions,
            CmdGfxrCapture } },
        { Command::kGfxrReplay,
          { "gfxr_replay",
            "Deploy and run a GFXR replay.",
            ValidateGfxrReplayOptions,
            CmdGfxrReplay } },
        { Command::kCleanup,
          { "cleanup",
            "Clean up app-specific settings on the device.",
            [](const GlobalOptions& o) {
                return o.package.empty() ? absl::InvalidArgumentError("Missing --package") :
                                           absl::OkStatus();
            },
            CmdCleanup } },
    };
    return *registry;
}

std::string GenerateUsageString()
{
    std::string usage = "Available values for flag 'command':\n";
    for (const auto& [cmd, meta] : GetCommandRegistry())
    {
        usage.append(absl::StrFormat("\t%-15s : %s\n", meta.name, meta.description));
    }
    return usage;
}

absl::Status WaitForExitConfirmation()
{
    std::cout << "Press any key+enter to exit" << std::endl;
    std::string input;
    if (std::getline(std::cin, input))
    {
        std::cout << "Exiting..." << std::endl;
    }
    return absl::OkStatus();
}

bool AbslParseFlag(absl::string_view text, Command* command, std::string* error)
{
    if (text.empty())
    {
        *command = Command::kNone;
        return true;
    }
    for (const auto& [cmd_enum, meta] : GetCommandRegistry())
    {
        if (text == meta.name)
        {
            *command = cmd_enum;
            return true;
        }
    }
    *error = absl::StrCat("\n" + GenerateUsageString());
    return false;
}

std::string AbslUnparseFlag(Command command)
{
    if (command == Command::kNone)
    {
        return "";
    }
    auto& reg = GetCommandRegistry();
    auto  it = reg.find(command);
    return (it != reg.end()) ? it->second.name : "unknown";
}

namespace Dive
{

bool AbslParseFlag(absl::string_view text, GfxrReplayOptions* run_type, std::string* error)
{
    if (text == "normal")
    {
        *run_type = GfxrReplayOptions::kNormal;
        return true;
    }
    if (text == "pm4_dump")
    {
        *run_type = GfxrReplayOptions::kPm4Dump;
        return true;
    }
    if (text == "perf_counters")
    {
        *run_type = GfxrReplayOptions::kPerfCounters;
        return true;
    }
    if (text == "gpu_timing")
    {
        *run_type = GfxrReplayOptions::kGpuTiming;
        return true;
    }
    if (text == "renderdoc")
    {
        *run_type = GfxrReplayOptions::kRenderDoc;
        return true;
    }
    *error = "unknown value for enumeration";
    return false;
}

std::string AbslUnparseFlag(GfxrReplayOptions run_type)
{
    switch (run_type)
    {
    case GfxrReplayOptions::kNormal:
        return "normal";
    case GfxrReplayOptions::kPm4Dump:
        return "pm4_dump";
    case GfxrReplayOptions::kPerfCounters:
        return "perf_counters";
    case GfxrReplayOptions::kGpuTiming:
        return "gpu_timing";
    case GfxrReplayOptions::kRenderDoc:
        return "renderdoc";

    default:
        return absl::StrCat(run_type);
    }
}

}  // namespace Dive

ABSL_FLAG(Command,
          command,
          Command::kNone,
          "list of actions: \n\tlist_device \n\tgfxr_capture \n\tgfxr_replay "
          "\n\tlist_package \n\trun \n\tcapture \n\tcleanup");
ABSL_FLAG(
std::string,
device,
"",
"Device serial. If not specified and only one device is plugged in then that device is used.");
ABSL_FLAG(std::string, package, "", "Package on the device");
ABSL_FLAG(std::string, vulkan_command, "", "the command for vulkan cli application to run");
ABSL_FLAG(std::string, vulkan_command_args, "", "the arguments for vulkan cli application to run");
ABSL_FLAG(std::string,
          type,
          "openxr",
          "application type: \n\t`openxr` for OpenXR applications(apk) \n\t `vulkan` for Vulkan "
          "applications(apk)"
          "\n\t`vulkan_cli` for command line Vulkan application.");
ABSL_FLAG(
std::string,
download_dir,
".",
"specify the directory path on the host to download the capture, default to current directory.");

ABSL_FLAG(std::string,
          device_architecture,
          "",
          "specify the device architecture to capture with gfxr (arm64-v8, armeabi-v7a, x86, or "
          "x86_64). If not specified, the default is the architecture of --device.");
ABSL_FLAG(std::string,
          gfxr_capture_file_dir,
          "gfxr_capture",
          "specify the name of the directory for the gfxr capture. If not specified, the default "
          "file name is gfxr_capture.");

ABSL_FLAG(
int,
trigger_capture_after,
5,
"specify how long in seconds the capture be triggered after the application starts when running "
"with the `capture` command. If not specified, it will be triggered after 5 seconds.");
ABSL_FLAG(std::string,
          gfxr_replay_file_path,
          "",
          "specify the on-device path of the gfxr capture to replay.");
ABSL_FLAG(std::string, gfxr_replay_flags, "", "specify flags to pass to gfxr replay.");

ABSL_FLAG(std::vector<std::string>,
          metrics,
          {},
          "comma-separated list of metrics to profile for gfxr_replay command with "
          "`--gfxr_replay_run_type perf_counters`.");
ABSL_FLAG(Dive::GfxrReplayOptions,
          gfxr_replay_run_type,
          Dive::GfxrReplayOptions::kNormal,
          "Kind of analysis to perform during replay. Possible values:\n\tnormal: No "
          "analysis\n\tpm4_dump: Capture all PM4 packets"
          "\n\tperf_counters: Collect metrics\n\tgpu_timing: Collect GPU timing\n\trenderdoc: "
          "Create a RenderDoc capture");
ABSL_FLAG(bool, validation_layer, false, "Run GFXR replay with the Vulkan Validation Layer");

absl::StatusOr<Dive::AndroidDevice*> GetTargetDevice(Dive::DeviceManager& mgr,
                                                     const std::string&   serial_flag)
{
    auto list = mgr.ListDevice();
    if (list.empty())
    {
        return absl::UnavailableError("No Android devices connected.");
    }

    std::string target_serial = serial_flag;
    if (target_serial.empty())
    {
        if (list.size() == 1)
        {
            target_serial = list.front().m_serial;
            std::cout << "Using single connected device: " << target_serial << std::endl;
        }
        else
        {
            std::string
            msg = "Multiple devices connected. Specify --device [serial].\nAvailables:\n";
            for (const auto& d : list)
            {
                msg.append("\t" + d.GetDisplayName() + "\n");
            }
            return absl::InvalidArgumentError(msg);
        }
    }
    else
    {
        bool        found = false;
        std::string msg;
        for (const auto& d : list)
        {
            if (d.m_serial == target_serial)
            {
                found = true;
                break;
            }
            msg.append("\t" + d.GetDisplayName() + "\n");
        }
        if (!found)
        {
            return absl::InvalidArgumentError("Device with serial '" + target_serial +
                                              "' not found.\n" + "Available devices:\n" + msg);
        }
    }

    auto device = mgr.SelectDevice(target_serial);
    if (!device.ok())
    {
        return device.status();
    }

    auto ret = (*device)->SetupDevice();
    if (!ret.ok())
    {
        return absl::InternalError("Failed to setup device: " + std::string(ret.message()));
    }
    return *device;
}

absl::Status InternalRunPackage(const CommandContext& ctx, bool enable_gfxr)
{
    auto* device = ctx.mgr.GetDevice();
    if (device == nullptr)
    {
        return absl::FailedPreconditionError(
        "No device selected. Did you provide --device serial?");
    }
    device->EnableGfxr(enable_gfxr);

    absl::Status ret;
    if (ctx.options.app_type == "openxr")
    {
        ret = device->SetupApp(ctx.options.package,
                               Dive::ApplicationType::OPENXR_APK,
                               ctx.options.vulkan_command_args,
                               ctx.options.device_architecture,
                               ctx.options.gfxr_capture_file_dir);
    }
    else if (ctx.options.app_type == "vulkan")
    {
        ret = device->SetupApp(ctx.options.package,
                               Dive::ApplicationType::VULKAN_APK,
                               ctx.options.vulkan_command_args,
                               ctx.options.device_architecture,
                               ctx.options.gfxr_capture_file_dir);
    }
    else if (ctx.options.app_type == "vulkan_cli")
    {
        ret = device->SetupApp(ctx.options.vulkan_command,
                               ctx.options.vulkan_command_args,
                               Dive::ApplicationType::VULKAN_CLI,
                               ctx.options.device_architecture,
                               ctx.options.gfxr_capture_file_dir);
    }
    else
    {
        return absl::InvalidArgumentError("Unknown app type: " + ctx.options.app_type);
    }

    if (!ret.ok())
    {
        return absl::InternalError("Setup failed: " + std::string(ret.message()));
    }

    ret = device->StartApp();
    if (!ret.ok())
    {
        return absl::InternalError("Start app failed: " + std::string(ret.message()));
    }
    return absl::OkStatus();
}

absl::Status TriggerCapture(Dive::DeviceManager& mgr)
{
    if (mgr.GetDevice() == nullptr)
    {
        return absl::FailedPreconditionError("No device selected, can't capture.");
    }

    std::string        download_dir = absl::GetFlag(FLAGS_download_dir);
    Network::TcpClient client;
    const std::string  host = "127.0.0.1";
    int                port = mgr.GetDevice()->Port();

    absl::Status status = client.Connect(host, port);
    if (!status.ok())
    {
        return absl::UnavailableError("Connection failed: " + std::string(status.message()));
    }

    absl::StatusOr<std::string> capture_file_path = client.StartPm4Capture();
    if (!capture_file_path.ok())
    {
        return capture_file_path.status();
    }

    std::filesystem::path target_download_dir(download_dir);
    if (!std::filesystem::is_directory(target_download_dir))
    {
        return absl::InvalidArgumentError("Invalid download directory: " +
                                          target_download_dir.string());
    }

    std::filesystem::path p(*capture_file_path);
    std::string           download_file_path = (target_download_dir / p.filename()).string();

    status = client.DownloadFileFromServer(*capture_file_path, download_file_path);
    if (!status.ok())
    {
        return status;
    }

    std::cout << "Capture saved at " << download_file_path << std::endl;
    return absl::OkStatus();
}

absl::Status CheckCaptureFinished(Dive::DeviceManager& mgr,
                                  const std::string&   gfxr_capture_directory)
{
    std::string                 on_device_capture_directory = absl::StrCat(Dive::kDeviceCapturePath,
                                                           "/",
                                                           gfxr_capture_directory);
    std::string                 command = "shell lsof " + on_device_capture_directory;
    absl::StatusOr<std::string> output = mgr.GetDevice()->Adb().RunAndGetResult(command);

    if (!output.ok())
    {
        std::cout << "Error checking directory: " << output.status().message() << std::endl;
    }

    std::stringstream ss(output->c_str());
    std::string       line;
    int               line_count = 0;
    while (std::getline(ss, line))
    {
        line_count++;
    }

    return line_count <= 1 ? absl::OkStatus() :
                             absl::InternalError("Capture file operation in progress.");
}

absl::Status RenameScreenshotFile(const std::filesystem::path& full_target_download_dir,
                                  const std::filesystem::path& gfxr_capture_file_name)
{
    const std::filesystem::path old_screenshot_file_path = full_target_download_dir /
                                                           Dive::kCaptureScreenshotFile;

    // Ensure the file to rename actually exists.
    if (!std::filesystem::exists(old_screenshot_file_path))
    {
        return absl::NotFoundError(absl::StrCat("Could not find the expected screenshot file: ",
                                                old_screenshot_file_path.string()));
    }

    // Derive the base name from the GFXR file.
    std::string base_name = gfxr_capture_file_name.stem().string();

    // Define the new, final path of the screenshot.
    const std::filesystem::path new_screenshot_file_path = full_target_download_dir /
                                                           absl::StrCat(base_name, ".png");

    std::cout << "Renaming screenshot from " << old_screenshot_file_path.string() << " to "
              << new_screenshot_file_path.string() << std::endl;

    try
    {
        // Avoid renaming if the names are accidentally the same
        if (old_screenshot_file_path != new_screenshot_file_path)
        {
            std::filesystem::rename(old_screenshot_file_path, new_screenshot_file_path);
        }
    }
    catch (const std::exception& e)
    {
        return absl::InternalError("Failed to rename screenshot file locally: " +
                                   std::string(e.what()));
    }

    return absl::OkStatus();
}

absl::StatusOr<std::filesystem::path> GetGfxrCaptureFileName(
const std::filesystem::path&    full_target_download_dir,
const std::vector<std::string>& file_list)
{
    for (const std::string& filename : file_list)
    {
        std::string trimmed_filename(absl::StripAsciiWhitespace(filename));
        if (absl::EndsWith(trimmed_filename, ".gfxr"))
        {
            return full_target_download_dir / trimmed_filename;
        }
    }
    return absl::NotFoundError("No file with '.gfxr' extension found in the list.");
}

absl::Status RetrieveGfxrCapture(Dive::DeviceManager& mgr,
                                 const std::string&   gfxr_capture_directory)
{
    std::filesystem::path download_dir = absl::GetFlag(FLAGS_download_dir);

    // Need to explicitly use forward slash so that this works on Windows targetting Android
    std::string on_device_capture_directory = absl::StrCat(Dive::kDeviceCapturePath,
                                                           "/",
                                                           gfxr_capture_directory);

    std::cout << "Retrieving capture..." << std::endl;

    // Retrieve the list of files in the capture directory on the device.
    std::string command = absl::StrFormat("shell ls %s", on_device_capture_directory);
    absl::StatusOr<std::string> output = mgr.GetDevice()->Adb().RunAndGetResult(command);
    if (!output.ok())
    {
        return absl::InternalError("Error getting capture_file name: " +
                                   std::string(output.status().message()));
    }

    std::vector<std::string> file_list = absl::StrSplit(std::string(output->data()),
                                                        '\n',
                                                        absl::SkipEmpty());

    if (file_list.empty())
    {
        return absl::NotFoundError("Error, captures not present on device at: " +
                                   on_device_capture_directory);
    }

    // Find name for new local target directory
    std::filesystem::path full_target_download_dir = download_dir / gfxr_capture_directory;
    bool local_target_dir_exists = std::filesystem::exists(full_target_download_dir);
    int  suffix = 0;
    while (local_target_dir_exists)
    {
        // Append numerical suffix to make a fresh dir
        full_target_download_dir = download_dir / absl::StrFormat("%s_%s",
                                                                  gfxr_capture_directory,
                                                                  std::to_string(suffix));
        suffix++;
        local_target_dir_exists = std::filesystem::exists(full_target_download_dir);
    }

    command = absl::StrFormat(R"(pull "%s" "%s")",
                              on_device_capture_directory,
                              full_target_download_dir.string());
    output = mgr.GetDevice()->Adb().RunAndGetResult(command);
    if (!output.ok())
    {
        return absl::InternalError("Error pulling files: " +
                                   std::string(output.status().message()));
    }

    auto gfxr_capture_file = GetGfxrCaptureFileName(full_target_download_dir, file_list);

    if (!gfxr_capture_file.ok())
    {
        return gfxr_capture_file.status();
    }

    if (absl::Status ret = RenameScreenshotFile(full_target_download_dir, *gfxr_capture_file);
        !ret.ok())
    {
        std::cout << "Warning: Error renaming screenshot: " << ret.message() << std::endl;
    }

    std::cout << "Capture sucessfully saved at " << full_target_download_dir << std::endl;
    return absl::OkStatus();
}

absl::Status TriggerGfxrCapture(Dive::DeviceManager& mgr, const std::string& gfxr_capture_directory)
{
    std::cout
    << "Press key g+enter to trigger a capture and g+enter again to retrieve the capture. Press "
       "any other key+enter to stop the application. Note that this may impact your "
       "capture file if the capture has not been completed. \n";
    std::string
    capture_complete_message = "Capture complete. Press key g+enter to trigger another capture or "
                               "any other key+enter to stop the application.";

    std::string  input;
    bool         is_capturing = false;
    absl::Status ret;
    while (std::getline(std::cin, input))
    {
        if (input == "g")
        {
            if (is_capturing)
            {
                while (!CheckCaptureFinished(mgr, gfxr_capture_directory).ok())
                {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    std::cout << "GFXR capture in progress, please wait for current capture to "
                                 "complete before starting another."
                              << std::endl;
                }

                ret = mgr.GetDevice()->Adb().Run(
                "shell setprop debug.gfxrecon.capture_android_trigger false");
                if (!ret.ok())
                {
                    return absl::InternalError("Error stopping gfxr runtime capture: " +
                                               std::string(ret.message()));
                }

                // Retrieve the capture. If this fails, we print an error but don't exit the tool,
                // allowing the user to try again.
                absl::Status retrieve_status = RetrieveGfxrCapture(mgr, gfxr_capture_directory);
                if (!retrieve_status.ok())
                {
                    std::cout << "Failed to retrieve capture: " << retrieve_status.message()
                              << std::endl;
                }
                else
                {
                    std::cout << capture_complete_message << std::endl;
                }
                is_capturing = false;
            }
            else
            {
                ret = mgr.GetDevice()->Adb().Run(
                "shell setprop debug.gfxrecon.capture_android_trigger true");
                if (!ret.ok())
                {
                    return absl::InternalError("Error starting gfxr runtime capture: " +
                                               std::string(ret.message()));
                }

                std::filesystem::path gfxr_capture_directory_path(gfxr_capture_directory);
                ret = mgr.GetDevice()->TriggerScreenCapture(gfxr_capture_directory_path);
                if (!ret.ok())
                {
                    return absl::InternalError("Error creating capture screenshot: " +
                                               std::string(ret.message()));
                }

                is_capturing = true;
                std::cout << "Capture started. Press key g+enter to retrieve the capture."
                          << std::endl;
            }
        }
        else
        {
            if (is_capturing)
            {
                std::cout << "GFXR capture in progress, press key g+enter to retrieve the capture."
                          << std::endl;
            }
            else
            {
                std::cout << "Exiting..." << std::endl;
                break;
            }
        }
    }

    // Only delete the on device capture directory when the application is closed.
    std::string on_device_capture_directory = absl::StrCat(Dive::kDeviceCapturePath,
                                                           "/",
                                                           gfxr_capture_directory);
    ret = mgr.GetDevice()->Adb().Run(
    absl::StrFormat("shell rm -rf %s", on_device_capture_directory));

    return absl::OkStatus();
}

absl::Status CmdListDevice(const CommandContext& ctx)
{
    auto list = ctx.mgr.ListDevice();
    if (list.empty())
    {
        std::cout << "No device connected." << std::endl;
        return absl::OkStatus();
    }
    std::cout << "Devices: " << std::endl;
    for (const auto& device : list)
    {
        std::cout << "\t" << device.GetDisplayName() << std::endl;
    }
    return absl::OkStatus();
}

absl::Status CmdListPackage(const CommandContext& ctx)
{
    auto* device = ctx.mgr.GetDevice();
    auto  ret = device->ListPackage();
    if (!ret.ok())
    {
        return ret.status();
    }
    std::cout << "Packages: " << std::endl;
    for (const auto& pkg : *ret)
    {
        std::cout << "\t" << pkg << std::endl;
    }
    return absl::OkStatus();
}

absl::Status CmdRunPackage(const CommandContext& ctx)
{
    absl::Status status = InternalRunPackage(ctx, false);
    if (!status.ok())
    {
        return status;
    }
    return WaitForExitConfirmation();
}

absl::Status CmdRunAndCapture(const CommandContext& ctx)
{
    absl::Status status = InternalRunPackage(ctx, false);
    if (!status.ok())
    {
        return status;
    }

    std::cout << "Waiting " << ctx.options.trigger_capture_after << " seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(ctx.options.trigger_capture_after));

    status = TriggerCapture(ctx.mgr);
    if (!status.ok())
    {
        return status;
    }
    return WaitForExitConfirmation();
}

absl::Status CmdGfxrCapture(const CommandContext& ctx)
{
    absl::Status status = InternalRunPackage(ctx, true);
    if (!status.ok())
    {
        return status;
    }
    return TriggerGfxrCapture(ctx.mgr, ctx.options.gfxr_capture_file_dir);
}

absl::Status CmdGfxrReplay(const CommandContext& ctx)
{
    absl::Status status = ctx.mgr.DeployReplayApk(ctx.options.serial);
    if (!status.ok())
    {
        return absl::InternalError("Failed to deploy replay apk: " + std::string(status.message()));
    }

    status = ctx.mgr.RunReplayApk(ctx.options.replay_settings);
    if (!status.ok())
    {
        return absl::InternalError("Failed to run replay apk: " + std::string(status.message()));
    }
    return absl::OkStatus();
}

absl::Status CmdCleanup(const CommandContext& ctx)
{
    return ctx.mgr.CleanupPackageProperties(ctx.options.package);
}

int main(int argc, char** argv)
{
    absl::SetProgramUsageMessage("Dive Tool CLI. Use --help for details.");
    absl::ParseCommandLine(argc, argv);

    GlobalOptions opts;
    opts.serial = absl::GetFlag(FLAGS_device);
    opts.package = absl::GetFlag(FLAGS_package);
    opts.vulkan_command = absl::GetFlag(FLAGS_vulkan_command);
    opts.vulkan_command_args = absl::GetFlag(FLAGS_vulkan_command_args);
    opts.app_type = absl::GetFlag(FLAGS_type);
    opts.device_architecture = absl::GetFlag(FLAGS_device_architecture);
    opts.download_dir = absl::GetFlag(FLAGS_download_dir);
    opts.gfxr_capture_file_dir = absl::GetFlag(FLAGS_gfxr_capture_file_dir);
    opts.trigger_capture_after = absl::GetFlag(FLAGS_trigger_capture_after);

    opts.replay_settings.remote_capture_path = absl::GetFlag(FLAGS_gfxr_replay_file_path);
    opts.replay_settings.local_download_dir = absl::GetFlag(FLAGS_download_dir);
    opts.replay_settings.use_validation_layer = absl::GetFlag(FLAGS_validation_layer);
    opts.replay_settings.run_type = absl::GetFlag(FLAGS_gfxr_replay_run_type);
    opts.replay_settings.replay_flags_str = absl::GetFlag(FLAGS_gfxr_replay_flags);
    opts.replay_settings.metrics = absl::GetFlag(FLAGS_metrics);

    Command     cmd = absl::GetFlag(FLAGS_command);
    const auto& registry = GetCommandRegistry();
    auto        it = registry.find(cmd);
    if (cmd == Command::kNone || it == registry.end())
    {
        std::cout << "Error: No valid command specified.\n" << GenerateUsageString() << std::endl;
        return 1;
    }

    const auto& command_meta = it->second;
    auto        ret = command_meta.validator(opts);
    if (!ret.ok())
    {
        std::cout << "Validation error for command '" << command_meta.name << "': " << ret.message()
                  << std::endl;
        return 1;
    }

    Dive::DeviceManager mgr;
    if (cmd != Command::kListDevice)
    {
        auto device = GetTargetDevice(mgr, opts.serial);
        if (!device.ok())
        {
            std::cout << device.status().message() << std::endl;
            return 1;
        }
    }

    CommandContext ctx{ mgr, opts };
    ret = command_meta.executor(ctx);
    if (!ret.ok())
    {
        std::cout << "Error executing command '" << command_meta.name << "': " << ret.message()
                  << std::endl;
        return 1;
    }
    return 0;
}

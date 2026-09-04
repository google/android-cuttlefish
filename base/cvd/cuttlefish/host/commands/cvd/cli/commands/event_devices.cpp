/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/cvd/cli/commands/event_devices.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_join.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {
const char* kListCommand = "list";
const char* kLsAlias = "ls";
const char* kCaptureCommand = "capture";
const char* kInjectCommand = "inject";

struct __attribute__((packed)) Event {
  // The virtio_input_event structure actually contains 2 16 bits integers and
  // one 32 bits one, in little endian representation. However the only
  // operation on these events is going to be comparing the first two integers
  // with zero, for which endianness doesn't matter and can be done in a single
  // comparisson using 32 bits.
  uint32_t ev_type;
  uint32_t value;
};
static_assert(sizeof(Event) == 8,
              "Event structure doesn't match virtio_input_event size");

Result<void> SaveEventsToFile(Reader& conn, Writer& out) {
  std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  std::vector<Event> events;
  for (;;) {
    Event event = CF_EXPECT(ReadExactBinary<Event>(conn),
                            "Failed to read complete event");
    events.push_back(event);
    if (event.ev_type == 0) {
      uint64_t timestamp =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      CF_EXPECT(WriteExactBinary(out, timestamp),
                "Unable to write timestamp to file");
      CF_EXPECT(WriteExact(out, reinterpret_cast<const char*>(events.data()),
                           events.size() * sizeof(Event)),
                "Unable to write events to file");
      events.clear();
    }
    // The loop ends when the user interrupts the command with CTRL-C
  }
  return {};
}

Result<void> InjectEventsFromFile(Writer& conn, Reader& in) {
  std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  for (;;) {
    std::optional<uint64_t> delay =
        CF_EXPECT(ReadExactBinaryOrEof<uint64_t>(in));
    if (!delay) {
      break;
    }
    std::this_thread::sleep_until(start + std::chrono::milliseconds(*delay));
    Event event;
    do {
      event = CF_EXPECT(ReadExactBinary<Event>(in));
      CF_EXPECT(WriteExactBinary(conn, event));
    } while (event.ev_type != 0);
  }
  return {};
}

}  // namespace

CvdInputDevicesHandler::CvdInputDevicesHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

std::vector<std::string> CvdInputDevicesHandler::CmdList() const {
  return {"event_devices"};
}

std::string CvdInputDevicesHandler::SummaryHelp() const {
  return "List, inject events to or capture events from input devices";
}

std::vector<HelpParagraph> CvdInputDevicesHandler::Description() const {
  return {
      HelpParagraph("Usage"),

      HelpParagraph::Raw(
          R"(   cvd [SELECTOR_ARGS] event_devices list
   cvd [SELECTOR_ARGS] event_devices capture --device=DEVICE_NAME EVENTS_FILE)
   cvd [SELECTOR_ARGS] event_devices inject --device=DEVICE_NAME EVENTS_FILE)"),

      HelpParagraph("The `list` subcommand accepts no flags and can target "
                    "multiple instances and groups. For each selected instance "
                    "it prints the instance's group and name, followed by a "
                    "space separated list of input device names. For example:"),

      HelpParagraph::Raw(
          R"(    cvd-1/instance1: keyboard touch_0
    cvd-1/instance2: keyboard touch_0)"),

      HelpParagraph(
          "The `capture` and `inject` subcommands require  an events file and "
          "the device name. These subcommands arget a single instance. The "
          "events file is in binary  should not be edited directly. Event "
          "files should always be created via the `capture` subcommand."),

      HelpParagraph("Events capture ends when the process is interrupted, "
                    "typically by pressing CTRL+C in the terminal."),

      HelpParagraph(
          "When the special name \"-\" is given as the events file, events "
          "will be written to/read from standard output/input."),
  };
}

Result<std::vector<Flag>> CvdInputDevicesHandler::Flags(
    const CommandRequest& request) {
  return std::vector<Flag>{
      GflagsCompatFlag("device_name", flags_.device_name)
          .ValueNameHint("DEVICE")
          .Alias("d")
          .Help("Name of input device to capture events from/inject events to. "
                "Valid only with `capture` and `inject`. Available device "
                "names can be obtained with the `list` subcommand."),
  };
}

Result<void> CvdInputDevicesHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  std::string cmd = kListCommand;
  if (!args.empty()) {
    cmd = std::move(args[0]);
    args.erase(args.begin());
  }
  if (cmd == kListCommand || cmd == kLsAlias) {
    CF_EXPECT(ListDevices(request, std::move(args)));
  } else if (cmd == kCaptureCommand) {
    CF_EXPECT(CaptureEvents(request, std::move(args)));
  } else if (cmd == kInjectCommand) {
    CF_EXPECT(InjectEvents(request, std::move(args)));
  } else {
    return CF_ERRF(
        "Unknown event_devices subcommand: `{}`. Must be one of `ls`, "
        "`inject` or `capture`.",
        cmd);
  }
  return {};
}

Result<void> CvdInputDevicesHandler::ListDevices(
    const CommandRequest& request, std::vector<std::string> args) {
  CF_EXPECTF(args.empty(), "Unknown arguments to `event_devices ls`: {}",
             absl::StrJoin(args, " "));
  const std::vector<std::pair<LocalInstanceGroup, std::vector<LocalInstance>>>
      found_instances =
          CF_EXPECT(selector::SelectInstances(instance_manager_, request));

  if (found_instances.empty()) {
    LOG(INFO) << "No devices found";
    return {};
  }

  for (const auto& [group, instances] : found_instances) {
    for (const LocalInstance& instance : instances) {
      std::cout << group.GroupName() << "/" << instance.Name() << ": "
                << absl::StrJoin(CF_EXPECT(instance.InputDevices()), " ")
                << std::endl;
    }
  }

  return {};
}

Result<void> CvdInputDevicesHandler::CaptureEvents(
    const CommandRequest& request, std::vector<std::string> args) {
  std::vector<Flag> flags = CF_EXPECT(Flags(request));
  CF_EXPECT(ConsumeFlags(flags, args));
  CF_EXPECTF(args.size() < 2,
             "Too many standalone arguments provided ({}), expected "
             "`EVENTS_FILE` only",
             absl::StrJoin(args, " "));
  CF_EXPECT_EQ(args.size(), 1, "Missing events file name");
  CF_EXPECT(!flags_.device_name.empty(),
            "A device name is required to capture events");
  if (args[0] == "-") {
    args[0] = "/proc/self/fd/1";
  }
  const auto [instance, group] =
      CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                "Unable to select an instance");
  Fd conn =
      CF_EXPECT(instance.NewCaptureInputDeviceEventsConn(flags_.device_name));
  Fd out = CF_EXPECT(Fd::Creat(args[0], 0600), "Unable to create events file");
  std::cerr << "Capturing events from '" << flags_.device_name
            << "'. Press CTRL+C to stop" << std::endl;
  CF_EXPECT(SaveEventsToFile(conn, out));
  return {};
}

Result<void> CvdInputDevicesHandler::InjectEvents(
    const CommandRequest& request, std::vector<std::string> args) {
  std::vector<Flag> flags = CF_EXPECT(Flags(request));
  CF_EXPECT(ConsumeFlags(flags, args));
  CF_EXPECTF(args.size() < 2,
             "Too many standalone arguments provided ({}), expected "
             "`EVENTS_FILE` only",
             absl::StrJoin(args, " "));
  CF_EXPECT_EQ(args.size(), 1, "Missing events file name");
  CF_EXPECT(!flags_.device_name.empty(),
            "A device name is required to inject events");
  if (args[0] == "-") {
    args[0] = "/proc/self/fd/0";
  }
  const auto [instance, _] =
      CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                "Unable to select an instance");
  Fd conn =
      CF_EXPECT(instance.NewInjectInputDeviceEventsConn(flags_.device_name));
  Fd in =
      CF_EXPECT(Fd::Open(args[0], O_RDONLY), "Unable to create events file");
  std::cerr << "Injecting events to '" << flags_.device_name << "'."
            << std::endl;
  CF_EXPECT(InjectEventsFromFile(conn, in));
  return {};
}
}  // namespace cuttlefish

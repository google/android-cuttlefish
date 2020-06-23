//
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "flag_forwarder.h"

#include <cstring>

#include <sstream>
#include <map>
#include <string>
#include <vector>

#include <gflags/gflags.h>
#include <android-base/logging.h>
#include <libxml/tree.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"

/**
 * Superclass for a flag loaded from another process.
 *
 * An instance of this class defines a flag available either in this subprocess
 * or another flag. If a flag needs to be registered in the current process, see
 * the DynamicFlag subclass. If multiple subprocesses declare a flag with the
 * same name, they all should receive that flag, but the DynamicFlag should only
 * be created zero or one times. Zero times if the parent process defines it as
 * well, one time if the parent does not define it.
 *
 * Notably, gflags itself defines some flags that are present in every binary.
 */
class SubprocessFlag {
  std::string subprocess_;
  std::string name_;
public:
  SubprocessFlag(const std::string& subprocess, const std::string& name)
      : subprocess_(subprocess), name_(name) {
  }
  virtual ~SubprocessFlag() = default;
  SubprocessFlag(const SubprocessFlag&) = delete;
  SubprocessFlag& operator=(const SubprocessFlag&) = delete;
  SubprocessFlag(SubprocessFlag&&) = delete;
  SubprocessFlag& operator=(SubprocessFlag&&) = delete;

  const std::string& Subprocess() const { return subprocess_; }
  const std::string& Name() const { return name_; }
};

/*
 * A dynamic gflags flag. Creating an instance of this class is equivalent to
 * registering a flag with DEFINE_<type>. Instances of this class should not
 * be deleted while flags are still in use (likely through the end of main).
 *
 * This is implemented as a wrapper around gflags::FlagRegisterer. This class
 * serves a dual purpose of holding the memory for gflags::FlagRegisterer as
 * that normally expects memory to be held statically. The other reason is to
 * subclass class SubprocessFlag to fit into the flag-forwarding scheme.
 */
template<typename T>
class DynamicFlag : public SubprocessFlag {
  std::string help_;
  std::string filename_;
  T current_storage_;
  T defvalue_storage_;
  gflags::FlagRegisterer registerer_;
public:
  DynamicFlag(const std::string& subprocess, const std::string& name,
              const std::string& help, const std::string& filename,
              const T& current, const T& defvalue)
      : SubprocessFlag(subprocess, name), help_(help), filename_(filename),
        current_storage_(current), defvalue_storage_(defvalue),
        registerer_(Name().c_str(), help_.c_str(), filename_.c_str(),
                    &current_storage_, &defvalue_storage_) {
  }
};

namespace {

/**
 * Returns a mapping between flag name and "gflags type" as strings for flags
 * defined in the binary.
 */
std::map<std::string, std::string> CurrentFlagsToTypes() {
  std::map<std::string, std::string> name_to_type;
  std::vector<gflags::CommandLineFlagInfo> self_flags;
  gflags::GetAllFlags(&self_flags);
  for (auto& flag : self_flags) {
    name_to_type[flag.name] = flag.type;
  }
  return name_to_type;
}

/**
 * Returns a pointer to the child of `node` with name `name`.
 *
 * For example, invoking `xmlChildWithName(<foo><bar>abc</bar></foo>, "foo")`
 * will return <bar>abc</bar>.
 */
xmlNodePtr xmlChildWithName(xmlNodePtr node, const std::string& name) {
  for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
    if (child->type != XML_ELEMENT_NODE) {
      continue;
    }
    if (std::strcmp((const char*) child->name, name.c_str()) == 0) {
      return child;
    }
  }
  LOG(WARNING) << "no child with name " << name;
  return nullptr;
}

/**
 * Returns a string with the content of an xml node.
 *
 * For example, calling `xmlContent(<bar>abc</bar>)` will return "abc".
 */
std::string xmlContent(xmlNodePtr node) {
  if (node == nullptr || node->children == NULL
      || node->children->type != xmlElementType::XML_TEXT_NODE) {
    return "";
  }
  return std::string((char*) node->children->content);
}

template<typename T>
T FromString(const std::string& str) {
  std::stringstream stream(str);
  T output;
  stream >> output;
  return output;
}

/**
 * Creates a dynamic flag
 */
std::unique_ptr<SubprocessFlag> MakeDynamicFlag(
    const std::string& subprocess,
    const gflags::CommandLineFlagInfo& flag_info) {
  std::unique_ptr<SubprocessFlag> ptr;
  if (flag_info.type == "bool") {
    ptr.reset(new DynamicFlag<bool>(subprocess, flag_info.name,
                                    flag_info.description,
                                    flag_info.filename,
                                    FromString<bool>(flag_info.default_value),
                                    FromString<bool>(flag_info.current_value)));
  } else if (flag_info.type == "int32") {
    ptr.reset(new DynamicFlag<int32_t>(subprocess, flag_info.name,
                                       flag_info.description,
                                       flag_info.filename,
                                       FromString<int32_t>(flag_info.default_value),
                                       FromString<int32_t>(flag_info.current_value)));
  } else if (flag_info.type == "uint32") {
    ptr.reset(new DynamicFlag<uint32_t>(subprocess, flag_info.name,
                                        flag_info.description,
                                        flag_info.filename,
                                        FromString<uint32_t>(flag_info.default_value),
                                        FromString<uint32_t>(flag_info.current_value)));
  } else if (flag_info.type == "int64") {
    ptr.reset(new DynamicFlag<int64_t>(subprocess, flag_info.name,
                                       flag_info.description,
                                       flag_info.filename,
                                       FromString<int64_t>(flag_info.default_value),
                                       FromString<int64_t>(flag_info.current_value)));
  } else if (flag_info.type == "uint64") {
    ptr.reset(new DynamicFlag<uint64_t>(subprocess, flag_info.name,
                                        flag_info.description,
                                        flag_info.filename,
                                        FromString<uint64_t>(flag_info.default_value),
                                        FromString<uint64_t>(flag_info.current_value)));
  } else if (flag_info.type == "double") {
    ptr.reset(new DynamicFlag<double>(subprocess, flag_info.name,
                                      flag_info.description,
                                      flag_info.filename,
                                      FromString<double>(flag_info.default_value),
                                      FromString<double>(flag_info.current_value)));
  } else if (flag_info.type == "string") {
    ptr.reset(new DynamicFlag<std::string>(subprocess, flag_info.name,
                                           flag_info.description,
                                           flag_info.filename,
                                           flag_info.default_value,
                                           flag_info.current_value));
  } else {
    LOG(FATAL) << "Unknown type \"" << flag_info.type << "\" for flag " << flag_info.name;
  }
  return ptr;
}

std::vector<gflags::CommandLineFlagInfo> FlagsForSubprocess(std::string helpxml_output) {
  // Hack to try to filter out log messages that come before the xml
  helpxml_output = helpxml_output.substr(helpxml_output.find("<?xml"));

  xmlDocPtr doc = xmlReadMemory(helpxml_output.c_str(), helpxml_output.size(),
                                NULL, NULL, 0);
  if (doc == NULL) {
    LOG(FATAL) << "Could not parse xml of subprocess `--helpxml`";
  }
  xmlNodePtr root_element = xmlDocGetRootElement(doc);
  std::vector<gflags::CommandLineFlagInfo> flags;
  for (xmlNodePtr flag = root_element->children; flag != nullptr; flag = flag->next) {
    if (std::strcmp((const char*) flag->name, "flag") != 0) {
      continue;
    }
    gflags::CommandLineFlagInfo flag_info;
    flag_info.name = xmlContent(xmlChildWithName(flag, "name"));
    flag_info.type = xmlContent(xmlChildWithName(flag, "type"));
    flag_info.filename = xmlContent(xmlChildWithName(flag, "file"));
    flag_info.description = xmlContent(xmlChildWithName(flag, "meaning"));
    flag_info.current_value = xmlContent(xmlChildWithName(flag, "current"));
    flag_info.default_value = xmlContent(xmlChildWithName(flag, "default"));
    flags.emplace_back(std::move(flag_info));
  }
  xmlFree(doc);
  xmlCleanupParser();
  return flags;
}

} // namespace

FlagForwarder::FlagForwarder(std::set<std::string> subprocesses)
    : subprocesses_(std::move(subprocesses)) {
  std::map<std::string, std::string> flag_to_type = CurrentFlagsToTypes();

  for (const auto& subprocess : subprocesses_) {
    cuttlefish::Command cmd(subprocess);
    cmd.AddParameter("--helpxml");
    std::string helpxml_input, helpxml_output, helpxml_error;
    cuttlefish::SubprocessOptions options;
    options.Verbose(false);
    int helpxml_ret = cuttlefish::RunWithManagedStdio(std::move(cmd), &helpxml_input,
                                               &helpxml_output, &helpxml_error,
                                               options);
    if (helpxml_ret != 1) {
      LOG(FATAL) << subprocess << " --helpxml returned unexpected response "
                 << helpxml_ret << ". Stderr was " << helpxml_error;
      return;
    }

    auto subprocess_flags = FlagsForSubprocess(helpxml_output);
    for (const auto& flag : subprocess_flags) {
      if (flag_to_type.count(flag.name)) {
        if (flag_to_type[flag.name] == flag.type) {
          flags_.emplace(std::make_unique<SubprocessFlag>(subprocess, flag.name));
        } else {
          LOG(FATAL) << flag.name << "defined as " << flag_to_type[flag.name]
                     << " and " << flag.type;
          return;
        }
      } else {
        flag_to_type[flag.name] = flag.type;
        flags_.emplace(MakeDynamicFlag(subprocess, flag));
      }
    }
  }
}

// Destructor must be defined in an implementation file.
// https://stackoverflow.com/questions/6012157
FlagForwarder::~FlagForwarder() = default;

void FlagForwarder::UpdateFlagDefaults() const {

  for (const auto& subprocess : subprocesses_) {
    cuttlefish::Command cmd(subprocess);
    std::vector<std::string> invocation = {subprocess};
    for (const auto& flag : ArgvForSubprocess(subprocess)) {
      cmd.AddParameter(flag);
    }
    // Disable flags that could cause the subprocess to exit before helpxml.
    // See gflags_reporting.cc.
    cmd.AddParameter("--nohelp");
    cmd.AddParameter("--nohelpfull");
    cmd.AddParameter("--nohelpshort");
    cmd.AddParameter("--helpon=");
    cmd.AddParameter("--helpmatch=");
    cmd.AddParameter("--nohelppackage=");
    cmd.AddParameter("--noversion");
    // Ensure this is set on by putting it at the end.
    cmd.AddParameter("--helpxml");
    std::string helpxml_input, helpxml_output, helpxml_error;
    cuttlefish::SubprocessOptions options;
    options.Verbose(false);
    int helpxml_ret = cuttlefish::RunWithManagedStdio(std::move(cmd), &helpxml_input,
                                               &helpxml_output, &helpxml_error,
                                               options);
    if (helpxml_ret != 1) {
      LOG(FATAL) << subprocess << " --helpxml returned unexpected response "
                 << helpxml_ret << ". Stderr was " << helpxml_error;
      return;
    }

    auto subprocess_flags = FlagsForSubprocess(helpxml_output);
    for (const auto& flag : subprocess_flags) {
      gflags::SetCommandLineOptionWithMode(
          flag.name.c_str(),
          flag.default_value.c_str(),
          gflags::FlagSettingMode::SET_FLAGS_DEFAULT);
    }
  }
}

std::vector<std::string> FlagForwarder::ArgvForSubprocess(
    const std::string& subprocess) const {
  std::vector<std::string> subprocess_argv;
  for (const auto& flag : flags_) {
    if (flag->Subprocess() == subprocess) {
      gflags::CommandLineFlagInfo flag_info =
          gflags::GetCommandLineFlagInfoOrDie(flag->Name().c_str());
      if (!flag_info.is_default) {
        subprocess_argv.push_back("--" + flag->Name() + "=" + flag_info.current_value);
      }
    }
  }
  return subprocess_argv;
}


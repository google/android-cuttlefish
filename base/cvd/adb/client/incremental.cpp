/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "incremental.h"

#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <android-base/errors.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/scopeguard.h>
#include <openssl/base64.h>

#include "adb_install.h"
#include "adb_unique_fd.h"
#include "commandline.h"
#include "incremental_utils.h"
#include "sysdeps.h"

using namespace std::literals;

namespace incremental {

// Used to be sent as arguments via install-incremental, to describe the IncrementalServer database.
class ISDatabaseEntry {
  public:
    ISDatabaseEntry(std::string filename, size_t size, int file_id)
        : filename_(std::move(filename)), size_(size), file_id_(file_id) {}

    virtual ~ISDatabaseEntry() = default;

    virtual bool is_v4_signed() const = 0;
    int file_id() const { return file_id_; }

    // Convert the database entry to a string that can be sent to `pm` as a command-line parameter.
    virtual std::string serialize() const = 0;

  protected:
    std::string filename_;
    size_t size_;
    int file_id_;
};

// A database entry for an signed file.
class ISSignedDatabaseEntry : public ISDatabaseEntry {
  public:
    ISSignedDatabaseEntry(std::string filename, size_t size, int file_id, std::string signature,
                          std::string path)
        : ISDatabaseEntry(std::move(filename), size, file_id),
          signature_(std::move(signature)),
          path_(std::move(path)) {}

    bool is_v4_signed() const override { return true; };

    std::string serialize() const override {
        return std::format("{}:{}:{}:{}:{}", filename_, size_, file_id_, signature_,
                           kProtocolVersion);
    }

    std::string path() const { return path_; }

  private:
    static constexpr int kProtocolVersion = 1;

    std::string signature_;
    std::string path_;
};

// A database entry for an unsigned file.
class ISUnsignedDatabaseEntry : public ISDatabaseEntry {
  public:
    ISUnsignedDatabaseEntry(std::string filename, int64_t size, int file_id, unique_fd fd)
        : ISDatabaseEntry(std::move(filename), size, file_id), fd_(std::move(fd)) {}

    bool is_v4_signed() const override { return false; };

    std::string serialize() const override {
        return std::format("{}:{}:{}", filename_, size_, file_id_);
    }

    borrowed_fd fd() const { return fd_; }

  private:
    unique_fd fd_;
};

static bool requires_v4_signature(const std::string& file) {
    // Signature has to be present for APKs.
    return android::base::EndsWithIgnoreCase(file, ".apk") ||
           android::base::EndsWithIgnoreCase(file, kSdmExtension);
}

// Read and return the signature bytes and the tree size.
static std::optional<std::pair<std::vector<char>, int32_t>> read_signature(
        const std::string& signature_file, std::string* error) {
    unique_fd fd(adb_open(signature_file.c_str(), O_RDONLY));
    if (fd < 0) {
        if (errno == ENOENT) {
            return std::make_pair(std::vector<char>{}, 0);
        }
        *error = std::format("Failed to open signature file '{}': {}", signature_file,
                             strerror(errno));
        return {};
    }

    return read_id_sig_headers(fd, error);
}

static bool validate_signature(const std::vector<char>& signature, int32_t tree_size,
                               size_t file_size, std::string* error) {
    if (signature.size() > kMaxSignatureSize) {
        *error = std::format("Signature is too long: {}. Max allowed is {}", signature.size(),
                             kMaxSignatureSize);
        return false;
    }

    if (Size expected = verity_tree_size_for_file(file_size); tree_size != expected) {
        *error =
                std::format("Verity tree size mismatch [was {}, expected {}]", tree_size, expected);
        return false;
    }

    return true;
}

// Base64-encode signature bytes.
static std::optional<std::string> encode_signature(const std::vector<char>& signature,
                                                   std::string* error) {
    std::string encoded_signature;

    size_t base64_len = 0;
    if (!EVP_EncodedLength(&base64_len, signature.size())) {
        *error = "Fail to estimate base64 encoded length";
        return {};
    }

    encoded_signature.resize(base64_len, '\0');
    encoded_signature.resize(EVP_EncodeBlock((uint8_t*)encoded_signature.data(),
                                             (const uint8_t*)signature.data(), signature.size()));

    return std::move(encoded_signature);
}

static std::optional<std::pair<unique_fd, size_t>> open_and_get_size(const std::string& file,
                                                                     std::string* error) {
    unique_fd fd(adb_open(file.c_str(), O_RDONLY));
    if (fd < 0) {
        *error = std::format("Failed to open input file '{}': {}", file, strerror(errno));
        return {};
    }

    struct stat st;
    if (fstat(fd.get(), &st)) {
        *error = std::format("Failed to stat input file '{}': {}", file, strerror(errno));
        return {};
    }

    return std::make_pair(std::move(fd), st.st_size);
}

// Returns a list of IncrementalServer database entries.
// - The caller is expected to send the entries as arguments via install-incremental.
// - For signed files in the list, the caller is expected to send them via streaming, with file ids
//   being the indexes in the list.
// - For unsigned files in the list, the caller is expected to send them through stdin before
//   streaming the signed ones, in the order specified by the list.
static std::optional<std::vector<std::unique_ptr<ISDatabaseEntry>>> build_database(
        const Files& files, std::string* error) {
    std::unordered_map<std::string, std::pair<std::vector<char>, int32_t>> signatures_by_file;

    for (const std::string& file : files) {
        auto signature_and_tree_size = read_signature(std::string(file).append(IDSIG), error);
        if (!signature_and_tree_size.has_value()) {
            return {};
        }
        if (requires_v4_signature(file) && signature_and_tree_size->first.empty()) {
            *error = std::format("V4 signature missing for '{}'", file);
            return {};
        }
        signatures_by_file[file] = std::move(*signature_and_tree_size);
    }

    // Constraints:
    // - Signed files are later passed to IncrementalServer, which assumes the list indexes being
    //   the file ids, and the file ids for `incremental-install` and IncrementalServer must match.
    //   Therefore, we assign the leading file ids to the signed files, so their file ids match
    //   their list indexes and the indexes are unchanged when we discard unsigned files from the
    //   list.
    // - Unsigned files are later sent through stdin, while `pm` on the other end assumes the
    //   inputs being ordered by the file ids incrementally. Therefore, we assign file ids to
    //   unsigned files in the same order as their list indexes.
    std::vector<std::unique_ptr<ISDatabaseEntry>> database;
    int file_id = 0;

    for (const std::string& file : files) {
        const auto& [signature, tree_size] = signatures_by_file[file];
        if (signature.empty()) {
            continue;
        }
        // Signed files. Will be sent in streaming mode.
        auto fd_and_size = open_and_get_size(file, error);
        if (!fd_and_size.has_value()) {
            return {};
        }
        if (!validate_signature(signature, tree_size, fd_and_size->second, error)) {
            return {};
        }
        std::optional<std::string> encoded_signature = encode_signature(signature, error);
        if (!encoded_signature.has_value()) {
            return {};
        }
        database.push_back(std::make_unique<ISSignedDatabaseEntry>(android::base::Basename(file),
                                                                   fd_and_size->second, file_id++,
                                                                   *encoded_signature, file));
    }

    for (const std::string& file : files) {
        const auto& [signature, _] = signatures_by_file[file];
        if (!signature.empty()) {
            continue;
        }
        // Unsigned files. Will be sent in stdin mode.
        // Open the file for reading. We'll return the FD for the caller to send it through stdin.
        auto fd_and_size = open_and_get_size(file, error);
        if (!fd_and_size.has_value()) {
            return {};
        }
        database.push_back(std::make_unique<ISUnsignedDatabaseEntry>(
                android::base::Basename(file), fd_and_size->second, file_id++,
                std::move(fd_and_size->first)));
    }

    return std::move(database);
}

// Opens a connection and sends install-incremental to the device along with the database.
// Returns a socket FD connected to the `abb` deamon on device, where writes to it go to `pm`
// shell's stdin and reads from it come from `pm` shell's stdout.
static std::optional<unique_fd> connect_and_send_database(
        const std::vector<std::unique_ptr<ISDatabaseEntry>>& database, const Args& passthrough_args,
        std::string* error) {
    std::vector<std::string> command_args{"package", "install-incremental"};
    command_args.insert(command_args.end(), passthrough_args.begin(), passthrough_args.end());
    for (const std::unique_ptr<ISDatabaseEntry>& entry : database) {
        command_args.push_back(entry->serialize());
    }

    std::string inner_error;
    auto connection_fd = unique_fd(send_abb_exec_command(command_args, &inner_error));
    if (connection_fd < 0) {
        *error = std::format("Failed to run '{}': {}", android::base::Join(command_args, " "),
                             inner_error);
        return {};
    }

    return connection_fd;
}

bool can_install(const Files& files) {
    for (const auto& file : files) {
        struct stat st;
        if (stat(file.c_str(), &st)) {
            return false;
        }

        if (requires_v4_signature(file)) {
            std::string error;
            auto signature_and_tree_size = read_signature(std::string(file).append(IDSIG), &error);
            if (!signature_and_tree_size.has_value()) {
                return false;
            }
            if (!validate_signature(signature_and_tree_size->first, signature_and_tree_size->second,
                                    st.st_size, &error)) {
                return false;
            }
        }
    }
    return true;
}

static bool send_unsigned_files(borrowed_fd connection_fd,
                                const std::vector<std::unique_ptr<ISDatabaseEntry>>& database,
                                std::string* error) {
    std::once_flag print_once;
    for (const std::unique_ptr<ISDatabaseEntry>& entry : database) {
        if (entry->is_v4_signed()) {
            continue;
        }
        auto unsigned_entry = static_cast<ISUnsignedDatabaseEntry*>(entry.get());
        std::call_once(print_once, [] { printf("Sending unsigned files...\n"); });
        if (!copy_to_file(unsigned_entry->fd().get(), connection_fd.get())) {
            *error = "adb: failed to send unsigned files";
            return false;
        }
    }
    return true;
}

// Wait until the Package Manager returns either "Success" or "Failure". The streaming
// may not have finished when this happens but PM received all the blocks is needs
// to decide if installation was ok.
static bool wait_for_installation(int read_fd, std::string* error) {
    static constexpr int kMaxMessageSize = 256;
    std::string child_stdout;
    child_stdout.resize(kMaxMessageSize);
    int bytes_read = adb_read(read_fd, child_stdout.data(), kMaxMessageSize);
    if (bytes_read < 0) {
        *error = std::format("Failed to read output: {}", strerror(errno));
        return false;
    }
    child_stdout.resize(bytes_read);
    // wait till installation either succeeds or fails
    if (child_stdout.find("Success") != std::string::npos) {
        return true;
    }
    // on failure, wait for full message
    auto begin_itr = child_stdout.find("Failure [");
    if (begin_itr != std::string::npos) {
        auto end_itr = child_stdout.rfind("]");
        if (end_itr != std::string::npos && end_itr >= begin_itr) {
            *error = std::format(
                    "Install failed: {}",
                    std::string_view(child_stdout).substr(begin_itr, end_itr - begin_itr + 1));
            return false;
        }
    }
    if (bytes_read == kMaxMessageSize) {
        *error = std::format("Output too long: {}", child_stdout);
        return false;
    }
    *error = std::format("Failed to parse output: {}", child_stdout);
    return false;
}

static std::optional<Process> start_inc_server_and_stream_signed_files(
        borrowed_fd connection_fd, const std::vector<std::unique_ptr<ISDatabaseEntry>>& database,
        std::string* error) {
    // pipe for child process to write output
    int print_fds[2];
    if (adb_socketpair(print_fds) != 0) {
        *error = "adb: failed to create socket pair for child to print to parent";
        return {};
    }
    auto [pipe_read_fd, pipe_write_fd] = print_fds;
    auto fd_cleaner = android::base::make_scope_guard([&] {
        adb_close(pipe_read_fd);
        adb_close(pipe_write_fd);
    });
    close_on_exec(pipe_read_fd);

    // We spawn an incremental server that will be up until all blocks have been fed to the
    // Package Manager. This could take a long time depending on the size of the files to
    // stream so we use a process able to outlive adb.
    std::vector<std::string> args{
            "inc-server",
            std::to_string(cast_handle_to_int(adb_get_os_handle(connection_fd.get()))),
            std::to_string(cast_handle_to_int(adb_get_os_handle(pipe_write_fd)))};
    int arg_pos = 0;
    for (const std::unique_ptr<ISDatabaseEntry>& entry : database) {
        if (!entry->is_v4_signed()) {
            continue;
        }
        // The incremental server assumes the argument position being the file ids.
        CHECK_EQ(entry->file_id(), arg_pos++);
        auto signed_entry = static_cast<ISSignedDatabaseEntry*>(entry.get());
        args.push_back(signed_entry->path());
    }
    std::string adb_path = android::base::GetExecutablePath();
    Process child =
            adb_launch_process(adb_path, std::move(args), {connection_fd.get(), pipe_write_fd});
    if (!child) {
        *error = "adb: failed to fork";
        return {};
    }
    auto server_killer = android::base::make_scope_guard([&] { child.kill(); });

    // Block until the Package Manager has received enough blocks to declare the installation
    // successful or failure. Meanwhile, the incremental server is still sending blocks to the
    // device.
    if (!wait_for_installation(pipe_read_fd, error)) {
        return {};
    }

    // adb client exits now but inc-server can continue
    server_killer.Disable();
    return child;
}

std::optional<Process> install(const Files& files, const Args& passthrough_args,
                               std::string* error) {
    std::optional<std::vector<std::unique_ptr<ISDatabaseEntry>>> database =
            build_database(files, error);
    if (!database.has_value()) {
        return {};
    }
    std::optional<unique_fd> connection_fd =
            connect_and_send_database(*database, passthrough_args, error);
    if (!connection_fd.has_value()) {
        return {};
    }
    if (!send_unsigned_files(*connection_fd, *database, error)) {
        return {};
    }
    return start_inc_server_and_stream_signed_files(*connection_fd, *database, error);
}

std::optional<Process> install(const Files& files, const Args& passthrough_args, bool silent) {
    std::string error;
    std::optional<Process> res = install(files, passthrough_args, &error);
    if (!res.has_value() && !silent) {
        fprintf(stderr, "%s.\n", error.c_str());
    }
    return res;
}

}  // namespace incremental

/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef __TRANSPORT_H
#define __TRANSPORT_H

#include <sys/types.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <android-base/macros.h>
#include <android-base/thread_annotations.h>
#include <openssl/rsa.h>

#include "adb.h"
#include "adb_unique_fd.h"
#include "types.h"

// Even though the feature set is used as a set, we only have a dozen or two
// of available features at any moment. Vector works much better in terms of
// both memory usage and performance for these sizes.
using FeatureSet = std::vector<std::string>;

namespace adb {
namespace tls {

class TlsConnection;

}  // namespace tls
}  // namespace adb

const FeatureSet& supported_features();

// Encodes and decodes FeatureSet objects into human-readable strings.
std::string FeatureSetToString(const FeatureSet& features);
FeatureSet StringToFeatureSet(const std::string& features_string);

// Returns true if both local features and |feature_set| support |feature|.
bool CanUseFeature(const FeatureSet& feature_set, const std::string& feature);

// Do not use any of [:;=,] in feature strings, they have special meaning
// in the connection banner.
extern const char* const kFeatureShell2;
// The 'cmd' command is available
extern const char* const kFeatureCmd;
extern const char* const kFeatureStat2;
extern const char* const kFeatureLs2;
// The server is running with libusb enabled.
extern const char* const kFeatureLibusb;
// adbd supports `push --sync`.
extern const char* const kFeaturePushSync;
// adbd supports installing .apex packages.
extern const char* const kFeatureApex;
// adbd has b/110953234 fixed.
extern const char* const kFeatureFixedPushMkdir;
// adbd supports android binder bridge (abb) in interactive mode using shell protocol.
extern const char* const kFeatureAbb;
// adbd supports abb using raw pipe.
extern const char* const kFeatureAbbExec;
// adbd properly updates symlink timestamps on push.
extern const char* const kFeatureFixedPushSymlinkTimestamp;
// Implement `adb remount` via shelling out to /system/bin/remount.
extern const char* const kFeatureRemountShell;
// adbd supports `track-app` service reporting debuggable/profileable apps.
extern const char* const kFeatureTrackApp;
// adbd supports version 2 of send/recv.
extern const char* const kFeatureSendRecv2;
// adbd supports brotli for send/recv v2.
extern const char* const kFeatureSendRecv2Brotli;
// adbd supports LZ4 for send/recv v2.
extern const char* const kFeatureSendRecv2LZ4;
// adbd supports Zstd for send/recv v2.
extern const char* const kFeatureSendRecv2Zstd;
// adbd supports dry-run send for send/recv v2.
extern const char* const kFeatureSendRecv2DryRunSend;
// adbd supports delayed acks.
extern const char* const kFeatureDelayedAck;
// adbd supports `dev-raw` service
extern const char* const kFeatureDevRaw;

TransportId NextTransportId();

// Abstraction for a non-blocking packet transport.
struct Connection {
    Connection() = default;
    virtual ~Connection() = default;

    void SetTransport(atransport* transport) { transport_ = transport; }

    virtual bool Write(std::unique_ptr<apacket> packet) = 0;

    // Return true if the transport successfully started.
    virtual bool Start() = 0;
    virtual void Stop() = 0;

    virtual bool DoTlsHandshake(RSA* key, std::string* auth_key = nullptr) = 0;

    // Stop, and reset the device if it's a USB connection.
    virtual void Reset();

    virtual bool SupportsDetach() const { return false; }

    virtual bool Attach(std::string* error) {
        *error = "transport type doesn't support attach";
        return false;
    }

    virtual bool Detach(std::string* error) {
        *error = "transport type doesn't support detach";
        return false;
    }

    std::string Serial() const;

    atransport* transport_ = nullptr;

    static std::unique_ptr<Connection> FromFd(unique_fd fd);

    virtual uint64_t NegotiatedSpeedMbps() { return 0; }
    virtual uint64_t MaxSpeedMbps() { return 0; }
};

// Abstraction for a blocking packet transport.
struct BlockingConnection {
    BlockingConnection() = default;
    BlockingConnection(const BlockingConnection& copy) = delete;
    BlockingConnection(BlockingConnection&& move) = delete;

    // Destroy a BlockingConnection. Formerly known as 'Close' in atransport.
    virtual ~BlockingConnection() = default;

    // Read/Write a packet. These functions are concurrently called from a transport's reader/writer
    // threads.
    virtual bool Read(apacket* packet) = 0;
    virtual bool Write(apacket* packet) = 0;

    virtual bool DoTlsHandshake(RSA* key, std::string* auth_key = nullptr) = 0;

    // Terminate a connection.
    // This method must be thread-safe, and must cause concurrent Reads/Writes to terminate.
    // Formerly known as 'Kick' in atransport.
    virtual void Close() = 0;

    // Terminate a connection, and reset it.
    virtual void Reset() = 0;
};

struct BlockingConnectionAdapter : public Connection {
    explicit BlockingConnectionAdapter(std::unique_ptr<BlockingConnection> connection);

    virtual ~BlockingConnectionAdapter();

    virtual bool Write(std::unique_ptr<apacket> packet) override final;

    virtual bool Start() override final;
    virtual void Stop() override final;
    virtual bool DoTlsHandshake(RSA* key, std::string* auth_key) override final;

    virtual void Reset() override final;

  private:
    void StartReadThread() REQUIRES(mutex_);
    bool started_ GUARDED_BY(mutex_) = false;
    bool stopped_ GUARDED_BY(mutex_) = false;

    std::unique_ptr<BlockingConnection> underlying_;
    std::thread read_thread_ GUARDED_BY(mutex_);
    std::thread write_thread_ GUARDED_BY(mutex_);

    std::deque<std::unique_ptr<apacket>> write_queue_ GUARDED_BY(mutex_);
    std::mutex mutex_;
    std::condition_variable cv_;

    std::once_flag error_flag_;
};

struct FdConnection : public BlockingConnection {
    explicit FdConnection(unique_fd fd);
    ~FdConnection();

    bool Read(apacket* packet) override final;
    bool Write(apacket* packet) override final;
    bool DoTlsHandshake(RSA* key, std::string* auth_key) override final;

    void Close() override;
    virtual void Reset() override final { Close(); }

  private:
    bool DispatchRead(void* buf, size_t len);
    bool DispatchWrite(void* buf, size_t len);

    unique_fd fd_;
    std::unique_ptr<adb::tls::TlsConnection> tls_;
};

// Waits for a transport's connection to be not pending. This is a separate
// object so that the transport can be destroyed and another thread can be
// notified of it in a race-free way.
class ConnectionWaitable {
  public:
    ConnectionWaitable() = default;
    ~ConnectionWaitable() = default;

    // Waits until the first CNXN packet has been received by the owning
    // atransport, or the specified timeout has elapsed. Can be called from any
    // thread.
    //
    // Returns true if the CNXN packet was received in a timely fashion, false
    // otherwise.
    bool WaitForConnection(std::chrono::milliseconds timeout);

    // Can be called from any thread when the connection stops being pending.
    // Only the first invocation will be acknowledged, the rest will be no-ops.
    void SetConnectionEstablished(bool success);

  private:
    bool connection_established_ GUARDED_BY(mutex_) = false;
    bool connection_established_ready_ GUARDED_BY(mutex_) = false;
    std::mutex mutex_;
    std::condition_variable cv_;

    DISALLOW_COPY_AND_ASSIGN(ConnectionWaitable);
};

enum class ReconnectResult {
    Retry,
    Success,
    Abort,
};

#if ADB_HOST
struct usb_handle;
#endif

class atransport : public enable_weak_from_this<atransport> {
  public:
    // TODO(danalbert): We expose waaaaaaay too much stuff because this was
    // historically just a struct, but making the whole thing a more idiomatic
    // class in one go is a very large change. Given how bad our testing is,
    // it's better to do this piece by piece.

    using ReconnectCallback = std::function<ReconnectResult(atransport*)>;

    atransport(TransportType t, ReconnectCallback reconnect, ConnectionState state)
        : id(NextTransportId()),
          type(t),
          kicked_(false),
          connection_state_(state),
          connection_(nullptr),
          reconnect_(std::move(reconnect)) {
#if ADB_HOST
        connection_waitable_ = std::make_shared<ConnectionWaitable>();
#endif

        // Initialize protocol to min version for compatibility with older versions.
        // Version will be updated post-connect.
        protocol_version = A_VERSION_MIN;
        max_payload = MAX_PAYLOAD;
    }
    atransport(TransportType t, ConnectionState state = kCsOffline)
        : atransport(t, [](atransport*) { return ReconnectResult::Abort; }, state) {}
    ~atransport();

    int Write(apacket* p);
    void Reset();
    void Kick();
    bool kicked() const { return kicked_; }

    // ConnectionState can be read by all threads, but can only be written in the main thread.
    ConnectionState GetConnectionState() const;
    void SetConnectionState(ConnectionState state);

    void SetConnection(std::shared_ptr<Connection> connection);
    std::shared_ptr<Connection> connection() {
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_;
    }

    bool HandleRead(std::unique_ptr<apacket> p);
    void HandleError(const std::string& error);

#if ADB_HOST
    void SetUsbHandle(usb_handle* h) { usb_handle_ = h; }
    usb_handle* GetUsbHandle() { return usb_handle_; }

    // Interface for management/filter on forward:reverse: configuration.
    void UpdateReverseConfig(std::string_view service_addr);
    bool IsReverseConfigured(const std::string& local_addr);
#endif

    const TransportId id;

    bool online = false;
    TransportType type = kTransportAny;

    // Used to identify transports for clients.
    std::string serial;
    std::string product;
    std::string model;
    std::string device;
    std::string devpath;

    // If this is set, the transport will initiate the connection with a
    // START_TLS command, instead of AUTH.
    bool use_tls = false;
    int tls_version = A_STLS_VERSION;
    int get_tls_version() const;

#if !ADB_HOST
    // Used to provide the key to the framework.
    std::string auth_key;
    std::optional<uint64_t> auth_id;
#endif

    bool IsTcpDevice() const { return type == kTransportLocal; }

#if ADB_HOST
    // The current key being authorized.
    std::shared_ptr<RSA> Key();
    std::shared_ptr<RSA> NextKey();
    void ResetKeys();
#endif

    char token[TOKEN_SIZE] = {};
    size_t failed_auth_attempts = 0;

    std::string serial_name() const { return !serial.empty() ? serial : "<unknown>"; }

    void update_version(int version, size_t payload);
    int get_protocol_version() const;
    size_t get_max_payload() const;

    const FeatureSet& features() const { return features_; }

    bool has_feature(const std::string& feature) const;

    bool SupportsDelayedAck() const {
        return delayed_ack_;
    }

    // Loads the transport's feature set from the given string.
    void SetFeatures(const std::string& features_string);

    void AddDisconnect(adisconnect* disconnect);
    void RemoveDisconnect(adisconnect* disconnect);
    void RunDisconnects();

#if ADB_HOST
    bool Attach(std::string* error);
    bool Detach(std::string* error);
#endif

#if ADB_HOST
    // Returns true if |target| matches this transport. A matching |target| can be any of:
    //   * <serial>
    //   * <devpath>
    //   * product:<product>
    //   * model:<model>
    //   * device:<device>
    //
    // If this is a local transport, serial will also match [tcp:|udp:]<hostname>[:port] targets.
    // For example, serial "100.100.100.100:5555" would match any of:
    //   * 100.100.100.100
    //   * tcp:100.100.100.100
    //   * udp:100.100.100.100:5555
    // This is to make it easier to use the same network target for both fastboot and adb.
    bool MatchesTarget(const std::string& target) const;

    // Notifies that the atransport is no longer waiting for the connection
    // being established.
    void SetConnectionEstablished(bool success);

    // Gets a shared reference to the ConnectionWaitable.
    std::shared_ptr<ConnectionWaitable> connection_waitable() { return connection_waitable_; }

    // Attempts to reconnect with the underlying Connection.
    ReconnectResult Reconnect();
#endif

  private:
    std::atomic<bool> kicked_;

    // A set of features transmitted in the banner with the initial connection.
    // This is stored in the banner as 'features=feature0,feature1,etc'.
    FeatureSet features_;
    int protocol_version;
    size_t max_payload;

    // A list of adisconnect callbacks called when the transport is kicked.
    std::list<adisconnect*> disconnects_;

    std::atomic<ConnectionState> connection_state_;
#if ADB_HOST
    std::deque<std::shared_ptr<RSA>> keys_;
#endif

#if ADB_HOST
    // A sharable object that can be used to wait for the atransport's
    // connection to be established.
    std::shared_ptr<ConnectionWaitable> connection_waitable_;
#endif

    // The underlying connection object.
    std::shared_ptr<Connection> connection_ GUARDED_BY(mutex_);

#if ADB_HOST
    // USB handle for the connection, if available.
    usb_handle* usb_handle_ = nullptr;
#endif

    // A callback that will be invoked when the atransport needs to reconnect.
    ReconnectCallback reconnect_;

    std::mutex mutex_;

    bool delayed_ack_ = false;

#if ADB_HOST
    // Track remote addresses against local addresses (configured)
    // through `adb reverse` commands.
    // Access constrained to primary thread by virtue of check_main_thread().
    std::unordered_map<std::string, std::string> reverse_forwards_;
#endif

    DISALLOW_COPY_AND_ASSIGN(atransport);
};

// --one-device command line parameter is eventually put here.
void transport_set_one_device(const char* adb_one_device);

// Returns one device owned by this server of nullptr if all devices belong to server.
const char* transport_get_one_device();

// Returns true if the adb server owns all devices, or `serial`.
bool transport_server_owns_device(std::string_view serial);

// Returns true if the adb server owns all devices, `serial`, or `dev_path`.
bool transport_server_owns_device(std::string_view dev_path, std::string_view serial);

/*
 * Obtain a transport from the available transports.
 * If serial is non-null then only the device with that serial will be chosen.
 * If transport_id is non-zero then only the device with that transport ID will be chosen.
 * If multiple devices/emulators would match, *is_ambiguous (if non-null)
 * is set to true and nullptr returned.
 * If no suitable transport is found, error is set and nullptr returned.
 */
atransport* acquire_one_transport(TransportType type, const char* serial, TransportId transport_id,
                                  bool* is_ambiguous, std::string* error_out,
                                  bool accept_any_state = false);
void kick_transport(atransport* t, bool reset = false);
void update_transports();

// Iterates across all of the current and pending transports.
// Stops iteration and returns false if fn returns false, otherwise returns true.
bool iterate_transports(std::function<bool(const atransport*)> fn);

void init_reconnect_handler();
void init_mdns_transport_discovery();

#if ADB_HOST
atransport* find_transport(const char* serial);

void kick_all_tcp_devices();
#endif

void kick_all_transports();

void kick_all_tcp_tls_transports();

#if !ADB_HOST
void kick_all_transports_by_auth_key(std::string_view auth_key);
#endif

void register_transport(atransport* transport);

#if ADB_HOST
void init_usb_transport(atransport* t, usb_handle* usb);

void register_libusb_transport(std::shared_ptr<Connection> connection, const char* serial,
                               const char* devpath, unsigned writable);

void register_usb_transport(usb_handle* h, const char* serial, const char* devpath,
                            unsigned writeable);

// This should only be used for transports with connection_state == kCsNoPerm.
void unregister_usb_transport(usb_handle* usb);
#endif

/* Connect to a network address and register it as a device */
void connect_device(const std::string& address, std::string* response);

/* initialize a transport object's func pointers and state */
int init_socket_transport(atransport* t, unique_fd s, int port, bool is_emulator);

/* cause new transports to be init'd and added to the list */
bool register_socket_transport(unique_fd s, std::string serial, int port, bool is_emulator,
                               atransport::ReconnectCallback reconnect, bool use_tls,
                               int* error = nullptr);

bool check_header(apacket* p, atransport* t);

void close_usb_devices(bool reset = false);
void close_usb_devices(std::function<bool(const atransport*)> predicate, bool reset = false);

void send_packet(apacket* p, atransport* t);

#if ADB_HOST
enum TrackerOutputType { SHORT_TEXT, LONG_TEXT, PROTOBUF, TEXT_PROTOBUF };
asocket* create_device_tracker(TrackerOutputType type);
std::string list_transports(TrackerOutputType type);
bool burst_mode_enabled();
#endif

#endif /* __TRANSPORT_H */

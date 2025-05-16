/*
 * Copyright (C) 2007 The Android Open Source Project
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

#define TRACE_TAG TRANSPORT

#include "sysdeps.h"

#include "transport.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <adb/crypto/rsa_2048_key.h>
#include <adb/crypto/x509_generator.h>
#include <adb/tls/tls_connection.h>
#include <android-base/logging.h>
#include <android-base/no_destructor.h>
#include <android-base/parsenetaddress.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/thread_annotations.h>
#include <diagnose_usb.h>

#include "adb.h"
#include "adb_auth.h"
#include "adb_io.h"
#include "adb_trace.h"
#include "adb_utils.h"
#include "fdevent/fdevent.h"
#include "sysdeps/chrono.h"

#if ADB_HOST
#include <google/protobuf/text_format.h>
#include "adb_host.pb.h"
#include "client/detach.h"
#include "client/usb.h"
#endif

using namespace adb::crypto;
using namespace adb::tls;
using namespace std::string_literals;
using android::base::ScopedLockAssertion;
using TlsError = TlsConnection::TlsError;

static void remove_transport(atransport* transport);
static void transport_destroy(atransport* transport);

static auto& transport_lock = *new std::recursive_mutex();
// When a tranport is created, it is not started yet (and in the case of the host side, it has
// not yet sent CNXN). These transports are staged in the pending list.
static auto& pending_list = *new std::list<atransport*>();
// TODO: unordered_map<TransportId, atransport*>
static auto& transport_list = *new std::list<atransport*>();

const char* const kFeatureShell2 = "shell_v2";
const char* const kFeatureCmd = "cmd";
const char* const kFeatureStat2 = "stat_v2";
const char* const kFeatureLs2 = "ls_v2";
const char* const kFeatureLibusb = "libusb";
const char* const kFeaturePushSync = "push_sync";
const char* const kFeatureApex = "apex";
const char* const kFeatureFixedPushMkdir = "fixed_push_mkdir";
const char* const kFeatureAbb = "abb";
const char* const kFeatureFixedPushSymlinkTimestamp = "fixed_push_symlink_timestamp";
const char* const kFeatureAbbExec = "abb_exec";
const char* const kFeatureRemountShell = "remount_shell";
const char* const kFeatureTrackApp = "track_app";
const char* const kFeatureSendRecv2 = "sendrecv_v2";
const char* const kFeatureSendRecv2Brotli = "sendrecv_v2_brotli";
const char* const kFeatureSendRecv2LZ4 = "sendrecv_v2_lz4";
const char* const kFeatureSendRecv2Zstd = "sendrecv_v2_zstd";
const char* const kFeatureSendRecv2DryRunSend = "sendrecv_v2_dry_run_send";
const char* const kFeatureDelayedAck = "delayed_ack";
// TODO(joshuaduong): Bump to v2 when openscreen discovery is enabled by default
const char* const kFeatureOpenscreenMdns = "openscreen_mdns";
const char* const kFeatureDeviceTrackerProtoFormat = "devicetracker_proto_format";
const char* const kFeatureDevRaw = "devraw";
const char* const kFeatureAppInfo = "app_info";  // Add information to track-app (package name, ...)
const char* const kFeatureServerStatus = "server_status";  // Ability to output server status
const char* const kFeatureTrackMdns = "track_mdns";        // Track and stream mdns services.

namespace {

#if ADB_HOST

// Tracks and handles atransport*s that are attempting reconnection.
class ReconnectHandler {
  public:
    ReconnectHandler() = default;
    ~ReconnectHandler() = default;

    // Starts the ReconnectHandler thread.
    void Start();

    // Requests the ReconnectHandler thread to stop.
    void Stop();

    // Adds the atransport* to the queue of reconnect attempts.
    void TrackTransport(atransport* transport);

    // Wake up the ReconnectHandler thread to have it check for kicked transports.
    void CheckForKicked();

  private:
    // The main thread loop.
    void Run();

    // Tracks a reconnection attempt.
    struct ReconnectAttempt {
        atransport* transport;
        std::chrono::steady_clock::time_point reconnect_time;
        size_t attempts_left;

        bool operator<(const ReconnectAttempt& rhs) const {
            if (reconnect_time == rhs.reconnect_time) {
                return reinterpret_cast<uintptr_t>(transport) <
                       reinterpret_cast<uintptr_t>(rhs.transport);
            }
            return reconnect_time < rhs.reconnect_time;
        }
    };

    // Only retry for up to one minute.
    static constexpr const std::chrono::seconds kDefaultTimeout = 3s;
    static constexpr const size_t kMaxAttempts = 20;

    // Protects all members.
    std::mutex reconnect_mutex_;
    bool running_ GUARDED_BY(reconnect_mutex_) = true;
    std::thread handler_thread_;
    std::condition_variable reconnect_cv_;
    std::set<ReconnectAttempt> reconnect_queue_ GUARDED_BY(reconnect_mutex_);

    DISALLOW_COPY_AND_ASSIGN(ReconnectHandler);
};

void ReconnectHandler::Start() {
    fdevent_check_looper();
    handler_thread_ = std::thread(&ReconnectHandler::Run, this);
}

void ReconnectHandler::Stop() {
    fdevent_check_looper();
    {
        std::lock_guard<std::mutex> lock(reconnect_mutex_);
        running_ = false;
    }
    reconnect_cv_.notify_one();
    handler_thread_.join();

    // Drain the queue to free all resources.
    std::lock_guard<std::mutex> lock(reconnect_mutex_);
    while (!reconnect_queue_.empty()) {
        ReconnectAttempt attempt = *reconnect_queue_.begin();
        reconnect_queue_.erase(reconnect_queue_.begin());
        remove_transport(attempt.transport);
    }
}

void ReconnectHandler::TrackTransport(atransport* transport) {
    fdevent_check_looper();
    {
        std::lock_guard<std::mutex> lock(reconnect_mutex_);
        if (!running_) return;
        // Arbitrary sleep to give adbd time to get ready, if we disconnected because it exited.
        auto reconnect_time = std::chrono::steady_clock::now() + 250ms;
        reconnect_queue_.emplace(
                ReconnectAttempt{transport, reconnect_time, ReconnectHandler::kMaxAttempts});
    }
    reconnect_cv_.notify_one();
}

void ReconnectHandler::CheckForKicked() {
    reconnect_cv_.notify_one();
}

void ReconnectHandler::Run() {
    while (true) {
        ReconnectAttempt attempt;
        {
            std::unique_lock<std::mutex> lock(reconnect_mutex_);
            ScopedLockAssertion assume_lock(reconnect_mutex_);

            if (!reconnect_queue_.empty()) {
                // FIXME: libstdc++ (used on Windows) implements condition_variable with
                //        system_clock as its clock, so we're probably hosed if the clock changes,
                //        even if we use steady_clock throughout. This problem goes away once we
                //        switch to libc++.
                reconnect_cv_.wait_until(lock, reconnect_queue_.begin()->reconnect_time);
            } else {
                reconnect_cv_.wait(lock);
            }

            if (!running_) return;

            // Scan the whole list for kicked transports, so that we immediately handle an explicit
            // disconnect request.
            for (auto it = reconnect_queue_.begin(); it != reconnect_queue_.end();) {
                if (it->transport->kicked()) {
                    D("transport %s was kicked. giving up on it.", it->transport->serial.c_str());
                    remove_transport(it->transport);
                    it = reconnect_queue_.erase(it);
                } else {
                    ++it;
                }
            }

            if (reconnect_queue_.empty()) continue;

            // Go back to sleep if we either woke up spuriously, or we were woken up to remove
            // a kicked transport, and the first transport isn't ready for reconnection yet.
            auto now = std::chrono::steady_clock::now();
            if (reconnect_queue_.begin()->reconnect_time > now) {
                continue;
            }

            attempt = *reconnect_queue_.begin();
            reconnect_queue_.erase(reconnect_queue_.begin());
        }
        D("attempting to reconnect %s", attempt.transport->serial.c_str());

        switch (attempt.transport->Reconnect()) {
            case ReconnectResult::Retry: {
                D("attempting to reconnect %s failed.", attempt.transport->serial.c_str());
                if (attempt.attempts_left == 0) {
                    D("transport %s exceeded the number of retry attempts. giving up on it.",
                      attempt.transport->serial.c_str());
                    remove_transport(attempt.transport);
                    continue;
                }

                std::lock_guard<std::mutex> lock(reconnect_mutex_);
                reconnect_queue_.emplace(ReconnectAttempt{
                        attempt.transport,
                        std::chrono::steady_clock::now() + ReconnectHandler::kDefaultTimeout,
                        attempt.attempts_left - 1});
                continue;
            }

            case ReconnectResult::Success:
                D("reconnection to %s succeeded.", attempt.transport->serial.c_str());
                register_transport(attempt.transport);
                continue;

            case ReconnectResult::Abort:
                D("cancelling reconnection attempt to %s.", attempt.transport->serial.c_str());
                remove_transport(attempt.transport);
                continue;
        }
    }
}

static auto& reconnect_handler = *new ReconnectHandler();

#endif

}  // namespace

TransportId NextTransportId() {
    static std::atomic<TransportId> next(1);
    return next++;
}

void Connection::Reset() {
    LOG(INFO) << "Connection::Reset(): stopping";
    Stop();
}

std::string Connection::Serial() const {
    return transport_ ? transport_->serial_name() : "<unknown>";
}

BlockingConnectionAdapter::BlockingConnectionAdapter(std::unique_ptr<BlockingConnection> connection)
    : underlying_(std::move(connection)) {}

BlockingConnectionAdapter::~BlockingConnectionAdapter() {
    LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): destructing";
    Stop();
}

bool BlockingConnectionAdapter::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) {
        LOG(FATAL) << "BlockingConnectionAdapter(" << Serial() << "): started multiple times";
    }

    StartReadThread();

    write_thread_ = std::thread([this]() {
        LOG(INFO) << Serial() << ": write thread spawning";
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            ScopedLockAssertion assume_locked(mutex_);
            cv_.wait(lock, [this]() REQUIRES(mutex_) {
                return this->stopped_ || !this->write_queue_.empty();
            });

            if (this->stopped_) {
                return;
            }

            std::unique_ptr<apacket> packet = std::move(this->write_queue_.front());
            this->write_queue_.pop_front();
            lock.unlock();

            if (!this->underlying_->Write(packet.get())) {
                break;
            }
        }
        std::call_once(this->error_flag_, [this]() { transport_->HandleError("write failed"); });
    });

    started_ = true;
    return true;
}

void BlockingConnectionAdapter::StartReadThread() {
    read_thread_ = std::thread([this]() {
        LOG(INFO) << Serial() << ": read thread spawning";
        while (true) {
            auto packet = std::make_unique<apacket>();
            if (!underlying_->Read(packet.get())) {
                PLOG(INFO) << Serial() << ": read failed";
                break;
            }

            bool got_stls_cmd = false;
            if (packet->msg.command == A_STLS) {
                got_stls_cmd = true;
            }

            transport_->HandleRead(std::move(packet));

            // If we received the STLS packet, we are about to perform the TLS
            // handshake. So this read thread must stop and resume after the
            // handshake completes otherwise this will interfere in the process.
            if (got_stls_cmd) {
                LOG(INFO) << Serial() << ": Received STLS packet. Stopping read thread.";
                return;
            }
        }
        std::call_once(this->error_flag_, [this]() { transport_->HandleError("read failed"); });
    });
}

bool BlockingConnectionAdapter::DoTlsHandshake(RSA* key, std::string* auth_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
    bool success = this->underlying_->DoTlsHandshake(key, auth_key);
    StartReadThread();
    return success;
}

void BlockingConnectionAdapter::Reset() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) {
            LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): not started";
            return;
        }

        if (stopped_) {
            LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): already stopped";
            return;
        }
    }

    LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): resetting";
    this->underlying_->Reset();
    Stop();
}

void BlockingConnectionAdapter::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) {
            LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): not started";
            return;
        }

        if (stopped_) {
            LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): already stopped";
            return;
        }

        stopped_ = true;
    }

    LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): stopping";

    this->underlying_->Close();
    this->cv_.notify_one();

    // Move the threads out into locals with the lock taken, and then unlock to let them exit.
    std::thread read_thread;
    std::thread write_thread;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        read_thread = std::move(read_thread_);
        write_thread = std::move(write_thread_);
    }

    read_thread.join();
    write_thread.join();

    LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): stopped";
    std::call_once(this->error_flag_, [this]() { transport_->HandleError("requested stop"); });
}

bool BlockingConnectionAdapter::Write(std::unique_ptr<apacket> packet) {
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        write_queue_.emplace_back(std::move(packet));
    }

    cv_.notify_one();
    return true;
}

FdConnection::FdConnection(unique_fd fd) : fd_(std::move(fd)) {}

FdConnection::~FdConnection() {}

bool FdConnection::DispatchRead(void* buf, size_t len) {
    if (tls_ != nullptr) {
        // The TlsConnection doesn't allow 0 byte reads
        if (len == 0) {
            return true;
        }
        return tls_->ReadFully(buf, len);
    }

    return ReadFdExactly(fd_.get(), buf, len);
}

bool FdConnection::DispatchWrite(void* buf, size_t len) {
    if (tls_ != nullptr) {
        // The TlsConnection doesn't allow 0 byte writes
        if (len == 0) {
            return true;
        }
        return tls_->WriteFully(std::string_view(reinterpret_cast<const char*>(buf), len));
    }

    return WriteFdExactly(fd_.get(), buf, len);
}

bool FdConnection::Read(apacket* packet) {
    if (!DispatchRead(&packet->msg, sizeof(amessage))) {
        D("remote local: read terminated (message)");
        return false;
    }

    if (packet->msg.data_length > MAX_PAYLOAD) {
        D("remote local: read overflow (data length = %" PRIu32 ")", packet->msg.data_length);
        return false;
    }

    packet->payload.resize(packet->msg.data_length);

    if (!DispatchRead(&packet->payload[0], packet->payload.size())) {
        D("remote local: terminated (data)");
        return false;
    }

    return true;
}

bool FdConnection::Write(apacket* packet) {
    if (!DispatchWrite(&packet->msg, sizeof(packet->msg))) {
        D("remote local: write terminated");
        return false;
    }

    if (packet->msg.data_length) {
        if (!DispatchWrite(&packet->payload[0], packet->msg.data_length)) {
            D("remote local: write terminated");
            return false;
        }
    }

    return true;
}

bool FdConnection::DoTlsHandshake(RSA* key, std::string* auth_key) {
    bssl::UniquePtr<EVP_PKEY> evp_pkey(EVP_PKEY_new());
    if (!EVP_PKEY_set1_RSA(evp_pkey.get(), key)) {
        LOG(ERROR) << "EVP_PKEY_set1_RSA failed";
        return false;
    }
    auto x509 = GenerateX509Certificate(evp_pkey.get());
    auto x509_str = X509ToPEMString(x509.get());
    auto evp_str = Key::ToPEMString(evp_pkey.get());

    int osh = cast_handle_to_int(adb_get_os_handle(fd_));
#if ADB_HOST
    tls_ = TlsConnection::Create(TlsConnection::Role::Client, x509_str, evp_str, osh);
#else
    tls_ = TlsConnection::Create(TlsConnection::Role::Server, x509_str, evp_str, osh);
#endif
    CHECK(tls_);
#if ADB_HOST
    // TLS 1.3 gives the client no message if the server rejected the
    // certificate. This will enable a check in the tls connection to check
    // whether the client certificate got rejected. Note that this assumes
    // that, on handshake success, the server speaks first.
    tls_->EnableClientPostHandshakeCheck(true);
    // Add callback to set the certificate when server issues the
    // CertificateRequest.
    tls_->SetCertificateCallback(adb_tls_set_certificate);
    // Allow any server certificate
    tls_->SetCertVerifyCallback([](X509_STORE_CTX*) { return 1; });
#else
    // Add callback to check certificate against a list of known public keys
    tls_->SetCertVerifyCallback(
            [auth_key](X509_STORE_CTX* ctx) { return adbd_tls_verify_cert(ctx, auth_key); });
    // Add the list of allowed client CA issuers
    auto ca_list = adbd_tls_client_ca_list();
    tls_->SetClientCAList(ca_list.get());
#endif

    auto err = tls_->DoHandshake();
    if (err == TlsError::Success) {
        return true;
    }

    tls_.reset();
    return false;
}

void FdConnection::Close() {
    adb_shutdown(fd_.get());
    fd_.reset();
}

void send_packet(apacket* p, atransport* t) {
    VLOG(PACKETS) << std::format("packet --> {}{}{}{}", ((char*)(&(p->msg.command)))[0],
                                 ((char*)(&(p->msg.command)))[1], ((char*)(&(p->msg.command)))[2],
                                 ((char*)(&(p->msg.command)))[3]);

    p->msg.magic = p->msg.command ^ 0xffffffff;
    // compute a checksum for connection/auth packets for compatibility reasons
    if (t->get_protocol_version() >= A_VERSION_SKIP_CHECKSUM) {
        p->msg.data_check = 0;
    } else {
        p->msg.data_check = calculate_apacket_checksum(p);
    }

    VLOG(TRANSPORT) << dump_packet(t->serial.c_str(), "to remote", p);

    if (t == nullptr) {
        LOG(FATAL) << "Transport is null";
    }

    if (t->Write(p) != 0) {
        D("%s: failed to enqueue packet, closing transport", t->serial.c_str());
        t->Kick();
    }
}

void kick_transport(atransport* t, bool reset) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    // As kick_transport() can be called from threads without guarantee that t is valid,
    // check if the transport is in transport_list first.
    //
    // TODO(jmgao): WTF? Is this actually true?
    if (std::find(transport_list.begin(), transport_list.end(), t) != transport_list.end()) {
        if (reset) {
            t->Reset();
        } else {
            t->Kick();
        }
    }

#if ADB_HOST
    reconnect_handler.CheckForKicked();
#endif
}

#if ADB_HOST

/* this adds support required by the 'track-devices' service.
 * this is used to send the content of "list_transport" to any
 * number of client connections that want it through a single
 * live TCP connection
 */
struct device_tracker {
    asocket socket;
    bool update_needed = false;
    TrackerOutputType output_type = SHORT_TEXT;
    device_tracker* next = nullptr;
};

/* linked list of all device trackers */
static device_tracker* device_tracker_list;

static void device_tracker_remove(device_tracker* tracker) {
    device_tracker** pnode = &device_tracker_list;
    device_tracker* node = *pnode;

    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    while (node) {
        if (node == tracker) {
            *pnode = node->next;
            break;
        }
        pnode = &node->next;
        node = *pnode;
    }
}

static void device_tracker_close(asocket* socket) {
    device_tracker* tracker = (device_tracker*)socket;
    asocket* peer = socket->peer;

    D("device tracker %p removed", tracker);
    if (peer) {
        peer->peer = nullptr;
        peer->close(peer);
    }
    device_tracker_remove(tracker);
    delete tracker;
}

static int device_tracker_enqueue(asocket* socket, apacket::payload_type) {
    /* you can't read from a device tracker, close immediately */
    device_tracker_close(socket);
    return -1;
}

static int device_tracker_send(device_tracker* tracker, const std::string& string) {
    asocket* peer = tracker->socket.peer;

    apacket::payload_type data;
    data.resize(4 + string.size());
    char buf[5];
    snprintf(buf, sizeof(buf), "%04x", static_cast<int>(string.size()));
    memcpy(&data[0], buf, 4);
    memcpy(&data[4], string.data(), string.size());
    return peer->enqueue(peer, std::move(data));
}

static void device_tracker_ready(asocket* socket) {
    device_tracker* tracker = reinterpret_cast<device_tracker*>(socket);

    // We want to send the device list when the tracker connects
    // for the first time, even if no update occurred.
    if (tracker->update_needed) {
        tracker->update_needed = false;
        device_tracker_send(tracker, list_transports(tracker->output_type));
    }
}

asocket* create_device_tracker(TrackerOutputType output_type) {
    device_tracker* tracker = new device_tracker();
    if (tracker == nullptr) LOG(FATAL) << "cannot allocate device tracker";

    D("device tracker %p created", tracker);

    tracker->socket.enqueue = device_tracker_enqueue;
    tracker->socket.ready = device_tracker_ready;
    tracker->socket.close = device_tracker_close;
    tracker->update_needed = true;
    tracker->output_type = output_type;

    tracker->next = device_tracker_list;
    device_tracker_list = tracker;

    return &tracker->socket;
}

// Check if all of the USB transports are connected.
bool iterate_transports(std::function<bool(const atransport*)> fn) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (const auto& t : transport_list) {
        if (!fn(t)) {
            return false;
        }
    }
    for (const auto& t : pending_list) {
        if (!fn(t)) {
            return false;
        }
    }
    return true;
}

// Call this function each time the transport list has changed.
void update_transports() {
    update_transport_status();

    // Notify `adb track-devices` clients.
    device_tracker* tracker = device_tracker_list;
    while (tracker != nullptr) {
        device_tracker* next = tracker->next;
        // This may destroy the tracker if the connection is closed.
        device_tracker_send(tracker, list_transports(tracker->output_type));
        tracker = next;
    }
}

#else

void update_transports() {
    // Nothing to do on the device side.
}

#endif  // ADB_HOST

static void fdevent_unregister_transport(atransport* t) {
    VLOG(TRANSPORT) << "unregistering transport: " << t->serial;

    {
        std::lock_guard<std::recursive_mutex> lock(transport_lock);
        transport_list.remove(t);
        pending_list.remove(t);
    }

    t->connection()->SetTransport(nullptr);
    delete t;

    update_transports();
}

static void fdevent_register_transport(atransport* t) {
    auto state = to_string(t->GetConnectionState());
    VLOG(TRANSPORT) << "registering: " << t->serial.c_str() << " state=" << state
                    << " type=" << t->type;

    /* don't create transport threads for inaccessible devices */
    if (t->GetConnectionState() != kCsNoPerm) {
        t->connection()->SetTransport(t);

#if ADB_HOST
        if (t->type == kTransportUsb &&
            attached_devices.ShouldStartDetached(*t->connection().get())) {
            VLOG(TRANSPORT) << "Force-detaching transport:" << t->serial;
            t->SetConnectionState(kCsDetached);
        }

        VLOG(TRANSPORT) << "transport:" << t->serial << "(" << state << ")";
        if (t->GetConnectionState() != kCsDetached) {
            VLOG(TRANSPORT) << "Starting transport:" << t->serial;
            if (t->connection()->Start()) {
                send_connect(t);
            } else {
                VLOG(TRANSPORT) << "transport:" << t->serial << " failed to start.";
                return;
            }
        }
#else
        VLOG(TRANSPORT) << "Starting transport:" << t->serial;
        t->connection()->Start();
#endif
    }

    {
        std::lock_guard<std::recursive_mutex> lock(transport_lock);
        auto it = std::find(pending_list.begin(), pending_list.end(), t);
        if (it != pending_list.end()) {
            pending_list.remove(t);
            transport_list.push_front(t);
        }
    }

    update_transports();
}

#if ADB_HOST
void init_reconnect_handler() {
    reconnect_handler.Start();
}
#endif

void kick_all_transports() {
#if ADB_HOST
    reconnect_handler.Stop();
#endif
    // To avoid only writing part of a packet to a transport after exit, kick all transports.
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto t : transport_list) {
        t->Kick();
    }
}

void kick_all_tcp_tls_transports() {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto t : transport_list) {
        if (t->IsTcpDevice() && t->use_tls) {
            t->Kick();
        }
    }
}

#if !ADB_HOST
void kick_all_transports_by_auth_key(std::string_view auth_key) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto t : transport_list) {
        if (auth_key == t->auth_key) {
            t->Kick();
        }
    }
}
#endif

void register_transport(atransport* transport) {
    fdevent_run_on_looper([=]() { fdevent_register_transport(transport); });
}

static void remove_transport(atransport* transport) {
    fdevent_run_on_looper([=]() { fdevent_unregister_transport(transport); });
}

static void transport_destroy(atransport* t) {
    fdevent_check_looper();
    CHECK(t != nullptr);

    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    VLOG(TRANSPORT) << "destroying transport " << t->serial_name();
    t->connection()->Stop();
#if ADB_HOST
    if (t->IsTcpDevice() && !t->kicked()) {
        D("transport: %s destroy (attempting reconnection)", t->serial.c_str());

        // We need to clear the transport's keys, so that on the next connection, it tries
        // again from the beginning.
        t->ResetKeys();
        reconnect_handler.TrackTransport(t);
        return;
    }
#endif

    D("transport: %s destroy (kicking and closing)", t->serial.c_str());
    remove_transport(t);
}

#if ADB_HOST
static int qual_match(const std::string& to_test, const char* prefix, const std::string& qual,
                      bool sanitize_qual) {
    if (to_test.empty()) /* Return true if both the qual and to_test are empty strings. */
        return qual.empty();

    if (qual.empty()) return 0;

    const char* ptr = to_test.c_str();
    if (prefix) {
        while (*prefix) {
            if (*prefix++ != *ptr++) return 0;
        }
    }

    for (char ch : qual) {
        if (sanitize_qual && !isalnum(ch)) ch = '_';
        if (ch != *ptr++) return 0;
    }

    /* Everything matched so far.  Return true if *ptr is a NUL. */
    return !*ptr;
}

// Contains either a device serial string or a USB device address like "usb:2-6"
const char* __transport_server_one_device = nullptr;

void transport_set_one_device(const char* adb_one_device) {
    __transport_server_one_device = adb_one_device;
}

const char* transport_get_one_device() {
    return __transport_server_one_device;
}

bool transport_server_owns_device(std::string_view serial) {
    if (!__transport_server_one_device) {
        // If the server doesn't own one device, server owns all devices.
        return true;
    }
    return serial.compare(__transport_server_one_device) == 0;
}

bool transport_server_owns_device(std::string_view dev_path, std::string_view serial) {
    if (!__transport_server_one_device) {
        // If the server doesn't own one device, server owns all devices.
        return true;
    }
    return serial.compare(__transport_server_one_device) == 0 ||
           dev_path.compare(__transport_server_one_device) == 0;
}

atransport* acquire_one_transport(TransportType type, const char* serial, TransportId transport_id,
                                  bool* is_ambiguous, std::string* error_out,
                                  bool accept_any_state) {
    atransport* result = nullptr;

    if (transport_id != 0) {
        *error_out = android::base::StringPrintf("no device with transport id '%" PRIu64 "'",
                                                 transport_id);
    } else if (serial) {
        *error_out = android::base::StringPrintf("device '%s' not found", serial);
    } else if (type == kTransportLocal) {
        *error_out = "no emulators found";
    } else if (type == kTransportAny) {
        *error_out = "no devices/emulators found";
    } else {
        *error_out = "no devices found";
    }

    std::unique_lock<std::recursive_mutex> lock(transport_lock);
    for (const auto& t : transport_list) {
        if (t->GetConnectionState() == kCsNoPerm) {
            *error_out = UsbNoPermissionsLongHelpText();
            continue;
        }

        if (transport_id) {
            if (t->id == transport_id) {
                result = t;
                break;
            }
        } else if (serial) {
            if (t->MatchesTarget(serial)) {
                if (result) {
                    *error_out = "more than one device with serial "s + serial;
                    if (is_ambiguous) *is_ambiguous = true;
                    result = nullptr;
                    break;
                }
                result = t;
            }
        } else {
            if (type == kTransportUsb && t->type == kTransportUsb) {
                if (result) {
                    *error_out = "more than one USB device";
                    if (is_ambiguous) *is_ambiguous = true;
                    result = nullptr;
                    break;
                }
                result = t;
            } else if (type == kTransportLocal && t->type == kTransportLocal) {
                if (result) {
                    *error_out = "more than one emulator";
                    if (is_ambiguous) *is_ambiguous = true;
                    result = nullptr;
                    break;
                }
                result = t;
            } else if (type == kTransportAny) {
                if (result) {
                    *error_out = "more than one device/emulator";
                    if (is_ambiguous) *is_ambiguous = true;
                    result = nullptr;
                    break;
                }
                result = t;
            }
        }
    }
    lock.unlock();

    if (result && !accept_any_state) {
        // The caller requires an active transport.
        // Make sure that we're actually connected.
        ConnectionState state = result->GetConnectionState();
        switch (state) {
            case kCsConnecting:
                *error_out = "device still connecting";
                result = nullptr;
                break;

            case kCsAuthorizing:
                *error_out = "device still authorizing";
                result = nullptr;
                break;

            case kCsUnauthorized: {
                *error_out = "device unauthorized.\n";
                char* ADB_VENDOR_KEYS = getenv("ADB_VENDOR_KEYS");
                *error_out += "This adb server's $ADB_VENDOR_KEYS is ";
                *error_out += ADB_VENDOR_KEYS ? ADB_VENDOR_KEYS : "not set";
                *error_out += "\n";
                *error_out += "Try 'adb kill-server' if that seems wrong.\n";
                *error_out += "Otherwise check for a confirmation dialog on your device.";
                result = nullptr;
                break;
            }

            case kCsOffline:
                *error_out = "device offline";
                result = nullptr;
                break;

            default:
                break;
        }
    }

    if (result) {
        *error_out = "success";
    }

    return result;
}

bool ConnectionWaitable::WaitForConnection(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    ScopedLockAssertion assume_locked(mutex_);
    return cv_.wait_for(lock, timeout, [&]() REQUIRES(mutex_) {
        return connection_established_ready_;
    }) && connection_established_;
}

void ConnectionWaitable::SetConnectionEstablished(bool success) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_established_ready_) return;
        connection_established_ready_ = true;
        connection_established_ = success;
        D("connection established with %d", success);
    }
    cv_.notify_one();
}
#endif

atransport::~atransport() {
#if ADB_HOST
    // If the connection callback had not been run before, run it now.
    SetConnectionEstablished(false);
#endif
}

int atransport::Write(apacket* p) {
    return this->connection()->Write(std::unique_ptr<apacket>(p)) ? 0 : -1;
}

void atransport::Reset() {
    if (!kicked_.exchange(true)) {
        LOG(INFO) << "resetting transport " << this << " " << this->serial;
        this->connection()->Reset();
    }
}

void atransport::Kick() {
    if (!kicked_.exchange(true)) {
        LOG(INFO) << "kicking transport " << this << " " << this->serial;
        this->connection()->Stop();
    }
}

ConnectionState atransport::GetConnectionState() const {
    return connection_state_;
}

void atransport::SetConnectionState(ConnectionState state) {
    fdevent_check_looper();
    connection_state_ = state;
    update_transports();
}

#if ADB_HOST
bool atransport::Attach(std::string* error) {
    D("%s: attach", serial.c_str());
    fdevent_check_looper();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connection_->SupportsDetach()) {
            *error = "attach/detach not supported";
            return false;
        }
    }

    if (GetConnectionState() != ConnectionState::kCsDetached) {
        *error = android::base::StringPrintf("transport %s is not detached", serial.c_str());
        return false;
    }

    ResetKeys();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connection_->Attach(error)) {
            return false;
        }
    }

    send_connect(this);
    return true;
}

bool atransport::Detach(std::string* error) {
    D("%s: detach", serial.c_str());
    fdevent_check_looper();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connection_->SupportsDetach()) {
            *error = "attach/detach not supported!";
            return false;
        }
    }

    if (GetConnectionState() == ConnectionState::kCsDetached) {
        *error = android::base::StringPrintf("transport %s is already detached", serial.c_str());
        return false;
    }

    handle_offline(this);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connection_->Detach(error)) {
            return false;
        }
    }

    this->SetConnectionState(kCsDetached);
    return true;
}
#endif  // ADB_HOST

void atransport::SetConnection(std::shared_ptr<Connection> connection) {
    std::lock_guard<std::mutex> lock(mutex_);
    connection_ = std::shared_ptr<Connection>(std::move(connection));
}

bool atransport::HandleRead(std::unique_ptr<apacket> p) {
    if (!check_header(p.get(), this)) {
        D("%s: remote read: bad header", serial.c_str());
        return false;
    }

    VLOG(TRANSPORT) << dump_packet(serial.c_str(), "from remote", p.get());
    apacket* packet = p.release();

    // This needs to run on the looper thread since the associated fdevent
    // message pump exists in that context.
    fdevent_run_on_looper([packet, this]() { handle_packet(packet, this); });

    return true;
}

void atransport::HandleError(const std::string& error) {
    LOG(INFO) << serial_name() << ": connection terminated: " << error;
    fdevent_run_on_looper([this]() {
        handle_offline(this);
        transport_destroy(this);
    });
}

void atransport::update_version(int version, size_t payload) {
    protocol_version = std::min(version, A_VERSION);
    max_payload = std::min(payload, MAX_PAYLOAD);
}

int atransport::get_protocol_version() const {
    return protocol_version;
}

int atransport::get_tls_version() const {
    return tls_version;
}

size_t atransport::get_max_payload() const {
    return max_payload;
}

#if ADB_HOST
bool burst_mode_enabled() {
    static const char* env = getenv("ADB_BURST_MODE");
    static bool result = env && strcmp(env, "1") == 0;
    return result;
}
#endif

const FeatureSet& supported_features() {
    static const android::base::NoDestructor<FeatureSet> features([]() {
        // Increment ADB_SERVER_VERSION when adding a feature that adbd needs
        // to know about. Otherwise, the client can be stuck running an old
        // version of the server even after upgrading their copy of adb.
        // (http://b/24370690)

        // clang-format off
        FeatureSet result {
            kFeatureShell2,
            kFeatureCmd,
            kFeatureStat2,
            kFeatureLs2,
            kFeatureFixedPushMkdir,
            kFeatureApex,
            kFeatureAbb,
            kFeatureFixedPushSymlinkTimestamp,
            kFeatureAbbExec,
            kFeatureRemountShell,
            kFeatureTrackApp,
            kFeatureSendRecv2,
            kFeatureSendRecv2Brotli,
            kFeatureSendRecv2LZ4,
            kFeatureSendRecv2Zstd,
            kFeatureSendRecv2DryRunSend,
            kFeatureOpenscreenMdns,
            kFeatureDeviceTrackerProtoFormat,
            kFeatureDevRaw,
            kFeatureAppInfo,
            kFeatureServerStatus,
            kFeatureTrackMdns,
        };
        // clang-format on

#if ADB_HOST
        if (burst_mode_enabled()) {
            result.push_back(kFeatureDelayedAck);
        }
#else
        result.push_back(kFeatureDelayedAck);
#endif
        return result;
    }());

    return *features;
}

std::string FeatureSetToString(const FeatureSet& features) {
    return android::base::Join(features, ',');
}

FeatureSet StringToFeatureSet(const std::string& features_string) {
    if (features_string.empty()) {
        return FeatureSet();
    }

    return android::base::Split(features_string, ",");
}

template <class Range, class Value>
static bool contains(const Range& r, const Value& v) {
    return std::find(std::begin(r), std::end(r), v) != std::end(r);
}

bool CanUseFeature(const FeatureSet& feature_set, const std::string& feature) {
    return contains(feature_set, feature) && contains(supported_features(), feature);
}

bool atransport::has_feature(const std::string& feature) const {
    return contains(features_, feature);
}

void atransport::SetFeatures(const std::string& features_string) {
    features_ = StringToFeatureSet(features_string);
    delayed_ack_ = CanUseFeature(features_, kFeatureDelayedAck);
}

void atransport::AddDisconnect(adisconnect* disconnect) {
    disconnects_.push_back(disconnect);
}

void atransport::RemoveDisconnect(adisconnect* disconnect) {
    disconnects_.remove(disconnect);
}

void atransport::RunDisconnects() {
    for (const auto& disconnect : disconnects_) {
        disconnect->func(disconnect->opaque, this);
    }
    disconnects_.clear();
}

#if ADB_HOST
bool atransport::MatchesTarget(const std::string& target) const {
    if (!serial.empty()) {
        if (target == serial) {
            return true;
        } else if (type == kTransportLocal) {
            // Local transports can match [tcp:|udp:]<hostname>[:port].
            const char* local_target_ptr = target.c_str();

            // For fastboot compatibility, ignore protocol prefixes.
            if (android::base::StartsWith(target, "tcp:") ||
                android::base::StartsWith(target, "udp:")) {
                local_target_ptr += 4;
            }

            // Parse our |serial| and the given |target| to check if the hostnames and ports match.
            std::string serial_host, error;
            int serial_port = -1;
            if (android::base::ParseNetAddress(serial, &serial_host, &serial_port, nullptr,
                                               &error)) {
                // |target| may omit the port to default to ours.
                std::string target_host;
                int target_port = serial_port;
                if (android::base::ParseNetAddress(local_target_ptr, &target_host, &target_port,
                                                   nullptr, &error) &&
                    serial_host == target_host && serial_port == target_port) {
                    return true;
                }
            }
        }
    }

    return (target == devpath) || qual_match(target, "product:", product, false) ||
           qual_match(target, "model:", model, true) ||
           qual_match(target, "device:", device, false);
}

void atransport::SetConnectionEstablished(bool success) {
    connection_waitable_->SetConnectionEstablished(success);
}

ReconnectResult atransport::Reconnect() {
    return reconnect_(this);
}

// We use newline as our delimiter, make sure to never output it.
static std::string sanitize(std::string str, bool alphanumeric) {
    auto pred = alphanumeric ? [](const char c) { return !isalnum(c); }
                             : [](const char c) { return c == '\n'; };
    std::replace_if(str.begin(), str.end(), pred, '_');
    return str;
}

static adb::proto::ConnectionState adbStateFromProto(ConnectionState state) {
    switch (state) {
        case kCsConnecting:
            return adb::proto::ConnectionState::CONNECTING;
        case kCsAuthorizing:
            return adb::proto::ConnectionState::AUTHORIZING;
        case kCsUnauthorized:
            return adb::proto::ConnectionState::UNAUTHORIZED;
        case kCsNoPerm:
            return adb::proto::ConnectionState::NOPERMISSION;
        case kCsDetached:
            return adb::proto::ConnectionState::DETACHED;
        case kCsOffline:
            return adb::proto::ConnectionState::OFFLINE;
        case kCsBootloader:
            return adb::proto::ConnectionState::BOOTLOADER;
        case kCsDevice:
            return adb::proto::ConnectionState::DEVICE;
        case kCsHost:
            return adb::proto::ConnectionState::HOST;
        case kCsRecovery:
            return adb::proto::ConnectionState::RECOVERY;
        case kCsSideload:
            return adb::proto::ConnectionState::SIDELOAD;
        case kCsRescue:
            return adb::proto::ConnectionState::RESCUE;
        case kCsAny:
            return adb::proto::ConnectionState::ANY;
    }
}

static std::string transportListToProto(const std::list<atransport*>& sorted_transport_list,
                                        bool text_version) {
    adb::proto::Devices devices;
    for (const auto& t : sorted_transport_list) {
        auto* device = devices.add_device();
        device->set_serial(t->serial.c_str());
        device->set_connection_type(t->type == kTransportUsb ? adb::proto::ConnectionType::USB
                                                             : adb::proto::ConnectionType::SOCKET);
        device->set_state(adbStateFromProto(t->GetConnectionState()));
        device->set_bus_address(sanitize(t->devpath, false));
        device->set_product(sanitize(t->product, false));
        device->set_model(sanitize(t->model, true));
        device->set_device(sanitize(t->device, false));
        device->set_negotiated_speed(t->connection()->NegotiatedSpeedMbps());
        device->set_max_speed(t->connection()->MaxSpeedMbps());
        device->set_transport_id(t->id);
    }

    std::string proto;
    if (text_version) {
        google::protobuf::TextFormat::PrintToString(devices, &proto);
    } else {
        devices.SerializeToString(&proto);
    }
    return proto;
}

static void append_transport_info(std::string* result, const char* key, const std::string& value,
                                  bool alphanumeric) {
    if (value.empty()) {
        return;
    }

    *result += ' ';
    *result += key;
    *result += sanitize(value, alphanumeric);
}

static void append_transport(const atransport* t, std::string* result, bool long_listing) {
    std::string serial = t->serial;
    if (serial.empty()) {
        serial = "(no serial number)";
    }

    if (!long_listing) {
        *result += serial;
        *result += '\t';
        *result += to_string(t->GetConnectionState());
    } else {
        android::base::StringAppendF(result, "%-22s %s", serial.c_str(),
                                     to_string(t->GetConnectionState()).c_str());

        append_transport_info(result, "", t->devpath, false);
        append_transport_info(result, "product:", t->product, false);
        append_transport_info(result, "model:", t->model, true);
        append_transport_info(result, "device:", t->device, false);

        // Put id at the end, so that anyone parsing the output here can always find it by scanning
        // backwards from newlines, even with hypothetical devices named 'transport_id:1'.
        *result += " transport_id:";
        *result += std::to_string(t->id);
    }
    *result += '\n';
}

static std::string transportListToText(const std::list<atransport*>& sorted_transport_list,
                                       bool long_listing) {
    std::string result;
    for (const auto& t : sorted_transport_list) {
        append_transport(t, &result, long_listing);
    }
    return result;
}

std::string list_transports(TrackerOutputType outputType) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);

    auto sorted_transport_list = transport_list;
    sorted_transport_list.sort([](atransport*& x, atransport*& y) {
        if (x->type != y->type) {
            return x->type < y->type;
        }
        return x->serial < y->serial;
    });

    switch (outputType) {
        case SHORT_TEXT:
        case LONG_TEXT: {
            return transportListToText(sorted_transport_list, outputType == LONG_TEXT);
        }
        case PROTOBUF:
        case TEXT_PROTOBUF: {
            return transportListToProto(sorted_transport_list, outputType == TEXT_PROTOBUF);
        }
    }
}

void close_usb_devices(std::function<bool(const atransport*)> predicate, bool reset) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto& t : transport_list) {
        if (predicate(t)) {
            if (reset) {
                t->Reset();
            } else {
                t->Kick();
            }
        }
    }
}

/* hack for osx */
void close_usb_devices(bool reset) {
    close_usb_devices([](const atransport*) { return true; }, reset);
}
#endif

bool validate_transport_list(const std::list<atransport*>& list, const std::string& serial,
                             atransport* t, int* error) {
    for (const auto& transport : list) {
        if (serial == transport->serial) {
            const std::string list_name(&list == &pending_list ? "pending" : "transport");
            VLOG(TRANSPORT) << "socket transport " << transport->serial << " is already in the "
                            << list_name << " list and fails to register";
            delete t;
            if (error) *error = EALREADY;
            return false;
        }
    }
    return true;
}

bool register_socket_transport(unique_fd s, std::string serial, int port, bool is_emulator,
                               atransport::ReconnectCallback reconnect, bool use_tls, int* error) {
#if ADB_HOST
    // Below in this method, we block up to 10s on the waitable. This should never run on the
    // fdevent thread.
    fdevent_check_not_looper();
#endif

    atransport* t = new atransport(kTransportLocal, std::move(reconnect), kCsOffline);
    t->use_tls = use_tls;
    t->serial = std::move(serial);

    D("transport: %s init'ing for socket %d, on port %d", t->serial.c_str(), s.get(), port);
    if (init_socket_transport(t, std::move(s), port, is_emulator) < 0) {
        delete t;
        if (error) *error = errno;
        return false;
    }

    std::unique_lock<std::recursive_mutex> lock(transport_lock);
    if (!validate_transport_list(pending_list, t->serial, t, error)) {
        return false;
    }

    if (!validate_transport_list(transport_list, t->serial, t, error)) {
        return false;
    }

    pending_list.push_front(t);

    lock.unlock();

#if ADB_HOST
    auto waitable = t->connection_waitable();
#endif
    register_transport(t);

    if (is_emulator) {
        return true;
    }

#if ADB_HOST
    if (!waitable->WaitForConnection(std::chrono::seconds(10))) {
        if (error) *error = ETIMEDOUT;
        return false;
    }

    if (t->GetConnectionState() == kCsUnauthorized) {
        if (error) *error = EPERM;
        return false;
    }
#endif

    return true;
}

#if ADB_HOST
atransport* find_transport(const char* serial) {
    atransport* result = nullptr;

    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto& t : transport_list) {
        if (strcmp(serial, t->serial.c_str()) == 0) {
            result = t;
            break;
        }
    }

    return result;
}

void kick_all_tcp_devices() {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto& t : transport_list) {
        if (t->IsTcpDevice()) {
            // Kicking breaks the read_transport thread of this transport out of any read, then
            // the read_transport thread will notify the main thread to make this transport
            // offline. Then the main thread will notify the write_transport thread to exit.
            // Finally, this transport will be closed and freed in the main thread.
            t->Kick();
        }
    }
    reconnect_handler.CheckForKicked();
}

#if ADB_HOST
void register_libusb_transport(std::shared_ptr<Connection> connection, const char* serial,
                               const char* devpath, unsigned writeable) {
    atransport* t = new atransport(kTransportUsb, writeable ? kCsOffline : kCsNoPerm);
    if (serial) {
        t->serial = serial;
    }
    if (devpath) {
        t->devpath = devpath;
    }

    t->SetConnection(std::move(connection));

    {
        std::lock_guard<std::recursive_mutex> lock(transport_lock);
        pending_list.push_front(t);
    }

    register_transport(t);
}

void register_usb_transport(usb_handle* usb, const char* serial, const char* devpath,
                            unsigned writeable) {
    atransport* t = new atransport(kTransportUsb, writeable ? kCsOffline : kCsNoPerm);

    D("transport: %p init'ing for usb_handle %p (sn='%s')", t, usb, serial ? serial : "");
    init_usb_transport(t, usb);
    if (serial) {
        t->serial = serial;
    }

    if (devpath) {
        t->devpath = devpath;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(transport_lock);
        pending_list.push_front(t);
    }

    register_transport(t);
}

// This should only be used for transports with connection_state == kCsNoPerm.
void unregister_usb_transport(usb_handle* usb) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    transport_list.remove_if([usb](atransport* t) {
        return t->GetUsbHandle() == usb && t->GetConnectionState() == kCsNoPerm;
    });
}
#endif

// Track reverse:forward commands, so that info can be used to develop
// an 'allow-list':
//   - adb reverse tcp:<device_port> localhost:<host_port> : responds with the
//   device_port
//   - adb reverse --remove tcp:<device_port> : responds OKAY
//   - adb reverse --remove-all : responds OKAY
void atransport::UpdateReverseConfig(std::string_view service_addr) {
    fdevent_check_looper();
    if (!android::base::ConsumePrefix(&service_addr, "reverse:")) {
        return;
    }

    if (android::base::ConsumePrefix(&service_addr, "forward:")) {
        // forward:[norebind:]<remote>;<local>
        bool norebind = android::base::ConsumePrefix(&service_addr, "norebind:");
        auto it = service_addr.find(';');
        if (it == std::string::npos) {
            return;
        }
        std::string remote(service_addr.substr(0, it));

        if (norebind && reverse_forwards_.contains(remote)) {
            // This will fail, don't update the map.
            LOG(DEBUG) << "ignoring reverse forward that will fail due to norebind";
            return;
        }

        std::string local(service_addr.substr(it + 1));
        reverse_forwards_[remote] = local;
    } else if (android::base::ConsumePrefix(&service_addr, "killforward:")) {
        // kill-forward:<remote>
        auto it = service_addr.find(';');
        if (it != std::string::npos) {
            return;
        }
        reverse_forwards_.erase(std::string(service_addr));
    } else if (service_addr == "killforward-all") {
        reverse_forwards_.clear();
    } else if (service_addr == "list-forward") {
        LOG(DEBUG) << __func__ << " ignoring --list";
    } else {  // Anything else we need to know about?
        LOG(FATAL) << "unhandled reverse service: " << service_addr;
    }
}

// Is this an authorized :connect request?
bool atransport::IsReverseConfigured(const std::string& local_addr) {
    fdevent_check_looper();
    for (const auto& [remote, local] : reverse_forwards_) {
        if (local == local_addr) {
            return true;
        }
    }
    return false;
}

#endif

bool check_header(apacket* p, atransport* t) {
    if (p->msg.magic != (p->msg.command ^ 0xffffffff)) {
        VLOG(RWX) << "check_header(): invalid magic command = " << std::hex << p->msg.command
                  << ", magic = " << p->msg.magic;
        return false;
    }

    if (p->msg.data_length > t->get_max_payload()) {
        VLOG(RWX) << "check_header(): " << p->msg.data_length
                  << " atransport::max_payload = " << t->get_max_payload();
        return false;
    }

    return true;
}

#if ADB_HOST
std::shared_ptr<RSA> atransport::Key() {
    if (keys_.empty()) {
        return nullptr;
    }

    std::shared_ptr<RSA> result = keys_[0];
    return result;
}

std::shared_ptr<RSA> atransport::NextKey() {
    if (keys_.empty()) {
        VLOG(ADB) << "fetching keys for transport " << this->serial_name();
        keys_ = adb_auth_get_private_keys();

        // We should have gotten at least one key: the one that's automatically generated.
        CHECK(!keys_.empty());
    } else {
        keys_.pop_front();
    }

    return Key();
}

void atransport::ResetKeys() {
    keys_.clear();
}
#endif

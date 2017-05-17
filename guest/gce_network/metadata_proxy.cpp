/*
 * Copyright (C) 2016 The Android Open Source Project
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
#define LOG_TAG "GceMetadataProxy"

#include "metadata_proxy.h"

#include <dlfcn.h>
#include <list>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>
#include <cutils/klog.h>
#include <json/json.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <AutoResources.h>
#include <GceMetadataAttributes.h>
#include <GceResourceLocation.h>
#include <Pthread.h>
#include <SharedFD.h>
#include <SharedSelect.h>
#include <Thunkers.h>


namespace avd {
namespace {
const int64_t kEventValue = 1;

size_t WriteToBuffer(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t realsize = size * nmemb;
  if (userp) {
    AutoFreeBuffer* buffer = static_cast<AutoFreeBuffer*>(userp);
    buffer->Append(static_cast<const char*>(contents), realsize);
  }
  return realsize;
}

bool ExtractETag(const AutoFreeBuffer* headers, AutoFreeBuffer* etag) {
  if (!headers->size()) {
    return false;
  }
  static const char kETagPrefix[] = "ETag: ";
  // Ignore trailing \0.
  static const size_t kETagLength = sizeof(kETagPrefix) - 1;

  // WARNING: buffer does not have to be null terminated.
  const char* header_start = headers->data();
  const char* etag_start = header_start;
  const char* header_end = headers->data() + headers->size();
  etag->Clear();
  while (true) {
    etag_start = static_cast<const char*>(
        memmem(etag_start, header_end - etag_start, kETagPrefix, kETagLength));

    if (etag_start == NULL) {
      KLOG_ERROR(LOG_TAG, "Failed to find ETag in headers!\n");
      return false;
    }
    // Verify that there is a newline or a carriage return just before the
    // ETag. If they're missing this a a spurious match and we should keep
    // looking.
    if ((etag_start != header_start) &&
        (etag_start[-1] != '\r') &&
        (etag_start[-1] != '\n')) {
      KLOG_ERROR(LOG_TAG, "False match on etag header!\n");
      etag_start++;
      continue;
    }
    break;
  }

  etag_start += kETagLength;
  const char* etag_end = static_cast<const char*>(
      memmem(etag_start, header_end - etag_start, "\r\n", 2));

  if (etag_end == NULL) {
    KLOG_ERROR(LOG_TAG, "Could not find header line terminator!\n");
    etag_end = header_end;
  }

  size_t etag_length = etag_end - etag_start;
  etag->Resize(etag_length);
  memmove(etag->data(), etag_start, etag_length);
  // Append trailing \0.
  etag->Resize(etag_length + 1);
  return true;
}

bool Check(CURLcode code, const char* message, const char* details) {
  if (code == CURLE_OK) return false;
  KLOG_ERROR(LOG_TAG, "%s (%s): %s\n",
             message, curl_easy_strerror(code), details);
  return true;
}

// Save initial metadata response from GCE metadata server to file.
// We care about saving file on a partition that already exists and is
// readable and writable.
// The file is mostly important for debugging purposes.
// Keep both headers and content.
bool SaveInitialMetadata(
    const AutoFreeBuffer& headers, const AutoFreeBuffer& content) {
  AutoCloseFileDescriptor fd(
      open(GceResourceLocation::kInitialMetadataPath,
           O_CREAT|O_TRUNC|O_WRONLY, 0600));

  if (fd.IsError()) {
    KLOG_ERROR(LOG_TAG, "Failed to create initial metadata file (%s)\n",
               strerror(errno));
    return false;
  }

  if (TEMP_FAILURE_RETRY(write(fd, headers.data(), headers.size()))
      != static_cast<int>(headers.size())) {
    KLOG_ERROR(LOG_TAG,
               "Failed to write %zu bytes to initial metadata file: %d(%s)\n",
               headers.size(), errno, strerror(errno));
    return false;
  }

  if (TEMP_FAILURE_RETRY(write(fd, content.data(), content.size()))
      != static_cast<int>(content.size())) {
    KLOG_ERROR(LOG_TAG,
               "Failed to write %zu bytes to initial metadata file: %d(%s)\n",
               content.size(), errno, strerror(errno));
    return false;
  }

  KLOG_INFO(LOG_TAG, "Successfully stored %zu bytes in initial.metadata file.\n",
        headers.size() + content.size());

  fd.close();
  // So that any HAL instances can read the initial config.
  chmod(GceResourceLocation::kInitialMetadataPath, 0644);
  return true;
}


// Implementation of MetadataProxy interface.
// Starts a background threads polling the GCE Metadata Server for updates.
// Notifies all connected clients about metadata updates.
class MetadataProxyImpl : public MetadataProxy {
 public:
  MetadataProxyImpl(
      SysClient* client, NetworkNamespaceManager* ns_manager)
      : client_(client),
        ns_manager_(ns_manager) {}

  ~MetadataProxyImpl() {}

  bool Fetch(AutoFreeBuffer* headers, AutoFreeBuffer* result) {
    CURL* curl = curl_easy_init();
    if (!curl) {
      KLOG_ERROR(LOG_TAG, "curl_easy_init returned NULL\n");
      return false;
    }

    AutoFreeBuffer url;
    if (last_etag_.size()) {
      url.PrintF(
          "http://169.254.169.254/computeMetadata/v1/?"
          "recursive=true&timeout_sec=30&wait_for_change=true&last_etag=%s",
          last_etag_.data());
    } else {
      KLOG_INFO(LOG_TAG,
                "Fetching metadata without etag. This should happen once\n");
      url.PrintF(
          "http://169.254.169.254/computeMetadata/v1/?"
          "recursive=true&timeout_sec=30");
    }

    headers->Resize(0);
    result->Resize(0);

    struct curl_slist* list = curl_slist_append(NULL, "Metadata-Flavor: Google");
    list = curl_slist_append(list, "Accept: */*");

    char* curl_error_buffer = new char[CURL_ERROR_SIZE];
    curl_error_buffer[0] = 0;

    long respcode = 0;

    if (Check(curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error_buffer),
              "Failed to set ERRORBUFFER", curl_error_buffer) ||
        Check(curl_easy_setopt(curl, CURLOPT_URL, url.data()),
              "Failed to set URL", curl_error_buffer) ||
        Check(curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list),
              "Failed to set headers", curl_error_buffer) ||
        Check(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToBuffer),
              "Failed to set writer", curl_error_buffer) ||
        Check(curl_easy_setopt(curl, CURLOPT_WRITEDATA, result),
              "Failed to set writedata", curl_error_buffer) ||
        Check(curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteToBuffer),
              "Failed to set headerfunction", curl_error_buffer) ||
        Check(curl_easy_setopt(curl, CURLOPT_HEADERDATA, headers),
              "Failed to set headerdata", curl_error_buffer) ||
        Check(curl_easy_perform(curl),
              "curl_easy_perform_failed", curl_error_buffer) ||
        Check(curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &respcode),
              "Failed to get response code", curl_error_buffer)) {
      curl_slist_free_all(list);
      curl_easy_cleanup(curl);
      delete[] curl_error_buffer;

      return false;
    }

    curl_slist_free_all(list);
    if (respcode != 200) {
      KLOG_ERROR(LOG_TAG, "Could not reach out to metadata server: %ld\n",
                 respcode);
    }

    curl_easy_cleanup(curl);
    delete[] curl_error_buffer;
    return (respcode == 200);
  }

  bool IssueMetadataServerQuery(AutoFreeBuffer* headers,
                                AutoFreeBuffer* buffer,
                                Json::Value* root) {
    bool result = Fetch(headers, buffer);
    if (!result) return false;

    AutoFreeBuffer new_etag;
    ExtractETag(headers, &new_etag);
    if (new_etag != last_etag_) {
      KLOG_INFO(LOG_TAG, "Processing metadata for etag %s\n", new_etag.data());
      last_etag_.Swap(new_etag);
    }

    Json::Reader reader;
    // Avoid overflows.
    if (!reader.parse(
        buffer->data(), buffer->data() + buffer->size(),
        *root,
        // Don't collect comments.
        false)) {
      KLOG_ERROR(LOG_TAG, "Could not parse metadata: %s\n",
                 reader.getFormattedErrorMessages().c_str());
      return false;
    }

    return true;
  }

  // Background thread polling for updates from GCE metadata server.
  void* MetadataClientThread() {
    AutoFreeBuffer headers;
    AutoFreeBuffer new_metadata;
    int backoff_time_ms = 1000;

    KLOG_INFO(LOG_TAG, "%s: fetcher starting on pid %d tid %d\n", __FUNCTION__,
              (int)getpid(), (int)gettid());
    while (true) {
      // Read update from metadata server.
      Json::Value new_root;
      if (!IssueMetadataServerQuery(&headers, &new_metadata, &new_root)) {
        KLOG_INFO(LOG_TAG, "Metadata server returned an error. "
                  "Will try again in %d seconds.\n", backoff_time_ms / 1000);
        sleep(backoff_time_ms / 1000);
        backoff_time_ms *= 1.5;
        if (backoff_time_ms > 30000) backoff_time_ms = 30000;
        continue;
      }

      {
        // Update local copy of the metadata.
        avd::LockGuard<avd::Mutex> guard(metadata_mutex_);
        metadata_.Swap(new_metadata);
        json_current_metadata_.swap(new_root);
      }

      // Notify master thread of the update.
      new_metadata_event_->Write(&kEventValue, sizeof(kEventValue));
    }
    return NULL;
  }

  // Send metadata update to specified clients.
  //
  // Returns true, if sending metadata was successful.
  bool SendMetadata(SharedFD client, AutoFreeBuffer& metadata) {
    int32_t length = metadata.size();
    // Do we have anything to send?
    // If not, we probably failed to fetch update from metadata server.
    if (length == 0) return true;

    if ((client->Send(&length, sizeof(length), MSG_NOSIGNAL) < 0) ||
        (client->Send(metadata.data(), length, MSG_NOSIGNAL) < 0)) {
      KLOG_WARNING(LOG_TAG, "Dropping metadata client: write error %d (%s)\n",
                   errno, strerror(errno));
      return false;
    }
    return true;
  }


  // Broadcast metadata update to all clients.
  void BroadcastMetadataUpdate() {
    // Include testing attributes if instance is configured to allow this.
    IncludeTestingAttributesIfTestingEnabled();

    // Propagate metadata update to all connected clients.
    avd::LockGuard<avd::Mutex> guard(metadata_mutex_);
    KLOG_DEBUG(LOG_TAG, "Metadata update received, sending updates.\n");
    for (std::list<SharedFD>::iterator client = clients_.begin();
         client != clients_.end(); ++client) {
      SendMetadata(*client, metadata_);
    }
  }


  // Checks whether metadata injections are permitted.
  // Returns true if initial metadata reports testing as permitted.
  bool IsMetadataInjectionAllowed() {
    static enum {
      UNKNOWN = -1,
      DISABLED,
      ENABLED,
    } testModeState = UNKNOWN;

    if (testModeState == UNKNOWN) {
      Json::Value testModeConfig = json_initial_metadata_
          ["instance"]["attributes"]["cfg_test_allow_metadata_injections"];

      if (!testModeConfig.isString()) {
        testModeConfig = json_initial_metadata_
            ["project"]["attributes"]["cfg_test_allow_metadata_injections"];
      }

      if (!testModeConfig.isString()) {
        testModeState = DISABLED;
      } else {
        testModeState = testModeConfig.asString() ==
            "true" ? ENABLED : DISABLED;
      }
    }

    return testModeState == ENABLED;
  }


  // If injection is allowed, include [testing][attributes] in metadata json
  // object. This metadata MAY or MAY NOT be selected by components.
  void IncludeTestingAttributesIfTestingEnabled() {
    if (!IsMetadataInjectionAllowed()) return;

    avd::LockGuard<avd::Mutex> guard(metadata_mutex_);
    json_current_metadata_["testing"]["attributes"] = json_testing_attributes_;

    Json::FastWriter writer;
    std::string new_metadata = writer.write(json_current_metadata_);
    metadata_.SetToString(new_metadata.c_str());
  }


  // Read metadata updates coming from inside of Android.
  // The one and only purpose of this is to perform TESTING of Android device.
  //
  // Metadata updates are allowed only if kTestAllowMetadataInjectionsKey is set
  // to 'true'.
  bool ReadLocalMetadataUpdate(SharedFD client) {
    // Drop connections from anyone trying to modify metadata if not permitted.
    if (!IsMetadataInjectionAllowed()) return false;

    int32_t length;
    if (sizeof(length) != client->Recv(&length, sizeof(length), MSG_NOSIGNAL)) {
      // Client likely disconnected. Stay quiet.
      return false;
    }

    // Length accepted here is arbitrary, but 2k is already much more than we
    // should use for testing.
    if (length > 2048 || length <= 0) {
      KLOG_WARNING(LOG_TAG, "Unexpected metadata update size: %d\n", length);
      return false;
    }

    AutoFreeBuffer b(length);
    if (length != client->Recv(b.data(), length, MSG_NOSIGNAL)) {
      KLOG_WARNING(LOG_TAG, "Could not read metadata update.\n");
      return false;
    }

    // Process incoming metadata. Avoid overflows.
    Json::Value update;
    Json::Reader reader;
    if (!reader.parse(b.data(), b.data() + length, update, false)) {
      KLOG_ERROR(LOG_TAG, "Could not parse metadata update: %s\n",
                 reader.getFormattedErrorMessages().c_str());
      return false;
    }

    // All attributes will be placed under [testing][attributes].
    // Components are not required to reference these attributes.
    if (!update.isObject()) {
      KLOG_WARNING(LOG_TAG, "No instance attributes found in update.\n");
      return true;
    }

    // ONLY SUPPORT string attributes for local updates.
    // NO nesting.
    // NOTE ON THREADING
    // Updating testing attributes is done from within the same thread as
    // accessing them. This removes the requirement for locking any mutexes.
    json_testing_attributes_ = Json::objectValue;
    for (Json::ValueIterator iter = update.begin();
         iter != update.end(); iter++) {
      if (iter->isString()) {
        json_testing_attributes_[iter.memberName()] = iter->asString();
      }
    }

    BroadcastMetadataUpdate();

    return true;
  }


  int StartProxy(const std::string& socket_name) {
    // Current metadata is the same as initial metadata.
    metadata_.Append(initial_metadata_.data(), initial_metadata_.size());

    // Create new eventfd to receive notifications about updates from
    // metadata fetcher thread.
    new_metadata_event_ = SharedFD::Event();

    // Boot up the thread that will fetch metadata updates.
    avd::ScopedThread fetch_thread(
        &ThunkerBase<void, MetadataProxyImpl, void*()>
        ::call<&MetadataProxyImpl::MetadataClientThread>, this);

    SharedFD server_sock = CreateServerSocket(socket_name);

    KLOG_INFO(LOG_TAG, "Starting metadata proxy service. Listening on @%s.\n",
              socket_name.c_str());

    while(true) {
      // Wait for signal from either server socket (that new client has
      // connected), or any of the client sockets, indicating clients have
      // closed their end.
      SharedFDSet wait_set;
      wait_set.Set(server_sock);
      wait_set.Set(new_metadata_event_);
      for (std::list<SharedFD>::iterator client = clients_.begin();
           client != clients_.end(); ++client) {
        wait_set.Set(*client);
      }
      Select(&wait_set, NULL, NULL, NULL);

      // Check if new client connected.
      if (wait_set.IsSet(server_sock)) {
        AcceptNewClient(server_sock);
      }

      // Check if receiven metadata update.
      if (wait_set.IsSet(new_metadata_event_)) {
        // Clear event.
        int64_t dummy;
        new_metadata_event_->Read(&dummy, sizeof(dummy));
        BroadcastMetadataUpdate();
      }

      // Detect existing client disconnected.
      for (std::list<SharedFD>::iterator client = clients_.begin();
           client != clients_.end();) {
        if (wait_set.IsSet(*client)) {
          if (!ReadLocalMetadataUpdate(*client)) {
            KLOG_INFO(LOG_TAG, "Metadata proxy client disconnected.\n");
            client = clients_.erase(client);
          }
        } else {
          ++client;
        }
      }
    }
    // Not reached, but we need a return value for some compilers.
    return 0;
  }

  // Metadata proxy process body.
  // Accepts new clients of metadata proxy socket named |socket_name|.
  // Upon connection sends two complete metadata updates:
  // - initial metadata (first update ever),
  // - current metadata (may be same as initial metadata).
  bool Start(const std::string& socket_name) {
    // Fetch initial metadata before anything else happens.
    {
      AutoFreeBuffer initial_headers;
      if (!IssueMetadataServerQuery(
          &initial_headers, &initial_metadata_, &json_initial_metadata_)) {
        // Should that be 'VIRTUAL_DEVICE_BOOT_FAILED'?
        KLOG_ERROR(LOG_TAG, "COULD NOT PROCESS INITIAL METADATA!\n");
      }

      SaveInitialMetadata(initial_headers, initial_metadata_);
    }
    // getpid() and gettid() return 1 on this thread, so fork to get the process
    // into a saner state. Use this thread to monitor the child and restart it.
    while (true) {
      SysClient::ProcessHandle* h = client_->Clone(
          "gce.meta.proxy",
          ::avd::Callback<int()>(&MetadataProxyImpl::StartProxy, this, socket_name), 0);
      h->WaitResult();

      // Wait a bit so we done flood with forks
      sleep(5);
    }
    return false;
  }

  // Create new server socket in an Android network namespace.
  //
  // Unix domain sockets are associated with network namespaces.
  // This means that an unix socket created in namespace A will not be
  // accessible from namespace B.
  //
  // Now that the metadata client thread has started, reparent this thread, so
  // that the unix socket is created in android namespace.
  SharedFD CreateServerSocket(const std::string& socket_name) {
    if (client_->SetNs(
        ns_manager_->GetNamespaceDescriptor(
            NetworkNamespaceManager::kAndroidNs), kCloneNewNet) < 0) {
      KLOG_ERROR(LOG_TAG, "%s: Failed to switch namespace: %s\n",
                 __FUNCTION__, strerror(errno));
      return SharedFD();
    }

    // Start listening for metadata updates.
    SharedFD server_sock = SharedFD::SocketLocalServer(
        socket_name.c_str(), ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);

    if (!server_sock->IsOpen()) {
      KLOG_ERROR(LOG_TAG, "Failed to start local server %s: %d (%s).\n",
                 socket_name.c_str(), errno, strerror(errno));
      return SharedFD();
    }

    // Return to original network namespace.
    client_->SetNs(
        ns_manager_->GetNamespaceDescriptor(
            NetworkNamespaceManager::kOuterNs), kCloneNewNet);

    return server_sock;
  }

  // Accept new client connection on supplied server socket.
  void AcceptNewClient(SharedFD server_sock) {
    SharedFD client_sock = SharedFD::Accept(*server_sock);

    if (!client_sock->IsOpen()) {
      KLOG_WARNING(LOG_TAG, "Metadata proxy failed to connect new client.\n");
      return;
    }

    KLOG_INFO(LOG_TAG, "Accepted new metadata proxy client.\n");

    avd::LockGuard<avd::Mutex> guard(metadata_mutex_);
    // Append client to all clients, if we could successfully send initial
    // update.
    if (SendMetadata(client_sock, initial_metadata_) &&
        SendMetadata(client_sock, metadata_)) {
      clients_.push_back(client_sock);
    }
  }

 private:
  SysClient* const client_;
  NetworkNamespaceManager* const ns_manager_;

  SharedFD new_metadata_event_;

  Json::Value json_initial_metadata_;
  Json::Value json_current_metadata_;
  Json::Value json_testing_attributes_;

  AutoFreeBuffer initial_metadata_;
  AutoFreeBuffer metadata_;
  AutoFreeBuffer last_etag_;
  avd::Mutex metadata_mutex_;

  std::list<SharedFD> clients_;

  MetadataProxyImpl(const MetadataProxyImpl&);
  MetadataProxyImpl& operator= (const MetadataProxyImpl&);
};

}  // namespace

MetadataProxy* MetadataProxy::New(
    SysClient* client,
    NetworkNamespaceManager* ns_manager) {
  return new MetadataProxyImpl(client, ns_manager);
}

}  // namespace avd

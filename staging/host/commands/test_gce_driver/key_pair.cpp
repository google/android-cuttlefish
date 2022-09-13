//
// Copyright (C) 2021 The Android Open Source Project
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

#include "host/commands/test_gce_driver/key_pair.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <memory>
#include <string>

#include <android-base/logging.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"

namespace cuttlefish {

static int SslRecordErrCallback(const char* str, size_t len, void* data) {
  *reinterpret_cast<std::string*>(data) = std::string(str, len);
  return 1;  // success
}

class BoringSslKeyPair : public KeyPair {
 public:
  /*
   * We interact with boringssl directly here to avoid ssh-keygen writing
   * directly to the filesystem. The relevant ssh-keygen command here is
   *
   * $ ssh-keygen -t rsa -N "" -f ${TARGET}
   *
   * which unfortunately tries to write to `${TARGET}.pub`, making it hard to
   * use something like /dev/stdout or /proc/self/fd/1 to get the keys.
   */
  static Result<std::unique_ptr<KeyPair>> CreateRsa(size_t bytes) {
    std::unique_ptr<EVP_PKEY_CTX, void (*)(EVP_PKEY_CTX*)> ctx{
        EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL), EVP_PKEY_CTX_free};
    std::string error;
    if (!ctx) {
      ERR_print_errors_cb(SslRecordErrCallback, &error);
      return CF_ERR("EVP_PKEY_CTX_new_id failed: " << error);
    }
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0) {
      ERR_print_errors_cb(SslRecordErrCallback, &error);
      return CF_ERR("EVP_PKEY_keygen_init failed: " << error);
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), bytes) <= 0) {
      ERR_print_errors_cb(SslRecordErrCallback, &error);
      return CF_ERR("EVP_PKEY_CTX_set_rsa_keygen_bits failed: " << error);
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &pkey) <= 0) {
      ERR_print_errors_cb(SslRecordErrCallback, &error);
      return CF_ERR("EVP_PKEY_keygen failed: " << error);
    }
    return std::unique_ptr<KeyPair>{new BoringSslKeyPair(pkey)};
  }

  Result<std::string> PemPrivateKey() const override {
    std::unique_ptr<BIO, int (*)(BIO*)> bo(BIO_new(BIO_s_mem()), BIO_free);
    std::string error;
    if (!bo) {
      ERR_print_errors_cb(SslRecordErrCallback, &error);
      return CF_ERR("BIO_new failed: " << error);
    }
    if (!PEM_write_bio_PrivateKey(bo.get(), pkey_.get(), NULL, NULL, 0, 0,
                                  NULL)) {
      ERR_print_errors_cb(SslRecordErrCallback, &error);
      return CF_ERR("PEM_write_bio_PrivateKey failed: " << error);
    }
    std::string priv(BIO_pending(bo.get()), ' ');
    auto written = BIO_read(bo.get(), priv.data(), priv.size());
    if (written != priv.size()) {
      return CF_ERR("Unexpected amount of data written: " << written << " != "
                                                          << priv.size());
    }
    return priv;
  }

  Result<std::string> PemPublicKey() const override {
    std::unique_ptr<BIO, int (*)(BIO*)> bo(BIO_new(BIO_s_mem()), BIO_free);
    std::string error;
    if (!bo) {
      ERR_print_errors_cb(SslRecordErrCallback, &error);
      return CF_ERR("BIO_new failed: " << error);
    }
    if (!PEM_write_bio_PUBKEY(bo.get(), pkey_.get())) {
      ERR_print_errors_cb(SslRecordErrCallback, &error);
      return CF_ERR("PEM_write_bio_PUBKEY failed: " << error);
    }

    std::string priv(BIO_pending(bo.get()), ' ');
    auto written = BIO_read(bo.get(), priv.data(), priv.size());
    if (written != priv.size()) {
      return CF_ERR("Unexpected amount of data written: " << written << " != "
                                                          << priv.size());
    }
    return priv;
  }

  /*
   * OpenSSH has its own distinct format for public keys, which cannot be
   * produced directly with OpenSSL/BoringSSL primitives. Luckily it is possible
   * to convert the BoringSSL-generated RSA key without touching the filesystem.
   */
  Result<std::string> OpenSshPublicKey() const override {
    auto pem_pubkey =
        CF_EXPECT(PemPublicKey(), "Failed to get pem public key: ");
    auto fd = SharedFD::MemfdCreateWithData("", pem_pubkey);
    CF_EXPECT(fd->IsOpen(),
              "Could not create pubkey memfd: " << fd->StrError());
    Command cmd("/usr/bin/ssh-keygen");
    cmd.AddParameter("-i");
    cmd.AddParameter("-f");
    cmd.AddParameter("/proc/self/fd/0");
    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, fd);
    cmd.AddParameter("-m");
    cmd.AddParameter("PKCS8");
    std::string out;
    std::string err;
    CF_EXPECT(RunWithManagedStdio(std::move(cmd), nullptr, &out, &err) == 0,
              "Could not convert pem key to openssh key. "
                  << "stdout=\"" << out << "\", stderr=\"" << err << "\"");
    return out;
  }

 private:
  BoringSslKeyPair(EVP_PKEY* pkey) : pkey_(pkey, EVP_PKEY_free) {}

  std::unique_ptr<EVP_PKEY, void (*)(EVP_PKEY*)> pkey_;
};

Result<std::unique_ptr<KeyPair>> KeyPair::CreateRsa(size_t bytes) {
  return BoringSslKeyPair::CreateRsa(bytes);
}

}  // namespace cuttlefish

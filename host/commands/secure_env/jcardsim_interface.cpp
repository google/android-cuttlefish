/*
 * Copyright 2024 The Android Open Source Project
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

#include "host/commands/secure_env/jcardsim_interface.h"

#include <dlfcn.h>
#include <string>

#include <android-base/logging.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "host/commands/secure_env/tpm_ffi.h"
#include "host/libs/config/config_utils.h"

namespace cuttlefish {

constexpr uint8_t kKeyMintAppletAid[] = {0xa0, 0x00, 0x00, 0x00, 0x62, 0x03,
                                         0x02, 0x0c, 0x01, 0x01, 0x01};

constexpr uint8_t kManageChannel[] = {0x00, 0x70, 0x00, 0x00, 0x01};
constexpr uint8_t KM3_P1 = 0x60;
constexpr int32_t kSuccess = 0x9000;
constexpr std::string kLibJvm = "lib/server/libjvm.so";
constexpr std::string kDefaultJavaPath = "/usr/lib/jvm/jdk-64";
constexpr std::string kJcardsimJar = "framework/jcardsim.jar";

namespace {

std::string JVMLibrary() {
  return StringFromEnv("JAVA_HOME", kDefaultJavaPath) + "/" + kLibJvm;
}

std::string JcardSimLib() { return DefaultHostArtifactsPath(kJcardsimJar); }

Result<void> ResponseOK(const std::vector<uint8_t>& response) {
  CF_EXPECT(response.size() >= 2, "Response Size less than 2");
  size_t size = response.size();
  CF_EXPECT(((response[size - 2] << 8) | response[size - 1]) == kSuccess);
  return {};
}

Result<JNIEnv*> GetOrAttachJNIEnvironment(JavaVM* jvm) {
  JNIEnv* env;
  auto resp = jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (resp != JNI_OK) {
    resp = jvm->AttachCurrentThread(&env, nullptr);
    CF_EXPECT(resp == JNI_OK, "JVM thread attach failed.");
    struct VmThreadDetacher {
      VmThreadDetacher(JavaVM* vm) : mVm(vm) {}
      ~VmThreadDetacher() { mVm->DetachCurrentThread(); }

     private:
      JavaVM* const mVm;
    };
    static thread_local VmThreadDetacher detacher(jvm);
  }
  return env;
}

}  // namespace

Result<std::unique_ptr<JCardSimInterface>> JCardSimInterface::Create() {
  std::string jvm_lib = JVMLibrary();
  void* handle = dlopen(jvm_lib.c_str(), RTLD_NOW | RTLD_NODELETE);
  CF_EXPECT(handle != nullptr, "Failed to open JVM library");

  JavaVMInitArgs args;
  JavaVMOption options[1];

  args.version = JNI_VERSION_1_6;
  args.nOptions = 1;
  std::string option_str = std::string("-Djava.class.path=") + JcardSimLib();
  options[0].optionString = option_str.c_str();
  args.options = options;
  args.ignoreUnrecognized = JNI_FALSE;

  typedef jint (*p_JNI_CreateJavaVM)(JavaVM**, void**, void**);
  p_JNI_CreateJavaVM jni_create_java_vm =
      (p_JNI_CreateJavaVM)dlsym(handle, "JNI_CreateJavaVM");
  CF_EXPECT(jni_create_java_vm != nullptr, "JNI_CreateJavaVM Symbol not found");

  JavaVM* jvm;
  JNIEnv* env;
  jint ret = jni_create_java_vm(&jvm, (void**)&env, (void**)&args);
  CF_EXPECT(ret == JNI_OK, "Failed to create JavaVM");

  auto jcardsim_interface =
      std::unique_ptr<JCardSimInterface>(new JCardSimInterface(jvm));
  CF_EXPECT(jcardsim_interface->PersonalizeKeymintApplet(env));
  CF_EXPECT(jcardsim_interface->ProvisionPresharedSecret(env));
  return jcardsim_interface;
}

JCardSimInterface::JCardSimInterface(JavaVM* jvm)
    : jcardsim_proxy_inst_(nullptr),
      jcardsim_proxy_class_(nullptr),
      jvm_(jvm) {}

JCardSimInterface::~JCardSimInterface() {
  auto result = GetOrAttachJNIEnvironment(jvm_);
  if (result.ok()) {
    JNIEnv* env = result.value();
    if (jcardsim_proxy_class_) {
      env->DeleteGlobalRef(jcardsim_proxy_class_);
      jcardsim_proxy_class_ = nullptr;
    }
    if (jcardsim_proxy_inst_) {
      env->DeleteGlobalRef(jcardsim_proxy_inst_);
      jcardsim_proxy_inst_ = nullptr;
    }
  }
}

Result<void> JCardSimInterface::PersonalizeKeymintApplet(JNIEnv* env) {
  jclass jcardsim_proxy_class =
      env->FindClass("com/android/javacard/jcproxy/JCardSimProxy");
  CF_EXPECT(jcardsim_proxy_class != nullptr, "JCardSimProxy class not found");

  // Create Global reference to JCardSimProxy class
  jcardsim_proxy_class_ =
      reinterpret_cast<jclass>(env->NewGlobalRef(jcardsim_proxy_class));

  jmethodID constructor =
      env->GetMethodID(jcardsim_proxy_class, "<init>", "()V");
  CF_EXPECT(constructor != nullptr, "Constructor method not found");

  // Create Object
  jobject main_obj = env->NewObject(jcardsim_proxy_class, constructor);
  CF_EXPECT(main_obj != nullptr, "Failed to create JCardSimProxy instance");

  // Create Global reference to JCardSimProxy instance
  jcardsim_proxy_inst_ = reinterpret_cast<jobject>(env->NewGlobalRef(main_obj));

  jmethodID init_method =
      env->GetMethodID(jcardsim_proxy_class, "initialize", "()V");
  CF_EXPECT(init_method != nullptr, "Initialize method not found");

  // Call initialize method on JCardSimProxy
  env->CallVoidMethod(jcardsim_proxy_inst_, init_method);
  return {};
}

Result<std::vector<uint8_t>> JCardSimInterface::OpenChannel(JNIEnv* env) {
  int size = sizeof(kManageChannel) / sizeof(kManageChannel[0]);
  std::vector<uint8_t> manage_channel(kManageChannel, kManageChannel + size);
  return CF_EXPECT(
      InternalTransmit(env, manage_channel.data(), manage_channel.size()));
}

Result<std::vector<uint8_t>> JCardSimInterface::SelectKeymintApplet(
    JNIEnv* env, uint8_t cla) {
  uint8_t aid_size = sizeof(kKeyMintAppletAid) / sizeof(kKeyMintAppletAid[0]);
  std::vector<uint8_t> select_cmd = {
      cla,
      0xA4, /* Instruction code */
      0x04, /* Instruction parameter 1 */
      0x00, /* Instruction parameter 2 */
      static_cast<uint8_t>(aid_size),
  };
  select_cmd.insert(select_cmd.end(), kKeyMintAppletAid,
                    kKeyMintAppletAid + aid_size);
  select_cmd.push_back((uint8_t)0x00);
  return CF_EXPECT(InternalTransmit(env, select_cmd.data(), select_cmd.size()));
}

Result<std::vector<uint8_t>> JCardSimInterface::CloseChannel(
    JNIEnv* env, uint8_t channel_number) {
  std::vector<uint8_t> close_channel = {0x00, 0x70, 0x80, channel_number, 0x00};
  return CF_EXPECT(
      InternalTransmit(env, close_channel.data(), close_channel.size()));
}

Result<std::vector<uint8_t>> JCardSimInterface::PreSharedKey() {
  return std::vector<uint8_t>(32, 0);
}

Result<void> JCardSimInterface::ProvisionPresharedSecret(JNIEnv* env) {
  std::vector<uint8_t> key =
      CF_EXPECT(PreSharedKey(), "Failed to get pre-shared key");

  auto response = CF_EXPECT(OpenChannel(env));
  CF_EXPECT(ResponseOK(response), "Open Channel command failed");

  uint8_t cla = 0xff;
  if ((response[0] > 0x03) && (response[0] < 0x14)) {
    // update CLA byte according to GP spec Table 11-12
    cla = (0x40 + (response[0] - 4));
  } else if ((response[0] > 0x00) && (response[0] < 0x04)) {
    // update CLA byte according to GP spec Table 11-11
    cla = response[0];
  } else {
    CF_ERR("Invalid Channel " << response[0]);
  }
  uint8_t channel_number = response[0];

  do {
    auto response = SelectKeymintApplet(env, cla);
    if (!response.ok() || !ResponseOK(*response).ok()) {
      LOG(ERROR) << "Failed to select the Applet";
      break;
    }

    // send preshared secret apdu
    std::vector<uint8_t> shared_secret_apdu = {
        static_cast<uint8_t>(0x80 | channel_number),  // CLA
        0x0F,                                         // INS
        KM3_P1,                                       // P1
        0x00,                                         // P2
        0x00,                                         // Lc - 0x000023
        0x00,
        0x23,
        0x81,  // Array of size 1
        0x58,  // byte string with additional information(24)
        0x20,  // length of the bytestring(32)
    };
    shared_secret_apdu.insert(shared_secret_apdu.end(), key.begin(), key.end());
    shared_secret_apdu.push_back(0x00);  // Le 0x0000
    shared_secret_apdu.push_back(0x00);
    response = InternalTransmit(env, shared_secret_apdu.data(),
                                shared_secret_apdu.size());
    if (!response.ok() || !ResponseOK(*response).ok()) {
      LOG(ERROR) << "Failed to provision preshared secret";
      break;
    }
  } while (false);

  response = CF_EXPECT(CloseChannel(env, channel_number));
  CF_EXPECT(ResponseOK(response), "Close Channel command failed");
  return {};
}

Result<std::vector<uint8_t>> JCardSimInterface::InternalTransmit(
    JNIEnv* env, const uint8_t* bytes, size_t len) const {
  std::vector<uint8_t> out;
  jmethodID transmit =
      env->GetMethodID(jcardsim_proxy_class_, "transmit", "([B)[B");
  if (transmit == nullptr) {
    LOG(ERROR) << "ERROR: transmit method not found !";
    return out;
  }
  jbyteArray java_array = env->NewByteArray(len);
  env->SetByteArrayRegion(java_array, 0, len,
                          reinterpret_cast<const jbyte*>(bytes));
  jbyteArray resp_array = (jbyteArray)env->CallObjectMethod(
      jcardsim_proxy_inst_, transmit, java_array);
  jsize num_bytes = env->GetArrayLength(resp_array);
  uint8_t* data = (uint8_t*)env->GetByteArrayElements(resp_array, NULL);
  std::copy(data, data + num_bytes, std::back_inserter(out));
  env->ReleaseByteArrayElements(resp_array, (jbyte*)data, JNI_ABORT);
  env->DeleteLocalRef(java_array);
  return out;
}

Result<std::vector<uint8_t>> JCardSimInterface::Transmit(const uint8_t* bytes,
                                                         size_t len) const {
  JNIEnv* env =
      CF_EXPECT(GetOrAttachJNIEnvironment(jvm_), "Failed to get JNIEnv");
  return InternalTransmit(env, bytes, len);
}

}  // namespace cuttlefish

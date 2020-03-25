/*
 * Copyright (C) 2019 The Android Open Source Project
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
#include <aidl/metadata.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gtest/gtest.h>
#include <hidl/metadata.h>
#include <hidl-util/FQName.h>
#include <vintf/VintfObject.h>

using namespace android;

static const std::set<std::string> kKnownMissingHidl = {
    "android.frameworks.bufferhub@1.0",
    "android.frameworks.cameraservice.device@2.0",
    "android.frameworks.vr.composer@1.0",
    "android.hardware.audio@2.0",
    "android.hardware.audio@4.0",
    "android.hardware.audio@5.0",
    "android.hardware.audio.effect@2.0",
    "android.hardware.audio.effect@4.0",
    "android.hardware.audio.effect@5.0",
    "android.hardware.automotive.audiocontrol@1.0",
    "android.hardware.automotive.evs@1.0",
    "android.hardware.automotive.vehicle@2.0",
    "android.hardware.biometrics.face@1.0",
    "android.hardware.biometrics.fingerprint@2.1",
    "android.hardware.bluetooth.a2dp@1.0",
    "android.hardware.broadcastradio@1.1",
    "android.hardware.broadcastradio@2.0",
    "android.hardware.camera.provider@2.5",
    "android.hardware.cas@1.2",
    "android.hardware.cas.native@1.0",
    "android.hardware.confirmationui@1.0",
    "android.hardware.contexthub@1.0",
    "android.hardware.configstore@1.1", // deprecated, see b/149050985, b/149050733
    "android.hardware.fastboot@1.0",
    "android.hardware.gnss.measurement_corrections@1.0",
    "android.hardware.gnss.visibility_control@1.0",
    "android.hardware.graphics.allocator@3.0",
    "android.hardware.graphics.bufferqueue@1.0",
    "android.hardware.graphics.bufferqueue@2.0",
    "android.hardware.graphics.composer@2.3",
    "android.hardware.graphics.mapper@3.0",
    "android.hardware.health@1.0",
    "android.hardware.ir@1.0",
    "android.hardware.keymaster@3.0",
    "android.hardware.light@2.0",
    "android.hardware.media.bufferpool@1.0",
    "android.hardware.media.bufferpool@2.0",
    "android.hardware.memtrack@1.0",
    "android.hardware.nfc@1.2",
    "android.hardware.oemlock@1.0",
    "android.hardware.power@1.3",
    "android.hardware.radio.config@1.2",
    "android.hardware.radio.deprecated@1.0",
    "android.hardware.renderscript@1.0",
    "android.hardware.secure_element@1.2",
    "android.hardware.sensors@1.0",
    "android.hardware.soundtrigger@2.2",
    "android.hardware.tetheroffload.config@1.0",
    "android.hardware.tetheroffload.control@1.0",
    "android.hardware.thermal@1.1",
    "android.hardware.tv.cec@1.0",
    "android.hardware.tv.cec@2.0",
    "android.hardware.tv.input@1.0",
    "android.hardware.tv.tuner@1.0",
    "android.hardware.usb@1.2",
    "android.hardware.usb.gadget@1.0",
    "android.hardware.vibrator@1.3",
    "android.hardware.vr@1.0",
    "android.hardware.weaver@1.0",
    "android.hardware.wifi@1.3",
    "android.hardware.wifi.hostapd@1.1",
    "android.hardware.wifi.offload@1.0",
    "android.hidl.base@1.0",
    "android.hidl.memory.token@1.0",
};

static const std::set<std::string> kKnownMissingAidl = {
    // types-only packages, which never expect a default implementation
    // none right now

    // These KeyMaster types are in an AIDL types-only HAL because they're used
    // by the Identity Credential AIDL HAL. Remove this when fully porting
    // KeyMaster to AIDL.
    "android.hardware.keymaster.HardwareAuthToken",
    "android.hardware.keymaster.HardwareAuthenticatorType",
    "android.hardware.keymaster.Timestamp",
};

// AOSP packages which are never considered
static bool isHidlPackageWhitelist(const FQName& name) {
    static std::vector<std::string> gAospExclude = {
        // packages not implemented now that we never expect to be implemented
        "android.hardware.tests",
        // packages not registered with hwservicemanager, usually sub-interfaces
        "android.hardware.camera.device",
    };
    for (const std::string& package : gAospExclude) {
        if (name.inPackage(package)) {
            return true;
        }
    }
    return false;
}

static bool isAospHidlInterface(const FQName& name) {
    static const std::vector<std::string> kAospPackages = {
        "android.hidl",
        "android.hardware",
        "android.frameworks",
        "android.system",
    };
    for (const std::string& package : kAospPackages) {
        if (name.inPackage(package)) {
            return true;
        }
    }
    return false;
}

static std::set<FQName> allTreeHidlInterfaces() {
    std::set<FQName> ret;
    for (const auto& iface : HidlInterfaceMetadata::all()) {
        FQName f;
        CHECK(f.setTo(iface.name)) << iface.name;
        ret.insert(f);
    }
    return ret;
}

static std::set<FQName> allHidlManifestInterfaces() {
    std::set<FQName> ret;
    auto setInserter = [&] (const vintf::ManifestInstance& i) -> bool {
        if (i.format() != vintf::HalFormat::HIDL) {
            return true;  // continue
        }
        ret.insert(i.getFqInstance().getFqName());
        return true;  // continue
    };
    vintf::VintfObject::GetDeviceHalManifest()->forEachInstance(setInserter);
    vintf::VintfObject::GetFrameworkHalManifest()->forEachInstance(setInserter);
    return ret;
}

static bool isAospAidlInterface(const std::string& name) {
    return base::StartsWith(name, "android.") &&
        !base::StartsWith(name, "android.automotive.") &&
        !base::StartsWith(name, "android.hardware.automotive.") &&
        !base::StartsWith(name, "android.hardware.tests.");
}

static std::set<std::string> allAidlManifestInterfaces() {
    std::set<std::string> ret;
    auto setInserter = [&] (const vintf::ManifestInstance& i) -> bool {
        if (i.format() != vintf::HalFormat::AIDL) {
            return true;  // continue
        }
        ret.insert(i.package() + "." + i.interface());
        return true;  // continue
    };
    vintf::VintfObject::GetDeviceHalManifest()->forEachInstance(setInserter);
    vintf::VintfObject::GetFrameworkHalManifest()->forEachInstance(setInserter);
    return ret;
}

TEST(Hal, AllHidlInterfacesAreInAosp) {
    for (const FQName& name : allHidlManifestInterfaces()) {
        EXPECT_TRUE(isAospHidlInterface(name)) << name.string();
    }
}

TEST(Hal, HidlInterfacesImplemented) {
    // instances -> major version -> minor versions
    std::map<std::string, std::map<size_t, std::set<size_t>>> unimplemented;

    for (const FQName& f : allTreeHidlInterfaces()) {
        if (!isAospHidlInterface(f)) continue;
        if (isHidlPackageWhitelist(f)) continue;

        unimplemented[f.package()][f.getPackageMajorVersion()].insert(f.getPackageMinorVersion());
    }

    // we'll be removing items from this which we know are missing
    // in order to be left with those elements which we thought we
    // knew were missing but are actually present
    std::set<std::string> thoughtMissing = kKnownMissingHidl;

    for (const FQName& f : allHidlManifestInterfaces()) {
        if (thoughtMissing.erase(f.getPackageAndVersion().string()) > 0) {
             ADD_FAILURE() << "Instance in missing list, but available: " << f.string();
        }

        std::set<size_t>& minors = unimplemented[f.package()][f.getPackageMajorVersion()];
        size_t minor = f.getPackageMinorVersion();

        auto it = minors.find(minor);
        if (it == minors.end()) continue;

        // if 1.2 is implemented, also considere 1.0, 1.1 implemented
        minors.erase(minors.begin(), std::next(it));
    }

    for (const auto& [package, minorsPerMajor] : unimplemented) {
        for (const auto& [major, minors] : minorsPerMajor) {
            if (minors.empty()) continue;

            size_t maxMinor = *minors.rbegin();

            FQName missing;
            ASSERT_TRUE(missing.setTo(package, major, maxMinor));

            if (thoughtMissing.erase(missing.string()) > 0) continue;

            ADD_FAILURE() << "Missing implementation from " << missing.string();
        }
    }

    for (const std::string& missing : thoughtMissing) {
        ADD_FAILURE() << "Instance in missing list and cannot find it anywhere: " << missing
                  << " (multiple versions in missing list?)";
    }
}

TEST(Hal, AllAidlInterfacesAreInAosp) {
    for (const std::string& name : allAidlManifestInterfaces()) {
        EXPECT_TRUE(isAospAidlInterface(name)) << name;
    }
}

TEST(Hal, AidlInterfacesImplemented) {
    std::set<std::string> manifest = allAidlManifestInterfaces();
    std::set<std::string> thoughtMissing = kKnownMissingAidl;

    for (const auto& iface : AidlInterfaceMetadata::all()) {
        ASSERT_FALSE(iface.types.empty()) << iface.name;  // sanity
        if (std::none_of(iface.types.begin(), iface.types.end(), isAospAidlInterface)) continue;
        if (iface.stability != "vintf") continue;

        bool hasRegistration = false;
        bool knownMissing = false;
        for (const std::string& type : iface.types) {
            if (manifest.erase(type) > 0) hasRegistration = true;
            if (thoughtMissing.erase(type) > 0) knownMissing = true;
        }

        if (knownMissing) {
            if (hasRegistration) {
                ADD_FAILURE() << "Interface in missing list, but available: " << iface.name
                          << " which declares the following types:\n    "
                          << base::Join(iface.types, "\n    ");
            }

            continue;
        }

        EXPECT_TRUE(hasRegistration) << iface.name << " which declares the following types:\n    "
            << base::Join(iface.types, "\n    ");
    }

    for (const std::string& iface : thoughtMissing) {
        ADD_FAILURE() << "Interface in manifest list and cannot find it anywhere: " << iface;
    }

    for (const std::string& iface : manifest) {
        ADD_FAILURE() << "Can't find manifest entry in tree: " << iface;
    }
}

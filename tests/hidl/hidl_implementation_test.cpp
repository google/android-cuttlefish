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
#include <android-base/logging.h>
#include <gtest/gtest.h>
#include <hidl/metadata.h>
#include <hidl-util/FQName.h>
#include <vintf/VintfObject.h>

using namespace android;

static std::set<std::string> kKnownMissing = {
    "android.frameworks.bufferhub@1.0",
    "android.frameworks.cameraservice.device@2.0",
    "android.frameworks.vr.composer@1.0",
    "android.frameworks.vr.composer@2.0",
    "android.hardware.audio@2.0",
    "android.hardware.audio@4.0",
    "android.hardware.audio@6.0",
    "android.hardware.audio.effect@2.0",
    "android.hardware.audio.effect@4.0",
    "android.hardware.audio.effect@6.0",
    "android.hardware.automotive.audiocontrol@1.0",
    "android.hardware.automotive.can@1.0",
    "android.hardware.automotive.evs@1.0",
    "android.hardware.automotive.evs@1.1",
    "android.hardware.automotive.vehicle@2.0",
    "android.hardware.biometrics.face@1.0",
    "android.hardware.biometrics.fingerprint@2.1",
    "android.hardware.bluetooth.a2dp@1.0",
    "android.hardware.broadcastradio@1.1",
    "android.hardware.broadcastradio@2.0",
    "android.hardware.camera.device@1.0",
    "android.hardware.camera.device@3.5",
    "android.hardware.camera.provider@2.5",
    "android.hardware.cas.native@1.0",
    "android.hardware.confirmationui@1.0",
    "android.hardware.contexthub@1.0",
    "android.hardware.fastboot@1.0",
    "android.hardware.gnss.measurement_corrections@1.0",
    "android.hardware.gnss.visibility_control@1.0",
    "android.hardware.graphics.allocator@3.0",
    "android.hardware.graphics.allocator@4.0",
    "android.hardware.graphics.bufferqueue@1.0",
    "android.hardware.graphics.bufferqueue@2.0",
    "android.hardware.graphics.composer@2.3",
    "android.hardware.graphics.composer@2.4",
    "android.hardware.graphics.mapper@3.0",
    "android.hardware.graphics.mapper@4.0",
    "android.hardware.health@1.0",
    "android.hardware.ir@1.0",
    "android.hardware.keymaster@3.0",
    "android.hardware.media.bufferpool@1.0",
    "android.hardware.media.bufferpool@2.0",
    "android.hardware.memtrack@1.0",
    "android.hardware.nfc@1.2",
    "android.hardware.oemlock@1.0",
    "android.hardware.power@1.3",
    "android.hardware.radio.config@1.2",
    "android.hardware.radio.deprecated@1.0",
    "android.hardware.renderscript@1.0",
    "android.hardware.secure_element@1.1",
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
    "android.hardware.vibrator@1.4",
    "android.hardware.vr@1.0",
    "android.hardware.weaver@1.0",
    "android.hardware.wifi@1.3",
    "android.hardware.wifi@1.4",
    "android.hardware.wifi.hostapd@1.1",
    "android.hardware.wifi.offload@1.0",
    "android.hidl.base@1.0",
    "android.hidl.memory.token@1.0",
};

// AOSP packages which are never considered
static bool isPackageWhitelist(const FQName& name) {
    static std::vector<std::string> gAospExclude = {
        // packages not implemented now that we never expect to be implemented
        "android.hardware.tests",
    };
    for (const std::string& package : gAospExclude) {
        if (name.inPackage(package)) {
            return true;
        }
    }
    return false;
}

static bool isAospInterface(const FQName& name) {
    static std::vector<std::string> gAospPackages = {
        "android.hidl",
        "android.hardware",
        "android.frameworks",
        "android.system",
    };
    for (const std::string& package : gAospPackages) {
        if (name.inPackage(package) && !isPackageWhitelist(name)) {
            return true;
        }
    }
    return false;
}

static std::set<FQName> allTreeInterfaces() {
    std::set<FQName> ret;
    for (const auto& iface : HidlInterfaceMetadata::all()) {
        FQName f;
        CHECK(f.setTo(iface.name)) << iface.name;
        ret.insert(f);
    }
    return ret;
}

static std::set<FQName> allManifestInstances() {
    std::set<FQName> ret;
    auto setInserter = [&] (const vintf::ManifestInstance& i) -> bool {
        if (i.format() != vintf::HalFormat::HIDL) {
            std::cout << "[ WARNING ] Not checking non-HIDL instance: " << i.description() << std::endl;
            return true;  // continue
        }
        ret.insert(i.getFqInstance().getFqName());
        return true;  // continue
    };
    vintf::VintfObject::GetDeviceHalManifest()->forEachInstance(setInserter);
    vintf::VintfObject::GetFrameworkHalManifest()->forEachInstance(setInserter);
    return ret;
}

TEST(Hidl, IsAospDevice) {
    for (const FQName& name : allManifestInstances()) {
        EXPECT_TRUE(isAospInterface(name)) << name.string();
    }
}

TEST(Hidl, InterfacesImplemented) {
    // instances -> major version -> minor versions version
    std::map<std::string, std::map<size_t, std::set<size_t>>> unimplemented;

    for (const FQName& f : allTreeInterfaces()) {
        if (!isAospInterface(f)) continue;

        unimplemented[f.package()][f.getPackageMajorVersion()].insert(f.getPackageMinorVersion());
    }

    for (const FQName& f : allManifestInstances()) {
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

            if (kKnownMissing.find(missing.string()) != kKnownMissing.end()) continue;

            ADD_FAILURE() << "Missing implementation from " << missing.string();
        }
    }
}

/*
 * Copyright 2017 The Android Open Source Project
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

#include "HWC2.h"

//#define LOG_NDEBUG 0

#undef LOG_TAG
#define LOG_TAG "CfHWC2"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <inttypes.h>

#include <chrono>
#include <cstdlib>
#include <sstream>

#include <hardware/hwcomposer.h>
#include <log/log.h>
#include <utils/Trace.h>

#include "guest/hals/hwcomposer/common/hwcomposer.h"
#include "guest/hals/hwcomposer/cutf_cvm/vsocket_screen_view.h"

using namespace std::chrono_literals;

static uint8_t getMinorVersion(struct hwc_composer_device_1* device)
{
    auto version = device->common.version & HARDWARE_API_VERSION_2_MAJ_MIN_MASK;
    return (version >> 16) & 0xF;
}

template <typename PFN, typename T>
static hwc2_function_pointer_t asFP(T function)
{
    static_assert(std::is_same<PFN, T>::value, "Incompatible function pointer");
    return reinterpret_cast<hwc2_function_pointer_t>(function);
}

using namespace HWC2;

static constexpr Attribute ColorMode = static_cast<Attribute>(6);

namespace android {

class CfHWC2::Callbacks : public hwc_procs_t {
    public:
        explicit Callbacks(CfHWC2& adapter) : mAdapter(adapter) {
            invalidate = &invalidateHook;
            vsync = &vsyncHook;
            hotplug = &hotplugHook;
        }

        static void invalidateHook(const hwc_procs_t* procs) {
            auto callbacks = static_cast<const Callbacks*>(procs);
            callbacks->mAdapter.hwc1Invalidate();
        }

        static void vsyncHook(const hwc_procs_t* procs, int display,
                int64_t timestamp) {
            auto callbacks = static_cast<const Callbacks*>(procs);
            callbacks->mAdapter.hwc1Vsync(display, timestamp);
        }

        static void hotplugHook(const hwc_procs_t* procs, int display,
                int connected) {
            auto callbacks = static_cast<const Callbacks*>(procs);
            callbacks->mAdapter.hwc1Hotplug(display, connected);
        }

    private:
        CfHWC2& mAdapter;
};

static int closeHook(hw_device_t* /*device*/)
{
    // Do nothing, since the real work is done in the class destructor, but we
    // need to provide a valid function pointer for hwc2_close to call
    return 0;
}

CfHWC2::CfHWC2(hwc_composer_device_1_t* hwc1Device)
  : mDumpString(),
    mHwc1Device(hwc1Device),
    mHwc1MinorVersion(getMinorVersion(hwc1Device)),
    mHwc1SupportsVirtualDisplays(false),
    mHwc1SupportsBackgroundColor(false),
    mHwc1Callbacks(std::make_unique<Callbacks>(*this)),
    mCapabilities(),
    mLayers(),
    mHwc1VirtualDisplay(),
    mStateMutex(),
    mCallbacks(),
    mHasPendingInvalidate(false),
    mPendingVsyncs(),
    mPendingHotplugs(),
    mDisplays(),
    mHwc1DisplayMap()
{
    common.tag = HARDWARE_DEVICE_TAG;
    common.version = HWC_DEVICE_API_VERSION_2_0;
    common.close = closeHook;
    getCapabilities = getCapabilitiesHook;
    getFunction = getFunctionHook;
    populateCapabilities();
    populatePrimary();
    mHwc1Device->registerProcs(mHwc1Device,
            static_cast<const hwc_procs_t*>(mHwc1Callbacks.get()));
}

CfHWC2::~CfHWC2() {
    hwc_close_1(mHwc1Device);
}

void CfHWC2::doGetCapabilities(uint32_t* outCount,
        int32_t* outCapabilities) {
    if (outCapabilities == nullptr) {
        *outCount = mCapabilities.size();
        return;
    }

    auto capabilityIter = mCapabilities.cbegin();
    for (size_t written = 0; written < *outCount; ++written) {
        if (capabilityIter == mCapabilities.cend()) {
            return;
        }
        outCapabilities[written] = static_cast<int32_t>(*capabilityIter);
        ++capabilityIter;
    }
}

hwc2_function_pointer_t CfHWC2::doGetFunction(
        FunctionDescriptor descriptor) {
    switch (descriptor) {
        // Device functions
        case FunctionDescriptor::CreateVirtualDisplay:
            return asFP<HWC2_PFN_CREATE_VIRTUAL_DISPLAY>(
                    createVirtualDisplayHook);
        case FunctionDescriptor::DestroyVirtualDisplay:
            return asFP<HWC2_PFN_DESTROY_VIRTUAL_DISPLAY>(
                    destroyVirtualDisplayHook);
        case FunctionDescriptor::Dump:
            return asFP<HWC2_PFN_DUMP>(dumpHook);
        case FunctionDescriptor::GetMaxVirtualDisplayCount:
            return asFP<HWC2_PFN_GET_MAX_VIRTUAL_DISPLAY_COUNT>(
                    getMaxVirtualDisplayCountHook);
        case FunctionDescriptor::RegisterCallback:
            return asFP<HWC2_PFN_REGISTER_CALLBACK>(registerCallbackHook);

        // Display functions
        case FunctionDescriptor::AcceptDisplayChanges:
            return asFP<HWC2_PFN_ACCEPT_DISPLAY_CHANGES>(
                    displayHook<decltype(&Display::acceptChanges),
                    &Display::acceptChanges>);
        case FunctionDescriptor::CreateLayer:
            return asFP<HWC2_PFN_CREATE_LAYER>(
                    displayHook<decltype(&Display::createLayer),
                    &Display::createLayer, hwc2_layer_t*>);
        case FunctionDescriptor::DestroyLayer:
            return asFP<HWC2_PFN_DESTROY_LAYER>(
                    displayHook<decltype(&Display::destroyLayer),
                    &Display::destroyLayer, hwc2_layer_t>);
        case FunctionDescriptor::GetActiveConfig:
            return asFP<HWC2_PFN_GET_ACTIVE_CONFIG>(
                    displayHook<decltype(&Display::getActiveConfig),
                    &Display::getActiveConfig, hwc2_config_t*>);
        case FunctionDescriptor::GetChangedCompositionTypes:
            return asFP<HWC2_PFN_GET_CHANGED_COMPOSITION_TYPES>(
                    displayHook<decltype(&Display::getChangedCompositionTypes),
                    &Display::getChangedCompositionTypes, uint32_t*,
                    hwc2_layer_t*, int32_t*>);
        case FunctionDescriptor::GetColorModes:
            return asFP<HWC2_PFN_GET_COLOR_MODES>(
                    displayHook<decltype(&Display::getColorModes),
                    &Display::getColorModes, uint32_t*, int32_t*>);
        case FunctionDescriptor::GetDisplayAttribute:
            return asFP<HWC2_PFN_GET_DISPLAY_ATTRIBUTE>(
                    getDisplayAttributeHook);
        case FunctionDescriptor::GetDisplayConfigs:
            return asFP<HWC2_PFN_GET_DISPLAY_CONFIGS>(
                    displayHook<decltype(&Display::getConfigs),
                    &Display::getConfigs, uint32_t*, hwc2_config_t*>);
        case FunctionDescriptor::GetDisplayName:
            return asFP<HWC2_PFN_GET_DISPLAY_NAME>(
                    displayHook<decltype(&Display::getName),
                    &Display::getName, uint32_t*, char*>);
        case FunctionDescriptor::GetDisplayRequests:
            return asFP<HWC2_PFN_GET_DISPLAY_REQUESTS>(
                    displayHook<decltype(&Display::getRequests),
                    &Display::getRequests, int32_t*, uint32_t*, hwc2_layer_t*,
                    int32_t*>);
        case FunctionDescriptor::GetDisplayType:
            return asFP<HWC2_PFN_GET_DISPLAY_TYPE>(
                    displayHook<decltype(&Display::getType),
                    &Display::getType, int32_t*>);
        case FunctionDescriptor::GetDozeSupport:
            return asFP<HWC2_PFN_GET_DOZE_SUPPORT>(
                    displayHook<decltype(&Display::getDozeSupport),
                    &Display::getDozeSupport, int32_t*>);
        case FunctionDescriptor::GetHdrCapabilities:
            return asFP<HWC2_PFN_GET_HDR_CAPABILITIES>(
                    displayHook<decltype(&Display::getHdrCapabilities),
                    &Display::getHdrCapabilities, uint32_t*, int32_t*, float*,
                    float*, float*>);
        case FunctionDescriptor::GetReleaseFences:
            return asFP<HWC2_PFN_GET_RELEASE_FENCES>(
                    displayHook<decltype(&Display::getReleaseFences),
                    &Display::getReleaseFences, uint32_t*, hwc2_layer_t*,
                    int32_t*>);
        case FunctionDescriptor::PresentDisplay:
            return asFP<HWC2_PFN_PRESENT_DISPLAY>(
                    displayHook<decltype(&Display::present),
                    &Display::present, int32_t*>);
        case FunctionDescriptor::SetActiveConfig:
            return asFP<HWC2_PFN_SET_ACTIVE_CONFIG>(
                    displayHook<decltype(&Display::setActiveConfig),
                    &Display::setActiveConfig, hwc2_config_t>);
        case FunctionDescriptor::SetClientTarget:
            return asFP<HWC2_PFN_SET_CLIENT_TARGET>(
                    displayHook<decltype(&Display::setClientTarget),
                    &Display::setClientTarget, buffer_handle_t, int32_t,
                    int32_t, hwc_region_t>);
        case FunctionDescriptor::SetColorMode:
            return asFP<HWC2_PFN_SET_COLOR_MODE>(setColorModeHook);
        case FunctionDescriptor::SetColorTransform:
            return asFP<HWC2_PFN_SET_COLOR_TRANSFORM>(setColorTransformHook);
        case FunctionDescriptor::SetOutputBuffer:
            return asFP<HWC2_PFN_SET_OUTPUT_BUFFER>(
                    displayHook<decltype(&Display::setOutputBuffer),
                    &Display::setOutputBuffer, buffer_handle_t, int32_t>);
        case FunctionDescriptor::SetPowerMode:
            return asFP<HWC2_PFN_SET_POWER_MODE>(setPowerModeHook);
        case FunctionDescriptor::SetVsyncEnabled:
            return asFP<HWC2_PFN_SET_VSYNC_ENABLED>(setVsyncEnabledHook);
        case FunctionDescriptor::ValidateDisplay:
            return asFP<HWC2_PFN_VALIDATE_DISPLAY>(
                    displayHook<decltype(&Display::validate),
                    &Display::validate, uint32_t*, uint32_t*>);
        case FunctionDescriptor::GetClientTargetSupport:
            return asFP<HWC2_PFN_GET_CLIENT_TARGET_SUPPORT>(
                    displayHook<decltype(&Display::getClientTargetSupport),
                    &Display::getClientTargetSupport, uint32_t, uint32_t,
                                                      int32_t, int32_t>);

        // 2.3 required functions
        case FunctionDescriptor::GetDisplayIdentificationData:
            return asFP<HWC2_PFN_GET_DISPLAY_IDENTIFICATION_DATA>(
                    displayHook<decltype(&Display::getDisplayIdentificationData),
                    &Display::getDisplayIdentificationData, uint8_t*, uint32_t*, uint8_t*>);
        case FunctionDescriptor::GetDisplayCapabilities:
            return asFP<HWC2_PFN_GET_DISPLAY_CAPABILITIES>(
                    displayHook<decltype(&Display::getDisplayCapabilities),
                    &Display::getDisplayCapabilities, uint32_t*, uint32_t*>);
        case FunctionDescriptor::GetDisplayBrightnessSupport:
            return asFP<HWC2_PFN_GET_DISPLAY_BRIGHTNESS_SUPPORT>(
                    displayHook<decltype(&Display::getDisplayBrightnessSupport),
                    &Display::getDisplayBrightnessSupport, bool*>);
        case FunctionDescriptor::SetDisplayBrightness:
            return asFP<HWC2_PFN_SET_DISPLAY_BRIGHTNESS>(
                    displayHook<decltype(&Display::setDisplayBrightness),
                    &Display::setDisplayBrightness, float>);

        // Layer functions
        case FunctionDescriptor::SetCursorPosition:
            return asFP<HWC2_PFN_SET_CURSOR_POSITION>(
                    layerHook<decltype(&Layer::setCursorPosition),
                    &Layer::setCursorPosition, int32_t, int32_t>);
        case FunctionDescriptor::SetLayerBuffer:
            return asFP<HWC2_PFN_SET_LAYER_BUFFER>(
                    layerHook<decltype(&Layer::setBuffer), &Layer::setBuffer,
                    buffer_handle_t, int32_t>);
        case FunctionDescriptor::SetLayerSurfaceDamage:
            return asFP<HWC2_PFN_SET_LAYER_SURFACE_DAMAGE>(
                    layerHook<decltype(&Layer::setSurfaceDamage),
                    &Layer::setSurfaceDamage, hwc_region_t>);

        // Layer state functions
        case FunctionDescriptor::SetLayerBlendMode:
            return asFP<HWC2_PFN_SET_LAYER_BLEND_MODE>(
                    setLayerBlendModeHook);
        case FunctionDescriptor::SetLayerColor:
            return asFP<HWC2_PFN_SET_LAYER_COLOR>(
                    layerHook<decltype(&Layer::setColor), &Layer::setColor,
                    hwc_color_t>);
        case FunctionDescriptor::SetLayerCompositionType:
            return asFP<HWC2_PFN_SET_LAYER_COMPOSITION_TYPE>(
                    setLayerCompositionTypeHook);
        case FunctionDescriptor::SetLayerDataspace:
            return asFP<HWC2_PFN_SET_LAYER_DATASPACE>(setLayerDataspaceHook);
        case FunctionDescriptor::SetLayerDisplayFrame:
            return asFP<HWC2_PFN_SET_LAYER_DISPLAY_FRAME>(
                    layerHook<decltype(&Layer::setDisplayFrame),
                    &Layer::setDisplayFrame, hwc_rect_t>);
        case FunctionDescriptor::SetLayerPlaneAlpha:
            return asFP<HWC2_PFN_SET_LAYER_PLANE_ALPHA>(
                    layerHook<decltype(&Layer::setPlaneAlpha),
                    &Layer::setPlaneAlpha, float>);
        case FunctionDescriptor::SetLayerSidebandStream:
            return asFP<HWC2_PFN_SET_LAYER_SIDEBAND_STREAM>(
                    layerHook<decltype(&Layer::setSidebandStream),
                    &Layer::setSidebandStream, const native_handle_t*>);
        case FunctionDescriptor::SetLayerSourceCrop:
            return asFP<HWC2_PFN_SET_LAYER_SOURCE_CROP>(
                    layerHook<decltype(&Layer::setSourceCrop),
                    &Layer::setSourceCrop, hwc_frect_t>);
        case FunctionDescriptor::SetLayerTransform:
            return asFP<HWC2_PFN_SET_LAYER_TRANSFORM>(setLayerTransformHook);
        case FunctionDescriptor::SetLayerVisibleRegion:
            return asFP<HWC2_PFN_SET_LAYER_VISIBLE_REGION>(
                    layerHook<decltype(&Layer::setVisibleRegion),
                    &Layer::setVisibleRegion, hwc_region_t>);
        case FunctionDescriptor::SetLayerZOrder:
            return asFP<HWC2_PFN_SET_LAYER_Z_ORDER>(setLayerZOrderHook);

        default:
            ALOGE("doGetFunction: Unknown function descriptor: %d (%s)",
                    static_cast<int32_t>(descriptor),
                    to_string(descriptor).c_str());
            return nullptr;
    }
}

// Device functions

Error CfHWC2::createVirtualDisplay(uint32_t width,
        uint32_t height, hwc2_display_t* outDisplay) {
    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    if (mHwc1VirtualDisplay) {
        // We have already allocated our only HWC1 virtual display
        ALOGE("createVirtualDisplay: HWC1 virtual display already allocated");
        return Error::NoResources;
    }

    mHwc1VirtualDisplay = std::make_shared<CfHWC2::Display>(*this,
            HWC2::DisplayType::Virtual);
    mHwc1VirtualDisplay->populateConfigs(width, height);
    const auto displayId = mHwc1VirtualDisplay->getId();
    mHwc1DisplayMap[HWC_DISPLAY_VIRTUAL] = displayId;
    mHwc1VirtualDisplay->setHwc1Id(HWC_DISPLAY_VIRTUAL);
    mDisplays.emplace(displayId, mHwc1VirtualDisplay);
    *outDisplay = displayId;

    return Error::None;
}

Error CfHWC2::destroyVirtualDisplay(hwc2_display_t displayId) {
    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    if (!mHwc1VirtualDisplay || (mHwc1VirtualDisplay->getId() != displayId)) {
        return Error::BadDisplay;
    }

    mHwc1VirtualDisplay.reset();
    mHwc1DisplayMap.erase(HWC_DISPLAY_VIRTUAL);
    mDisplays.erase(displayId);

    return Error::None;
}

void CfHWC2::dump(uint32_t* outSize, char* outBuffer) {
    if (outBuffer != nullptr) {
        auto copiedBytes = mDumpString.copy(outBuffer, *outSize);
        *outSize = static_cast<uint32_t>(copiedBytes);
        return;
    }

    std::stringstream output;

    output << "-- CfHWC2 --\n";

    output << "Adapting to a HWC 1." << static_cast<int>(mHwc1MinorVersion) <<
            " device\n";

    // Attempt to acquire the lock for 1 second, but proceed without the lock
    // after that, so we can still get some information if we're deadlocked
    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex,
            std::defer_lock);
    lock.try_lock_for(1s);

    if (mCapabilities.empty()) {
        output << "Capabilities: None\n";
    } else {
        output << "Capabilities:\n";
        for (auto capability : mCapabilities) {
            output << "  " << to_string(capability) << '\n';
        }
    }

    output << "Displays:\n";
    for (const auto& element : mDisplays) {
        const auto& display = element.second;
        output << display->dump();
    }
    output << '\n';

    // Release the lock before calling into HWC1, and since we no longer require
    // mutual exclusion to access mCapabilities or mDisplays
    lock.unlock();

    if (mHwc1Device->dump) {
        output << "HWC1 dump:\n";
        std::vector<char> hwc1Dump(4096);
        // Call with size - 1 to preserve a null character at the end
        mHwc1Device->dump(mHwc1Device, hwc1Dump.data(),
                static_cast<int>(hwc1Dump.size() - 1));
        output << hwc1Dump.data();
    }

    mDumpString = output.str();
    *outSize = static_cast<uint32_t>(mDumpString.size());
}

uint32_t CfHWC2::getMaxVirtualDisplayCount() {
    return mHwc1SupportsVirtualDisplays ? 1 : 0;
}

static bool isValid(Callback descriptor) {
    switch (descriptor) {
        case Callback::Hotplug: // Fall-through
        case Callback::Refresh: // Fall-through
        case Callback::Vsync: return true;
        default: return false;
    }
}

Error CfHWC2::registerCallback(Callback descriptor,
        hwc2_callback_data_t callbackData, hwc2_function_pointer_t pointer) {
    if (!isValid(descriptor)) {
        return Error::BadParameter;
    }

    ALOGV("registerCallback(%s, %p, %p)", to_string(descriptor).c_str(),
            callbackData, pointer);

    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    if (pointer != nullptr) {
        mCallbacks[descriptor] = {callbackData, pointer};
    } else {
        ALOGI("unregisterCallback(%s)", to_string(descriptor).c_str());
        mCallbacks.erase(descriptor);
        return Error::None;
    }

    bool hasPendingInvalidate = false;
    std::vector<hwc2_display_t> displayIds;
    std::vector<std::pair<hwc2_display_t, int64_t>> pendingVsyncs;
    std::vector<std::pair<hwc2_display_t, int>> pendingHotplugs;

    if (descriptor == Callback::Refresh) {
        hasPendingInvalidate = mHasPendingInvalidate;
        if (hasPendingInvalidate) {
            for (auto& displayPair : mDisplays) {
                displayIds.emplace_back(displayPair.first);
            }
        }
        mHasPendingInvalidate = false;
    } else if (descriptor == Callback::Vsync) {
        for (auto pending : mPendingVsyncs) {
            auto hwc1DisplayId = pending.first;
            if (mHwc1DisplayMap.count(hwc1DisplayId) == 0) {
                ALOGE("hwc1Vsync: Couldn't find display for HWC1 id %d",
                        hwc1DisplayId);
                continue;
            }
            auto displayId = mHwc1DisplayMap[hwc1DisplayId];
            auto timestamp = pending.second;
            pendingVsyncs.emplace_back(displayId, timestamp);
        }
        mPendingVsyncs.clear();
    } else if (descriptor == Callback::Hotplug) {
        // Hotplug the primary display
        pendingHotplugs.emplace_back(mHwc1DisplayMap[HWC_DISPLAY_PRIMARY],
                static_cast<int32_t>(Connection::Connected));

        for (auto pending : mPendingHotplugs) {
            auto hwc1DisplayId = pending.first;
            if (mHwc1DisplayMap.count(hwc1DisplayId) == 0) {
                ALOGE("hwc1Hotplug: Couldn't find display for HWC1 id %d",
                        hwc1DisplayId);
                continue;
            }
            auto displayId = mHwc1DisplayMap[hwc1DisplayId];
            auto connected = pending.second;
            pendingHotplugs.emplace_back(displayId, connected);
        }
    }

    // Call pending callbacks without the state lock held
    lock.unlock();

    if (hasPendingInvalidate) {
        auto refresh = reinterpret_cast<HWC2_PFN_REFRESH>(pointer);
        for (auto displayId : displayIds) {
            refresh(callbackData, displayId);
        }
    }
    if (!pendingVsyncs.empty()) {
        auto vsync = reinterpret_cast<HWC2_PFN_VSYNC>(pointer);
        for (auto& pendingVsync : pendingVsyncs) {
            vsync(callbackData, pendingVsync.first, pendingVsync.second);
        }
    }
    if (!pendingHotplugs.empty()) {
        auto hotplug = reinterpret_cast<HWC2_PFN_HOTPLUG>(pointer);
        for (auto& pendingHotplug : pendingHotplugs) {
            hotplug(callbackData, pendingHotplug.first, pendingHotplug.second);
        }
    }
    return Error::None;
}

// Display functions

std::atomic<hwc2_display_t> CfHWC2::Display::sNextId(1);

CfHWC2::Display::Display(CfHWC2& device, HWC2::DisplayType type)
  : mId(sNextId++),
    mDevice(device),
    mStateMutex(),
    mHwc1RequestedContents(nullptr),
    mRetireFence(),
    mChanges(),
    mHwc1Id(-1),
    mConfigs(),
    mActiveConfig(nullptr),
    mActiveColorMode(static_cast<android_color_mode_t>(-1)),
    mName(),
    mType(type),
    mPowerMode(PowerMode::Off),
    mVsyncEnabled(Vsync::Invalid),
    mClientTarget(),
    mOutputBuffer(),
    mHasColorTransform(false),
    mLayers(),
    mHwc1LayerMap(),
    mNumAvailableRects(0),
    mNextAvailableRect(nullptr),
    mGeometryChanged(false)
    {}

Error CfHWC2::Display::acceptChanges() {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mChanges) {
        ALOGV("[%" PRIu64 "] acceptChanges failed, not validated", mId);
        return Error::NotValidated;
    }

    ALOGV("[%" PRIu64 "] acceptChanges", mId);

    for (auto& change : mChanges->getTypeChanges()) {
        auto layerId = change.first;
        auto type = change.second;
        if (mDevice.mLayers.count(layerId) == 0) {
            // This should never happen but somehow does.
            ALOGW("Cannot accept change for unknown layer (%" PRIu64 ")",
                  layerId);
            continue;
        }
        auto layer = mDevice.mLayers[layerId];
        layer->setCompositionType(type);
    }

    mChanges->clearTypeChanges();

    return Error::None;
}

Error CfHWC2::Display::createLayer(hwc2_layer_t* outLayerId) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    auto layer = *mLayers.emplace(std::make_shared<Layer>(*this));
    mDevice.mLayers.emplace(std::make_pair(layer->getId(), layer));
    *outLayerId = layer->getId();
    ALOGV("[%" PRIu64 "] created layer %" PRIu64, mId, *outLayerId);
    markGeometryChanged();
    return Error::None;
}

Error CfHWC2::Display::destroyLayer(hwc2_layer_t layerId) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    const auto mapLayer = mDevice.mLayers.find(layerId);
    if (mapLayer == mDevice.mLayers.end()) {
        ALOGV("[%" PRIu64 "] destroyLayer(%" PRIu64 ") failed: no such layer",
                mId, layerId);
        return Error::BadLayer;
    }
    const auto layer = mapLayer->second;
    mDevice.mLayers.erase(mapLayer);
    const auto zRange = mLayers.equal_range(layer);
    for (auto current = zRange.first; current != zRange.second; ++current) {
        if (**current == *layer) {
            current = mLayers.erase(current);
            break;
        }
    }
    ALOGV("[%" PRIu64 "] destroyed layer %" PRIu64, mId, layerId);
    markGeometryChanged();
    return Error::None;
}

Error CfHWC2::Display::getActiveConfig(hwc2_config_t* outConfig) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mActiveConfig) {
        ALOGV("[%" PRIu64 "] getActiveConfig --> %s", mId,
                to_string(Error::BadConfig).c_str());
        return Error::BadConfig;
    }
    auto configId = mActiveConfig->getId();
    ALOGV("[%" PRIu64 "] getActiveConfig --> %u", mId, configId);
    *outConfig = configId;
    return Error::None;
}

Error CfHWC2::Display::getAttribute(hwc2_config_t configId,
        Attribute attribute, int32_t* outValue) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (configId > mConfigs.size() || !mConfigs[configId]->isOnDisplay(*this)) {
        ALOGV("[%" PRIu64 "] getAttribute failed: bad config (%u)", mId,
                configId);
        return Error::BadConfig;
    }
    *outValue = mConfigs[configId]->getAttribute(attribute);
    ALOGV("[%" PRIu64 "] getAttribute(%u, %s) --> %d", mId, configId,
            to_string(attribute).c_str(), *outValue);
    return Error::None;
}

Error CfHWC2::Display::getChangedCompositionTypes(
        uint32_t* outNumElements, hwc2_layer_t* outLayers, int32_t* outTypes) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mChanges) {
        ALOGE("[%" PRIu64 "] getChangedCompositionTypes failed: not validated",
                mId);
        return Error::NotValidated;
    }

    if ((outLayers == nullptr) || (outTypes == nullptr)) {
        *outNumElements = mChanges->getTypeChanges().size();
        return Error::None;
    }

    uint32_t numWritten = 0;
    for (const auto& element : mChanges->getTypeChanges()) {
        if (numWritten == *outNumElements) {
            break;
        }
        auto layerId = element.first;
        auto intType = static_cast<int32_t>(element.second);
        ALOGV("Adding %" PRIu64 " %s", layerId,
                to_string(element.second).c_str());
        outLayers[numWritten] = layerId;
        outTypes[numWritten] = intType;
        ++numWritten;
    }
    *outNumElements = numWritten;

    return Error::None;
}

Error CfHWC2::Display::getColorModes(uint32_t* outNumModes,
        int32_t* outModes) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!outModes) {
        *outNumModes = mColorModes.size();
        return Error::None;
    }
    uint32_t numModes = std::min(*outNumModes,
            static_cast<uint32_t>(mColorModes.size()));
    std::copy_n(mColorModes.cbegin(), numModes, outModes);
    *outNumModes = numModes;
    return Error::None;
}

Error CfHWC2::Display::getConfigs(uint32_t* outNumConfigs,
        hwc2_config_t* outConfigs) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!outConfigs) {
        *outNumConfigs = mConfigs.size();
        return Error::None;
    }
    uint32_t numWritten = 0;
    for (const auto& config : mConfigs) {
        if (numWritten == *outNumConfigs) {
            break;
        }
        outConfigs[numWritten] = config->getId();
        ++numWritten;
    }
    *outNumConfigs = numWritten;
    return Error::None;
}

Error CfHWC2::Display::getDozeSupport(int32_t* outSupport) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (mDevice.mHwc1MinorVersion < 4 || mHwc1Id != 0) {
        *outSupport = 0;
    } else {
        *outSupport = 1;
    }
    return Error::None;
}

Error CfHWC2::Display::getHdrCapabilities(uint32_t* outNumTypes,
        int32_t* /*outTypes*/, float* /*outMaxLuminance*/,
        float* /*outMaxAverageLuminance*/, float* /*outMinLuminance*/) {
    // This isn't supported on HWC1, so per the HWC2 header, return numTypes = 0
    *outNumTypes = 0;
    return Error::None;
}

Error CfHWC2::Display::getName(uint32_t* outSize, char* outName) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!outName) {
        *outSize = mName.size();
        return Error::None;
    }
    auto numCopied = mName.copy(outName, *outSize);
    *outSize = numCopied;
    return Error::None;
}

Error CfHWC2::Display::getReleaseFences(uint32_t* outNumElements,
        hwc2_layer_t* outLayers, int32_t* outFences) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    uint32_t numWritten = 0;
    bool outputsNonNull = (outLayers != nullptr) && (outFences != nullptr);
    for (const auto& layer : mLayers) {
        if (outputsNonNull && (numWritten == *outNumElements)) {
            break;
        }

        auto releaseFence = layer->getReleaseFence();
        if (releaseFence != MiniFence::NO_FENCE) {
            if (outputsNonNull) {
                outLayers[numWritten] = layer->getId();
                outFences[numWritten] = releaseFence->dup();
            }
            ++numWritten;
        }
    }
    *outNumElements = numWritten;

    return Error::None;
}

Error CfHWC2::Display::getRequests(int32_t* outDisplayRequests,
        uint32_t* outNumElements, hwc2_layer_t* outLayers,
        int32_t* outLayerRequests) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mChanges) {
        return Error::NotValidated;
    }

    if (outLayers == nullptr || outLayerRequests == nullptr) {
        *outNumElements = mChanges->getNumLayerRequests();
        return Error::None;
    }

    // Display requests (HWC2::DisplayRequest) are not supported by hwc1:
    // A hwc1 has always zero requests for the client.
    *outDisplayRequests = 0;

    uint32_t numWritten = 0;
    for (const auto& request : mChanges->getLayerRequests()) {
        if (numWritten == *outNumElements) {
            break;
        }
        outLayers[numWritten] = request.first;
        outLayerRequests[numWritten] = static_cast<int32_t>(request.second);
        ++numWritten;
    }

    return Error::None;
}

Error CfHWC2::Display::getType(int32_t* outType) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    *outType = static_cast<int32_t>(mType);
    return Error::None;
}

Error CfHWC2::Display::present(int32_t* outRetireFence) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (mChanges) {
        Error error = mDevice.setAllDisplays();
        if (error != Error::None) {
            ALOGE("[%" PRIu64 "] present: setAllDisplaysFailed (%s)", mId,
                    to_string(error).c_str());
            return error;
        }
    }

    *outRetireFence = mRetireFence.get()->dup();
    ALOGV("[%" PRIu64 "] present returning retire fence %d", mId,
            *outRetireFence);

    return Error::None;
}

Error CfHWC2::Display::setActiveConfig(hwc2_config_t configId) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    auto config = getConfig(configId);
    if (!config) {
        return Error::BadConfig;
    }
    if (config == mActiveConfig) {
        return Error::None;
    }

    if (mDevice.mHwc1MinorVersion >= 4) {
        uint32_t hwc1Id = 0;
        auto error = config->getHwc1IdForColorMode(mActiveColorMode, &hwc1Id);
        if (error != Error::None) {
            return error;
        }

        int intError = mDevice.mHwc1Device->setActiveConfig(mDevice.mHwc1Device,
                mHwc1Id, static_cast<int>(hwc1Id));
        if (intError != 0) {
            ALOGE("setActiveConfig: Failed to set active config on HWC1 (%d)",
                intError);
            return Error::BadConfig;
        }
        mActiveConfig = config;
    }

    return Error::None;
}

Error CfHWC2::Display::setClientTarget(buffer_handle_t target,
        int32_t acquireFence, int32_t /*dataspace*/, hwc_region_t /*damage*/) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    ALOGV("[%" PRIu64 "] setClientTarget(%p, %d)", mId, target, acquireFence);
    mClientTarget.setBuffer(target);
    mClientTarget.setFence(acquireFence);
    // dataspace and damage can't be used by HWC1, so ignore them
    return Error::None;
}

Error CfHWC2::Display::setColorMode(android_color_mode_t mode) {
    std::unique_lock<std::recursive_mutex> lock (mStateMutex);

    ALOGV("[%" PRIu64 "] setColorMode(%d)", mId, mode);

    if (mode == mActiveColorMode) {
        return Error::None;
    }
    if (mColorModes.count(mode) == 0) {
        ALOGE("[%" PRIu64 "] Mode %d not found in mColorModes", mId, mode);
        return Error::Unsupported;
    }

    if (mDevice.mHwc1MinorVersion >= 4) {
        uint32_t hwc1Config = 0;
        auto error = mActiveConfig->getHwc1IdForColorMode(mode, &hwc1Config);
        if (error != Error::None) {
            return error;
        }

        ALOGV("[%" PRIu64 "] Setting HWC1 config %u", mId, hwc1Config);
        int intError =
            mDevice.mHwc1Device->setActiveConfig(mDevice.mHwc1Device, mHwc1Id, hwc1Config);
        if (intError != 0) {
            ALOGE("[%" PRIu64 "] Failed to set HWC1 config (%d)", mId, intError);
            return Error::Unsupported;
        }
    }

    mActiveColorMode = mode;
    return Error::None;
}

Error CfHWC2::Display::setColorTransform(android_color_transform_t hint) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    ALOGV("%" PRIu64 "] setColorTransform(%d)", mId,
            static_cast<int32_t>(hint));
    mHasColorTransform = (hint != HAL_COLOR_TRANSFORM_IDENTITY);
    return Error::None;
}

Error CfHWC2::Display::setOutputBuffer(buffer_handle_t buffer,
        int32_t releaseFence) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    ALOGV("[%" PRIu64 "] setOutputBuffer(%p, %d)", mId, buffer, releaseFence);
    mOutputBuffer.setBuffer(buffer);
    mOutputBuffer.setFence(releaseFence);
    return Error::None;
}

static bool isValid(PowerMode mode) {
    switch (mode) {
        case PowerMode::Off: // Fall-through
        case PowerMode::DozeSuspend: // Fall-through
        case PowerMode::Doze: // Fall-through
        case PowerMode::On: return true;
    }
}

static int getHwc1PowerMode(PowerMode mode) {
    switch (mode) {
        case PowerMode::Off: return HWC_POWER_MODE_OFF;
        case PowerMode::DozeSuspend: return HWC_POWER_MODE_DOZE_SUSPEND;
        case PowerMode::Doze: return HWC_POWER_MODE_DOZE;
        case PowerMode::On: return HWC_POWER_MODE_NORMAL;
    }
}

Error CfHWC2::Display::setPowerMode(PowerMode mode) {
    if (!isValid(mode)) {
        return Error::BadParameter;
    }
    if (mode == mPowerMode) {
        return Error::None;
    }

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    int error = 0;
    if (mDevice.mHwc1MinorVersion < 4) {
        error = mDevice.mHwc1Device->blank(mDevice.mHwc1Device, mHwc1Id,
                mode == PowerMode::Off);
    } else {
        error = mDevice.mHwc1Device->setPowerMode(mDevice.mHwc1Device,
                mHwc1Id, getHwc1PowerMode(mode));
    }
    ALOGE_IF(error != 0, "setPowerMode: Failed to set power mode on HWC1 (%d)",
            error);

    ALOGV("[%" PRIu64 "] setPowerMode(%s)", mId, to_string(mode).c_str());
    mPowerMode = mode;
    return Error::None;
}

static bool isValid(Vsync enable) {
    switch (enable) {
        case Vsync::Enable: // Fall-through
        case Vsync::Disable: return true;
        case Vsync::Invalid: return false;
    }
}

Error CfHWC2::Display::setVsyncEnabled(Vsync enable) {
    if (!isValid(enable)) {
        return Error::BadParameter;
    }
    if (enable == mVsyncEnabled) {
        return Error::None;
    }

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    int error = mDevice.mHwc1Device->eventControl(mDevice.mHwc1Device,
            mHwc1Id, HWC_EVENT_VSYNC, enable == Vsync::Enable);
    ALOGE_IF(error != 0, "setVsyncEnabled: Failed to set vsync on HWC1 (%d)",
            error);

    mVsyncEnabled = enable;
    return Error::None;
}

Error CfHWC2::Display::validate(uint32_t* outNumTypes,
        uint32_t* outNumRequests) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mChanges) {
        if (!mDevice.prepareAllDisplays()) {
            return Error::BadDisplay;
        }
    } else {
        ALOGE("Validate was called more than once!");
    }

    *outNumTypes = mChanges->getNumTypes();
    *outNumRequests = mChanges->getNumLayerRequests();
    ALOGV("[%" PRIu64 "] validate --> %u types, %u requests", mId, *outNumTypes,
            *outNumRequests);
    for (auto request : mChanges->getTypeChanges()) {
        ALOGV("Layer %" PRIu64 " --> %s", request.first,
                to_string(request.second).c_str());
    }
    return *outNumTypes > 0 ? Error::HasChanges : Error::None;
}

Error CfHWC2::Display::updateLayerZ(hwc2_layer_t layerId, uint32_t z) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    const auto mapLayer = mDevice.mLayers.find(layerId);
    if (mapLayer == mDevice.mLayers.end()) {
        ALOGE("[%" PRIu64 "] updateLayerZ failed to find layer", mId);
        return Error::BadLayer;
    }

    const auto layer = mapLayer->second;
    const auto zRange = mLayers.equal_range(layer);
    bool layerOnDisplay = false;
    for (auto current = zRange.first; current != zRange.second; ++current) {
        if (**current == *layer) {
            if ((*current)->getZ() == z) {
                // Don't change anything if the Z hasn't changed
                return Error::None;
            }
            current = mLayers.erase(current);
            layerOnDisplay = true;
            break;
        }
    }

    if (!layerOnDisplay) {
        ALOGE("[%" PRIu64 "] updateLayerZ failed to find layer on display",
                mId);
        return Error::BadLayer;
    }

    layer->setZ(z);
    mLayers.emplace(std::move(layer));
    markGeometryChanged();

    return Error::None;
}

Error CfHWC2::Display::getClientTargetSupport(uint32_t width, uint32_t height,
                                      int32_t format, int32_t dataspace){
    if (mActiveConfig == nullptr) {
        return Error::Unsupported;
    }

    if (width == mActiveConfig->getAttribute(Attribute::Width) &&
            height == mActiveConfig->getAttribute(Attribute::Height) &&
            format == HAL_PIXEL_FORMAT_RGBA_8888 &&
            dataspace == HAL_DATASPACE_UNKNOWN) {
        return Error::None;
    }

    return Error::Unsupported;
}

// thess EDIDs are carefully generated according to the EDID spec version 1.3, more info
// can be found from the following file:
//   frameworks/native/services/surfaceflinger/DisplayHardware/DisplayIdentification.cpp
// approved pnp ids can be found here: https://uefi.org/pnp_id_list
// pnp id: GGL, name: EMU_display_0, last byte is checksum
// display id is local:8141603649153536
static const uint8_t sEDID0[] = {
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x1c, 0xec, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x1b, 0x10, 0x01, 0x03, 0x80, 0x50, 0x2d, 0x78, 0x0a, 0x0d, 0xc9, 0xa0, 0x57, 0x47, 0x98, 0x27,
    0x12, 0x48, 0x4c, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
    0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc,
    0x00, 0x45, 0x4d, 0x55, 0x5f, 0x64, 0x69, 0x73, 0x70, 0x6c, 0x61, 0x79, 0x5f, 0x30, 0x00, 0x4b
};

// pnp id: GGL, name: EMU_display_1
// display id is local:8140900251843329
static const uint8_t sEDID1[] = {
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x1c, 0xec, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x1b, 0x10, 0x01, 0x03, 0x80, 0x50, 0x2d, 0x78, 0x0a, 0x0d, 0xc9, 0xa0, 0x57, 0x47, 0x98, 0x27,
    0x12, 0x48, 0x4c, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
    0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc,
    0x00, 0x45, 0x4d, 0x55, 0x5f, 0x64, 0x69, 0x73, 0x70, 0x6c, 0x61, 0x79, 0x5f, 0x31, 0x00, 0x3b
};

// pnp id: GGL, name: EMU_display_2
// display id is local:8140940453066754
static const uint8_t sEDID2[] = {
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x1c, 0xec, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x1b, 0x10, 0x01, 0x03, 0x80, 0x50, 0x2d, 0x78, 0x0a, 0x0d, 0xc9, 0xa0, 0x57, 0x47, 0x98, 0x27,
    0x12, 0x48, 0x4c, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
    0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc,
    0x00, 0x45, 0x4d, 0x55, 0x5f, 0x64, 0x69, 0x73, 0x70, 0x6c, 0x61, 0x79, 0x5f, 0x32, 0x00, 0x49
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

Error CfHWC2::Display::getDisplayIdentificationData(uint8_t* outPort,
        uint32_t* outDataSize, uint8_t* outData) {
    ALOGV("%s DisplayId %u", __FUNCTION__, (uint32_t)mId);
    if (outPort == nullptr || outDataSize == nullptr)
        return Error::BadParameter;

    uint32_t len = std::min(*outDataSize, (uint32_t)ARRAY_SIZE(sEDID0));
    if (outData != nullptr && len < (uint32_t)ARRAY_SIZE(sEDID0)) {
        ALOGW("%s DisplayId %u, small buffer size: %u is specified",
                __FUNCTION__, (uint32_t)mId, len);
    }
    *outDataSize = ARRAY_SIZE(sEDID0);
    switch (mId) {
        case 0:
            *outPort = 0;
            if (outData)
                memcpy(outData, sEDID0, len);
            break;

        case 1:
            *outPort = 1;
            if (outData)
                memcpy(outData, sEDID1, len);
            break;

        case 2:
            *outPort = 2;
            if (outData)
                memcpy(outData, sEDID2, len);
            break;

        default:
            *outPort = (uint8_t)mId;
            if (outData) {
                memcpy(outData, sEDID2, len);
                uint32_t size = ARRAY_SIZE(sEDID0);
                // change the name to EMU_display_<mID>
                // note the 3rd char from back is the number, _0, _1, _2, etc.
                if (len >= size - 2)
                    outData[size-3] = '0' + (uint8_t)mId;
                if (len >= size) {
                    // update the last byte, which is checksum byte
                    uint8_t checksum = -(uint8_t)std::accumulate(
                            outData, outData + size - 1, static_cast<uint8_t>(0));
                    outData[size - 1] = checksum;
                }
            }
            break;
    }

    return Error::None;
}

Error CfHWC2::Display::getDisplayCapabilities(uint32_t* outNumCapabilities,
        uint32_t* outCapabilities) {
    ALOGV("%s DisplayId %u", __FUNCTION__, (uint32_t)mId);
    if (outNumCapabilities == nullptr) {
        return Error::None;
    }

    bool brightness_support = true;
    bool doze_support = true;

    uint32_t count = 1  + static_cast<uint32_t>(doze_support) + (brightness_support ? 1 : 0);
    int index = 0;
    if (outCapabilities != nullptr && (*outNumCapabilities >= count)) {
        outCapabilities[index++] = HWC2_DISPLAY_CAPABILITY_SKIP_CLIENT_COLOR_TRANSFORM;
        if (doze_support) {
            outCapabilities[index++] = HWC2_DISPLAY_CAPABILITY_DOZE;
        }
        if (brightness_support) {
            outCapabilities[index++] = HWC2_DISPLAY_CAPABILITY_BRIGHTNESS;
       }
    }

    *outNumCapabilities = count;
    return Error::None;
}

Error CfHWC2::Display::getDisplayBrightnessSupport(bool *out_support) {
    *out_support = false;
    return Error::None;
}

Error CfHWC2::Display::setDisplayBrightness(float brightness) {
    ALOGW("TODO: setDisplayBrightness() is not implemented yet: brightness=%f", brightness);
    return Error::None;
}

static constexpr uint32_t ATTRIBUTES_WITH_COLOR[] = {
    HWC_DISPLAY_VSYNC_PERIOD,
    HWC_DISPLAY_WIDTH,
    HWC_DISPLAY_HEIGHT,
    HWC_DISPLAY_DPI_X,
    HWC_DISPLAY_DPI_Y,
    HWC_DISPLAY_COLOR_TRANSFORM,
    HWC_DISPLAY_NO_ATTRIBUTE,
};

static constexpr uint32_t ATTRIBUTES_WITHOUT_COLOR[] = {
    HWC_DISPLAY_VSYNC_PERIOD,
    HWC_DISPLAY_WIDTH,
    HWC_DISPLAY_HEIGHT,
    HWC_DISPLAY_DPI_X,
    HWC_DISPLAY_DPI_Y,
    HWC_DISPLAY_NO_ATTRIBUTE,
};

static constexpr size_t NUM_ATTRIBUTES_WITH_COLOR =
        sizeof(ATTRIBUTES_WITH_COLOR) / sizeof(uint32_t);
static_assert(sizeof(ATTRIBUTES_WITH_COLOR) > sizeof(ATTRIBUTES_WITHOUT_COLOR),
        "Attribute tables have unexpected sizes");

static constexpr uint32_t ATTRIBUTE_MAP_WITH_COLOR[] = {
    6, // HWC_DISPLAY_NO_ATTRIBUTE = 0
    0, // HWC_DISPLAY_VSYNC_PERIOD = 1,
    1, // HWC_DISPLAY_WIDTH = 2,
    2, // HWC_DISPLAY_HEIGHT = 3,
    3, // HWC_DISPLAY_DPI_X = 4,
    4, // HWC_DISPLAY_DPI_Y = 5,
    5, // HWC_DISPLAY_COLOR_TRANSFORM = 6,
};

static constexpr uint32_t ATTRIBUTE_MAP_WITHOUT_COLOR[] = {
    5, // HWC_DISPLAY_NO_ATTRIBUTE = 0
    0, // HWC_DISPLAY_VSYNC_PERIOD = 1,
    1, // HWC_DISPLAY_WIDTH = 2,
    2, // HWC_DISPLAY_HEIGHT = 3,
    3, // HWC_DISPLAY_DPI_X = 4,
    4, // HWC_DISPLAY_DPI_Y = 5,
};

template <uint32_t attribute>
static constexpr bool attributesMatch()
{
    bool match = (attribute ==
            ATTRIBUTES_WITH_COLOR[ATTRIBUTE_MAP_WITH_COLOR[attribute]]);
    if (attribute == HWC_DISPLAY_COLOR_TRANSFORM) {
        return match;
    }

    return match && (attribute ==
            ATTRIBUTES_WITHOUT_COLOR[ATTRIBUTE_MAP_WITHOUT_COLOR[attribute]]);
}
static_assert(attributesMatch<HWC_DISPLAY_VSYNC_PERIOD>(),
        "Tables out of sync");
static_assert(attributesMatch<HWC_DISPLAY_WIDTH>(), "Tables out of sync");
static_assert(attributesMatch<HWC_DISPLAY_HEIGHT>(), "Tables out of sync");
static_assert(attributesMatch<HWC_DISPLAY_DPI_X>(), "Tables out of sync");
static_assert(attributesMatch<HWC_DISPLAY_DPI_Y>(), "Tables out of sync");
static_assert(attributesMatch<HWC_DISPLAY_COLOR_TRANSFORM>(),
        "Tables out of sync");

void CfHWC2::Display::populateConfigs() {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    ALOGV("[%" PRIu64 "] populateConfigs", mId);

    if (mHwc1Id == -1) {
        ALOGE("populateConfigs: HWC1 ID not set");
        return;
    }

    const size_t MAX_NUM_CONFIGS = 128;
    uint32_t configs[MAX_NUM_CONFIGS] = {};
    size_t numConfigs = MAX_NUM_CONFIGS;
    mDevice.mHwc1Device->getDisplayConfigs(mDevice.mHwc1Device, mHwc1Id,
            configs, &numConfigs);

    for (size_t c = 0; c < numConfigs; ++c) {
        uint32_t hwc1ConfigId = configs[c];
        auto newConfig = std::make_shared<Config>(*this);

        int32_t values[NUM_ATTRIBUTES_WITH_COLOR] = {};
        bool hasColor = true;
        auto result = mDevice.mHwc1Device->getDisplayAttributes(
                mDevice.mHwc1Device, mHwc1Id, hwc1ConfigId,
                ATTRIBUTES_WITH_COLOR, values);
        if (result != 0) {
            mDevice.mHwc1Device->getDisplayAttributes(mDevice.mHwc1Device,
                    mHwc1Id, hwc1ConfigId, ATTRIBUTES_WITHOUT_COLOR, values);
            hasColor = false;
        }

        auto attributeMap = hasColor ?
                ATTRIBUTE_MAP_WITH_COLOR : ATTRIBUTE_MAP_WITHOUT_COLOR;

        newConfig->setAttribute(Attribute::VsyncPeriod,
                values[attributeMap[HWC_DISPLAY_VSYNC_PERIOD]]);
        newConfig->setAttribute(Attribute::Width,
                values[attributeMap[HWC_DISPLAY_WIDTH]]);
        newConfig->setAttribute(Attribute::Height,
                values[attributeMap[HWC_DISPLAY_HEIGHT]]);
        newConfig->setAttribute(Attribute::DpiX,
                values[attributeMap[HWC_DISPLAY_DPI_X]]);
        newConfig->setAttribute(Attribute::DpiY,
                values[attributeMap[HWC_DISPLAY_DPI_Y]]);
        if (hasColor) {
            // In HWC1, color modes are referred to as color transforms. To avoid confusion with
            // the HWC2 concept of color transforms, we internally refer to them as color modes for
            // both HWC1 and 2.
            newConfig->setAttribute(ColorMode,
                    values[attributeMap[HWC_DISPLAY_COLOR_TRANSFORM]]);
        }

        // We can only do this after attempting to read the color mode
        newConfig->setHwc1Id(hwc1ConfigId);

        for (auto& existingConfig : mConfigs) {
            if (existingConfig->merge(*newConfig)) {
                ALOGV("Merged config %d with existing config %u: %s",
                        hwc1ConfigId, existingConfig->getId(),
                        existingConfig->toString().c_str());
                newConfig.reset();
                break;
            }
        }

        // If it wasn't merged with any existing config, add it to the end
        if (newConfig) {
            newConfig->setId(static_cast<hwc2_config_t>(mConfigs.size()));
            ALOGV("Found new config %u: %s", newConfig->getId(),
                    newConfig->toString().c_str());
            mConfigs.emplace_back(std::move(newConfig));
        }
    }

    initializeActiveConfig();
    populateColorModes();
}

void CfHWC2::Display::populateConfigs(uint32_t width, uint32_t height) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    mConfigs.emplace_back(std::make_shared<Config>(*this));
    auto& config = mConfigs[0];

    config->setAttribute(Attribute::Width, static_cast<int32_t>(width));
    config->setAttribute(Attribute::Height, static_cast<int32_t>(height));
    config->setHwc1Id(0);
    config->setId(0);
    mActiveConfig = config;
}

bool CfHWC2::Display::prepare() {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    // Only prepare display contents for displays HWC1 knows about
    if (mHwc1Id == -1) {
        return true;
    }

    // It doesn't make sense to prepare a display for which there is no active
    // config, so return early
    if (!mActiveConfig) {
        ALOGE("[%" PRIu64 "] Attempted to prepare, but no config active", mId);
        return false;
    }

    allocateRequestedContents();
    assignHwc1LayerIds();

    mHwc1RequestedContents->retireFenceFd = -1;
    mHwc1RequestedContents->flags = 0;
    if (mGeometryChanged) {
        mHwc1RequestedContents->flags |= HWC_GEOMETRY_CHANGED;
    }
    mHwc1RequestedContents->outbuf = mOutputBuffer.getBuffer();
    mHwc1RequestedContents->outbufAcquireFenceFd = mOutputBuffer.getFence();

    // +1 is for framebuffer target layer.
    mHwc1RequestedContents->numHwLayers = mLayers.size() + 1;
    for (auto& layer : mLayers) {
        auto& hwc1Layer = mHwc1RequestedContents->hwLayers[layer->getHwc1Id()];
        hwc1Layer.releaseFenceFd = -1;
        hwc1Layer.acquireFenceFd = -1;
        ALOGV("Applying states for layer %" PRIu64 " ", layer->getId());
        layer->applyState(hwc1Layer);
    }

    prepareFramebufferTarget();

    resetGeometryMarker();

    return true;
}

void CfHWC2::Display::generateChanges() {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    mChanges.reset(new Changes);

    size_t numLayers = mHwc1RequestedContents->numHwLayers;
    for (size_t hwc1Id = 0; hwc1Id < numLayers; ++hwc1Id) {
        const auto& receivedLayer = mHwc1RequestedContents->hwLayers[hwc1Id];
        if (mHwc1LayerMap.count(hwc1Id) == 0) {
            ALOGE_IF(receivedLayer.compositionType != HWC_FRAMEBUFFER_TARGET,
                    "generateChanges: HWC1 layer %zd doesn't have a"
                    " matching HWC2 layer, and isn't the framebuffer target",
                    hwc1Id);
            continue;
        }

        Layer& layer = *mHwc1LayerMap[hwc1Id];
        updateTypeChanges(receivedLayer, layer);
        updateLayerRequests(receivedLayer, layer);
    }
}

bool CfHWC2::Display::hasChanges() const {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);
    return mChanges != nullptr;
}

Error CfHWC2::Display::set(hwc_display_contents_1& hwcContents) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mChanges || (mChanges->getNumTypes() > 0)) {
        ALOGE("[%" PRIu64 "] set failed: not validated", mId);
        return Error::NotValidated;
    }

    // Set up the client/framebuffer target
    auto numLayers = hwcContents.numHwLayers;

    // Close acquire fences on FRAMEBUFFER layers, since they will not be used
    // by HWC
    for (size_t l = 0; l < numLayers - 1; ++l) {
        auto& layer = hwcContents.hwLayers[l];
        if (layer.compositionType == HWC_FRAMEBUFFER) {
            ALOGV("Closing fence %d for layer %zd", layer.acquireFenceFd, l);
            close(layer.acquireFenceFd);
            layer.acquireFenceFd = -1;
        }
    }

    auto& clientTargetLayer = hwcContents.hwLayers[numLayers - 1];
    if (clientTargetLayer.compositionType == HWC_FRAMEBUFFER_TARGET) {
        clientTargetLayer.handle = mClientTarget.getBuffer();
        clientTargetLayer.acquireFenceFd = mClientTarget.getFence();
    } else {
        ALOGE("[%" PRIu64 "] set: last HWC layer wasn't FRAMEBUFFER_TARGET",
                mId);
    }

    mChanges.reset();

    return Error::None;
}

void CfHWC2::Display::addRetireFence(int fenceFd) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);
    mRetireFence.add(fenceFd);
}

void CfHWC2::Display::addReleaseFences(
        const hwc_display_contents_1_t& hwcContents) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    size_t numLayers = hwcContents.numHwLayers;
    for (size_t hwc1Id = 0; hwc1Id < numLayers; ++hwc1Id) {
        const auto& receivedLayer = hwcContents.hwLayers[hwc1Id];
        if (mHwc1LayerMap.count(hwc1Id) == 0) {
            if (receivedLayer.compositionType != HWC_FRAMEBUFFER_TARGET) {
                ALOGE("addReleaseFences: HWC1 layer %zd doesn't have a"
                        " matching HWC2 layer, and isn't the framebuffer"
                        " target", hwc1Id);
            }
            // Close the framebuffer target release fence since we will use the
            // display retire fence instead
            if (receivedLayer.releaseFenceFd != -1) {
                close(receivedLayer.releaseFenceFd);
            }
            continue;
        }

        Layer& layer = *mHwc1LayerMap[hwc1Id];
        ALOGV("Adding release fence %d to layer %" PRIu64,
                receivedLayer.releaseFenceFd, layer.getId());
        layer.addReleaseFence(receivedLayer.releaseFenceFd);
    }
}

bool CfHWC2::Display::hasColorTransform() const {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);
    return mHasColorTransform;
}

static std::string hwc1CompositionString(int32_t type) {
    switch (type) {
        case HWC_FRAMEBUFFER: return "Framebuffer";
        case HWC_OVERLAY: return "Overlay";
        case HWC_BACKGROUND: return "Background";
        case HWC_FRAMEBUFFER_TARGET: return "FramebufferTarget";
        case HWC_SIDEBAND: return "Sideband";
        case HWC_CURSOR_OVERLAY: return "CursorOverlay";
        default:
            return std::string("Unknown (") + std::to_string(type) + ")";
    }
}

static std::string hwc1TransformString(int32_t transform) {
    switch (transform) {
        case 0: return "None";
        case HWC_TRANSFORM_FLIP_H: return "FlipH";
        case HWC_TRANSFORM_FLIP_V: return "FlipV";
        case HWC_TRANSFORM_ROT_90: return "Rotate90";
        case HWC_TRANSFORM_ROT_180: return "Rotate180";
        case HWC_TRANSFORM_ROT_270: return "Rotate270";
        case HWC_TRANSFORM_FLIP_H_ROT_90: return "FlipHRotate90";
        case HWC_TRANSFORM_FLIP_V_ROT_90: return "FlipVRotate90";
        default:
            return std::string("Unknown (") + std::to_string(transform) + ")";
    }
}

static std::string hwc1BlendModeString(int32_t mode) {
    switch (mode) {
        case HWC_BLENDING_NONE: return "None";
        case HWC_BLENDING_PREMULT: return "Premultiplied";
        case HWC_BLENDING_COVERAGE: return "Coverage";
        default:
            return std::string("Unknown (") + std::to_string(mode) + ")";
    }
}

static std::string rectString(hwc_rect_t rect) {
    std::stringstream output;
    output << "[" << rect.left << ", " << rect.top << ", ";
    output << rect.right << ", " << rect.bottom << "]";
    return output.str();
}

static std::string approximateFloatString(float f) {
    if (static_cast<float>(static_cast<int32_t>(f)) == f) {
        return std::to_string(static_cast<int32_t>(f));
    }
    int32_t truncated = static_cast<int32_t>(f * 10);
    bool approximate = (static_cast<float>(truncated) != f * 10);
    const size_t BUFFER_SIZE = 32;
    char buffer[BUFFER_SIZE] = {};
    auto bytesWritten = snprintf(buffer, BUFFER_SIZE,
            "%s%.1f", approximate ? "~" : "", f);
    return std::string(buffer, bytesWritten);
}

static std::string frectString(hwc_frect_t frect) {
    std::stringstream output;
    output << "[" << approximateFloatString(frect.left) << ", ";
    output << approximateFloatString(frect.top) << ", ";
    output << approximateFloatString(frect.right) << ", ";
    output << approximateFloatString(frect.bottom) << "]";
    return output.str();
}

static std::string colorString(hwc_color_t color) {
    std::stringstream output;
    output << "RGBA [";
    output << static_cast<int32_t>(color.r) << ", ";
    output << static_cast<int32_t>(color.g) << ", ";
    output << static_cast<int32_t>(color.b) << ", ";
    output << static_cast<int32_t>(color.a) << "]";
    return output.str();
}

static std::string alphaString(float f) {
    const size_t BUFFER_SIZE = 8;
    char buffer[BUFFER_SIZE] = {};
    auto bytesWritten = snprintf(buffer, BUFFER_SIZE, "%.3f", f);
    return std::string(buffer, bytesWritten);
}

static std::string to_string(const hwc_layer_1_t& hwcLayer,
        int32_t hwc1MinorVersion) {
    const char* fill = "          ";

    std::stringstream output;

    output << "  Composition: " <<
            hwc1CompositionString(hwcLayer.compositionType);

    if (hwcLayer.compositionType == HWC_BACKGROUND) {
        output << "  Color: " << colorString(hwcLayer.backgroundColor) << '\n';
    } else if (hwcLayer.compositionType == HWC_SIDEBAND) {
        output << "  Stream: " << hwcLayer.sidebandStream << '\n';
    } else {
        output << "  Buffer: " << hwcLayer.handle << "/" <<
                hwcLayer.acquireFenceFd << '\n';
    }

    output << fill << "Display frame: " << rectString(hwcLayer.displayFrame) <<
            '\n';

    output << fill << "Source crop: ";
    if (hwc1MinorVersion >= 3) {
        output << frectString(hwcLayer.sourceCropf) << '\n';
    } else {
        output << rectString(hwcLayer.sourceCropi) << '\n';
    }

    output << fill << "Transform: " << hwc1TransformString(hwcLayer.transform);
    output << "  Blend mode: " << hwc1BlendModeString(hwcLayer.blending);
    if (hwcLayer.planeAlpha != 0xFF) {
        output << "  Alpha: " << alphaString(hwcLayer.planeAlpha / 255.0f);
    }
    output << '\n';

    if (hwcLayer.hints != 0) {
        output << fill << "Hints:";
        if ((hwcLayer.hints & HWC_HINT_TRIPLE_BUFFER) != 0) {
            output << " TripleBuffer";
        }
        if ((hwcLayer.hints & HWC_HINT_CLEAR_FB) != 0) {
            output << " ClearFB";
        }
        output << '\n';
    }

    if (hwcLayer.flags != 0) {
        output << fill << "Flags:";
        if ((hwcLayer.flags & HWC_SKIP_LAYER) != 0) {
            output << " SkipLayer";
        }
        if ((hwcLayer.flags & HWC_IS_CURSOR_LAYER) != 0) {
            output << " IsCursorLayer";
        }
        output << '\n';
    }

    return output.str();
}

static std::string to_string(const hwc_display_contents_1_t& hwcContents,
        int32_t hwc1MinorVersion) {
    const char* fill = "      ";

    std::stringstream output;
    output << fill << "Geometry changed: " <<
            ((hwcContents.flags & HWC_GEOMETRY_CHANGED) != 0 ? "Y\n" : "N\n");

    output << fill << hwcContents.numHwLayers << " Layer" <<
            ((hwcContents.numHwLayers == 1) ? "\n" : "s\n");
    for (size_t layer = 0; layer < hwcContents.numHwLayers; ++layer) {
        output << fill << "  Layer " << layer;
        output << to_string(hwcContents.hwLayers[layer], hwc1MinorVersion);
    }

    if (hwcContents.outbuf != nullptr) {
        output << fill << "Output buffer: " << hwcContents.outbuf << "/" <<
                hwcContents.outbufAcquireFenceFd << '\n';
    }

    return output.str();
}

std::string CfHWC2::Display::dump() const {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    std::stringstream output;

    output << "  Display " << mId << ": ";
    output << to_string(mType) << "  ";
    output << "HWC1 ID: " << mHwc1Id << "  ";
    output << "Power mode: " << to_string(mPowerMode) << "  ";
    output << "Vsync: " << to_string(mVsyncEnabled) << '\n';

    output << "    Color modes [active]:";
    for (const auto& mode : mColorModes) {
        if (mode == mActiveColorMode) {
            output << " [" << mode << ']';
        } else {
            output << " " << mode;
        }
    }
    output << '\n';

    output << "    " << mConfigs.size() << " Config" <<
            (mConfigs.size() == 1 ? "" : "s") << " (* active)\n";
    for (const auto& config : mConfigs) {
        output << (config == mActiveConfig ? "    * " : "      ");
        output << config->toString(true) << '\n';
    }

    output << "    " << mLayers.size() << " Layer" <<
            (mLayers.size() == 1 ? "" : "s") << '\n';
    for (const auto& layer : mLayers) {
        output << layer->dump();
    }

    output << "    Client target: " << mClientTarget.getBuffer() << '\n';

    if (mOutputBuffer.getBuffer() != nullptr) {
        output << "    Output buffer: " << mOutputBuffer.getBuffer() << '\n';
    }

    if (mHwc1RequestedContents) {
        output << "    Last requested HWC1 state\n";
        output << to_string(*mHwc1RequestedContents, mDevice.mHwc1MinorVersion);
    }

    return output.str();
}

hwc_rect_t* CfHWC2::Display::GetRects(size_t numRects) {
    if (numRects == 0) {
        return nullptr;
    }

    if (numRects > mNumAvailableRects) {
        // This should NEVER happen since we calculated how many rects the
        // display would need.
        ALOGE("Rect allocation failure! SF is likely to crash soon!");
        return nullptr;

    }
    hwc_rect_t* rects = mNextAvailableRect;
    mNextAvailableRect += numRects;
    mNumAvailableRects -= numRects;
    return rects;
}

hwc_display_contents_1* CfHWC2::Display::getDisplayContents() {
    return mHwc1RequestedContents.get();
}

void CfHWC2::Display::Config::setAttribute(HWC2::Attribute attribute,
        int32_t value) {
    mAttributes[attribute] = value;
}

int32_t CfHWC2::Display::Config::getAttribute(Attribute attribute) const {
    if (mAttributes.count(attribute) == 0) {
        return -1;
    }
    return mAttributes.at(attribute);
}

void CfHWC2::Display::Config::setHwc1Id(uint32_t id) {
    android_color_mode_t colorMode = static_cast<android_color_mode_t>(getAttribute(ColorMode));
    mHwc1Ids.emplace(colorMode, id);
}

bool CfHWC2::Display::Config::hasHwc1Id(uint32_t id) const {
    for (const auto& idPair : mHwc1Ids) {
        if (id == idPair.second) {
            return true;
        }
    }
    return false;
}

Error CfHWC2::Display::Config::getColorModeForHwc1Id(
        uint32_t id, android_color_mode_t* outMode) const {
    for (const auto& idPair : mHwc1Ids) {
        if (id == idPair.second) {
            *outMode = idPair.first;
            return Error::None;
        }
    }
    ALOGE("Unable to find color mode for HWC ID %" PRIu32 " on config %u", id, mId);
    return Error::BadParameter;
}

Error CfHWC2::Display::Config::getHwc1IdForColorMode(android_color_mode_t mode,
        uint32_t* outId) const {
    for (const auto& idPair : mHwc1Ids) {
        if (mode == idPair.first) {
            *outId = idPair.second;
            return Error::None;
        }
    }
    ALOGE("Unable to find HWC1 ID for color mode %d on config %u", mode, mId);
    return Error::BadParameter;
}

bool CfHWC2::Display::Config::merge(const Config& other) {
    auto attributes = {HWC2::Attribute::Width, HWC2::Attribute::Height,
            HWC2::Attribute::VsyncPeriod, HWC2::Attribute::DpiX,
            HWC2::Attribute::DpiY};
    for (auto attribute : attributes) {
        if (getAttribute(attribute) != other.getAttribute(attribute)) {
            return false;
        }
    }
    android_color_mode_t otherColorMode =
            static_cast<android_color_mode_t>(other.getAttribute(ColorMode));
    if (mHwc1Ids.count(otherColorMode) != 0) {
        ALOGE("Attempted to merge two configs (%u and %u) which appear to be "
                "identical", mHwc1Ids.at(otherColorMode),
                other.mHwc1Ids.at(otherColorMode));
        return false;
    }
    mHwc1Ids.emplace(otherColorMode,
            other.mHwc1Ids.at(otherColorMode));
    return true;
}

std::set<android_color_mode_t> CfHWC2::Display::Config::getColorModes() const {
    std::set<android_color_mode_t> colorModes;
    for (const auto& idPair : mHwc1Ids) {
        colorModes.emplace(idPair.first);
    }
    return colorModes;
}

std::string CfHWC2::Display::Config::toString(bool splitLine) const {
    std::string output;

    const size_t BUFFER_SIZE = 100;
    char buffer[BUFFER_SIZE] = {};
    auto writtenBytes = snprintf(buffer, BUFFER_SIZE,
            "%u x %u", mAttributes.at(HWC2::Attribute::Width),
            mAttributes.at(HWC2::Attribute::Height));
    output.append(buffer, writtenBytes);

    if (mAttributes.count(HWC2::Attribute::VsyncPeriod) != 0) {
        std::memset(buffer, 0, BUFFER_SIZE);
        writtenBytes = snprintf(buffer, BUFFER_SIZE, " @ %.1f Hz",
                1e9 / mAttributes.at(HWC2::Attribute::VsyncPeriod));
        output.append(buffer, writtenBytes);
    }

    if (mAttributes.count(HWC2::Attribute::DpiX) != 0 &&
            mAttributes.at(HWC2::Attribute::DpiX) != -1) {
        std::memset(buffer, 0, BUFFER_SIZE);
        writtenBytes =
                snprintf(buffer, BUFFER_SIZE, ", DPI: %.1f x %.1f",
                         static_cast<float>(mAttributes.at(HWC2::Attribute::DpiX)) / 1000.0f,
                         static_cast<float>(mAttributes.at(HWC2::Attribute::DpiY)) / 1000.0f);
        output.append(buffer, writtenBytes);
    }

    std::memset(buffer, 0, BUFFER_SIZE);
    if (splitLine) {
        writtenBytes = snprintf(buffer, BUFFER_SIZE,
                "\n        HWC1 ID/Color transform:");
    } else {
        writtenBytes = snprintf(buffer, BUFFER_SIZE,
                ", HWC1 ID/Color transform:");
    }
    output.append(buffer, writtenBytes);


    for (const auto& id : mHwc1Ids) {
        android_color_mode_t colorMode = id.first;
        uint32_t hwc1Id = id.second;
        std::memset(buffer, 0, BUFFER_SIZE);
        if (colorMode == mDisplay.mActiveColorMode) {
            writtenBytes = snprintf(buffer, BUFFER_SIZE, " [%u/%d]", hwc1Id,
                    colorMode);
        } else {
            writtenBytes = snprintf(buffer, BUFFER_SIZE, " %u/%d", hwc1Id,
                    colorMode);
        }
        output.append(buffer, writtenBytes);
    }

    return output;
}

std::shared_ptr<const CfHWC2::Display::Config>
        CfHWC2::Display::getConfig(hwc2_config_t configId) const {
    if (configId > mConfigs.size() || !mConfigs[configId]->isOnDisplay(*this)) {
        return nullptr;
    }
    return mConfigs[configId];
}

void CfHWC2::Display::populateColorModes() {
    mColorModes = mConfigs[0]->getColorModes();
    for (const auto& config : mConfigs) {
        std::set<android_color_mode_t> intersection;
        auto configModes = config->getColorModes();
        std::set_intersection(mColorModes.cbegin(), mColorModes.cend(),
                configModes.cbegin(), configModes.cend(),
                std::inserter(intersection, intersection.begin()));
        std::swap(intersection, mColorModes);
    }
}

void CfHWC2::Display::initializeActiveConfig() {
    if (mDevice.mHwc1Device->getActiveConfig == nullptr) {
        ALOGV("getActiveConfig is null, choosing config 0");
        mActiveConfig = mConfigs[0];
        mActiveColorMode = HAL_COLOR_MODE_NATIVE;
        return;
    }

    auto activeConfig = mDevice.mHwc1Device->getActiveConfig(
            mDevice.mHwc1Device, mHwc1Id);

    // Some devices startup without an activeConfig:
    // We need to set one ourselves.
    if (activeConfig == HWC_ERROR) {
        ALOGV("There is no active configuration: Picking the first one: 0.");
        const int defaultIndex = 0;
        mDevice.mHwc1Device->setActiveConfig(mDevice.mHwc1Device, mHwc1Id, defaultIndex);
        activeConfig = defaultIndex;
    }

    for (const auto& config : mConfigs) {
        if (config->hasHwc1Id(activeConfig)) {
            ALOGE("Setting active config to %d for HWC1 config %u", config->getId(), activeConfig);
            mActiveConfig = config;
            if (config->getColorModeForHwc1Id(activeConfig, &mActiveColorMode) != Error::None) {
                // This should never happen since we checked for the config's presence before
                // setting it as active.
                ALOGE("Unable to find color mode for active HWC1 config %d", config->getId());
                mActiveColorMode = HAL_COLOR_MODE_NATIVE;
            }
            break;
        }
    }
    if (!mActiveConfig) {
        ALOGV("Unable to find active HWC1 config %u, defaulting to "
                "config 0", activeConfig);
        mActiveConfig = mConfigs[0];
        mActiveColorMode = HAL_COLOR_MODE_NATIVE;
    }




}

void CfHWC2::Display::allocateRequestedContents() {
    // What needs to be allocated:
    // 1 hwc_display_contents_1_t
    // 1 hwc_layer_1_t for each layer
    // 1 hwc_rect_t for each layer's surfaceDamage
    // 1 hwc_rect_t for each layer's visibleRegion
    // 1 hwc_layer_1_t for the framebuffer
    // 1 hwc_rect_t for the framebuffer's visibleRegion

    // Count # of surfaceDamage
    size_t numSurfaceDamages = 0;
    for (const auto& layer : mLayers) {
        numSurfaceDamages += layer->getNumSurfaceDamages();
    }

    // Count # of visibleRegions (start at 1 for mandatory framebuffer target
    // region)
    size_t numVisibleRegion = 1;
    for (const auto& layer : mLayers) {
        numVisibleRegion += layer->getNumVisibleRegions();
    }

    size_t numRects = numVisibleRegion + numSurfaceDamages;
    auto numLayers = mLayers.size() + 1;
    size_t size = sizeof(hwc_display_contents_1_t) +
            sizeof(hwc_layer_1_t) * numLayers +
            sizeof(hwc_rect_t) * numRects;
    auto contents = static_cast<hwc_display_contents_1_t*>(std::calloc(size, 1));
    mHwc1RequestedContents.reset(contents);
    mNextAvailableRect = reinterpret_cast<hwc_rect_t*>(&contents->hwLayers[numLayers]);
    mNumAvailableRects = numRects;
}

void CfHWC2::Display::assignHwc1LayerIds() {
    mHwc1LayerMap.clear();
    size_t nextHwc1Id = 0;
    for (auto& layer : mLayers) {
        mHwc1LayerMap[nextHwc1Id] = layer;
        layer->setHwc1Id(nextHwc1Id++);
    }
}

void CfHWC2::Display::updateTypeChanges(const hwc_layer_1_t& hwc1Layer,
        const Layer& layer) {
    auto layerId = layer.getId();
    switch (hwc1Layer.compositionType) {
        case HWC_FRAMEBUFFER:
            if (layer.getCompositionType() != Composition::Client) {
                mChanges->addTypeChange(layerId, Composition::Client);
            }
            break;
        case HWC_OVERLAY:
            if (layer.getCompositionType() != Composition::Device) {
                mChanges->addTypeChange(layerId, Composition::Device);
            }
            break;
        case HWC_BACKGROUND:
            ALOGE_IF(layer.getCompositionType() != Composition::SolidColor,
                    "updateTypeChanges: HWC1 requested BACKGROUND, but HWC2"
                    " wasn't expecting SolidColor");
            break;
        case HWC_FRAMEBUFFER_TARGET:
            // Do nothing, since it shouldn't be modified by HWC1
            break;
        case HWC_SIDEBAND:
            ALOGE_IF(layer.getCompositionType() != Composition::Sideband,
                    "updateTypeChanges: HWC1 requested SIDEBAND, but HWC2"
                    " wasn't expecting Sideband");
            break;
        case HWC_CURSOR_OVERLAY:
            ALOGE_IF(layer.getCompositionType() != Composition::Cursor,
                    "updateTypeChanges: HWC1 requested CURSOR_OVERLAY, but"
                    " HWC2 wasn't expecting Cursor");
            break;
    }
}

void CfHWC2::Display::updateLayerRequests(
        const hwc_layer_1_t& hwc1Layer, const Layer& layer) {
    if ((hwc1Layer.hints & HWC_HINT_CLEAR_FB) != 0) {
        mChanges->addLayerRequest(layer.getId(),
                LayerRequest::ClearClientTarget);
    }
}

void CfHWC2::Display::prepareFramebufferTarget() {
    // We check that mActiveConfig is valid in Display::prepare
    int32_t width = mActiveConfig->getAttribute(Attribute::Width);
    int32_t height = mActiveConfig->getAttribute(Attribute::Height);

    auto& hwc1Target = mHwc1RequestedContents->hwLayers[mLayers.size()];
    hwc1Target.compositionType = HWC_FRAMEBUFFER_TARGET;
    hwc1Target.releaseFenceFd = -1;
    hwc1Target.hints = 0;
    hwc1Target.flags = 0;
    hwc1Target.transform = 0;
    hwc1Target.blending = HWC_BLENDING_PREMULT;
    if (mDevice.getHwc1MinorVersion() < 3) {
        hwc1Target.sourceCropi = {0, 0, width, height};
    } else {
        hwc1Target.sourceCropf = {0.0f, 0.0f, static_cast<float>(width),
                static_cast<float>(height)};
    }
    hwc1Target.displayFrame = {0, 0, width, height};
    hwc1Target.planeAlpha = 255;

    hwc1Target.visibleRegionScreen.numRects = 1;
    hwc_rect_t* rects = GetRects(1);
    rects[0].left = 0;
    rects[0].top = 0;
    rects[0].right = width;
    rects[0].bottom = height;
    hwc1Target.visibleRegionScreen.rects = rects;

    // We will set this to the correct value in set
    hwc1Target.acquireFenceFd = -1;
}

// Layer functions

std::atomic<hwc2_layer_t> CfHWC2::Layer::sNextId(1);

CfHWC2::Layer::Layer(Display& display)
  : mId(sNextId++),
    mDisplay(display),
    mBuffer(),
    mSurfaceDamage(),
    mBlendMode(BlendMode::None),
    mColor({0, 0, 0, 0}),
    mCompositionType(Composition::Invalid),
    mDisplayFrame({0, 0, -1, -1}),
    mPlaneAlpha(0.0f),
    mSidebandStream(nullptr),
    mSourceCrop({0.0f, 0.0f, -1.0f, -1.0f}),
    mTransform(Transform::None),
    mVisibleRegion(),
    mZ(0),
    mReleaseFence(),
    mHwc1Id(0),
    mHasUnsupportedPlaneAlpha(false) {}

bool CfHWC2::SortLayersByZ::operator()(const std::shared_ptr<Layer>& lhs,
                                               const std::shared_ptr<Layer>& rhs) const {
    return lhs->getZ() < rhs->getZ();
}

Error CfHWC2::Layer::setBuffer(buffer_handle_t buffer,
        int32_t acquireFence) {
    ALOGV("Setting acquireFence to %d for layer %" PRIu64, acquireFence, mId);
    mBuffer.setBuffer(buffer);
    mBuffer.setFence(acquireFence);
    return Error::None;
}

Error CfHWC2::Layer::setCursorPosition(int32_t x, int32_t y) {
    if (mCompositionType != Composition::Cursor) {
        return Error::BadLayer;
    }

    if (mDisplay.hasChanges()) {
        return Error::NotValidated;
    }

    auto displayId = mDisplay.getHwc1Id();
    auto hwc1Device = mDisplay.getDevice().getHwc1Device();
    hwc1Device->setCursorPositionAsync(hwc1Device, displayId, x, y);
    return Error::None;
}

Error CfHWC2::Layer::setSurfaceDamage(hwc_region_t damage) {
    // HWC1 supports surface damage starting only with version 1.5.
    if (mDisplay.getDevice().mHwc1MinorVersion < 5) {
        return Error::None;
    }
    mSurfaceDamage.resize(damage.numRects);
    std::copy_n(damage.rects, damage.numRects, mSurfaceDamage.begin());
    return Error::None;
}

// Layer state functions

Error CfHWC2::Layer::setBlendMode(BlendMode mode) {
    mBlendMode = mode;
    mDisplay.markGeometryChanged();
    return Error::None;
}

Error CfHWC2::Layer::setColor(hwc_color_t color) {
    mColor = color;
    mDisplay.markGeometryChanged();
    return Error::None;
}

Error CfHWC2::Layer::setCompositionType(Composition type) {
    mCompositionType = type;
    mDisplay.markGeometryChanged();
    return Error::None;
}

Error CfHWC2::Layer::setDataspace(android_dataspace_t) {
    return Error::None;
}

Error CfHWC2::Layer::setDisplayFrame(hwc_rect_t frame) {
    mDisplayFrame = frame;
    mDisplay.markGeometryChanged();
    return Error::None;
}

Error CfHWC2::Layer::setPlaneAlpha(float alpha) {
    mPlaneAlpha = alpha;
    mDisplay.markGeometryChanged();
    return Error::None;
}

Error CfHWC2::Layer::setSidebandStream(const native_handle_t* stream) {
    mSidebandStream = stream;
    mDisplay.markGeometryChanged();
    return Error::None;
}

Error CfHWC2::Layer::setSourceCrop(hwc_frect_t crop) {
    mSourceCrop = crop;
    mDisplay.markGeometryChanged();
    return Error::None;
}

Error CfHWC2::Layer::setTransform(Transform transform) {
    mTransform = transform;
    mDisplay.markGeometryChanged();
    return Error::None;
}

static bool compareRects(const hwc_rect_t& rect1, const hwc_rect_t& rect2) {
    return rect1.left == rect2.left &&
            rect1.right == rect2.right &&
            rect1.top == rect2.top &&
            rect1.bottom == rect2.bottom;
}

Error CfHWC2::Layer::setVisibleRegion(hwc_region_t visible) {
    if ((getNumVisibleRegions() != visible.numRects) ||
        !std::equal(mVisibleRegion.begin(), mVisibleRegion.end(), visible.rects,
                    compareRects)) {
        mVisibleRegion.resize(visible.numRects);
        std::copy_n(visible.rects, visible.numRects, mVisibleRegion.begin());
        mDisplay.markGeometryChanged();
    }
    return Error::None;
}

Error CfHWC2::Layer::setZ(uint32_t z) {
    mZ = z;
    return Error::None;
}

void CfHWC2::Layer::addReleaseFence(int fenceFd) {
    ALOGV("addReleaseFence %d to layer %" PRIu64, fenceFd, mId);
    mReleaseFence.add(fenceFd);
}

const sp<MiniFence>& CfHWC2::Layer::getReleaseFence() const {
    return mReleaseFence.get();
}

void CfHWC2::Layer::applyState(hwc_layer_1_t& hwc1Layer) {
    applyCommonState(hwc1Layer);
    applyCompositionType(hwc1Layer);
    switch (mCompositionType) {
        case Composition::SolidColor : applySolidColorState(hwc1Layer); break;
        case Composition::Sideband : applySidebandState(hwc1Layer); break;
        default: applyBufferState(hwc1Layer); break;
    }
}

static std::string regionStrings(const std::vector<hwc_rect_t>& visibleRegion,
        const std::vector<hwc_rect_t>& surfaceDamage) {
    std::string regions;
    regions += "        Visible Region";
    regions.resize(40, ' ');
    regions += "Surface Damage\n";

    size_t numPrinted = 0;
    size_t maxSize = std::max(visibleRegion.size(), surfaceDamage.size());
    while (numPrinted < maxSize) {
        std::string line("        ");
        if (visibleRegion.empty() && numPrinted == 0) {
            line += "None";
        } else if (numPrinted < visibleRegion.size()) {
            line += rectString(visibleRegion[numPrinted]);
        }
        line.resize(40, ' ');
        if (surfaceDamage.empty() && numPrinted == 0) {
            line += "None";
        } else if (numPrinted < surfaceDamage.size()) {
            line += rectString(surfaceDamage[numPrinted]);
        }
        line += '\n';
        regions += line;
        ++numPrinted;
    }
    return regions;
}

std::string CfHWC2::Layer::dump() const {
    std::stringstream output;
    const char* fill = "      ";

    output << fill << to_string(mCompositionType);
    output << " Layer  HWC2/1: " << mId << "/" << mHwc1Id << "  ";
    output << "Z: " << mZ;
    if (mCompositionType == HWC2::Composition::SolidColor) {
        output << "  " << colorString(mColor);
    } else if (mCompositionType == HWC2::Composition::Sideband) {
        output << "  Handle: " << mSidebandStream << '\n';
    } else {
        output << "  Buffer: " << mBuffer.getBuffer() << '\n';
        output << fill << "  Display frame [LTRB]: " <<
                rectString(mDisplayFrame) << '\n';
        output << fill << "  Source crop: " <<
                frectString(mSourceCrop) << '\n';
        output << fill << "  Transform: " << to_string(mTransform);
        output << "  Blend mode: " << to_string(mBlendMode);
        if (mPlaneAlpha != 1.0f) {
            output << "  Alpha: " <<
                alphaString(mPlaneAlpha) << '\n';
        } else {
            output << '\n';
        }
        output << regionStrings(mVisibleRegion, mSurfaceDamage);
    }
    return output.str();
}

static int getHwc1Blending(HWC2::BlendMode blendMode) {
    switch (blendMode) {
        case BlendMode::Coverage: return HWC_BLENDING_COVERAGE;
        case BlendMode::Premultiplied: return HWC_BLENDING_PREMULT;
        default: return HWC_BLENDING_NONE;
    }
}

void CfHWC2::Layer::applyCommonState(hwc_layer_1_t& hwc1Layer) {
    auto minorVersion = mDisplay.getDevice().getHwc1MinorVersion();
    hwc1Layer.blending = getHwc1Blending(mBlendMode);
    hwc1Layer.displayFrame = mDisplayFrame;

    auto pendingAlpha = mPlaneAlpha;
    if (minorVersion < 2) {
        mHasUnsupportedPlaneAlpha = pendingAlpha < 1.0f;
    } else {
        hwc1Layer.planeAlpha =
                static_cast<uint8_t>(255.0f * pendingAlpha + 0.5f);
    }

    if (minorVersion < 3) {
        auto pending = mSourceCrop;
        hwc1Layer.sourceCropi.left =
                static_cast<int32_t>(std::ceil(pending.left));
        hwc1Layer.sourceCropi.top =
                static_cast<int32_t>(std::ceil(pending.top));
        hwc1Layer.sourceCropi.right =
                static_cast<int32_t>(std::floor(pending.right));
        hwc1Layer.sourceCropi.bottom =
                static_cast<int32_t>(std::floor(pending.bottom));
    } else {
        hwc1Layer.sourceCropf = mSourceCrop;
    }

    hwc1Layer.transform = static_cast<uint32_t>(mTransform);

    auto& hwc1VisibleRegion = hwc1Layer.visibleRegionScreen;
    hwc1VisibleRegion.numRects = mVisibleRegion.size();
    hwc_rect_t* rects = mDisplay.GetRects(hwc1VisibleRegion.numRects);
    hwc1VisibleRegion.rects = rects;
    for (size_t i = 0; i < mVisibleRegion.size(); i++) {
        rects[i] = mVisibleRegion[i];
    }
}

void CfHWC2::Layer::applySolidColorState(hwc_layer_1_t& hwc1Layer) {
    // If the device does not support background color it is likely to make
    // assumption regarding backgroundColor and handle (both fields occupy
    // the same location in hwc_layer_1_t union).
    // To not confuse these devices we don't set background color and we
    // make sure handle is a null pointer.
    if (hasUnsupportedBackgroundColor()) {
        hwc1Layer.handle = nullptr;
    } else {
        hwc1Layer.backgroundColor = mColor;
    }
}

void CfHWC2::Layer::applySidebandState(hwc_layer_1_t& hwc1Layer) {
    hwc1Layer.sidebandStream = mSidebandStream;
}

void CfHWC2::Layer::applyBufferState(hwc_layer_1_t& hwc1Layer) {
    hwc1Layer.handle = mBuffer.getBuffer();
    hwc1Layer.acquireFenceFd = mBuffer.getFence();
}

void CfHWC2::Layer::applyCompositionType(hwc_layer_1_t& hwc1Layer) {
    // HWC1 never supports color transforms or dataspaces and only sometimes
    // supports plane alpha (depending on the version). These require us to drop
    // some or all layers to client composition.
    if (mHasUnsupportedPlaneAlpha || mDisplay.hasColorTransform() ||
            hasUnsupportedBackgroundColor()) {
        hwc1Layer.compositionType = HWC_FRAMEBUFFER;
        hwc1Layer.flags = HWC_SKIP_LAYER;
        return;
    }

    hwc1Layer.flags = 0;
    switch (mCompositionType) {
        case Composition::Client:
            hwc1Layer.compositionType = HWC_FRAMEBUFFER;
            hwc1Layer.flags |= HWC_SKIP_LAYER;
            break;
        case Composition::Device:
            hwc1Layer.compositionType = HWC_FRAMEBUFFER;
            break;
        case Composition::SolidColor:
            // In theory the following line should work, but since the HWC1
            // version of SurfaceFlinger never used HWC_BACKGROUND, HWC1
            // devices may not work correctly. To be on the safe side, we
            // fall back to client composition.
            //
            // hwc1Layer.compositionType = HWC_BACKGROUND;
            hwc1Layer.compositionType = HWC_FRAMEBUFFER;
            hwc1Layer.flags |= HWC_SKIP_LAYER;
            break;
        case Composition::Cursor:
            hwc1Layer.compositionType = HWC_FRAMEBUFFER;
            if (mDisplay.getDevice().getHwc1MinorVersion() >= 4) {
                hwc1Layer.hints |= HWC_IS_CURSOR_LAYER;
            }
            break;
        case Composition::Sideband:
            if (mDisplay.getDevice().getHwc1MinorVersion() < 4) {
                hwc1Layer.compositionType = HWC_SIDEBAND;
            } else {
                hwc1Layer.compositionType = HWC_FRAMEBUFFER;
                hwc1Layer.flags |= HWC_SKIP_LAYER;
            }
            break;
        default:
            hwc1Layer.compositionType = HWC_FRAMEBUFFER;
            hwc1Layer.flags |= HWC_SKIP_LAYER;
            break;
    }
    ALOGV("Layer %" PRIu64 " %s set to %d", mId,
            to_string(mCompositionType).c_str(),
            hwc1Layer.compositionType);
    ALOGV_IF(hwc1Layer.flags & HWC_SKIP_LAYER, "    and skipping");
}

// Adapter helpers

void CfHWC2::populateCapabilities() {
    if (mHwc1MinorVersion >= 3U) {
        int supportedTypes = 0;
        auto result = mHwc1Device->query(mHwc1Device,
                HWC_DISPLAY_TYPES_SUPPORTED, &supportedTypes);
        if ((result == 0) && ((supportedTypes & HWC_DISPLAY_VIRTUAL_BIT) != 0)) {
            ALOGI("Found support for HWC virtual displays");
            mHwc1SupportsVirtualDisplays = true;
        }
    }
    if (mHwc1MinorVersion >= 4U) {
        mCapabilities.insert(Capability::SidebandStream);
    }

    // Check for HWC background color layer support.
    if (mHwc1MinorVersion >= 1U) {
        int backgroundColorSupported = 0;
        auto result = mHwc1Device->query(mHwc1Device,
                                         HWC_BACKGROUND_LAYER_SUPPORTED,
                                         &backgroundColorSupported);
        if ((result == 0) && (backgroundColorSupported == 1)) {
            ALOGV("Found support for HWC background color");
            mHwc1SupportsBackgroundColor = true;
        }
    }

    // Some devices might have HWC1 retire fences that accurately emulate
    // HWC2 present fences when they are deferred, but it's not very reliable.
    // To be safe, we indicate PresentFenceIsNotReliable for all HWC1 devices.
    //mCapabilities.insert(Capability::PresentFenceIsNotReliable);
}

CfHWC2::Display* CfHWC2::getDisplay(hwc2_display_t id) {
    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    auto display = mDisplays.find(id);
    if (display == mDisplays.end()) {
        return nullptr;
    }

    return display->second.get();
}

std::tuple<CfHWC2::Layer*, Error> CfHWC2::getLayer(
        hwc2_display_t displayId, hwc2_layer_t layerId) {
    auto display = getDisplay(displayId);
    if (!display) {
        return std::make_tuple(static_cast<Layer*>(nullptr), Error::BadDisplay);
    }

    auto layerEntry = mLayers.find(layerId);
    if (layerEntry == mLayers.end()) {
        return std::make_tuple(static_cast<Layer*>(nullptr), Error::BadLayer);
    }

    auto layer = layerEntry->second;
    if (layer->getDisplay().getId() != displayId) {
        return std::make_tuple(static_cast<Layer*>(nullptr), Error::BadLayer);
    }
    return std::make_tuple(layer.get(), Error::None);
}

void CfHWC2::populatePrimary() {
    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    auto display = std::make_shared<Display>(*this, HWC2::DisplayType::Physical);
    mHwc1DisplayMap[HWC_DISPLAY_PRIMARY] = display->getId();
    display->setHwc1Id(HWC_DISPLAY_PRIMARY);
    display->populateConfigs();
    mDisplays.emplace(display->getId(), std::move(display));
}

bool CfHWC2::prepareAllDisplays() {
    ATRACE_CALL();

    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    for (const auto& displayPair : mDisplays) {
        auto& display = displayPair.second;
        if (!display->prepare()) {
            return false;
        }
    }

    if (mHwc1DisplayMap.count(HWC_DISPLAY_PRIMARY) == 0) {
        ALOGE("prepareAllDisplays: Unable to find primary HWC1 display");
        return false;
    }

    // Build an array of hwc_display_contents_1 to call prepare() on HWC1.
    mHwc1Contents.clear();

    // Always push the primary display
    auto primaryDisplayId = mHwc1DisplayMap[HWC_DISPLAY_PRIMARY];
    auto& primaryDisplay = mDisplays[primaryDisplayId];
    mHwc1Contents.push_back(primaryDisplay->getDisplayContents());

    // Push the external display, if present
    if (mHwc1DisplayMap.count(HWC_DISPLAY_EXTERNAL) != 0) {
        auto externalDisplayId = mHwc1DisplayMap[HWC_DISPLAY_EXTERNAL];
        auto& externalDisplay = mDisplays[externalDisplayId];
        mHwc1Contents.push_back(externalDisplay->getDisplayContents());
    } else {
        // Even if an external display isn't present, we still need to send
        // at least two displays down to HWC1
        mHwc1Contents.push_back(nullptr);
    }

    // Push the hardware virtual display, if supported and present
    if (mHwc1MinorVersion >= 3) {
        if (mHwc1DisplayMap.count(HWC_DISPLAY_VIRTUAL) != 0) {
            auto virtualDisplayId = mHwc1DisplayMap[HWC_DISPLAY_VIRTUAL];
            auto& virtualDisplay = mDisplays[virtualDisplayId];
            mHwc1Contents.push_back(virtualDisplay->getDisplayContents());
        } else {
            mHwc1Contents.push_back(nullptr);
        }
    }

    for (auto& displayContents : mHwc1Contents) {
        if (!displayContents) {
            continue;
        }

        ALOGV("Display %zd layers:", mHwc1Contents.size() - 1);
        for (size_t l = 0; l < displayContents->numHwLayers; ++l) {
            auto& layer = displayContents->hwLayers[l];
            ALOGV("  %zd: %d", l, layer.compositionType);
        }
    }

    ALOGV("Calling HWC1 prepare");
    {
        ATRACE_NAME("HWC1 prepare");
        mHwc1Device->prepare(mHwc1Device, mHwc1Contents.size(),
                mHwc1Contents.data());
    }

    for (size_t c = 0; c < mHwc1Contents.size(); ++c) {
        auto& contents = mHwc1Contents[c];
        if (!contents) {
            continue;
        }
        ALOGV("Display %zd layers:", c);
        for (size_t l = 0; l < contents->numHwLayers; ++l) {
            ALOGV("  %zd: %d", l, contents->hwLayers[l].compositionType);
        }
    }

    // Return the received contents to their respective displays
    for (size_t hwc1Id = 0; hwc1Id < mHwc1Contents.size(); ++hwc1Id) {
        if (mHwc1Contents[hwc1Id] == nullptr) {
            continue;
        }

        auto displayId = mHwc1DisplayMap[hwc1Id];
        auto& display = mDisplays[displayId];
        display->generateChanges();
    }

    return true;
}

void dumpHWC1Message(hwc_composer_device_1* device, size_t numDisplays,
                     hwc_display_contents_1_t** displays) {
    ALOGV("*****************************");
    size_t displayId = 0;
    while (displayId < numDisplays) {
        hwc_display_contents_1_t* display = displays[displayId];

        ALOGV("hwc_display_contents_1_t[%zu] @0x%p", displayId, display);
        if (display == nullptr) {
            displayId++;
            continue;
        }
        ALOGV("  retirefd:0x%08x", display->retireFenceFd);
        ALOGV("  outbuf  :0x%p", display->outbuf);
        ALOGV("  outbuffd:0x%08x", display->outbufAcquireFenceFd);
        ALOGV("  flags   :0x%08x", display->flags);
        for(size_t layerId=0 ; layerId < display->numHwLayers ; layerId++) {
            hwc_layer_1_t& layer = display->hwLayers[layerId];
            ALOGV("    Layer[%zu]:", layerId);
            ALOGV("      composition        : 0x%08x", layer.compositionType);
            ALOGV("      hints              : 0x%08x", layer.hints);
            ALOGV("      flags              : 0x%08x", layer.flags);
            ALOGV("      handle             : 0x%p", layer.handle);
            ALOGV("      transform          : 0x%08x", layer.transform);
            ALOGV("      blending           : 0x%08x", layer.blending);
            ALOGV("      sourceCropf        : %f, %f, %f, %f",
                  layer.sourceCropf.left,
                  layer.sourceCropf.top,
                  layer.sourceCropf.right,
                  layer.sourceCropf.bottom);
            ALOGV("      displayFrame       : %d, %d, %d, %d",
                  layer.displayFrame.left,
                  layer.displayFrame.left,
                  layer.displayFrame.left,
                  layer.displayFrame.left);
            hwc_region_t& visReg = layer.visibleRegionScreen;
            ALOGV("      visibleRegionScreen: #0x%08zx[@0x%p]",
                  visReg.numRects,
                  visReg.rects);
            for (size_t visRegId=0; visRegId < visReg.numRects ; visRegId++) {
                if (layer.visibleRegionScreen.rects == nullptr) {
                    ALOGV("        null");
                } else {
                    ALOGV("        visibleRegionScreen[%zu] %d, %d, %d, %d",
                          visRegId,
                          visReg.rects[visRegId].left,
                          visReg.rects[visRegId].top,
                          visReg.rects[visRegId].right,
                          visReg.rects[visRegId].bottom);
                }
            }
            ALOGV("      acquireFenceFd     : 0x%08x", layer.acquireFenceFd);
            ALOGV("      releaseFenceFd     : 0x%08x", layer.releaseFenceFd);
            ALOGV("      planeAlpha         : 0x%08x", layer.planeAlpha);
            if (getMinorVersion(device) < 5)
               continue;
            ALOGV("      surfaceDamage      : #0x%08zx[@0x%p]",
                  layer.surfaceDamage.numRects,
                  layer.surfaceDamage.rects);
            for (size_t sdId=0; sdId < layer.surfaceDamage.numRects ; sdId++) {
                if (layer.surfaceDamage.rects == nullptr) {
                    ALOGV("      null");
                } else {
                    ALOGV("      surfaceDamage[%zu] %d, %d, %d, %d",
                          sdId,
                          layer.surfaceDamage.rects[sdId].left,
                          layer.surfaceDamage.rects[sdId].top,
                          layer.surfaceDamage.rects[sdId].right,
                          layer.surfaceDamage.rects[sdId].bottom);
                }
            }
        }
        displayId++;
    }
    ALOGV("-----------------------------");
}

Error CfHWC2::setAllDisplays() {
    ATRACE_CALL();

    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    // Make sure we're ready to validate
    for (size_t hwc1Id = 0; hwc1Id < mHwc1Contents.size(); ++hwc1Id) {
        if (mHwc1Contents[hwc1Id] == nullptr) {
            continue;
        }

        auto displayId = mHwc1DisplayMap[hwc1Id];
        auto& display = mDisplays[displayId];
        Error error = display->set(*mHwc1Contents[hwc1Id]);
        if (error != Error::None) {
            ALOGE("setAllDisplays: Failed to set display %zd: %s", hwc1Id,
                    to_string(error).c_str());
            return error;
        }
    }

    ALOGV("Calling HWC1 set");
    {
        ATRACE_NAME("HWC1 set");
        //dumpHWC1Message(mHwc1Device, mHwc1Contents.size(), mHwc1Contents.data());
        mHwc1Device->set(mHwc1Device, mHwc1Contents.size(),
                mHwc1Contents.data());
    }

    // Add retire and release fences
    for (size_t hwc1Id = 0; hwc1Id < mHwc1Contents.size(); ++hwc1Id) {
        if (mHwc1Contents[hwc1Id] == nullptr) {
            continue;
        }

        auto displayId = mHwc1DisplayMap[hwc1Id];
        auto& display = mDisplays[displayId];
        auto retireFenceFd = mHwc1Contents[hwc1Id]->retireFenceFd;
        ALOGV("setAllDisplays: Adding retire fence %d to display %zd",
                retireFenceFd, hwc1Id);
        display->addRetireFence(mHwc1Contents[hwc1Id]->retireFenceFd);
        display->addReleaseFences(*mHwc1Contents[hwc1Id]);
    }

    return Error::None;
}

void CfHWC2::hwc1Invalidate() {
    ALOGV("Received hwc1Invalidate");

    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    // If the HWC2-side callback hasn't been registered yet, buffer this until
    // it is registered.
    if (mCallbacks.count(Callback::Refresh) == 0) {
        mHasPendingInvalidate = true;
        return;
    }

    const auto& callbackInfo = mCallbacks[Callback::Refresh];
    std::vector<hwc2_display_t> displays;
    for (const auto& displayPair : mDisplays) {
        displays.emplace_back(displayPair.first);
    }

    // Call back without the state lock held.
    lock.unlock();

    auto refresh = reinterpret_cast<HWC2_PFN_REFRESH>(callbackInfo.pointer);
    for (auto display : displays) {
        refresh(callbackInfo.data, display);
    }
}

void CfHWC2::hwc1Vsync(int hwc1DisplayId, int64_t timestamp) {
    ALOGV("Received hwc1Vsync(%d, %" PRId64 ")", hwc1DisplayId, timestamp);

    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    // If the HWC2-side callback hasn't been registered yet, buffer this until
    // it is registered.
    if (mCallbacks.count(Callback::Vsync) == 0) {
        mPendingVsyncs.emplace_back(hwc1DisplayId, timestamp);
        return;
    }

    if (mHwc1DisplayMap.count(hwc1DisplayId) == 0) {
        ALOGE("hwc1Vsync: Couldn't find display for HWC1 id %d", hwc1DisplayId);
        return;
    }

    const auto& callbackInfo = mCallbacks[Callback::Vsync];
    auto displayId = mHwc1DisplayMap[hwc1DisplayId];

    // Call back without the state lock held.
    lock.unlock();

    auto vsync = reinterpret_cast<HWC2_PFN_VSYNC>(callbackInfo.pointer);
    vsync(callbackInfo.data, displayId, timestamp);
}

void CfHWC2::hwc1Hotplug(int hwc1DisplayId, int connected) {
    ALOGV("Received hwc1Hotplug(%d, %d)", hwc1DisplayId, connected);

    if (hwc1DisplayId != HWC_DISPLAY_EXTERNAL) {
        ALOGE("hwc1Hotplug: Received hotplug for non-external display");
        return;
    }

    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    hwc2_display_t displayId = UINT64_MAX;
    if (mHwc1DisplayMap.count(hwc1DisplayId) == 0) {
        if (connected == 0) {
            ALOGW("hwc1Hotplug: Received disconnect for unconnected display");
            return;
        }

        // Create a new display on connect
        auto display = std::make_shared<CfHWC2::Display>(*this,
                HWC2::DisplayType::Physical);
        display->setHwc1Id(HWC_DISPLAY_EXTERNAL);
        display->populateConfigs();
        displayId = display->getId();
        mHwc1DisplayMap[HWC_DISPLAY_EXTERNAL] = displayId;
        mDisplays.emplace(displayId, std::move(display));
    } else {
        if (connected != 0) {
            ALOGW("hwc1Hotplug: Received connect for previously connected "
                    "display");
            return;
        }

        // Disconnect an existing display
        displayId = mHwc1DisplayMap[hwc1DisplayId];
        mHwc1DisplayMap.erase(HWC_DISPLAY_EXTERNAL);
        mDisplays.erase(displayId);
    }

    // If the HWC2-side callback hasn't been registered yet, buffer this until
    // it is registered
    if (mCallbacks.count(Callback::Hotplug) == 0) {
        mPendingHotplugs.emplace_back(hwc1DisplayId, connected);
        return;
    }

    const auto& callbackInfo = mCallbacks[Callback::Hotplug];

    // Call back without the state lock held
    lock.unlock();

    auto hotplug = reinterpret_cast<HWC2_PFN_HOTPLUG>(callbackInfo.pointer);
    auto hwc2Connected = (connected == 0) ?
            HWC2::Connection::Disconnected : HWC2::Connection::Connected;
    hotplug(callbackInfo.data, displayId, static_cast<int32_t>(hwc2Connected));
}

static int hwc2DevOpen(const struct hw_module_t *module, const char *name,
        struct hw_device_t **dev) {
    ALOGV("%s()", __FUNCTION__);
    if (strcmp(name, HWC_HARDWARE_COMPOSER)) {
        ALOGE("Invalid module name- %s", name);
        return -EINVAL;
    }

    std::unique_ptr<cuttlefish::ScreenView> screen_view(new cuttlefish::VsocketScreenView());
    if (!screen_view) {
      ALOGE("Failed to instantiate screen view");
      return -1;
    }

    hw_device_t* device;
    int error = cuttlefish::cvd_hwc_open(std::move(screen_view), module, name, &device);
    if (error) {
        ALOGE("failed to open hwcomposer device: %s", strerror(-error));
        return -1;
    }

    int major = (device->version >> 24) & 0xf;
    ALOGV("%s(): major=%d", __FUNCTION__, major);
    if (major < 2) {
        CfHWC2* hwc2 = new CfHWC2(std::move(reinterpret_cast<hwc_composer_device_1*>(device)));
        hwc2->common.module = const_cast<hw_module_t *>(module);
        *dev = &hwc2->common;
    } else {
        *dev = device;
    }

    return 0;
}

} // namespace android

static struct hw_module_methods_t hwc2_module_methods = {
    .open = android::hwc2DevOpen
};

hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 2,
    .version_minor = 0,
    .id = HWC_HARDWARE_MODULE_ID,
    .name = "CuttleFish HWC2 module",
    .author = "Google",
    .methods = &hwc2_module_methods,
    .dso = NULL,
    .reserved = {0},
};

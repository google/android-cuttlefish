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

#ifndef ANDROID_SF_HWC2_ON_1_ADAPTER_H
#define ANDROID_SF_HWC2_ON_1_ADAPTER_H

#define HWC2_INCLUDE_STRINGIFICATION
#define HWC2_USE_CPP11
#include <hardware/hwcomposer2.h>
#undef HWC2_INCLUDE_STRINGIFICATION
#undef HWC2_USE_CPP11

#include "MiniFence.h"

#include <atomic>
#include <map>
#include <mutex>
#include <numeric>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct hwc_composer_device_1;
struct hwc_display_contents_1;
struct hwc_layer_1;

namespace android {

class CfHWC2 : public hwc2_device_t
{
public:
    explicit CfHWC2(struct hwc_composer_device_1* hwc1Device);
    ~CfHWC2();

    struct hwc_composer_device_1* getHwc1Device() const { return mHwc1Device; }
    uint8_t getHwc1MinorVersion() const { return mHwc1MinorVersion; }

private:
    static inline CfHWC2* getAdapter(hwc2_device_t* device) {
        return static_cast<CfHWC2*>(device);
    }

    // getCapabilities

    void doGetCapabilities(uint32_t* outCount,
            int32_t* /*hwc2_capability_t*/ outCapabilities);
    static void getCapabilitiesHook(hwc2_device_t* device, uint32_t* outCount,
            int32_t* /*hwc2_capability_t*/ outCapabilities) {
        getAdapter(device)->doGetCapabilities(outCount, outCapabilities);
    }

    bool supportsBackgroundColor() {
        return mHwc1SupportsBackgroundColor;
    }

    // getFunction

    hwc2_function_pointer_t doGetFunction(HWC2::FunctionDescriptor descriptor);
    static hwc2_function_pointer_t getFunctionHook(hwc2_device_t* device,
            int32_t intDesc) {
        auto descriptor = static_cast<HWC2::FunctionDescriptor>(intDesc);
        return getAdapter(device)->doGetFunction(descriptor);
    }

    // Device functions

    HWC2::Error createVirtualDisplay(uint32_t width, uint32_t height,
            hwc2_display_t* outDisplay);
    static int32_t createVirtualDisplayHook(hwc2_device_t* device,
            uint32_t width, uint32_t height, int32_t* /*format*/,
            hwc2_display_t* outDisplay) {
        // HWC1 implementations cannot override the buffer format requested by
        // the consumer
        auto error = getAdapter(device)->createVirtualDisplay(width, height,
                outDisplay);
        return static_cast<int32_t>(error);
    }

    HWC2::Error destroyVirtualDisplay(hwc2_display_t display);
    static int32_t destroyVirtualDisplayHook(hwc2_device_t* device,
            hwc2_display_t display) {
        auto error = getAdapter(device)->destroyVirtualDisplay(display);
        return static_cast<int32_t>(error);
    }

    std::string mDumpString;
    void dump(uint32_t* outSize, char* outBuffer);
    static void dumpHook(hwc2_device_t* device, uint32_t* outSize,
            char* outBuffer) {
        getAdapter(device)->dump(outSize, outBuffer);
    }

    uint32_t getMaxVirtualDisplayCount();
    static uint32_t getMaxVirtualDisplayCountHook(hwc2_device_t* device) {
        return getAdapter(device)->getMaxVirtualDisplayCount();
    }

    HWC2::Error registerCallback(HWC2::Callback descriptor,
            hwc2_callback_data_t callbackData, hwc2_function_pointer_t pointer);
    static int32_t registerCallbackHook(hwc2_device_t* device,
            int32_t intDesc, hwc2_callback_data_t callbackData,
            hwc2_function_pointer_t pointer) {
        auto descriptor = static_cast<HWC2::Callback>(intDesc);
        auto error = getAdapter(device)->registerCallback(descriptor,
                callbackData, pointer);
        return static_cast<int32_t>(error);
    }

    // Display functions

    class Layer;

    class SortLayersByZ {
        public:
         bool operator()(const std::shared_ptr<Layer>& lhs,
                         const std::shared_ptr<Layer>& rhs) const;
    };

    // The semantics of the fences returned by the device differ between
    // hwc1.set() and hwc2.present(). Read hwcomposer.h and hwcomposer2.h
    // for more information.
    //
    // Release fences in hwc1 are obtained on set() for a frame n and signaled
    // when the layer buffer is not needed for read operations anymore
    // (typically on frame n+1). In HWC2, release fences are obtained with a
    // special call after present() for frame n. These fences signal
    // on frame n: More specifically, the fence for a given buffer provided in
    // frame n will signal when the prior buffer is no longer required.
    //
    // A retire fence (HWC1) is signaled when a composition is replaced
    // on the panel whereas a present fence (HWC2) is signaled when a
    // composition starts to be displayed on a panel.
    //
    // The HWC2to1Adapter emulates the new fence semantics for a frame
    // n by returning the fence from frame n-1. For frame 0, the adapter
    // returns NO_FENCE.
    class DeferredFence {
        public:
            DeferredFence()
              : mFences({MiniFence::NO_FENCE, MiniFence::NO_FENCE}) {}

            void add(int32_t fenceFd) {
                mFences.emplace(new MiniFence(fenceFd));
                mFences.pop();
            }

            const sp<MiniFence>& get() const {
                return mFences.front();
            }

        private:
            // There are always two fences in this queue.
            std::queue<sp<MiniFence>> mFences;
    };

    class FencedBuffer {
        public:
            FencedBuffer() : mBuffer(nullptr), mFence(MiniFence::NO_FENCE) {}

            void setBuffer(buffer_handle_t buffer) { mBuffer = buffer; }
            void setFence(int fenceFd) { mFence = new MiniFence(fenceFd); }

            buffer_handle_t getBuffer() const { return mBuffer; }
            int getFence() const { return mFence->dup(); }

        private:
            buffer_handle_t mBuffer;
            sp<MiniFence> mFence;
    };

    class Display {
        public:
            Display(CfHWC2& device, HWC2::DisplayType type);

            hwc2_display_t getId() const { return mId; }
            CfHWC2& getDevice() const { return mDevice; }

            // Does not require locking because it is set before adding the
            // Displays to the Adapter's list of displays
            void setHwc1Id(int32_t id) { mHwc1Id = id; }
            int32_t getHwc1Id() const { return mHwc1Id; }

            // HWC2 Display functions
            HWC2::Error acceptChanges();
            HWC2::Error createLayer(hwc2_layer_t* outLayerId);
            HWC2::Error destroyLayer(hwc2_layer_t layerId);
            HWC2::Error getActiveConfig(hwc2_config_t* outConfigId);
            HWC2::Error getAttribute(hwc2_config_t configId,
                    HWC2::Attribute attribute, int32_t* outValue);
            HWC2::Error getChangedCompositionTypes(uint32_t* outNumElements,
                    hwc2_layer_t* outLayers, int32_t* outTypes);
            HWC2::Error getColorModes(uint32_t* outNumModes, int32_t* outModes);
            HWC2::Error getConfigs(uint32_t* outNumConfigs,
                    hwc2_config_t* outConfigIds);
            HWC2::Error getDozeSupport(int32_t* outSupport);
            HWC2::Error getHdrCapabilities(uint32_t* outNumTypes,
                    int32_t* outTypes, float* outMaxLuminance,
                    float* outMaxAverageLuminance, float* outMinLuminance);
            HWC2::Error getName(uint32_t* outSize, char* outName);
            HWC2::Error getReleaseFences(uint32_t* outNumElements,
                    hwc2_layer_t* outLayers, int32_t* outFences);
            HWC2::Error getRequests(int32_t* outDisplayRequests,
                    uint32_t* outNumElements, hwc2_layer_t* outLayers,
                    int32_t* outLayerRequests);
            HWC2::Error getType(int32_t* outType);

            // Since HWC1 "presents" (called "set" in HWC1) all Displays
            // at once, the first call to any Display::present will trigger
            // present() on all Displays in the Device. Subsequent calls without
            // first calling validate() are noop (except for duping/returning
            // the retire fence).
            HWC2::Error present(int32_t* outRetireFence);

            HWC2::Error setActiveConfig(hwc2_config_t configId);
            HWC2::Error setClientTarget(buffer_handle_t target,
                    int32_t acquireFence, int32_t dataspace,
                    hwc_region_t damage);
            HWC2::Error setColorMode(android_color_mode_t mode);
            HWC2::Error setColorTransform(android_color_transform_t hint);
            HWC2::Error setOutputBuffer(buffer_handle_t buffer,
                    int32_t releaseFence);
            HWC2::Error setPowerMode(HWC2::PowerMode mode);
            HWC2::Error setVsyncEnabled(HWC2::Vsync enabled);

            // Since HWC1 "validates" (called "prepare" in HWC1) all Displays
            // at once, the first call to any Display::validate() will trigger
            // validate() on all other Displays in the Device.
            HWC2::Error validate(uint32_t* outNumTypes,
                    uint32_t* outNumRequests);

            HWC2::Error updateLayerZ(hwc2_layer_t layerId, uint32_t z);

            HWC2::Error getClientTargetSupport(uint32_t width, uint32_t height,
                     int32_t format, int32_t dataspace);

            // 2.3 required functions
            HWC2::Error getDisplayIdentificationData(uint8_t* outPort,
                    uint32_t* outDataSize, uint8_t* outData);
            HWC2::Error getDisplayCapabilities(uint32_t* outNumCapabilities,
                    uint32_t* outCapabilities);
            HWC2::Error getDisplayBrightnessSupport(bool *out_support);
            HWC2::Error setDisplayBrightness(float brightness);

            // Read configs from HWC1 device
            void populateConfigs();

            // Set configs for a virtual display
            void populateConfigs(uint32_t width, uint32_t height);

            bool prepare();

            // Called after hwc.prepare() with responses from the device.
            void generateChanges();

            bool hasChanges() const;
            HWC2::Error set(hwc_display_contents_1& hwcContents);
            void addRetireFence(int fenceFd);
            void addReleaseFences(const hwc_display_contents_1& hwcContents);

            bool hasColorTransform() const;

            std::string dump() const;

            // Return a rect from the pool allocated during validate()
            hwc_rect_t* GetRects(size_t numRects);

            hwc_display_contents_1* getDisplayContents();

            void markGeometryChanged() { mGeometryChanged = true; }
            void resetGeometryMarker() { mGeometryChanged = false;}
        private:
            class Config {
                public:
                    Config(Display& display)
                      : mDisplay(display),
                        mId(0),
                        mAttributes() {}

                    bool isOnDisplay(const Display& display) const {
                        return display.getId() == mDisplay.getId();
                    }

                    void setAttribute(HWC2::Attribute attribute, int32_t value);
                    int32_t getAttribute(HWC2::Attribute attribute) const;

                    void setHwc1Id(uint32_t id);
                    bool hasHwc1Id(uint32_t id) const;
                    HWC2::Error getColorModeForHwc1Id(uint32_t id,
                            android_color_mode_t *outMode) const;
                    HWC2::Error getHwc1IdForColorMode(android_color_mode_t mode,
                            uint32_t* outId) const;

                    void setId(hwc2_config_t id) { mId = id; }
                    hwc2_config_t getId() const { return mId; }

                    // Attempts to merge two configs that differ only in color
                    // mode. Returns whether the merge was successful
                    bool merge(const Config& other);

                    std::set<android_color_mode_t> getColorModes() const;

                    // splitLine divides the output into two lines suitable for
                    // dumpsys SurfaceFlinger
                    std::string toString(bool splitLine = false) const;

                private:
                    Display& mDisplay;
                    hwc2_config_t mId;
                    std::unordered_map<HWC2::Attribute, int32_t> mAttributes;

                    // Maps from color transform to HWC1 config ID
                    std::unordered_map<android_color_mode_t, uint32_t> mHwc1Ids;
            };

            // Stores changes requested from the device upon calling prepare().
            // Handles change request to:
            //   - Layer composition type.
            //   - Layer hints.
            class Changes {
                public:
                    uint32_t getNumTypes() const {
                        return static_cast<uint32_t>(mTypeChanges.size());
                    }

                    uint32_t getNumLayerRequests() const {
                        return static_cast<uint32_t>(mLayerRequests.size());
                    }

                    const std::unordered_map<hwc2_layer_t, HWC2::Composition>&
                            getTypeChanges() const {
                        return mTypeChanges;
                    }

                    const std::unordered_map<hwc2_layer_t, HWC2::LayerRequest>&
                            getLayerRequests() const {
                        return mLayerRequests;
                    }

                    void addTypeChange(hwc2_layer_t layerId,
                            HWC2::Composition type) {
                        mTypeChanges.insert({layerId, type});
                    }

                    void clearTypeChanges() { mTypeChanges.clear(); }

                    void addLayerRequest(hwc2_layer_t layerId,
                            HWC2::LayerRequest request) {
                        mLayerRequests.insert({layerId, request});
                    }

                private:
                    std::unordered_map<hwc2_layer_t, HWC2::Composition>
                            mTypeChanges;
                    std::unordered_map<hwc2_layer_t, HWC2::LayerRequest>
                            mLayerRequests;
            };

            std::shared_ptr<const Config>
                    getConfig(hwc2_config_t configId) const;

            void populateColorModes();
            void initializeActiveConfig();

            // Creates a bi-directional mapping between index in HWC1
            // prepare/set array and Layer object. Stores mapping in
            // mHwc1LayerMap and also updates Layer's attribute mHwc1Id.
            void assignHwc1LayerIds();

            // Called after a response to prepare() has been received:
            // Ingest composition type changes requested by the device.
            void updateTypeChanges(const struct hwc_layer_1& hwc1Layer,
                    const Layer& layer);

            // Called after a response to prepare() has been received:
            // Ingest layer hint changes requested by the device.
            void updateLayerRequests(const struct hwc_layer_1& hwc1Layer,
                    const Layer& layer);

            // Set all fields in HWC1 comm array for layer containing the
            // HWC_FRAMEBUFFER_TARGET (always the last layer).
            void prepareFramebufferTarget();

            // Display ID generator.
            static std::atomic<hwc2_display_t> sNextId;
            const hwc2_display_t mId;


            CfHWC2& mDevice;

            // The state of this display should only be modified from
            // SurfaceFlinger's main loop, with the exception of when dump is
            // called. To prevent a bad state from crashing us during a dump
            // call, all public calls into Display must acquire this mutex.
            //
            // It is recursive because we don't want to deadlock in validate
            // (or present) when we call CfHWC2::prepareAllDisplays
            // (or setAllDisplays), which calls back into Display functions
            // which require locking.
            mutable std::recursive_mutex mStateMutex;

            // Allocate RAM able to store all layers and rects used for
            // communication with HWC1. Place allocated RAM in variable
            // mHwc1RequestedContents.
            void allocateRequestedContents();

            // Array of structs exchanged between client and hwc1 device.
            // Sent to device upon calling prepare().
            std::unique_ptr<hwc_display_contents_1> mHwc1RequestedContents;
    private:
            DeferredFence mRetireFence;

            // Will only be non-null after the Display has been validated and
            // before it has been presented
            std::unique_ptr<Changes> mChanges;

            int32_t mHwc1Id;

            std::vector<std::shared_ptr<Config>> mConfigs;
            std::shared_ptr<const Config> mActiveConfig;
            std::set<android_color_mode_t> mColorModes;
            android_color_mode_t mActiveColorMode;
            std::string mName;
            HWC2::DisplayType mType;
            HWC2::PowerMode mPowerMode;
            HWC2::Vsync mVsyncEnabled;

            // Used to populate HWC1 HWC_FRAMEBUFFER_TARGET layer
            FencedBuffer mClientTarget;


            FencedBuffer mOutputBuffer;

            bool mHasColorTransform;

            // All layers this Display is aware of.
            std::multiset<std::shared_ptr<Layer>, SortLayersByZ> mLayers;

            // Mapping between layer index in array of hwc_display_contents_1*
            // passed to HWC1 during validate/set and Layer object.
            std::unordered_map<size_t, std::shared_ptr<Layer>> mHwc1LayerMap;

            // All communication with HWC1 via prepare/set is done with one
            // alloc. This pointer is pointing to a pool of hwc_rect_t.
            size_t mNumAvailableRects;
            hwc_rect_t* mNextAvailableRect;

            // True if any of the Layers contained in this Display have been
            // updated with anything other than a buffer since last call to
            // Display::set()
            bool mGeometryChanged;
    };

    // Utility template calling a Display object method directly based on the
    // hwc2_display_t displayId parameter.
    template <typename ...Args>
    static int32_t callDisplayFunction(hwc2_device_t* device,
            hwc2_display_t displayId, HWC2::Error (Display::*member)(Args...),
            Args... args) {
        auto display = getAdapter(device)->getDisplay(displayId);
        if (!display) {
            return static_cast<int32_t>(HWC2::Error::BadDisplay);
        }
        auto error = ((*display).*member)(std::forward<Args>(args)...);
        return static_cast<int32_t>(error);
    }

    template <typename MF, MF memFunc, typename ...Args>
    static int32_t displayHook(hwc2_device_t* device, hwc2_display_t displayId,
            Args... args) {
        return CfHWC2::callDisplayFunction(device, displayId, memFunc,
                std::forward<Args>(args)...);
    }

    static int32_t getDisplayAttributeHook(hwc2_device_t* device,
            hwc2_display_t display, hwc2_config_t config,
            int32_t intAttribute, int32_t* outValue) {
        auto attribute = static_cast<HWC2::Attribute>(intAttribute);
        return callDisplayFunction(device, display, &Display::getAttribute,
                config, attribute, outValue);
    }

    static int32_t setColorTransformHook(hwc2_device_t* device,
            hwc2_display_t display, const float* /*matrix*/,
            int32_t /*android_color_transform_t*/ intHint) {
        // We intentionally throw away the matrix, because if the hint is
        // anything other than IDENTITY, we have to fall back to client
        // composition anyway
        auto hint = static_cast<android_color_transform_t>(intHint);
        return callDisplayFunction(device, display, &Display::setColorTransform,
                hint);
    }

    static int32_t setColorModeHook(hwc2_device_t* device,
            hwc2_display_t display, int32_t /*android_color_mode_t*/ intMode) {
        auto mode = static_cast<android_color_mode_t>(intMode);
        return callDisplayFunction(device, display, &Display::setColorMode,
                mode);
    }

    static int32_t setPowerModeHook(hwc2_device_t* device,
            hwc2_display_t display, int32_t intMode) {
        auto mode = static_cast<HWC2::PowerMode>(intMode);
        return callDisplayFunction(device, display, &Display::setPowerMode,
                mode);
    }

    static int32_t setVsyncEnabledHook(hwc2_device_t* device,
            hwc2_display_t display, int32_t intEnabled) {
        auto enabled = static_cast<HWC2::Vsync>(intEnabled);
        return callDisplayFunction(device, display, &Display::setVsyncEnabled,
                enabled);
    }

    class Layer {
        public:
            explicit Layer(Display& display);

            bool operator==(const Layer& other) { return mId == other.mId; }
            bool operator!=(const Layer& other) { return !(*this == other); }

            hwc2_layer_t getId() const { return mId; }
            Display& getDisplay() const { return mDisplay; }

            // HWC2 Layer functions
            HWC2::Error setBuffer(buffer_handle_t buffer, int32_t acquireFence);
            HWC2::Error setCursorPosition(int32_t x, int32_t y);
            HWC2::Error setSurfaceDamage(hwc_region_t damage);

            // HWC2 Layer state functions
            HWC2::Error setBlendMode(HWC2::BlendMode mode);
            HWC2::Error setColor(hwc_color_t color);
            HWC2::Error setCompositionType(HWC2::Composition type);
            HWC2::Error setDataspace(android_dataspace_t dataspace);
            HWC2::Error setDisplayFrame(hwc_rect_t frame);
            HWC2::Error setPlaneAlpha(float alpha);
            HWC2::Error setSidebandStream(const native_handle_t* stream);
            HWC2::Error setSourceCrop(hwc_frect_t crop);
            HWC2::Error setTransform(HWC2::Transform transform);
            HWC2::Error setVisibleRegion(hwc_region_t visible);
            HWC2::Error setZ(uint32_t z);

            HWC2::Composition getCompositionType() const {
                return mCompositionType;
            }
            uint32_t getZ() const { return mZ; }

            void addReleaseFence(int fenceFd);
            const sp<MiniFence>& getReleaseFence() const;

            void setHwc1Id(size_t id) { mHwc1Id = id; }
            size_t getHwc1Id() const { return mHwc1Id; }

            // Write state to HWC1 communication struct.
            void applyState(struct hwc_layer_1& hwc1Layer);

            std::string dump() const;

            std::size_t getNumVisibleRegions() { return mVisibleRegion.size(); }

            std::size_t getNumSurfaceDamages() { return mSurfaceDamage.size(); }

            // True if a layer cannot be properly rendered by the device due
            // to usage of SolidColor (a.k.a BackgroundColor in HWC1).
            bool hasUnsupportedBackgroundColor() {
                return (mCompositionType == HWC2::Composition::SolidColor &&
                        !mDisplay.getDevice().supportsBackgroundColor());
            }
        private:
            void applyCommonState(struct hwc_layer_1& hwc1Layer);
            void applySolidColorState(struct hwc_layer_1& hwc1Layer);
            void applySidebandState(struct hwc_layer_1& hwc1Layer);
            void applyBufferState(struct hwc_layer_1& hwc1Layer);
            void applyCompositionType(struct hwc_layer_1& hwc1Layer);

            static std::atomic<hwc2_layer_t> sNextId;
            const hwc2_layer_t mId;
            Display& mDisplay;

            FencedBuffer mBuffer;
            std::vector<hwc_rect_t> mSurfaceDamage;

            HWC2::BlendMode mBlendMode;
            hwc_color_t mColor;
            HWC2::Composition mCompositionType;
            hwc_rect_t mDisplayFrame;
            float mPlaneAlpha;
            const native_handle_t* mSidebandStream;
            hwc_frect_t mSourceCrop;
            HWC2::Transform mTransform;
            std::vector<hwc_rect_t> mVisibleRegion;

            uint32_t mZ;

            DeferredFence mReleaseFence;

            size_t mHwc1Id;
            bool mHasUnsupportedPlaneAlpha;
    };

    // Utility tempate calling a Layer object method based on ID parameters:
    // hwc2_display_t displayId
    // and
    // hwc2_layer_t layerId
    template <typename ...Args>
    static int32_t callLayerFunction(hwc2_device_t* device,
            hwc2_display_t displayId, hwc2_layer_t layerId,
            HWC2::Error (Layer::*member)(Args...), Args... args) {
        auto result = getAdapter(device)->getLayer(displayId, layerId);
        auto error = std::get<HWC2::Error>(result);
        if (error == HWC2::Error::None) {
            auto layer = std::get<Layer*>(result);
            error = ((*layer).*member)(std::forward<Args>(args)...);
        }
        return static_cast<int32_t>(error);
    }

    template <typename MF, MF memFunc, typename ...Args>
    static int32_t layerHook(hwc2_device_t* device, hwc2_display_t displayId,
            hwc2_layer_t layerId, Args... args) {
        return CfHWC2::callLayerFunction(device, displayId, layerId,
                memFunc, std::forward<Args>(args)...);
    }

    // Layer state functions

    static int32_t setLayerBlendModeHook(hwc2_device_t* device,
            hwc2_display_t display, hwc2_layer_t layer, int32_t intMode) {
        auto mode = static_cast<HWC2::BlendMode>(intMode);
        return callLayerFunction(device, display, layer,
                &Layer::setBlendMode, mode);
    }

    static int32_t setLayerCompositionTypeHook(hwc2_device_t* device,
            hwc2_display_t display, hwc2_layer_t layer, int32_t intType) {
        auto type = static_cast<HWC2::Composition>(intType);
        return callLayerFunction(device, display, layer,
                &Layer::setCompositionType, type);
    }

    static int32_t setLayerDataspaceHook(hwc2_device_t* device,
            hwc2_display_t display, hwc2_layer_t layer, int32_t intDataspace) {
        auto dataspace = static_cast<android_dataspace_t>(intDataspace);
        return callLayerFunction(device, display, layer, &Layer::setDataspace,
                dataspace);
    }

    static int32_t setLayerTransformHook(hwc2_device_t* device,
            hwc2_display_t display, hwc2_layer_t layer, int32_t intTransform) {
        auto transform = static_cast<HWC2::Transform>(intTransform);
        return callLayerFunction(device, display, layer, &Layer::setTransform,
                transform);
    }

    static int32_t setLayerZOrderHook(hwc2_device_t* device,
            hwc2_display_t display, hwc2_layer_t layer, uint32_t z) {
        return callDisplayFunction(device, display, &Display::updateLayerZ,
                layer, z);
    }

    // Adapter internals

    void populateCapabilities();
    Display* getDisplay(hwc2_display_t id);
    std::tuple<Layer*, HWC2::Error> getLayer(hwc2_display_t displayId,
            hwc2_layer_t layerId);
    void populatePrimary();

    bool prepareAllDisplays();
    std::vector<struct hwc_display_contents_1*> mHwc1Contents;
    HWC2::Error setAllDisplays();

    // Callbacks
    void hwc1Invalidate();
    void hwc1Vsync(int hwc1DisplayId, int64_t timestamp);
    void hwc1Hotplug(int hwc1DisplayId, int connected);

    // These are set in the constructor and before any asynchronous events are
    // possible

    struct hwc_composer_device_1* const mHwc1Device;
    const uint8_t mHwc1MinorVersion;
    bool mHwc1SupportsVirtualDisplays;
    bool mHwc1SupportsBackgroundColor;

    class Callbacks;
    const std::unique_ptr<Callbacks> mHwc1Callbacks;

    std::unordered_set<HWC2::Capability> mCapabilities;

    // These are only accessed from the main SurfaceFlinger thread (not from
    // callbacks or dump

    std::map<hwc2_layer_t, std::shared_ptr<Layer>> mLayers;

    // A HWC1 supports only one virtual display.
    std::shared_ptr<Display> mHwc1VirtualDisplay;

    // These are potentially accessed from multiple threads, and are protected
    // by this mutex. This needs to be recursive, since the HWC1 implementation
    // can call back into the invalidate callback on the same thread that is
    // calling prepare.
    std::recursive_timed_mutex mStateMutex;

    struct CallbackInfo {
        hwc2_callback_data_t data;
        hwc2_function_pointer_t pointer;
    };
    std::unordered_map<HWC2::Callback, CallbackInfo> mCallbacks;
    bool mHasPendingInvalidate;

    // There is a small gap between the time the HWC1 module is started and
    // when the callbacks for vsync and hotplugs are registered by the
    // CfHWC2. To prevent losing events they are stored in these arrays
    // and fed to the callback as soon as possible.
    std::vector<std::pair<int, int64_t>> mPendingVsyncs;
    std::vector<std::pair<int, int>> mPendingHotplugs;

    // Mapping between HWC1 display id and Display objects.
    std::map<hwc2_display_t, std::shared_ptr<Display>> mDisplays;

    // Map HWC1 display type (HWC_DISPLAY_PRIMARY, HWC_DISPLAY_EXTERNAL,
    // HWC_DISPLAY_VIRTUAL) to Display IDs generated by CfHWC2 objects.
    std::unordered_map<int, hwc2_display_t> mHwc1DisplayMap;
};

} // namespace android

#endif

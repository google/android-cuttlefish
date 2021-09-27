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

#include "host/libs/graphics_detector/graphics_detector.h"

#include <sstream>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <vulkan/vulkan.h>

namespace cuttlefish {
namespace {

constexpr const char kEglLib[] = "libEGL.so.1";
constexpr const char kGlLib[] = "libOpenGL.so.0";
constexpr const char kGles1Lib[] = "libGLESv1_CM.so.1";
constexpr const char kGles2Lib[] = "libGLESv2.so.2";
constexpr const char kVulkanLib[] = "libvulkan.so.1";

constexpr const char kSurfacelessContextExt[] = "EGL_KHR_surfaceless_context";

class Closer {
public:
  Closer(std::function<void()> on_close) : on_close_(on_close) {}
  ~Closer() { on_close_(); }

private:
  std::function<void()> on_close_;
};

struct LibraryCloser {
 public:
  void operator()(void* library) { dlclose(library); }
};

using ManagedLibrary = std::unique_ptr<void, LibraryCloser>;

void PopulateGlAvailability(GraphicsAvailability* availability) {
  ManagedLibrary gl_lib(dlopen(kGlLib, RTLD_NOW | RTLD_LOCAL));
  if (!gl_lib) {
    LOG(DEBUG) << "Failed to dlopen " << kGlLib << ".";
    return;
  }
  LOG(DEBUG) << "Loaded " << kGlLib << ".";
  availability->has_gl = true;
}

void PopulateGles1Availability(GraphicsAvailability* availability) {
  ManagedLibrary gles1_lib(dlopen(kGles1Lib, RTLD_NOW | RTLD_LOCAL));
  if (!gles1_lib) {
    LOG(DEBUG) << "Failed to dlopen " << kGles1Lib << ".";
    return;
  }
  LOG(DEBUG) << "Loaded " << kGles1Lib << ".";
  availability->has_gles1 = true;
}

void PopulateGles2Availability(GraphicsAvailability* availability) {
  ManagedLibrary gles2_lib(dlopen(kGles2Lib, RTLD_NOW | RTLD_LOCAL));
  if (!gles2_lib) {
    LOG(DEBUG) << "Failed to dlopen " << kGles2Lib << ".";
    return;
  }
  LOG(DEBUG) << "Loaded " << kGles2Lib << ".";
  availability->has_gles2 = true;
}

void PopulateEglAvailability(GraphicsAvailability* availability) {
  ManagedLibrary egllib(dlopen(kEglLib, RTLD_NOW | RTLD_LOCAL));
  if (!egllib) {
    LOG(DEBUG) << "Failed to dlopen " << kEglLib << ".";
    return;
  }
  LOG(DEBUG) << "Loaded " << kEglLib << ".";
  availability->has_egl = true;

  PFNEGLGETPROCADDRESSPROC eglGetProcAddress =
      reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(
          dlsym(egllib.get(), "eglGetProcAddress"));
  if (eglGetProcAddress == nullptr) {
    LOG(DEBUG) << "Failed to find function eglGetProcAddress.";
    return;
  }
  LOG(DEBUG) << "Loaded eglGetProcAddress.";

  // Some implementations have it so that eglGetProcAddress is only for
  // loading EXT functions.
  auto EglLoadFunction = [&](const char* name) {
    void* func = dlsym(egllib.get(), name);
    if (func == NULL) {
      func = reinterpret_cast<void*>(eglGetProcAddress(name));
    }
    return func;
  };

  PFNEGLGETERRORPROC eglGetError =
    reinterpret_cast<PFNEGLGETERRORPROC>(EglLoadFunction("eglGetError"));
  if (eglGetError == nullptr) {
    LOG(DEBUG) << "Failed to find function eglGetError.";
    return;
  }
  LOG(DEBUG) << "Loaded eglGetError.";

  PFNEGLGETDISPLAYPROC eglGetDisplay =
    reinterpret_cast<PFNEGLGETDISPLAYPROC>(EglLoadFunction("eglGetDisplay"));
  if (eglGetDisplay == nullptr) {
    LOG(DEBUG) << "Failed to find function eglGetDisplay.";
    return;
  }
  LOG(DEBUG) << "Loaded eglGetDisplay.";

  PFNEGLQUERYSTRINGPROC eglQueryString =
    reinterpret_cast<PFNEGLQUERYSTRINGPROC>(EglLoadFunction("eglQueryString"));
  if (eglQueryString == nullptr) {
    LOG(DEBUG) << "Failed to find function eglQueryString";
    return;
  }
  LOG(DEBUG) << "Loaded eglQueryString.";

  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display != EGL_NO_DISPLAY) {
    LOG(DEBUG) << "Found default display.";
  } else {
    LOG(DEBUG) << "Failed to get default display. " << eglGetError()
                 << ". Attempting to get surfaceless display via "
                 << "eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA)";

    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        EglLoadFunction("eglGetPlatformDisplayEXT"));
    if (eglGetPlatformDisplayEXT == nullptr) {
      LOG(DEBUG) << "Failed to find function eglGetPlatformDisplayEXT";
    } else {
      display = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA,
                                         EGL_DEFAULT_DISPLAY, NULL);
    }
  }

  if (display == EGL_NO_DISPLAY) {
    LOG(DEBUG) << "Failed to find display.";
    return;
  }

  PFNEGLINITIALIZEPROC eglInitialize =
      reinterpret_cast<PFNEGLINITIALIZEPROC>(EglLoadFunction("eglInitialize"));
  if (eglInitialize == nullptr) {
    LOG(DEBUG) << "Failed to find function eglQueryString";
    return;
  }

  EGLint client_version_major = 0;
  EGLint client_version_minor = 0;
  if (eglInitialize(display,
                    &client_version_major,
                    &client_version_minor) != EGL_TRUE) {
    LOG(DEBUG) << "Failed to initialize display.";
    return;
  }
  LOG(DEBUG) << "Initialized display.";

  const std::string version_string = eglQueryString(display, EGL_VERSION);
  if (version_string.empty()) {
    LOG(DEBUG) << "Failed to query client version.";
    return;
  }
  LOG(DEBUG) << "Found version: " << version_string;
  availability->egl_version = version_string;

  const std::string vendor_string = eglQueryString(display, EGL_VENDOR);
  if (vendor_string.empty()) {
    LOG(DEBUG) << "Failed to query vendor.";
    return;
  }
  LOG(DEBUG) << "Found vendor: " << vendor_string;
  availability->egl_vendor = vendor_string;

  const std::string extensions_string = eglQueryString(display, EGL_EXTENSIONS);
  if (extensions_string.empty()) {
    LOG(DEBUG) << "Failed to query extensions.";
    return;
  }
  LOG(DEBUG) << "Found extensions: " << extensions_string;
  availability->egl_extensions = extensions_string;

  if (extensions_string.find(kSurfacelessContextExt) == std::string::npos) {
    LOG(DEBUG) << "Failed to find extension EGL_KHR_surfaceless_context.";
    return;
  }

  const std::string display_apis_string = eglQueryString(display,
                                                         EGL_CLIENT_APIS);
  if (display_apis_string.empty()) {
    LOG(DEBUG) << "Failed to query display apis.";
    return;
  }
  LOG(DEBUG) << "Found display apis: " << display_apis_string;

  PFNEGLBINDAPIPROC eglBindAPI =
    reinterpret_cast<PFNEGLBINDAPIPROC>(EglLoadFunction("eglBindAPI"));
  if (eglBindAPI == nullptr) {
    LOG(DEBUG) << "Failed to find function eglBindAPI";
    return;
  }
  LOG(DEBUG) << "Loaded eglBindAPI.";

  if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
    LOG(DEBUG) << "Failed to bind GLES API.";
    return;
  }
  LOG(DEBUG) << "Bound GLES API.";

  PFNEGLCHOOSECONFIGPROC eglChooseConfig =
    reinterpret_cast<PFNEGLCHOOSECONFIGPROC>(
      EglLoadFunction("eglChooseConfig"));
  if (eglChooseConfig == nullptr) {
    LOG(DEBUG) << "Failed to find function eglChooseConfig";
    return;
  }
  LOG(DEBUG) << "Loaded eglChooseConfig.";

  const EGLint framebuffer_config_attributes[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, 0,
    EGL_NONE,
  };

  EGLConfig framebuffer_config;
  EGLint num_framebuffer_configs = 0;
  if (eglChooseConfig(display,
                      framebuffer_config_attributes,
                      &framebuffer_config,
                      1,
                      &num_framebuffer_configs) != EGL_TRUE) {
    LOG(DEBUG) << "Failed to find matching framebuffer config.";
    return;
  }
  LOG(DEBUG) << "Found matching framebuffer config.";

  PFNEGLCREATECONTEXTPROC eglCreateContext =
    reinterpret_cast<PFNEGLCREATECONTEXTPROC>(
      EglLoadFunction("eglCreateContext"));
  if (eglCreateContext == nullptr) {
    LOG(DEBUG) << "Failed to find function eglCreateContext";
    return;
  }
  LOG(DEBUG) << "Loaded eglCreateContext.";

  PFNEGLDESTROYCONTEXTPROC eglDestroyContext =
    reinterpret_cast<PFNEGLDESTROYCONTEXTPROC>(
      EglLoadFunction("eglDestroyContext"));
  if (eglDestroyContext == nullptr) {
    LOG(DEBUG) << "Failed to find function eglDestroyContext";
    return;
  }
  LOG(DEBUG) << "Loaded eglDestroyContext.";

  const EGLint context_attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  EGLContext context = eglCreateContext(display,
                                        framebuffer_config,
                                        EGL_NO_CONTEXT,
                                        context_attributes);
  if (context == EGL_NO_CONTEXT) {
    LOG(DEBUG) << "Failed to create EGL context.";
    return;
  }
  LOG(DEBUG) << "Created EGL context.";
  Closer context_closer([&]() { eglDestroyContext(display, context); });

  PFNEGLMAKECURRENTPROC eglMakeCurrent =
    reinterpret_cast<PFNEGLMAKECURRENTPROC>(EglLoadFunction("eglMakeCurrent"));
  if (eglMakeCurrent == nullptr) {
    LOG(DEBUG) << "Failed to find function eglMakeCurrent";
    return;
  }
  LOG(DEBUG) << "Loaded eglMakeCurrent.";

  if (eglMakeCurrent(display,
                     EGL_NO_SURFACE,
                     EGL_NO_SURFACE,
                     context) != EGL_TRUE) {
    LOG(DEBUG) << "Failed to make EGL context current.";
    return;
  }
  LOG(DEBUG) << "Make EGL context current.";
  availability->can_init_gles2_on_egl_surfaceless = true;

  PFNGLGETSTRINGPROC glGetString =
      reinterpret_cast<PFNGLGETSTRINGPROC>(eglGetProcAddress("glGetString"));

  const GLubyte* gles2_vendor = glGetString(GL_VENDOR);
  if (gles2_vendor == nullptr) {
    LOG(DEBUG) << "Failed to query GLES2 vendor.";
    return;
  }
  const std::string gles2_vendor_string((const char*)gles2_vendor);
  LOG(DEBUG) << "Found GLES2 vendor: " << gles2_vendor_string;
  availability->gles2_vendor = gles2_vendor_string;

  const GLubyte* gles2_version = glGetString(GL_VERSION);
  if (gles2_version == nullptr) {
    LOG(DEBUG) << "Failed to query GLES2 vendor.";
    return;
  }
  const std::string gles2_version_string((const char*)gles2_version);
  LOG(DEBUG) << "Found GLES2 version: " << gles2_version_string;
  availability->gles2_version = gles2_version_string;

  const GLubyte* gles2_renderer = glGetString(GL_RENDERER);
  if (gles2_renderer == nullptr) {
    LOG(DEBUG) << "Failed to query GLES2 renderer.";
    return;
  }
  const std::string gles2_renderer_string((const char*)gles2_renderer);
  LOG(DEBUG) << "Found GLES2 renderer: " << gles2_renderer_string;
  availability->gles2_renderer = gles2_renderer_string;

  const GLubyte* gles2_extensions = glGetString(GL_EXTENSIONS);
  if (gles2_extensions == nullptr) {
    LOG(DEBUG) << "Failed to query GLES2 extensions.";
    return;
  }
  const std::string gles2_extensions_string((const char*)gles2_extensions);
  LOG(DEBUG) << "Found GLES2 extensions: " << gles2_extensions_string;
  availability->gles2_extensions = gles2_extensions_string;
}

void PopulateVulkanAvailability(GraphicsAvailability* availability) {
  ManagedLibrary vklib(dlopen(kVulkanLib, RTLD_NOW | RTLD_LOCAL));
  if (!vklib) {
    LOG(DEBUG) << "Failed to dlopen " << kVulkanLib << ".";
    return;
  }
  LOG(DEBUG) << "Loaded " << kVulkanLib << ".";
  availability->has_vulkan = true;

  uint32_t instance_version = 0;

  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
      reinterpret_cast<PFN_vkGetInstanceProcAddr>(
          dlsym(vklib.get(), "vkGetInstanceProcAddr"));
  if (vkGetInstanceProcAddr == nullptr) {
    LOG(DEBUG) << "Failed to find symbol vkGetInstanceProcAddr.";
    return;
  }

  PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
          dlsym(vklib.get(), "vkEnumerateInstanceVersion"));
  if (vkEnumerateInstanceVersion == nullptr ||
      vkEnumerateInstanceVersion(&instance_version) != VK_SUCCESS) {
    instance_version = VK_API_VERSION_1_0;
  }

  PFN_vkCreateInstance vkCreateInstance =
    reinterpret_cast<PFN_vkCreateInstance>(
      vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
  if (vkCreateInstance == nullptr) {
    LOG(DEBUG) << "Failed to get function vkCreateInstance.";
    return;
  }

  VkApplicationInfo application_info;
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pNext = nullptr;
  application_info.pApplicationName = "";
  application_info.applicationVersion = 1;
  application_info.pEngineName = "";
  application_info.engineVersion = 1;
  application_info.apiVersion = instance_version;

  VkInstanceCreateInfo instance_create_info = {};
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pNext = nullptr;
  instance_create_info.flags = 0;
  instance_create_info.pApplicationInfo = &application_info;
  instance_create_info.enabledLayerCount = 0;
  instance_create_info.ppEnabledLayerNames = nullptr;
  instance_create_info.enabledExtensionCount = 0;
  instance_create_info.ppEnabledExtensionNames = nullptr;

  VkInstance instance = VK_NULL_HANDLE;
  VkResult result = vkCreateInstance(&instance_create_info, nullptr, &instance);
  if (result != VK_SUCCESS) {
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY) {
      LOG(DEBUG) << "Failed to create Vulkan instance: "
                   << "VK_ERROR_OUT_OF_HOST_MEMORY.";
    } else if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
      LOG(DEBUG) << "Failed to create Vulkan instance: "
                   << "VK_ERROR_OUT_OF_DEVICE_MEMORY.";
    } else if (result == VK_ERROR_INITIALIZATION_FAILED) {
      LOG(DEBUG) << "Failed to create Vulkan instance: "
                   << "VK_ERROR_INITIALIZATION_FAILED.";
    } else if (result == VK_ERROR_LAYER_NOT_PRESENT) {
      LOG(DEBUG) << "Failed to create Vulkan instance: "
                   << "VK_ERROR_LAYER_NOT_PRESENT.";
    } else if (result == VK_ERROR_EXTENSION_NOT_PRESENT) {
      LOG(DEBUG) << "Failed to create Vulkan instance: "
                   << "VK_ERROR_EXTENSION_NOT_PRESENT.";
    } else if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
      LOG(DEBUG) << "Failed to create Vulkan instance: "
                   << "VK_ERROR_INCOMPATIBLE_DRIVER.";
    } else {
      LOG(DEBUG) << "Failed to create Vulkan instance.";
    }
    return;
  }

  PFN_vkDestroyInstance vkDestroyInstance =
    reinterpret_cast<PFN_vkDestroyInstance>(
      vkGetInstanceProcAddr(instance, "vkDestroyInstance"));
  if (vkDestroyInstance == nullptr) {
    LOG(DEBUG) << "Failed to get function vkDestroyInstance.";
    return;
  }

  Closer instancecloser([&]() {vkDestroyInstance(instance, nullptr); });

  PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices =
    reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
      vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
  if (vkEnumeratePhysicalDevices == nullptr) {
    LOG(DEBUG) << "Failed to "
                 << "vkGetInstanceProcAddr(vkEnumeratePhysicalDevices).";
    return;
  }

  PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties =
    reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
      vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"));
  if (vkGetPhysicalDeviceProperties == nullptr) {
    LOG(DEBUG) << "Failed to "
                 << "vkGetInstanceProcAddr(vkGetPhysicalDeviceProperties).";
    return;
  }

  auto vkEnumerateDeviceExtensionProperties =
    reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
      vkGetInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties"));
  if (vkEnumerateDeviceExtensionProperties == nullptr) {
    LOG(DEBUG) << "Failed to "
                 << "vkGetInstanceProcAddr("
                 << "vkEnumerateDeviceExtensionProperties"
                 << ").";
    return;
  }

  uint32_t device_count = 0;
  result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
  if (result != VK_SUCCESS) {
    if (result == VK_INCOMPLETE) {
      LOG(DEBUG) << "Failed to enumerate physical device count: "
                   << "VK_INCOMPLETE";
    } else if (result == VK_ERROR_OUT_OF_HOST_MEMORY) {
      LOG(DEBUG) << "Failed to enumerate physical device count: "
                   << "VK_ERROR_OUT_OF_HOST_MEMORY";
    } else if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
      LOG(DEBUG) << "Failed to enumerate physical device count: "
                   << "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    } else if (result == VK_ERROR_INITIALIZATION_FAILED) {
      LOG(DEBUG) << "Failed to enumerate physical device count: "
                   << "VK_ERROR_INITIALIZATION_FAILED";
    } else {
      LOG(DEBUG) << "Failed to enumerate physical device count.";
    }
    return;
  }

  if (device_count == 0) {
    LOG(DEBUG) << "No physical devices present.";
    return;
  }

  std::vector<VkPhysicalDevice> devices(device_count, VK_NULL_HANDLE);
  result = vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
  if (result != VK_SUCCESS) {
    LOG(DEBUG) << "Failed to enumerate physical devices.";
    return;
  }

  for (VkPhysicalDevice device : devices) {
    VkPhysicalDeviceProperties device_properties = {};
    vkGetPhysicalDeviceProperties(device, &device_properties);

    LOG(DEBUG) << "Found physical device: " << device_properties.deviceName;

    uint32_t device_extensions_count = 0;
    vkEnumerateDeviceExtensionProperties(device,
                                         nullptr,
                                         &device_extensions_count,
                                         nullptr);

    std::vector<VkExtensionProperties> device_extensions;
    device_extensions.resize(device_extensions_count);

    vkEnumerateDeviceExtensionProperties(device,
                                         nullptr,
                                         &device_extensions_count,
                                         device_extensions.data());

    std::vector<const char*> device_extensions_strings;
    for (const VkExtensionProperties& device_extension : device_extensions) {
      device_extensions_strings.push_back(device_extension.extensionName);
    }

    std::string device_extensions_string =
      android::base::Join(device_extensions_strings, ' ');

    LOG(DEBUG) << "Found physical device extensions: "
                 << device_extensions_string;

    if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      availability->has_discrete_gpu = true;
      availability->discrete_gpu_device_name = device_properties.deviceName;
      availability->discrete_gpu_device_extensions = device_extensions_string;
      break;
    }
  }
}

std::string ToLower(const std::string& v) {
  std::string result = v;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

bool IsLikelySoftwareRenderer(const std::string& renderer) {
  const std::string lower_renderer = ToLower(renderer);
  return lower_renderer.find("llvmpipe") != std::string::npos;
}

GraphicsAvailability GetGraphicsAvailability() {
  GraphicsAvailability availability;

  PopulateEglAvailability(&availability);
  PopulateGlAvailability(&availability);
  PopulateGles1Availability(&availability);
  PopulateGles2Availability(&availability);
  PopulateVulkanAvailability(&availability);

  return availability;
}

}  // namespace

bool ShouldEnableAcceleratedRendering(
    const GraphicsAvailability& availability) {
  return availability.can_init_gles2_on_egl_surfaceless &&
         !IsLikelySoftwareRenderer(availability.gles2_renderer) &&
         availability.has_discrete_gpu;
}

// Runs GetGraphicsAvailability() inside of a subprocess first to ensure that
// GetGraphicsAvailability() can complete successfully without crashing
// assemble_cvd. Configurations such as GCE instances without a GPU but with GPU
// drivers for example have seen crashes.
GraphicsAvailability GetGraphicsAvailabilityWithSubprocessCheck() {
  pid_t pid = fork();
  if (pid == 0) {
    GetGraphicsAvailability();
    std::exit(0);
  }
  int status;
  if (waitpid(pid, &status, 0) != pid) {
    PLOG(DEBUG) << "Failed to wait for graphics check subprocess";
    return GraphicsAvailability{};
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return GetGraphicsAvailability();
  }
  LOG(DEBUG) << "Subprocess for detect_graphics failed with " << status;
  return GraphicsAvailability{};
}

std::ostream& operator<<(std::ostream& stream,
                         const GraphicsAvailability& availability) {
  std::ios_base::fmtflags flags_backup(stream.flags());
  stream << std::boolalpha;
  stream << "Graphics Availability:\n";

  stream << "\n";
  stream << "OpenGL lib available: " << availability.has_gl << "\n";
  stream << "OpenGL ES1 lib available: " << availability.has_gles1 << "\n";
  stream << "OpenGL ES2 lib available: " << availability.has_gles2 << "\n";
  stream << "EGL lib available: " << availability.has_egl << "\n";
  stream << "Vulkan lib available: " << availability.has_vulkan << "\n";

  stream << "\n";
  stream << "EGL client extensions: " << availability.egl_client_extensions
         << "\n";

  stream << "\n";
  stream << "EGL display vendor: " << availability.egl_vendor << "\n";
  stream << "EGL display version: " << availability.egl_version << "\n";
  stream << "EGL display extensions: " << availability.egl_extensions << "\n";

  stream << "GLES2 can init on surfaceless display: "
         << availability.can_init_gles2_on_egl_surfaceless << "\n";
  stream << "\n";
  stream << "GLES2 vendor: " << availability.gles2_vendor << "\n";
  stream << "GLES2 version: " << availability.gles2_version << "\n";
  stream << "GLES2 renderer: " << availability.gles2_renderer << "\n";
  stream << "GLES2 extensions: " << availability.gles2_extensions << "\n";

  stream << "\n";
  stream << "Vulkan discrete GPU detected: " << availability.has_discrete_gpu
         << "\n";
  if (availability.has_discrete_gpu) {
    stream << "Vulkan discrete GPU device name: "
           << availability.discrete_gpu_device_name << "\n";
    stream << "Vulkan discrete GPU device extensions: "
           << availability.discrete_gpu_device_extensions << "\n";
  }

  stream << "\n";
  stream << "Accelerated rendering supported: "
         << ShouldEnableAcceleratedRendering(availability);

  stream.flags(flags_backup);
  return stream;
}

} // namespace cuttlefish

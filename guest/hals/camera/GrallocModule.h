#ifndef GCE_CAMERA_GRALLOC_MODULE_H_
#define GCE_CAMERA_GRALLOC_MODULE_H_

#include <hardware/gralloc.h>

class GrallocModule
{
public:
  static GrallocModule &getInstance() {
    static GrallocModule instance;
    return instance;
  }

  int lock(buffer_handle_t handle,
      int usage, int l, int t, int w, int h, void **vaddr) {
    return mModule->lock(mModule, handle, usage, l, t, w, h, vaddr);
  }

#ifdef GRALLOC_MODULE_API_VERSION_0_2
  int lock_ycbcr(buffer_handle_t handle,
      int usage, int l, int t, int w, int h,
      struct android_ycbcr *ycbcr) {
    return mModule->lock_ycbcr(mModule, handle, usage, l, t, w, h, ycbcr);
  }
#endif

  int unlock(buffer_handle_t handle) {
    return mModule->unlock(mModule, handle);
  }

private:
  GrallocModule() {
    const hw_module_t *module = NULL;
    int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
    if (ret) {
      ALOGE("%s: Failed to get gralloc module: %d", __FUNCTION__, ret);
    }
    mModule = reinterpret_cast<const gralloc_module_t*>(module);
  }
  const gralloc_module_t *mModule;
};

#endif

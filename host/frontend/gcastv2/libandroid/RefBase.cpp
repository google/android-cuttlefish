#include <utils/RefBase.h>

namespace android {

RefBase *WeakList::promote() const {
    if (mObject == NULL) {
        return NULL;
    }

    mObject->incStrong(this);
    return mObject;
}

}  // namespace android


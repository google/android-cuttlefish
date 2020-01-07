#include <media/stagefright/FileSource.h>

#include <fcntl.h>
#include <unistd.h>

#ifdef TARGET_IOS
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace android {

FileSource::FileSource(const char *path)
    : mFd(-1),
      mInitCheck(NO_INIT) {
#ifdef TARGET_IOS
    CFBundleRef mainBundle = CFBundleGetMainBundle();

    CFStringRef pathRef =
        CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);

    CFURLRef url = CFBundleCopyResourceURL(mainBundle, pathRef, NULL, NULL);

    CFRelease(pathRef);
    pathRef = nullptr;

    pathRef = CFURLCopyPath(url);

    CFRelease(url);
    url = nullptr;

    char fullPath[256];
    CFStringGetCString(
            pathRef, fullPath, sizeof(fullPath), kCFStringEncodingUTF8);

    CFRelease(pathRef);
    pathRef = nullptr;

    path = fullPath;
#endif

    mFd = open(path, O_RDONLY);
    mInitCheck = (mFd >= 0) ? OK : -errno;
}

FileSource::~FileSource() {
    if (mFd >= 0) {
        close(mFd);
        mFd = -1;
    }
}

status_t FileSource::initCheck() const {
    return mInitCheck;
}

status_t FileSource::getSize(off_t *size) const {
    *size = lseek(mFd, 0, SEEK_END);
    if (*size == -1) {
        return -errno;
    }

    return OK;
}

ssize_t FileSource::readAt(off_t offset, void *data, size_t size) {
    ssize_t n = pread(mFd, data, size, offset);

    if (n == -1) {
        return -errno;
    }

    return n;
}

}  // namespace android


#ifndef FILE_SOURCE_H_

#define FILE_SOURCE_H_

#include <utils/Errors.h>
#include <utils/RefBase.h>

namespace android {

struct FileSource : public RefBase {
    FileSource(const char *path);

    status_t initCheck() const;

    status_t getSize(off_t *size) const;
    ssize_t readAt(off_t offset, void *data, size_t size);

protected:
    virtual ~FileSource();

private:
    int mFd;
    status_t mInitCheck;

    DISALLOW_EVIL_CONSTRUCTORS(FileSource);
};

}  // namespace android

#endif  // FILE_SOURCE_H_

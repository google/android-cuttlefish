#ifndef ANDROID_METADATA_H_

#define ANDROID_METADATA_H_

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>
#include <utils/RefBase.h>

namespace android {

enum {
    kKeyMIMEType        = 'mime',
    kKeyWidth           = 'widt',
    kKeyHeight          = 'heig',
    kKeyDuration        = 'dura',
    kKeyAVCC            = 'avcc',
    kKeyESDS            = 'esds',
    kKeyTime            = 'time',
    kKeySampleRate      = 'srat',
    kKeyChannelCount    = '#chn',
    kKeyIsADTS          = 'adts',
};

enum {
  kTypeESDS = 'esds'
};

struct MetaData : public RefBase {
    MetaData()
        : mMessage(new AMessage) {
    }

    void setInt32(uint32_t key, int32_t x) {
        std::string tmp;
        tmp.append(std::to_string(key));

        mMessage->setInt32(tmp.c_str(), x);
    }

    void setInt64(uint32_t key, int64_t x) {
        std::string tmp;
        tmp.append(std::to_string(key));

        mMessage->setInt64(tmp.c_str(), x);
    }

    bool findInt32(uint32_t key, int32_t *x) const {
        std::string tmp;
      tmp.append(std::to_string(key));

      return mMessage->findInt32(tmp.c_str(), x);
    }

    bool findCString(uint32_t key, const char **x) const {
        std::string tmp;
      tmp.append(std::to_string(key));

      static std::string value;
      if (!mMessage->findString(tmp.c_str(), &value)) {
          *x = NULL;
          return false;
      }

      *x = value.c_str();
      return true;
    }

    void setCString(uint32_t key, const char *s) {
        std::string tmp;
        tmp.append(std::to_string(key));

        mMessage->setString(tmp.c_str(), s);
    }

    void setData(uint32_t key, uint32_t type, const void *data, size_t size) {
        std::string tmp;
        tmp.append(std::to_string(key));

        sp<ABuffer> buffer = new ABuffer(size);
        buffer->meta()->setInt32("type", type);
        memcpy(buffer->data(), data, size);

        mMessage->setObject(tmp.c_str(), buffer);
    }

    bool findData(
            uint32_t key, uint32_t *type, const void **data, size_t *size) const {
        std::string tmp;
        tmp.append(std::to_string(key));

        sp<RefBase> obj;
        if (!mMessage->findObject(tmp.c_str(), &obj)) {
            return false;
        }

        sp<ABuffer> buffer = static_cast<ABuffer *>(obj.get());
        CHECK(buffer->meta()->findInt32("type", (int32_t *)type));
        *data = buffer->data();
        *size = buffer->size();

        return true;
    }

protected:
    virtual ~MetaData() {}

private:
    sp<AMessage> mMessage;

    DISALLOW_EVIL_CONSTRUCTORS(MetaData);
};

}  // namespace android

#endif  // ANDROID_METADATA_H_

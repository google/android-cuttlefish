/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef A_MESSAGE_H_

#define A_MESSAGE_H_

#include "ABase.h"
#include "ALooper.h"

#include <memory>
#include <string>

namespace android {

struct ABuffer;

struct AMessage {
    explicit AMessage(uint32_t what = 0, ALooper::handler_id target = 0);

    AMessage(const AMessage &) = delete;
    AMessage &operator=(const AMessage &) = delete;

    void setWhat(uint32_t what);
    uint32_t what() const;

    void setTarget(ALooper::handler_id target);
    ALooper::handler_id target() const;

    void setInt32(const char *name, int32_t value);
    void setInt64(const char *name, int64_t value);
    void setSize(const char *name, size_t value);
    void setFloat(const char *name, float value);
    void setDouble(const char *name, double value);
    void setPointer(const char *name, void *value);
    void setString(const char *name, const char *s, ssize_t len = -1);
    void setObject(const char *name, const std::shared_ptr<void> &obj);
    void setMessage(const char *name, const std::shared_ptr<AMessage> &obj);
    void setBuffer(const char *name, const std::shared_ptr<ABuffer> &obj);

    bool findInt32(const char *name, int32_t *value) const;
    bool findInt64(const char *name, int64_t *value) const;
    bool findSize(const char *name, size_t *value) const;
    bool findFloat(const char *name, float *value) const;
    bool findDouble(const char *name, double *value) const;
    bool findPointer(const char *name, void **value) const;
    bool findString(const char *name, std::string *value) const;
    bool findObject(const char *name, std::shared_ptr<void> *obj) const;
    bool findMessage(const char *name, std::shared_ptr<AMessage> *obj) const;
    bool findBuffer(const char *name, std::shared_ptr<ABuffer> *obj) const;

    static void post(std::shared_ptr<AMessage> msg, int64_t delayUs = 0);

    std::shared_ptr<AMessage> dup() const;

    std::string debugString(size_t indent = 0) const;

    size_t countEntries() const;

    enum Type {
        kTypeInt32,
        kTypeInt64,
        kTypeSize,
        kTypeFloat,
        kTypeDouble,
        kTypePointer,
        kTypeString,
        kTypeObject,
        kTypeMessage,
        kTypeBuffer,
    };
    const char *getEntryNameAt(size_t i, Type *type) const;

    virtual ~AMessage();

private:
    uint32_t mWhat;
    ALooper::handler_id mTarget;

    struct Item {
        union {
            int32_t int32Value;
            int64_t int64Value;
            size_t sizeValue;
            float floatValue;
            double doubleValue;
            void *ptrValue;
            std::string *stringValue;
        } u;
        std::shared_ptr<void> refValue;
        const char *mName;
        Type mType;
    };

    enum {
        kMaxNumItems = 16
    };
    Item mItems[kMaxNumItems];
    size_t mNumItems;

    void clear();
    Item *allocateItem(const char *name);
    void freeItem(Item *item);
    const Item *findItem(const char *name, Type type) const;
};

}  // namespace android

#endif  // A_MESSAGE_H_

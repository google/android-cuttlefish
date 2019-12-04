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

#include <media/stagefright/foundation/AMessage.h>

#include <media/stagefright/Utils.h>
#include <media/stagefright/foundation/AAtomizer.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooperRoster.h>
#include <media/stagefright/foundation/hexdump.h>

namespace android {

AMessage::AMessage(uint32_t what, ALooper::handler_id target)
    : mWhat(what),
      mTarget(target),
      mNumItems(0) {
}

AMessage::~AMessage() {
    clear();
}

void AMessage::setWhat(uint32_t what) {
    mWhat = what;
}

uint32_t AMessage::what() const {
    return mWhat;
}

void AMessage::setTarget(ALooper::handler_id handlerID) {
    mTarget = handlerID;
}

ALooper::handler_id AMessage::target() const {
    return mTarget;
}

void AMessage::clear() {
    for (size_t i = 0; i < mNumItems; ++i) {
        Item *item = &mItems[i];
        freeItem(item);
    }
    mNumItems = 0;
}

void AMessage::freeItem(Item *item) {
    switch (item->mType) {
        case kTypeString:
        {
            delete item->u.stringValue;
            break;
        }

        case kTypeObject:
        case kTypeMessage:
        case kTypeBuffer:
        {
            if (item->refValue) {
                item->refValue.reset();
            }
            break;
        }

        default:
            break;
    }
}

AMessage::Item *AMessage::allocateItem(const char *name) {
    name = AAtomizer::Atomize(name);

    size_t i = 0;
    while (i < mNumItems && mItems[i].mName != name) {
        ++i;
    }

    Item *item;

    if (i < mNumItems) {
        item = &mItems[i];
        freeItem(item);
    } else {
        CHECK(mNumItems < kMaxNumItems);
        i = mNumItems++;
        item = &mItems[i];

        item->mName = name;
    }

    return item;
}

const AMessage::Item *AMessage::findItem(
        const char *name, Type type) const {
    name = AAtomizer::Atomize(name);

    for (size_t i = 0; i < mNumItems; ++i) {
        const Item *item = &mItems[i];

        if (item->mName == name) {
            return item->mType == type ? item : NULL;
        }
    }

    return NULL;
}

#define BASIC_TYPE(NAME,FIELDNAME,TYPENAME)                             \
void AMessage::set##NAME(const char *name, TYPENAME value) {            \
    Item *item = allocateItem(name);                                    \
                                                                        \
    item->mType = kType##NAME;                                          \
    item->u.FIELDNAME = value;                                          \
}                                                                       \
                                                                        \
bool AMessage::find##NAME(const char *name, TYPENAME *value) const {    \
    const Item *item = findItem(name, kType##NAME);                     \
    if (item) {                                                         \
        *value = item->u.FIELDNAME;                                     \
        return true;                                                    \
    }                                                                   \
    return false;                                                       \
}

BASIC_TYPE(Int32,int32Value,int32_t)
BASIC_TYPE(Int64,int64Value,int64_t)
BASIC_TYPE(Size,sizeValue,size_t)
BASIC_TYPE(Float,floatValue,float)
BASIC_TYPE(Double,doubleValue,double)
BASIC_TYPE(Pointer,ptrValue,void *)

#undef BASIC_TYPE

void AMessage::setString(
        const char *name, const char *s, ssize_t len) {
    Item *item = allocateItem(name);
    item->mType = kTypeString;
    item->u.stringValue = new std::string(s, len < 0 ? strlen(s) : len);
}

void AMessage::setObject(const char *name, const std::shared_ptr<void> &obj) {
    Item *item = allocateItem(name);
    item->mType = kTypeObject;

    item->refValue = obj;
}

void AMessage::setMessage(const char *name, const std::shared_ptr<AMessage> &obj) {
    Item *item = allocateItem(name);
    item->mType = kTypeMessage;

    item->refValue = obj;
}

void AMessage::setBuffer(const char *name, const std::shared_ptr<ABuffer> &obj) {
    Item *item = allocateItem(name);
    item->mType = kTypeBuffer;

    item->refValue = obj;
}

bool AMessage::findString(const char *name, std::string *value) const {
    const Item *item = findItem(name, kTypeString);
    if (item) {
        *value = *item->u.stringValue;
        return true;
    }
    return false;
}

bool AMessage::findObject(const char *name, std::shared_ptr<void> *obj) const {
    const Item *item = findItem(name, kTypeObject);
    if (item) {
        *obj = item->refValue;
        return true;
    }
    return false;
}

bool AMessage::findMessage(const char *name, std::shared_ptr<AMessage> *obj) const {
    const Item *item = findItem(name, kTypeMessage);
    if (item) {
        *obj = std::static_pointer_cast<AMessage>(item->refValue);
        return true;
    }
    return false;
}

bool AMessage::findBuffer(const char *name, std::shared_ptr<ABuffer> *obj) const {
    const Item *item = findItem(name, kTypeBuffer);
    if (item) {
        *obj = std::static_pointer_cast<ABuffer>(item->refValue);
        return true;
    }
    return false;
}

void AMessage::post(std::shared_ptr<AMessage> msg, int64_t delayUs) {
    extern ALooperRoster gLooperRoster;

    gLooperRoster.postMessage(msg, delayUs);
}

std::shared_ptr<AMessage> AMessage::dup() const {
    std::shared_ptr<AMessage> msg(new AMessage(mWhat, mTarget));
    msg->mNumItems = mNumItems;

    for (size_t i = 0; i < mNumItems; ++i) {
        const Item *from = &mItems[i];
        Item *to = &msg->mItems[i];

        to->mName = from->mName;
        to->mType = from->mType;
        to->refValue = from->refValue;

        switch (from->mType) {
            case kTypeString:
            {
                to->u.stringValue = new std::string(*from->u.stringValue);
                break;
            }

            case kTypeObject:
            case kTypeMessage:
            case kTypeBuffer:
            {
                break;
            }

            default:
            {
                to->u = from->u;
                break;
            }
        }
    }

    return msg;
}

static bool isFourcc(uint32_t what) {
    return isprint(what & 0xff)
        && isprint((what >> 8) & 0xff)
        && isprint((what >> 16) & 0xff)
        && isprint((what >> 24) & 0xff);
}

static void appendIndent(std::string *s, size_t indent) {
    static const char kWhitespace[] =
        "                                        "
        "                                        ";

    CHECK_LT((size_t)indent, sizeof(kWhitespace));

    s->append(kWhitespace, indent);
}

std::string AMessage::debugString(size_t indent) const {
    std::string s = "AMessage(what = ";

    std::string tmp;
    if (isFourcc(mWhat)) {
        tmp = StringPrintf(
                "'%c%c%c%c'",
                (char)(mWhat >> 24),
                (char)((mWhat >> 16) & 0xff),
                (char)((mWhat >> 8) & 0xff),
                (char)(mWhat & 0xff));
    } else {
        tmp = StringPrintf("0x%08x", mWhat);
    }
    s.append(tmp);

    if (mTarget != 0) {
        tmp = StringPrintf(", target = %d", mTarget);
        s.append(tmp);
    }
    s.append(") = {\n");

    for (size_t i = 0; i < mNumItems; ++i) {
        const Item &item = mItems[i];

        switch (item.mType) {
            case kTypeInt32:
                tmp = StringPrintf(
                        "int32_t %s = %d", item.mName, item.u.int32Value);
                break;
            case kTypeInt64:
                tmp = StringPrintf(
                        "int64_t %s = %lld", item.mName, item.u.int64Value);
                break;
            case kTypeSize:
                tmp = StringPrintf(
                        "size_t %s = %d", item.mName, item.u.sizeValue);
                break;
            case kTypeFloat:
                tmp = StringPrintf(
                        "float %s = %f", item.mName, item.u.floatValue);
                break;
            case kTypeDouble:
                tmp = StringPrintf(
                        "double %s = %f", item.mName, item.u.doubleValue);
                break;
            case kTypePointer:
                tmp = StringPrintf(
                        "void *%s = %p", item.mName, item.u.ptrValue);
                break;
            case kTypeString:
                tmp = StringPrintf(
                        "string %s = \"%s\"",
                        item.mName,
                        item.u.stringValue->c_str());
                break;
            case kTypeObject:
                tmp = StringPrintf(
                        "Object *%s = %p", item.mName, item.refValue.get());
                break;
            case kTypeBuffer:
            {
                std::shared_ptr<ABuffer> buffer = std::static_pointer_cast<ABuffer>(item.refValue);

                if (buffer != NULL && buffer->data() != NULL && buffer->size() <= 1024) {
                    tmp = StringPrintf("Buffer %s = {\n", item.mName);
                    hexdump(buffer->data(), buffer->size(), indent + 4, &tmp);
                    appendIndent(&tmp, indent + 2);
                    tmp.append("}");
                } else {
                    tmp = StringPrintf(
                            "Buffer *%s = %p", item.mName, buffer.get());
                }
                break;
            }
            case kTypeMessage:
                tmp = StringPrintf(
                        "AMessage %s = %s",
                        item.mName,
                        std::static_pointer_cast<AMessage>(
                            item.refValue)->debugString(
                                indent + strlen(item.mName) + 14).c_str());
                break;
            default:
                TRESPASS();
        }

        appendIndent(&s, indent);
        s.append("  ");
        s.append(tmp);
        s.append("\n");
    }

    appendIndent(&s, indent);
    s.append("}");

    return s;
}

size_t AMessage::countEntries() const {
    return mNumItems;
}

const char *AMessage::getEntryNameAt(size_t i, Type *type) const {
    CHECK_LT(i, mNumItems);
    *type = mItems[i].mType;

    return mItems[i].mName;
}

}  // namespace android

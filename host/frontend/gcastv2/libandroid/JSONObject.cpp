/*
 * Copyright (C) 2013 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "JSONObject"
#include <utils/Log.h>

#include <media/stagefright/foundation/JSONObject.h>

#include <ctype.h>
#include <media/stagefright/Utils.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaErrors.h>

namespace android {

// static
ssize_t JSONValue::Parse(const char *data, size_t size, JSONValue *out) {
    size_t offset = 0;
    while (offset < size && isspace(data[offset])) {
        ++offset;
    }

    if (offset == size) {
        return ERROR_MALFORMED;
    }

    if (data[offset] == '[') {
        sp<JSONArray> array = new JSONArray;
        ++offset;

        for (;;) {
            while (offset < size && isspace(data[offset])) {
                ++offset;
            }

            if (offset == size) {
                return ERROR_MALFORMED;
            }

            if (data[offset] == ']') {
                ++offset;
                break;
            }

            JSONValue val;
            ssize_t n = Parse(&data[offset], size - offset, &val);

            if (n < 0) {
                return n;
            }

            array->addValue(val);

            offset += n;

            while (offset < size && isspace(data[offset])) {
                ++offset;
            }

            if (offset == size) {
                return ERROR_MALFORMED;
            }

            if (data[offset] == ',') {
                ++offset;
            } else if (data[offset] != ']') {
                return ERROR_MALFORMED;
            }
        };

        out->setArray(array);

        return offset;
    } else if (data[offset] == '{') {
        sp<JSONObject> obj = new JSONObject;
        ++offset;

        for (;;) {
            while (offset < size && isspace(data[offset])) {
                ++offset;
            }

            if (offset == size) {
                return ERROR_MALFORMED;
            }

            if (data[offset] == '}') {
                ++offset;
                break;
            }

            JSONValue key;
            ssize_t n = Parse(&data[offset], size - offset, &key);

            if (n < 0) {
                return n;
            }

            if (key.type() != TYPE_STRING) {
                return ERROR_MALFORMED;
            }

            offset += n;

            while (offset < size && isspace(data[offset])) {
                ++offset;
            }

            if (offset == size || data[offset] != ':') {
                return ERROR_MALFORMED;
            }

            ++offset;

            JSONValue val;
            n = Parse(&data[offset], size - offset, &val);

            if (n < 0) {
                return n;
            }

            std::string keyVal;
            CHECK(key.getString(&keyVal));

            obj->setValue(keyVal.c_str(), val);

            offset += n;

            while (offset < size && isspace(data[offset])) {
                ++offset;
            }

            if (offset == size) {
                return ERROR_MALFORMED;
            }

            if (data[offset] == ',') {
                ++offset;
            } else if (data[offset] != '}') {
                return ERROR_MALFORMED;
            }
        };

        out->setObject(obj);

        return offset;
    } else if (data[offset] == '"') {
        ++offset;

        std::string s;
        bool escaped = false;
        while (offset < size) {
            if (escaped) {
                char c;
                switch (data[offset]) {
                    case '\"':
                    case '\\':
                    case '/':
                        c = data[offset];
                        break;
                    case 'b':
                        c = '\x08';
                        break;
                    case 'f':
                        c = '\x0c';
                        break;
                    case 'n':
                        c = '\x0a';
                        break;
                    case 'r':
                        c = '\x0d';
                        break;
                    case 't':
                        c = '\x09';
                        break;
                    default:
                        return ERROR_MALFORMED;
                }

                s.append(1, c);
                ++offset;

                escaped = false;
                continue;
            } else if (data[offset] == '\\') {
                escaped = true;
                ++offset;
                continue;
            } else if (data[offset] == '"') {
                break;
            }

            s.append(1, data[offset++]);
        }

        if (offset == size) {
            return ERROR_MALFORMED;
        }

        ++offset;
        out->setString(s);

        return offset;
    } else if (isdigit(data[offset]) || data[offset] == '-') {
        bool negate = false;
        if (data[offset] == '-') {
            negate = true;
            ++offset;

            if (offset == size) {
                return ERROR_MALFORMED;
            }
        }

        size_t firstDigitOffset = offset;
        while (offset < size && isdigit(data[offset])) {
            ++offset;
        }

        size_t numDigits = offset - firstDigitOffset;
        if (numDigits > 1 && data[firstDigitOffset] == '0') {
            // No leading zeros.
            return ERROR_MALFORMED;
        }

        size_t firstFracDigitOffset = 0;
        size_t numFracDigits = 0;

        if (offset < size && data[offset] == '.') {
            ++offset;

            firstFracDigitOffset = offset;
            while (offset < size && isdigit(data[offset])) {
                ++offset;
            }

            numFracDigits = offset - firstFracDigitOffset;
            if (numFracDigits == 0) {
                return ERROR_MALFORMED;
            }
        }

        bool negateExponent = false;
        size_t firstExpDigitOffset = 0;
        size_t numExpDigits = 0;

        if (offset < size && (data[offset] == 'e' || data[offset] == 'E')) {
            ++offset;

            if (offset == size) {
                return ERROR_MALFORMED;
            }

            if (data[offset] == '+' || data[offset] == '-') {
                if (data[offset] == '-') {
                    negateExponent = true;
                }

                ++offset;
            }

            firstExpDigitOffset = offset;
            while (offset < size && isdigit(data[offset])) {
                ++offset;
            }

            numExpDigits = offset - firstExpDigitOffset;
            if (numExpDigits == 0) {
                return ERROR_MALFORMED;
            }
        }

        CHECK_EQ(numFracDigits, 0u);
        CHECK_EQ(numExpDigits, 0u);

        int32_t x = 0;
        for (size_t i = 0; i < numDigits; ++i) {
            x *= 10;
            x += data[firstDigitOffset + i] - '0';

            CHECK_GE(x, 0);
        }

        if (negate) {
            x = -x;
        }

        out->setInt32(x);

        return offset;
    } else if (offset + 4 <= size && !strncmp("null", &data[offset], 4)) {
        out->unset();
        return offset + 4;
    } else if (offset + 4 <= size && !strncmp("true", &data[offset], 4)) {
        out->setBoolean(true);
        return offset + 4;
    } else if (offset + 5 <= size && !strncmp("false", &data[offset], 5)) {
        out->setBoolean(false);
        return offset + 5;
    }

    return ERROR_MALFORMED;
}

JSONValue::JSONValue()
    : mType(TYPE_NULL) {
}

JSONValue::JSONValue(const JSONValue &other)
    : mType(TYPE_NULL) {
    *this = other;
}

JSONValue &JSONValue::operator=(const JSONValue &other) {
    if (&other != this) {
        unset();
        mType = other.mType;
        mValue = other.mValue;

        switch (mType) {
            case TYPE_STRING:
                mValue.mString = new std::string(*other.mValue.mString);
                break;
            case TYPE_OBJECT:
            case TYPE_ARRAY:
                mValue.mObjectOrArray->incStrong(this);
                break;

            default:
                break;
        }
    }

    return *this;
}

JSONValue::~JSONValue() {
    unset();
}

JSONValue::FieldType JSONValue::type() const {
    return mType;
}

bool JSONValue::getInt32(int32_t *value) const {
    if (mType != TYPE_NUMBER) {
        return false;
    }

    *value = mValue.mInt32;
    return true;
}

bool JSONValue::getString(std::string *value) const {
    if (mType != TYPE_STRING) {
        return false;
    }

    *value = *mValue.mString;
    return true;
}

bool JSONValue::getBoolean(bool *value) const {
    if (mType != TYPE_BOOLEAN) {
        return false;
    }

    *value = mValue.mBoolean;
    return true;
}

bool JSONValue::getObject(sp<JSONObject> *value) const {
    if (mType != TYPE_OBJECT) {
        return false;
    }

    *value = static_cast<JSONObject *>(mValue.mObjectOrArray);
    return true;
}

bool JSONValue::getArray(sp<JSONArray> *value) const {
    if (mType != TYPE_ARRAY) {
        return false;
    }

    *value = static_cast<JSONArray *>(mValue.mObjectOrArray);
    return true;
}

void JSONValue::setInt32(int32_t value) {
    unset();

    mValue.mInt32 = value;
    mType = TYPE_NUMBER;
}

void JSONValue::setString(std::string_view value) {
    unset();

    mValue.mString = new std::string(value);
    mType = TYPE_STRING;
}

void JSONValue::setBoolean(bool value) {
    unset();

    mValue.mBoolean = value;
    mType = TYPE_BOOLEAN;
}

void JSONValue::setObject(const sp<JSONObject> &obj) {
    unset();

    mValue.mObjectOrArray = obj.get();
    mValue.mObjectOrArray->incStrong(this);

    mType = TYPE_OBJECT;
}

void JSONValue::setArray(const sp<JSONArray> &array) {
    unset();

    mValue.mObjectOrArray = array.get();
    mValue.mObjectOrArray->incStrong(this);

    mType = TYPE_ARRAY;
}

void JSONValue::unset() {
    switch (mType) {
        case TYPE_STRING:
            delete mValue.mString;
            break;
        case TYPE_OBJECT:
        case TYPE_ARRAY:
            mValue.mObjectOrArray->decStrong(this);
            break;

        default:
            break;
    }

    mType = TYPE_NULL;
}

static void EscapeString(const char *in, size_t inSize, std::string *out) {
    CHECK(in != out->c_str());
    out->clear();

    for (size_t i = 0; i < inSize; ++i) {
        char c = in[i];
        switch (c) {
            case '\"':
                out->append("\\\"");
                break;
            case '\\':
                out->append("\\\\");
                break;
            case '/':
                out->append("\\/");
                break;
            case '\x08':
                out->append("\\b");
                break;
            case '\x0c':
                out->append("\\f");
                break;
            case '\x0a':
                out->append("\\n");
                break;
            case '\x0d':
                out->append("\\r");
                break;
            case '\x09':
                out->append("\\t");
                break;
            default:
                out->append(1, c);
                break;
        }
    }
}

std::string JSONValue::toString(size_t depth, bool indentFirstLine) const {
    static const char kIndent[] = "                                        ";

    std::string out;

    switch (mType) {
        case TYPE_STRING:
        {
            std::string escaped;
            EscapeString(
                    mValue.mString->c_str(), mValue.mString->size(), &escaped);

            out.append("\"");
            out.append(escaped);
            out.append("\"");
            break;
        }

        case TYPE_NUMBER:
        {
            out = StringPrintf("%d", mValue.mInt32);
            break;
        }

        case TYPE_BOOLEAN:
        {
            out = mValue.mBoolean ? "true" : "false";
            break;
        }

        case TYPE_NULL:
        {
            out = "null";
            break;
        }

        case TYPE_OBJECT:
        case TYPE_ARRAY:
        {
            out = (mType == TYPE_OBJECT) ? "{\n" : "[\n";
            out.append(mValue.mObjectOrArray->internalToString(depth + 1, true));
            out.append("\n");
            out.append(kIndent, 2 * depth);
            out.append(mType == TYPE_OBJECT ? "}" : "]");
            break;
        }

        default:
            TRESPASS();
    }

    if (indentFirstLine) {
        out.insert(0, kIndent, 2 * depth);
    }

    return out;
}

////////////////////////////////////////////////////////////////////////////////

// static
sp<JSONCompound> JSONCompound::Parse(const char *data, size_t size) {
    JSONValue value;
    ssize_t result = JSONValue::Parse(data, size, &value);

    if (result < 0) {
        return NULL;
    }

    sp<JSONObject> obj;
    if (value.getObject(&obj)) {
        return obj;
    }

    sp<JSONArray> array;
    if (value.getArray(&array)) {
        return array;
    }

    return NULL;
}

std::string JSONCompound::toString(size_t depth, bool indentFirstLine) const {
    JSONValue val;
    if (isObject()) {
        val.setObject((JSONObject *)this);
    } else {
        val.setArray((JSONArray *)this);
    }

    return val.toString(depth, indentFirstLine);
}

////////////////////////////////////////////////////////////////////////////////

JSONObject::JSONObject() {}
JSONObject::~JSONObject() {}

bool JSONObject::isObject() const {
    return true;
}

bool JSONObject::getValue(const char *key, JSONValue *value) const {
    auto it = mValues.find(key);

    if (it == mValues.end()) {
        return false;
    }

    *value = it->second;

    return true;
}

void JSONObject::setValue(const char *key, const JSONValue &value) {
    mValues[std::string(key)] = value;
}

void JSONObject::remove(const char *key) {
    mValues.erase(key);
}

std::string JSONObject::internalToString(
        size_t depth, bool /* indentFirstLine */) const {
    static const char kIndent[] = "                                        ";

    std::string out;
    for (auto it = mValues.begin(); it != mValues.end();) {
        const std::string &key = it->first;
        std::string escapedKey;
        EscapeString(key.c_str(), key.size(), &escapedKey);

        out.append(kIndent, 2 * depth);
        out.append("\"");
        out.append(escapedKey);
        out.append("\": ");

        out.append(it->second.toString(depth + 1, false));

        ++it;
        if (it != mValues.end()) {
            out.append(",\n");
        }
    }

    return out;
}

////////////////////////////////////////////////////////////////////////////////

JSONArray::JSONArray() {}

JSONArray::~JSONArray() {}

bool JSONArray::isObject() const {
    return false;
}

size_t JSONArray::size() const {
    return mValues.size();
}

bool JSONArray::getValue(size_t key, JSONValue *value) const {
    if (key >= mValues.size()) {
        return false;
    }

    *value = mValues[key];

    return true;
}

void JSONArray::addValue(const JSONValue &value) {
    mValues.push_back(value);
}

std::string JSONArray::internalToString(
        size_t depth, bool /* indentFirstLine */) const {
    std::string out;
    for (size_t i = 0; i < mValues.size(); ++i) {
        out.append(mValues[i].toString(depth));

        if (i + 1 < mValues.size()) {
            out.append(",\n");
        }
    }

    return out;
}

////////////////////////////////////////////////////////////////////////////////

}  // namespace android


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

#ifndef JSON_OBJECT_H_

#define JSON_OBJECT_H_

#include <media/stagefright/foundation/ABase.h>
#include <utils/Errors.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace android {

struct JSONArray;
struct JSONCompound;
struct JSONObject;

struct JSONValue {
    enum FieldType {
        TYPE_STRING,
        TYPE_NUMBER,
        TYPE_BOOLEAN,
        TYPE_NULL,
        TYPE_OBJECT,
        TYPE_ARRAY,
    };

    // Returns the number of bytes consumed or an error.
    static ssize_t Parse(const char *data, size_t size, JSONValue *out);

    JSONValue();
    JSONValue(const JSONValue &);
    JSONValue &operator=(const JSONValue &);
    ~JSONValue();

    FieldType type() const;
    bool getInt32(int32_t *value) const;
    bool getString(std::string *value) const;
    bool getBoolean(bool *value) const;
    bool getObject(std::shared_ptr<JSONObject> *value) const;
    bool getArray(std::shared_ptr<JSONArray> *value) const;

    void setInt32(int32_t value);
    void setString(std::string_view value);
    void setBoolean(bool value);
    void setObject(std::shared_ptr<JSONObject> obj);
    void setArray(std::shared_ptr<JSONArray> array);
    void unset();  // i.e. setNull()

    std::string toString(size_t depth = 0, bool indentFirstLine = true) const;

private:
    FieldType mType;

    union {
        int32_t mInt32;
        std::string *mString;
        bool mBoolean;
    } mValue;
    std::shared_ptr<JSONCompound> mObjectOrArray;
};

struct JSONCompound {
    JSONCompound(const JSONCompound &) = delete;
    JSONCompound &operator=(const JSONCompound &) = delete;

    static std::shared_ptr<JSONCompound> Parse(const char *data, size_t size);

    std::string toString(size_t depth = 0, bool indentFirstLine = true) const;

    virtual bool isObject() const = 0;

    virtual ~JSONCompound() {}

protected:
    virtual std::string internalToString(
            size_t depth, bool indentFirstLine) const = 0;

    JSONCompound() {}

private:
    friend struct JSONValue;
};

template<class KEY>
struct JSONBase : public JSONCompound {
    explicit JSONBase() {}

    JSONBase(const JSONBase &) = delete;
    JSONBase &operator=(const JSONBase &) = delete;

#define PREAMBLE()                              \
    JSONValue value;                            \
    if (!getValue(key, &value)) {               \
        return false;                           \
    }

    bool getFieldType(KEY key, JSONValue::FieldType *type) const {
        PREAMBLE()
        *type = value.type();
        return true;
    }

    bool getInt32(KEY key, int32_t *out) const {
        PREAMBLE()
        return value.getInt32(out);
    }

    bool getString(KEY key, std::string *out) const {
        PREAMBLE()
        return value.getString(out);
    }

    bool getBoolean(KEY key, bool *out) const {
        PREAMBLE()
        return value.getBoolean(out);
    }

    bool getObject(KEY key, std::shared_ptr<JSONObject> *obj) const {
        PREAMBLE()
        return value.getObject(obj);
    }

    bool getArray(KEY key, std::shared_ptr<JSONArray> *obj) const {
        PREAMBLE()
        return value.getArray(obj);
    }

#undef PREAMBLE

    virtual ~JSONBase() {}

protected:
    virtual bool getValue(KEY key, JSONValue *value) const = 0;
};

struct JSONObject : public JSONBase<const char *> {
    explicit JSONObject();

    JSONObject(const JSONObject &) = delete;
    JSONObject &operator=(const JSONObject &) = delete;

    virtual bool isObject() const;
    void setValue(const char *key, const JSONValue &value);

    void setInt32(const char *key, int32_t in) {
        JSONValue val;
        val.setInt32(in);
        setValue(key, val);
    }

    void setString(const char *key, std::string_view in) {
        JSONValue val;
        val.setString(in);
        setValue(key, val);
    }

    void setBoolean(const char *key, bool in) {
        JSONValue val;
        val.setBoolean(in);
        setValue(key, val);
    }

    void setObject(const char *key, const std::shared_ptr<JSONObject> &obj) {
        JSONValue val;
        val.setObject(obj);
        setValue(key, val);
    }

    void setArray(const char *key, const std::shared_ptr<JSONArray> &obj) {
        JSONValue val;
        val.setArray(obj);
        setValue(key, val);
    }

    void remove(const char *key);

    virtual ~JSONObject();

protected:
    virtual bool getValue(const char *key, JSONValue *value) const;
    virtual std::string internalToString(size_t depth, bool indentFirstLine) const;

private:
    std::map<std::string, JSONValue> mValues;
};

struct JSONArray : public JSONBase<size_t> {
    explicit JSONArray();

    JSONArray(const JSONArray &) = delete;
    JSONArray &operator=(const JSONArray &) = delete;

    virtual bool isObject() const;
    size_t size() const;
    void addValue(const JSONValue &value);

    void addInt32(int32_t in) {
        JSONValue val;
        val.setInt32(in);
        addValue(val);
    }

    void addString(std::string_view in) {
        JSONValue val;
        val.setString(in);
        addValue(val);
    }

    void addBoolean(bool in) {
        JSONValue val;
        val.setBoolean(in);
        addValue(val);
    }

    void addObject(const std::shared_ptr<JSONObject> &obj) {
        JSONValue val;
        val.setObject(obj);
        addValue(val);
    }

    void addArray(const std::shared_ptr<JSONArray> &obj) {
        JSONValue val;
        val.setArray(obj);
        addValue(val);
    }

    virtual ~JSONArray();

protected:
    virtual bool getValue(size_t key, JSONValue *value) const;
    virtual std::string internalToString(size_t depth, bool indentFirstLine) const;


private:
    std::vector<JSONValue> mValues;
};

}  // namespace android

#endif  // JSON_OBJECT_H_

#include "ExifMetadataBuilder.h"

#define LOG_NDEBUG 0
#define LOG_TAG "ExifMetadataBuilder"
#include <cutils/log.h>

#include <stdlib.h>
#include <cmath>

namespace android {
// All supported EXIF data types.
enum ExifDataType {
  ExifUInt8 = 1,
  ExifString = 2,
  ExifUInt16 = 3,
  ExifUInt32 = 4,
  ExifRational = 5,
  ExifUndefined = 7,
  ExifSInt16 = 8,
  ExifSInt32 = 9,
  ExifFloat = 11,
  ExifDouble = 12,
};

enum ExifTagId {
  kExifTagGpsLatitudeRef = 0x1,
  kExifTagGpsLatitude = 0x2,
  kExifTagGpsLongitudeRef = 0x3,
  kExifTagGpsLongitude = 0x4,
  kExifTagGpsAltitudeRef = 0x5,
  kExifTagGpsAltitude = 0x6,
  kExifTagGpsTimestamp = 0x7,
  kExifTagGpsProcessingMethod = 0x1b,
  kExifTagGpsDatestamp = 0x1d,
  kExifTagImageWidth = 0x100,
  kExifTagImageHeight = 0x101,
  kExifTagImageDateTime = 0x132,
  kExifTagJpegData = 0x201,
  kExifTagJpegLength = 0x202,
  kExifTagCameraSubIFD = 0x8769,
  kExifTagGpsSubIFD = 0x8825,
  kExifTagCameraFocalLength = 0x920a,
};

const char kExifCharArrayAscii[8] = "ASCII";
const char kExifCharArrayUnicode[8] = "UNICODE";

// Structure of an individual EXIF tag.
struct ExifTagInfo {
  uint16_t tag;    // Describes unique tag type.
  uint16_t type;   // Describes data type (see ExifDataType).
  uint32_t count;  // Number of data elements.
  uint32_t value;  // Value (for shorter items) or offset (for longer).
};

// Interface describing individual EXIF tag.
class ExifTag {
 public:
  virtual ~ExifTag() {}
  virtual size_t DataSize() { return 0; }
  virtual void AppendTag(ExifTagInfo* info, size_t data_offset) = 0;
  virtual void AppendData(uint8_t* target) {}
};

// EXIF structure.
// This structure describes all types of tags:
// - Main image,
// - Thumbnail image,
// - Sub-IFDs.
class ExifStructure {
 public:
  typedef std::vector<ExifTag*> TagMap;

  ExifStructure() {}

  ~ExifStructure() {
    for (TagMap::iterator it = mTags.begin(); it != mTags.end(); ++it) {
      delete *it;
    }
  }

  size_t TagSize() {
    // Target tag structure:
    // - uint16_t: mTags.size();
    // - ExifTagInfo[mTags.size()]
    // - uint32_t: next_structure_available ? self_offset + Size() : NULL
    return sizeof(uint16_t)                      // mTags.size()
           + mTags.size() * sizeof(ExifTagInfo)  // [tags]
           + sizeof(uint32_t);                   // next offset
  }

  size_t DataSize() {
    size_t data_size = 0;
    for (TagMap::iterator it = mTags.begin(); it != mTags.end(); ++it) {
      data_size += (*it)->DataSize();
    }
    return data_size;
  }

  size_t Size() { return TagSize() + DataSize(); }

  uint32_t Build(uint8_t* buffer, const uint32_t self_offset,
                 const bool next_structure_available) {
    // Write number of items.
    uint16_t num_elements = mTags.size();
    memcpy(buffer, &num_elements, sizeof(num_elements));
    buffer += sizeof(num_elements);

    // Offset describes where tag data will be placed.
    uint32_t offset = self_offset + TagSize();

    // Combine EXIF for main image.
    for (TagMap::iterator it = mTags.begin(); it != mTags.end(); ++it) {
      // Each tag is exactly 12 bytes long, but data length can be anything.
      // We supply the data offset to anyone who wants to use data.
      (*it)->AppendTag(reinterpret_cast<ExifTagInfo*>(buffer), offset);
      offset += (*it)->DataSize();
      buffer += sizeof(ExifTagInfo);
    }

    // Append information about the second tag offset.
    // |offset| holds exactly the right position:
    // self_offset + TagSize() + DataSize().
    uint32_t next_tag_offset = next_structure_available ? offset : 0;
    memcpy(buffer, &next_tag_offset, sizeof(next_tag_offset));
    buffer += sizeof(next_tag_offset);

    // Combine EXIF data for main image.
    for (TagMap::iterator it = mTags.begin(); it != mTags.end(); ++it) {
      (*it)->AppendData(buffer);
      buffer += (*it)->DataSize();
    }

    return offset;
  }

  void PushTag(ExifTag* tag) { mTags.push_back(tag); }

 private:
  TagMap mTags;
};

// EXIF tags.
namespace {
// Tag with 8-bit unsigned integer.
class ExifUInt8Tag : public ExifTag {
 public:
  ExifUInt8Tag(uint16_t tag, size_t value) : mTag(tag), mValue(value) {}

  void AppendTag(ExifTagInfo* info, size_t data_offset) {
    info->tag = mTag;
    info->type = ExifUInt8;
    info->count = 1;
    info->value = mValue << 24;
  }

 private:
  uint16_t mTag;
  uint32_t mValue;
};

// Tag with 32-bit unsigned integer.
class ExifUInt32Tag : public ExifTag {
 public:
  ExifUInt32Tag(uint16_t tag, size_t value) : mTag(tag), mValue(value) {}

  void AppendTag(ExifTagInfo* info, size_t data_offset) {
    info->tag = mTag;
    info->type = ExifUInt32;
    info->count = 1;
    info->value = mValue;
  }

 private:
  uint16_t mTag;
  uint32_t mValue;
};

// Char array tag.
class ExifCharArrayTag : public ExifTag {
 public:
  ExifCharArrayTag(uint16_t tag, const char (&type)[8], const std::string& str)
      : mTag(tag), mType(type), mString(str) {}

  void AppendTag(ExifTagInfo* info, size_t data_offset) {
    info->tag = mTag;
    info->type = ExifUndefined;
    info->count = DataSize();
    info->value = data_offset;
  }

  size_t DataSize() { return sizeof(mType) + mString.size(); }

  void AppendData(uint8_t* data) {
    memcpy(data, mType, sizeof(mType));
    data += sizeof(mType);
    memcpy(data, mString.data(), mString.size());
  }

 private:
  uint16_t mTag;
  const char (&mType)[8];
  std::string mString;
};

// Data tag; writes LONG (pointer) and appends data.
class ExifPointerTag : public ExifTag {
 public:
  ExifPointerTag(uint16_t tag, void* data, int size)
      : mTag(tag), mData(data), mSize(size) {}

  ~ExifPointerTag() { free(mData); }

  void AppendTag(ExifTagInfo* info, size_t data_offset) {
    info->tag = mTag;
    info->type = ExifUInt32;
    info->count = 1;
    info->value = data_offset;
  }

  size_t DataSize() { return mSize; }

  void AppendData(uint8_t* data) { memcpy(data, mData, mSize); }

 private:
  uint16_t mTag;
  void* mData;
  int mSize;
};

// String tag.
class ExifStringTag : public ExifTag {
 public:
  ExifStringTag(uint16_t tag, const std::string& str)
      : mTag(tag), mString(str) {}

  void AppendTag(ExifTagInfo* info, size_t data_offset) {
    info->tag = mTag;
    info->type = ExifString;
    info->count = DataSize();
    info->value = data_offset;
  }

  size_t DataSize() {
    // Include padding \0.
    return mString.size() + 1;
  }

  void AppendData(uint8_t* data) { memcpy(data, mString.data(), DataSize()); }

 private:
  uint16_t mTag;
  std::string mString;
};

// SubIFD: sub-tags.
class ExifSubIfdTag : public ExifTag {
 public:
  ExifSubIfdTag(uint16_t tag) : mTag(tag), mSubStructure(new ExifStructure) {}

  ~ExifSubIfdTag() { delete mSubStructure; }

  void AppendTag(ExifTagInfo* info, size_t data_offset) {
    info->tag = mTag;
    info->type = ExifUInt32;
    info->count = 1;
    info->value = data_offset;
    mDataOffset = data_offset;
  }

  size_t DataSize() { return mSubStructure->Size(); }

  void AppendData(uint8_t* data) {
    mSubStructure->Build(data, mDataOffset, false);
  }

  ExifStructure* GetSubStructure() { return mSubStructure; }

 private:
  uint16_t mTag;
  ExifStructure* mSubStructure;
  size_t mDataOffset;
};

// Unsigned rational tag.
class ExifURationalTag : public ExifTag {
 public:
  ExifURationalTag(uint16_t tag, double value) : mTag(tag), mCount(1) {
    DoubleToRational(value, &mRationals[0].mNumerator,
                     &mRationals[0].mDenominator);
  }

  ExifURationalTag(uint16_t tag, double value1, double value2, double value3)
      : mTag(tag), mCount(3) {
    DoubleToRational(value1, &mRationals[0].mNumerator,
                     &mRationals[0].mDenominator);
    DoubleToRational(value2, &mRationals[1].mNumerator,
                     &mRationals[1].mDenominator);
    DoubleToRational(value3, &mRationals[2].mNumerator,
                     &mRationals[2].mDenominator);
  }

  void DoubleToRational(double value, int32_t* numerator,
                        int32_t* denominator) {
    int sign = 1;
    if (value < 0) {
      sign = -sign;
      value = -value;
    }
    // Take shortcuts. Plenty.
    *numerator = value;
    *denominator = 1;

    // Set some (arbitrary) threshold beyond which we do not proceed.
    while (*numerator < (1 << 24)) {
      if (std::fabs((*numerator / *denominator) - value) <= 0.0001) break;
      *denominator *= 10;
      *numerator = value * (*denominator);
    }
    *numerator *= sign;
  }

  void AppendTag(ExifTagInfo* info, size_t data_offset) {
    info->tag = mTag;
    info->type = ExifRational;
    info->count = mCount;
    info->value = data_offset;
  }

  size_t DataSize() { return sizeof(mRationals[0]) * mCount; }

  void AppendData(uint8_t* data) { memcpy(data, &mRationals[0], DataSize()); }

 private:
  static const int kMaxSupportedRationals = 3;
  uint16_t mTag;
  uint16_t mCount;
  struct {
    int32_t mNumerator;
    int32_t mDenominator;
  } mRationals[kMaxSupportedRationals];
};

std::string ToAsciiDate(time_t time) {
  struct tm loc;
  char res[12];  // YYYY:MM:DD\0\0
  localtime_r(&time, &loc);
  strftime(res, sizeof(res), "%Y:%m:%d", &loc);
  return res;
}

std::string ToAsciiTime(time_t time) {
  struct tm loc;
  char res[10];  // HH:MM:SS\0\0
  localtime_r(&time, &loc);
  strftime(res, sizeof(res), "%H:%M:%S", &loc);
  return res;
}

}  // namespace

ExifMetadataBuilder::ExifMetadataBuilder()
    : mImageIfd(new ExifStructure), mThumbnailIfd(new ExifStructure) {
  // Mandatory tag: camera details.
  ExifSubIfdTag* sub_ifd = new ExifSubIfdTag(kExifTagCameraSubIFD);
  // Pass ownership to mImageIfd.
  mImageIfd->PushTag(sub_ifd);
  mCameraSubIfd = sub_ifd->GetSubStructure();

  // Optional (yet, required) tag: GPS data.
  sub_ifd = new ExifSubIfdTag(kExifTagGpsSubIFD);
  // Pass ownership to mImageIfd.
  mImageIfd->PushTag(sub_ifd);
  mGpsSubIfd = sub_ifd->GetSubStructure();
}

ExifMetadataBuilder::~ExifMetadataBuilder() {
  delete mImageIfd;
  delete mThumbnailIfd;
}

void ExifMetadataBuilder::SetWidth(int width) {
  mImageIfd->PushTag(new ExifUInt32Tag(kExifTagImageWidth, width));
}

void ExifMetadataBuilder::SetHeight(int height) {
  mImageIfd->PushTag(new ExifUInt32Tag(kExifTagImageHeight, height));
}

void ExifMetadataBuilder::SetThumbnailWidth(int width) {
  mThumbnailIfd->PushTag(new ExifUInt32Tag(kExifTagImageWidth, width));
}

void ExifMetadataBuilder::SetThumbnailHeight(int height) {
  mThumbnailIfd->PushTag(new ExifUInt32Tag(kExifTagImageHeight, height));
}

void ExifMetadataBuilder::SetThumbnail(void* thumbnail, int size) {
  mThumbnailIfd->PushTag(new ExifUInt32Tag(kExifTagJpegLength, size));
  mThumbnailIfd->PushTag(new ExifPointerTag(kExifTagJpegData, thumbnail, size));
}

void ExifMetadataBuilder::SetDateTime(time_t date_time) {
  std::string res = ToAsciiDate(date_time) + " " + ToAsciiTime(date_time);
  mImageIfd->PushTag(new ExifStringTag(kExifTagImageDateTime, res));
}

void ExifMetadataBuilder::SetGpsLatitude(double latitude) {
  if (latitude < 0) {
    mGpsSubIfd->PushTag(new ExifStringTag(kExifTagGpsLatitudeRef, "S"));
  } else {
    mGpsSubIfd->PushTag(new ExifStringTag(kExifTagGpsLatitudeRef, "N"));
  }
  int degrees = latitude;
  latitude = (latitude - degrees) * 60.;
  int minutes = latitude;
  latitude = (latitude - minutes) * 60.;
  double seconds = latitude;
  mGpsSubIfd->PushTag(
      new ExifURationalTag(kExifTagGpsLatitude, degrees, minutes, seconds));
}

void ExifMetadataBuilder::SetGpsLongitude(double longitude) {
  if (longitude < 0) {
    mGpsSubIfd->PushTag(new ExifStringTag(kExifTagGpsLongitudeRef, "W"));
  } else {
    mGpsSubIfd->PushTag(new ExifStringTag(kExifTagGpsLongitudeRef, "E"));
  }
  int32_t degrees = longitude;
  longitude = (longitude - degrees) * 60.;
  int32_t minutes = longitude;
  longitude = (longitude - minutes) * 60.;
  double seconds = longitude;
  mGpsSubIfd->PushTag(
      new ExifURationalTag(kExifTagGpsLongitude, degrees, minutes, seconds));
}

void ExifMetadataBuilder::SetGpsAltitude(double altitude) {
  mGpsSubIfd->PushTag(new ExifUInt8Tag(kExifTagGpsAltitudeRef, 0));
  mGpsSubIfd->PushTag(new ExifURationalTag(kExifTagGpsAltitude, altitude));
}

void ExifMetadataBuilder::SetGpsProcessingMethod(const std::string& method) {
  mGpsSubIfd->PushTag(new ExifCharArrayTag(kExifTagGpsProcessingMethod,
                                           kExifCharArrayAscii, method));
}

void ExifMetadataBuilder::SetGpsDateTime(time_t timestamp) {
  std::string date = ToAsciiDate(timestamp);
  int32_t seconds = (timestamp % 60);
  timestamp /= 60;
  int32_t minutes = (timestamp % 60);
  timestamp /= 60;
  mGpsSubIfd->PushTag(new ExifURationalTag(kExifTagGpsTimestamp, timestamp % 24,
                                           minutes, seconds));
  mGpsSubIfd->PushTag(new ExifStringTag(kExifTagGpsDatestamp, date));
}

void ExifMetadataBuilder::SetLensFocalLength(double length) {
  mCameraSubIfd->PushTag(
      new ExifURationalTag(kExifTagCameraFocalLength, length));
}

void ExifMetadataBuilder::Build() {
  const uint8_t exif_header[] = {
      'E', 'x', 'i', 'f', 0, 0,  // EXIF header.
  };

  const uint8_t tiff_header[] = {
      'I', 'I', 0x2a, 0x00,  // TIFF Little endian header.
  };

  // EXIF data should be exactly this much.
  size_t exif_size = sizeof(exif_header) + sizeof(tiff_header) +
                     sizeof(uint32_t) +  // Offset of the following descriptors.
                     mImageIfd->Size() + mThumbnailIfd->Size();

  const uint8_t marker[] = {
      0xff,
      0xd8,
      0xff,
      0xe1,  // EXIF marker.
      uint8_t((exif_size + 2) >>
              8),  // Data length (including the length field)
      uint8_t((exif_size + 2) & 0xff),
  };

  // Reserve data for our exif info.
  mData.Resize(sizeof(marker) + exif_size);
  uint8_t* b = (uint8_t*)(mData.data());

  // Initialize marker & headers.
  memcpy(b, &marker, sizeof(marker));
  b += sizeof(marker);
  memcpy(b, &exif_header, sizeof(exif_header));
  b += sizeof(exif_header);
  memcpy(b, &tiff_header, sizeof(tiff_header));
  uint32_t data_offset = sizeof(tiff_header);

  // Write offset of the first IFD item.
  uint32_t first_tag_offset = data_offset + sizeof(uint32_t);
  memcpy(&b[data_offset], &first_tag_offset, sizeof(first_tag_offset));
  data_offset += sizeof(first_tag_offset);

  // At this moment |b| points to EXIF structure.
  // TIFF header is part of the structure itself and explains endianness of the
  // embedded data.
  ALOGI("%s: Building Main image EXIF tags", __FUNCTION__);
  data_offset = mImageIfd->Build(&b[data_offset], data_offset, true);
  ALOGI("%s: Building Thumbnail image EXIF tags", __FUNCTION__);
  data_offset = mThumbnailIfd->Build(&b[data_offset], data_offset, false);
  ALOGI("%s: EXIF metadata constructed (%d bytes).", __FUNCTION__, data_offset);
}

}  // namespace android

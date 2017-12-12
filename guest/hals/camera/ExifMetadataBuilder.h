#ifndef EXIFMETADATAWRITER_H_
#define EXIFMETADATAWRITER_H_

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <string>
#include <vector>

#include <AutoResources.h>

namespace android {
class ExifStructure;

// Simplistic EXIF metadata builder.
// www.exif.org/Exif2-2.PDF
//
// TODO(ender): Revisit using libexif after we drop support for JB.
class ExifMetadataBuilder {
 public:
  ExifMetadataBuilder();
  ~ExifMetadataBuilder();

  void SetWidth(int width);
  void SetHeight(int height);
  void SetThumbnailWidth(int width);
  void SetThumbnailHeight(int height);
  void SetThumbnail(void* thumbnail, int size);
  void SetGpsLatitude(double degrees);
  void SetGpsLongitude(double degrees);
  void SetGpsAltitude(double altitude);
  void SetGpsDateTime(time_t timestamp);
  void SetGpsProcessingMethod(const std::string& method);
  void SetDateTime(time_t t);
  void SetLensFocalLength(double length);
  void Build();

  const AutoFreeBuffer& Buffer() {
    return mData;
  }

 private:
  ExifStructure* mImageIfd;
  ExifStructure* mThumbnailIfd;
  ExifStructure* mCameraSubIfd;
  ExifStructure* mGpsSubIfd;

  AutoFreeBuffer mData;

  ExifMetadataBuilder(const ExifMetadataBuilder&);
  ExifMetadataBuilder& operator= (const ExifMetadataBuilder&);
};

}  // namespace android

#endif  // EXIFMETADATAWRITER_H_

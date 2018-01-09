#ifndef IMAGEMETADATA_H_
#define IMAGEMETADATA_H_

// min and max macros make the C++11 support break on nyc-mr1
#undef min
#undef max
#include <stdint.h>
#include <string>

extern "C" {
/* Describes various attributes of the picture. */
struct ImageMetadata {
  int mWidth;
  int mHeight;
  int mThumbnailWidth;
  int mThumbnailHeight;
  double mLensFocalLength;
  double mGpsLatitude;
  double mGpsLongitude;
  double mGpsAltitude;
  time_t mGpsTimestamp;
  std::string mGpsProcessingMethod;
};
}

#endif  // IMAGEMETADATA_H_

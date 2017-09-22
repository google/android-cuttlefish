#ifndef IMAGEMETADATA_H_
#define IMAGEMETADATA_H_

#include <string>
#include <stdint.h>

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

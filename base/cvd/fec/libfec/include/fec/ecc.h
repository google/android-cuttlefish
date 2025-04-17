/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef ___FEC_ECC_H___
#define ___FEC_ECC_H___

#include <fec/io.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ecc parameters */
#define FEC_RSM 255

/* parameters to init_rs_char */
#define FEC_PARAMS(roots) \
    8,          /* symbol size in bits */ \
    0x11d,      /* field generator polynomial coefficients */ \
    0,          /* first root of the generator */ \
    1,          /* primitive element to generate polynomial roots */ \
    (roots),    /* polynomial degree (number of roots) */ \
    0           /* padding bytes at the front of shortened block */

/* computes ceil(x / y) */
inline uint64_t fec_div_round_up(uint64_t x, uint64_t y)
{
    return (x / y) + (x % y > 0 ? 1 : 0);
}

/* rounds up x to the nearest multiple of y */
inline uint64_t fec_round_up(uint64_t x, uint64_t y)
{
    return fec_div_round_up(x, y) * y;
}

/* returns a physical offset for a byte in an RS block */
inline uint64_t fec_ecc_interleave(uint64_t offset, int rsn, uint64_t rounds)
{
    return (offset / rsn) + (offset % rsn) * rounds * FEC_BLOCKSIZE;
}

/* returns the size of ecc data given a file size and the number of roots */
inline uint64_t fec_ecc_get_size(uint64_t file_size, int roots)
{
    return fec_div_round_up(fec_div_round_up(file_size, FEC_BLOCKSIZE),
                FEC_RSM - roots)
                    * roots * FEC_BLOCKSIZE
                + FEC_BLOCKSIZE;
}


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ___FEC_ECC_H___ */

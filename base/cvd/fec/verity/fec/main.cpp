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

extern "C" {
    #include <fec.h>
}

#undef NDEBUG

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <android-base/file.h>
#include "image.h"

enum {
    MODE_ENCODE,
    MODE_DECODE,
    MODE_PRINTSIZE,
    MODE_GETECCSTART,
    MODE_GETVERITYSTART
};

static void encode_rs(struct image_proc_ctx *ctx)
{
    struct image *fcx = ctx->ctx;
    int j;
    uint8_t data[fcx->rs_n];
    uint64_t i;

    for (i = ctx->start; i < ctx->end; i += fcx->rs_n) {
        for (j = 0; j < fcx->rs_n; ++j) {
            data[j] = image_get_interleaved_byte(i + j, fcx);
        }

        encode_rs_char(ctx->rs, data, &fcx->fec[ctx->fec_pos]);
        ctx->fec_pos += fcx->roots;
    }
}

static void decode_rs(struct image_proc_ctx *ctx)
{
    struct image *fcx = ctx->ctx;
    int j, rv;
    uint8_t data[fcx->rs_n + fcx->roots];
    uint64_t i;

    assert(sizeof(data) == FEC_RSM);

    for (i = ctx->start; i < ctx->end; i += fcx->rs_n) {
        for (j = 0; j < fcx->rs_n; ++j) {
            data[j] = image_get_interleaved_byte(i + j, fcx);
        }

        memcpy(&data[fcx->rs_n], &fcx->fec[ctx->fec_pos], fcx->roots);
        rv = decode_rs_char(ctx->rs, data, nullptr, 0);

        if (rv < 0) {
            FATAL("failed to recover [%" PRIu64 ", %" PRIu64 ")\n",
                i, i + fcx->rs_n);
        } else if (rv > 0) {
            /* copy corrected data to output */
            for (j = 0; j < fcx->rs_n; ++j) {
                image_set_interleaved_byte(i + j, fcx, data[j]);
            }

            ctx->rv += rv;
        }

        ctx->fec_pos += fcx->roots;
    }
}

static int usage()
{
    printf("fec: a tool for encoding and decoding files using RS(255, N).\n"
           "\n"
           "usage: fec <mode> [ <options> ] [ <data> <fec> [ <output> ] ]\n"
           "mode:\n"
           "  -e  --encode                      encode (default)\n"
           "  -d  --decode                      decode\n"
           "  -s, --print-fec-size=<data size>  print FEC size\n"
           "  -E, --get-ecc-start=data          print ECC offset in data\n"
           "  -V, --get-verity-start=data       print verity offset\n"
           "options:\n"
           "  -h                                show this help\n"
           "  -v                                enable verbose logging\n"
           "  -r, --roots=<bytes>               number of parity bytes\n"
           "  -j, --threads=<threads>           number of threads to use\n"
           "  -S                                treat data as a sparse file\n"
           "encoding options:\n"
           "  -p, --padding=<bytes>             add padding after ECC data\n"
           "decoding options:\n"
           "  -i, --inplace                     correct <data> in place\n"
        );

    return 1;
}

static uint64_t parse_arg(const char *arg, const char *name, uint64_t maxval)
{
    char* endptr;
    errno = 0;

    unsigned long long int value = strtoull(arg, &endptr, 0);

    if (arg[0] == '\0' || *endptr != '\0' ||
            (errno == ERANGE && value == ULLONG_MAX)) {
        FATAL("invalid value of %s\n", name);
    }
    if (value > maxval) {
        FATAL("value of roots too large (max. %" PRIu64 ")\n", maxval);
    }

    return (uint64_t)value;
}

static int print_size(image& ctx)
{
    /* output size including header */
    printf("%" PRIu64 "\n", fec_ecc_get_size(ctx.inp_size, ctx.roots));
    return 0;
}

static int get_start(int mode, const std::string& filename)
{
    fec::io fh(filename, O_RDONLY, FEC_VERITY_DISABLE);

    if (!fh) {
        FATAL("failed to open input\n");
    }

    if (mode == MODE_GETECCSTART) {
        fec_ecc_metadata data;

        if (!fh.get_ecc_metadata(data)) {
            FATAL("no ecc data\n");
        }

        printf("%" PRIu64 "\n", data.start);
    } else {
        fec_verity_metadata data;

        if (!fh.get_verity_metadata(data)) {
            FATAL("no verity data\n");
        }

        printf("%" PRIu64 "\n", data.data_size);
    }

    return 0;
}

static int encode(image& ctx, const std::vector<std::string>& inp_filenames,
        const std::string& fec_filename)
{
    if (ctx.inplace) {
        FATAL("invalid parameters: inplace can only used when decoding\n");
    }

    if (!image_load(inp_filenames, &ctx)) {
        FATAL("failed to read input\n");
    }

    if (!image_ecc_new(fec_filename, &ctx)) {
        FATAL("failed to allocate ecc\n");
    }

    INFO("encoding RS(255, %d) to '%s' for input files:\n", ctx.rs_n,
        fec_filename.c_str());

    size_t n = 1;

    for (const auto& fn : inp_filenames) {
        INFO("\t%zu: '%s'\n", n++, fn.c_str());
    }

    if (ctx.verbose) {
        INFO("\traw fec size: %u\n", ctx.fec_size);
        INFO("\tblocks: %" PRIu64 "\n", ctx.blocks);
        INFO("\trounds: %" PRIu64 "\n", ctx.rounds);
    }

    if (!image_process(encode_rs, &ctx)) {
        FATAL("failed to process input\n");
    }

    if (!image_ecc_save(&ctx)) {
        FATAL("failed to write output\n");
    }

    image_free(&ctx);
    return 0;
}

static int decode(image& ctx, const std::vector<std::string>& inp_filenames,
        const std::string& fec_filename, std::string& out_filename)
{
    const std::string& inp_filename = inp_filenames.front();

    if (ctx.inplace && ctx.sparse) {
        FATAL("invalid parameters: inplace cannot be used with sparse "
            "files\n");
    }

    if (ctx.padding) {
        FATAL("invalid parameters: padding is only relevant when encoding\n");
    }

    if (!image_ecc_load(fec_filename, &ctx) ||
            !image_load(inp_filenames, &ctx)) {
        FATAL("failed to read input\n");
    }

    if (ctx.inplace) {
        INFO("correcting '%s' using RS(255, %d) from '%s'\n",
            inp_filename.c_str(), ctx.rs_n, fec_filename.c_str());

        out_filename = inp_filename;
    } else {
        INFO("decoding '%s' to '%s' using RS(255, %d) from '%s'\n",
            inp_filename.c_str(),
            out_filename.empty() ? out_filename.c_str() : "<none>", ctx.rs_n,
            fec_filename.c_str());
    }

    if (ctx.verbose) {
        INFO("\traw fec size: %u\n", ctx.fec_size);
        INFO("\tblocks: %" PRIu64 "\n", ctx.blocks);
        INFO("\trounds: %" PRIu64 "\n", ctx.rounds);
    }

    if (!image_process(decode_rs, &ctx)) {
        FATAL("failed to process input\n");
    }

    if (ctx.rv) {
        INFO("corrected %" PRIu64 " errors\n", ctx.rv);
    } else {
        INFO("no errors found\n");
    }

    if (!out_filename.empty() && !image_save(out_filename, &ctx)) {
        FATAL("failed to write output\n");
    }

    image_free(&ctx);
    return 0;
}

int main(int argc, char **argv)
{
    std::string fec_filename;
    std::string out_filename;
    std::vector<std::string> inp_filenames;
    int mode = MODE_ENCODE;
    image ctx;

    image_init(&ctx);
    ctx.roots = FEC_DEFAULT_ROOTS;

    while (1) {
        const static struct option long_options[] = {
            {"help", no_argument, nullptr, 'h'},
            {"encode", no_argument, nullptr, 'e'},
            {"decode", no_argument, nullptr, 'd'},
            {"sparse", no_argument, nullptr, 'S'},
            {"roots", required_argument, nullptr, 'r'},
            {"inplace", no_argument, nullptr, 'i'},
            {"threads", required_argument, nullptr, 'j'},
            {"print-fec-size", required_argument, nullptr, 's'},
            {"get-ecc-start", required_argument, nullptr, 'E'},
            {"get-verity-start", required_argument, nullptr, 'V'},
            {"padding", required_argument, nullptr, 'p'},
            {"verbose", no_argument, nullptr, 'v'},
            {nullptr, 0, nullptr, 0}
        };
        int c = getopt_long(argc, argv, "hedSr:ij:s:E:V:p:v", long_options, nullptr);
        if (c < 0) {
            break;
        }

        switch (c) {
        case 'h':
            return usage();
        case 'S':
            ctx.sparse = true;
            break;
        case 'e':
            if (mode != MODE_ENCODE) {
                return usage();
            }
            break;
        case 'd':
            if (mode != MODE_ENCODE) {
                return usage();
            }
            mode = MODE_DECODE;
            break;
        case 'r':
            ctx.roots = (int)parse_arg(optarg, "roots", FEC_RSM);
            break;
        case 'i':
            ctx.inplace = true;
            break;
        case 'j':
            ctx.threads = (int)parse_arg(optarg, "threads", IMAGE_MAX_THREADS);
            break;
        case 's':
            if (mode != MODE_ENCODE) {
                return usage();
            }
            mode = MODE_PRINTSIZE;
            ctx.inp_size = parse_arg(optarg, "print-fec-size", UINT64_MAX);
            break;
        case 'E':
            if (mode != MODE_ENCODE) {
                return usage();
            }
            mode = MODE_GETECCSTART;
            inp_filenames.push_back(optarg);
            break;
        case 'V':
            if (mode != MODE_ENCODE) {
                return usage();
            }
            mode = MODE_GETVERITYSTART;
            inp_filenames.push_back(optarg);
            break;
        case 'p':
            ctx.padding = (uint32_t)parse_arg(optarg, "padding", UINT32_MAX);
            if (ctx.padding % FEC_BLOCKSIZE) {
                FATAL("padding must be multiple of %u\n", FEC_BLOCKSIZE);
            }
            break;
        case 'v':
            ctx.verbose = true;
            break;
        case '?':
            return usage();
        default:
            abort();
        }
    }

    argc -= optind;
    argv += optind;

    assert(ctx.roots > 0 && ctx.roots < FEC_RSM);

    /* check for input / output parameters */
    if (mode == MODE_ENCODE) {
        /* allow multiple input files */
        for (int i = 0; i < (argc - 1); ++i) {
            inp_filenames.push_back(argv[i]);
        }

        if (inp_filenames.empty()) {
            return usage();
        }

        /* the last one is the output file */
        fec_filename = argv[argc - 1];
    } else if (mode == MODE_DECODE) {
        if (argc < 2 || argc > 3) {
            return usage();
        } else if (argc == 3) {
            if (ctx.inplace) {
                return usage();
            }
            out_filename = argv[2];
        }

        inp_filenames.push_back(argv[0]);
        fec_filename = argv[1];
    }

    switch (mode) {
    case MODE_PRINTSIZE:
        return print_size(ctx);
    case MODE_GETECCSTART:
    case MODE_GETVERITYSTART:
        return get_start(mode, inp_filenames.front());
    case MODE_ENCODE:
        return encode(ctx, inp_filenames, fec_filename);
    case MODE_DECODE:
        return decode(ctx, inp_filenames, fec_filename, out_filename);
    default:
        abort();
    }

    return 1;
}

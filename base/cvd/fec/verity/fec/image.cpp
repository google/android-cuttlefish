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

#undef NDEBUG
#define _LARGEFILE64_SOURCE

extern "C" {
    #include <fec.h>
}

#include <assert.h>
#include <android-base/file.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sparse/sparse.h>
#include "image.h"

#if defined(__linux__)
    #include <linux/fs.h>
#elif defined(__APPLE__)
    #include <sys/disk.h>
    #define BLKGETSIZE64 DKIOCGETBLOCKCOUNT
    #define O_LARGEFILE 0
#endif

void image_init(image *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

void image_free(image *ctx)
{
    assert(ctx->input == ctx->output);

    if (ctx->input) {
        delete[] ctx->input;
    }

    if (ctx->fec) {
        delete[] ctx->fec;
    }

    image_init(ctx);
}

static void calculate_rounds(uint64_t size, image *ctx)
{
    if (!size) {
        FATAL("empty file?\n");
    } else if (size % FEC_BLOCKSIZE) {
        FATAL("file size %" PRIu64 " is not a multiple of %u bytes\n",
            size, FEC_BLOCKSIZE);
    }

    ctx->inp_size = size;
    ctx->blocks = fec_div_round_up(ctx->inp_size, FEC_BLOCKSIZE);
    ctx->rounds = fec_div_round_up(ctx->blocks, ctx->rs_n);
}

static int process_chunk(void *priv, const void *data, size_t len)
{
    image *ctx = (image *)priv;

    if (data) {
        memcpy(&ctx->input[ctx->pos], data, len);
    }

    ctx->pos += len;
    return 0;
}

static void file_image_load(const std::vector<int>& fds, image *ctx)
{
    uint64_t size = 0;
    std::vector<struct sparse_file *> files;

    for (auto fd : fds) {
        uint64_t len = 0;
        struct sparse_file *file;

        if (ctx->sparse) {
            file = sparse_file_import(fd, false, false);
        } else {
            file = sparse_file_import_auto(fd, false, ctx->verbose);
        }

        if (!file) {
            FATAL("failed to read file %s\n", ctx->fec_filename);
        }

        len = sparse_file_len(file, false, false);
        files.push_back(file);

        size += len;
    }

    calculate_rounds(size, ctx);

    if (ctx->verbose) {
        INFO("allocating %" PRIu64 " bytes of memory\n", ctx->inp_size);
    }

    ctx->input = new uint8_t[ctx->inp_size];

    if (!ctx->input) {
        FATAL("failed to allocate memory\n");
    }

    memset(ctx->input, 0, ctx->inp_size);
    ctx->output = ctx->input;
    ctx->pos = 0;

    for (auto file : files) {
        sparse_file_callback(file, false, false, process_chunk, ctx);
        sparse_file_destroy(file);
    }

    assert(ctx->pos % FEC_BLOCKSIZE == 0);

    for (auto fd : fds) {
        close(fd);
    }
}

bool image_load(const std::vector<std::string>& filenames, image *ctx)
{
    assert(ctx->roots > 0 && ctx->roots < FEC_RSM);
    ctx->rs_n = FEC_RSM - ctx->roots;

    int flags = O_RDONLY;

    if (ctx->inplace) {
        flags = O_RDWR;
    }

    std::vector<int> fds;

    for (const auto& fn : filenames) {
        int fd = TEMP_FAILURE_RETRY(open(fn.c_str(), flags | O_LARGEFILE));

        if (fd < 0) {
            FATAL("failed to open file '%s': %s\n", fn.c_str(), strerror(errno));
        }

        fds.push_back(fd);
    }

    file_image_load(fds, ctx);

    return true;
}

bool image_save(const std::string& filename, image *ctx)
{
    /* TODO: support saving as a sparse file */
    int fd = TEMP_FAILURE_RETRY(open(filename.c_str(),
                O_WRONLY | O_CREAT | O_TRUNC, 0666));

    if (fd < 0) {
        FATAL("failed to open file '%s: %s'\n", filename.c_str(),
            strerror(errno));
    }

    if (!android::base::WriteFully(fd, ctx->output, ctx->inp_size)) {
        FATAL("failed to write to output: %s\n", strerror(errno));
    }

    close(fd);
    return true;
}

bool image_ecc_new(const std::string& filename, image *ctx)
{
    assert(ctx->rounds > 0); /* image_load should be called first */

    ctx->fec_filename = filename.c_str();
    ctx->fec_size = ctx->rounds * ctx->roots * FEC_BLOCKSIZE;

    if (ctx->verbose) {
        INFO("allocating %u bytes of memory\n", ctx->fec_size);
    }

    ctx->fec = new uint8_t[ctx->fec_size];

    if (!ctx->fec) {
        FATAL("failed to allocate %u bytes\n", ctx->fec_size);
    }

    return true;
}

bool image_ecc_load(const std::string& filename, image *ctx)
{
    int fd = TEMP_FAILURE_RETRY(open(filename.c_str(), O_RDONLY));

    if (fd < 0) {
        FATAL("failed to open file '%s': %s\n", filename.c_str(),
            strerror(errno));
    }

    if (lseek64(fd, -FEC_BLOCKSIZE, SEEK_END) < 0) {
        FATAL("failed to seek to header in '%s': %s\n", filename.c_str(),
            strerror(errno));
    }

    assert(sizeof(fec_header) <= FEC_BLOCKSIZE);

    uint8_t header[FEC_BLOCKSIZE];
    fec_header *p = (fec_header *)header;

    if (!android::base::ReadFully(fd, header, sizeof(header))) {
        FATAL("failed to read %zd bytes from '%s': %s\n", sizeof(header),
            filename.c_str(), strerror(errno));
    }

    if (p->magic != FEC_MAGIC) {
        FATAL("invalid magic in '%s': %08x\n", filename.c_str(), p->magic);
    }

    if (p->version != FEC_VERSION) {
        FATAL("unsupported version in '%s': %u\n", filename.c_str(),
            p->version);
    }

    if (p->size != sizeof(fec_header)) {
        FATAL("unexpected header size in '%s': %u\n", filename.c_str(),
            p->size);
    }

    if (p->roots == 0 || p->roots >= FEC_RSM) {
        FATAL("invalid roots in '%s': %u\n", filename.c_str(), p->roots);
    }

    if (p->fec_size % p->roots || p->fec_size % FEC_BLOCKSIZE) {
        FATAL("invalid length in '%s': %u\n", filename.c_str(), p->fec_size);
    }

    ctx->roots = (int)p->roots;
    ctx->rs_n = FEC_RSM - ctx->roots;

    calculate_rounds(p->inp_size, ctx);

    if (!image_ecc_new(filename, ctx)) {
        FATAL("failed to allocate ecc\n");
    }

    if (p->fec_size != ctx->fec_size) {
        FATAL("inconsistent header in '%s'\n", filename.c_str());
    }

    if (lseek64(fd, 0, SEEK_SET) < 0) {
        FATAL("failed to rewind '%s': %s", filename.c_str(), strerror(errno));
    }

    if (!android::base::ReadFully(fd, ctx->fec, ctx->fec_size)) {
        FATAL("failed to read %u bytes from '%s': %s\n", ctx->fec_size,
            filename.c_str(), strerror(errno));
    }

    close(fd);

    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(ctx->fec, ctx->fec_size, hash);

    if (memcmp(hash, p->hash, SHA256_DIGEST_LENGTH) != 0) {
        FATAL("invalid ecc data\n");
    }

    return true;
}

bool image_ecc_save(image *ctx)
{
    assert(2 * sizeof(fec_header) <= FEC_BLOCKSIZE);

    uint8_t header[FEC_BLOCKSIZE] = {0};

    fec_header *f = (fec_header *)header;

    f->magic = FEC_MAGIC;
    f->version = FEC_VERSION;
    f->size = sizeof(fec_header);
    f->roots = ctx->roots;
    f->fec_size = ctx->fec_size;
    f->inp_size = ctx->inp_size;

    SHA256(ctx->fec, ctx->fec_size, f->hash);

    /* store a copy of the fec_header at the end of the header block */
    memcpy(&header[sizeof(header) - sizeof(fec_header)], header,
        sizeof(fec_header));

    assert(ctx->fec_filename);

    int fd = TEMP_FAILURE_RETRY(open(ctx->fec_filename,
                O_WRONLY | O_CREAT | O_TRUNC, 0666));

    if (fd < 0) {
        FATAL("failed to open file '%s': %s\n", ctx->fec_filename,
            strerror(errno));
    }

    if (!android::base::WriteFully(fd, ctx->fec, ctx->fec_size)) {
        FATAL("failed to write to output: %s\n", strerror(errno));
    }

    if (ctx->padding > 0) {
        uint8_t padding[FEC_BLOCKSIZE] = {0};

        for (uint32_t i = 0; i < ctx->padding; i += FEC_BLOCKSIZE) {
            if (!android::base::WriteFully(fd, padding, FEC_BLOCKSIZE)) {
                FATAL("failed to write padding: %s\n", strerror(errno));
            }
        }
    }

    if (!android::base::WriteFully(fd, header, sizeof(header))) {
        FATAL("failed to write to header: %s\n", strerror(errno));
    }

    close(fd);

    return true;
}

static void * process(void *cookie)
{
    image_proc_ctx *ctx = (image_proc_ctx *)cookie;
    ctx->func(ctx);
    return nullptr;
}

bool image_process(image_proc_func func, image *ctx)
{
    int threads = ctx->threads;

    if (threads < IMAGE_MIN_THREADS) {
        threads = sysconf(_SC_NPROCESSORS_ONLN);

        if (threads < IMAGE_MIN_THREADS) {
            threads = IMAGE_MIN_THREADS;
        }
    }

    assert(ctx->rounds > 0);

    if ((uint64_t)threads > ctx->rounds) {
        threads = (int)ctx->rounds;
    }
    if (threads > IMAGE_MAX_THREADS) {
        threads = IMAGE_MAX_THREADS;
    }

    if (ctx->verbose) {
        INFO("starting %d threads to compute RS(255, %d)\n", threads,
            ctx->rs_n);
    }

    pthread_t pthreads[threads];
    image_proc_ctx args[threads];

    uint64_t current = 0;
    uint64_t end = ctx->rounds * ctx->rs_n * FEC_BLOCKSIZE;
    uint64_t rs_blocks_per_thread =
        fec_div_round_up(ctx->rounds * FEC_BLOCKSIZE, threads);

    if (ctx->verbose) {
        INFO("computing %" PRIu64 " codes per thread\n", rs_blocks_per_thread);
    }

    for (int i = 0; i < threads; ++i) {
        args[i].func = func;
        args[i].id = i;
        args[i].ctx = ctx;
        args[i].rv = 0;
        args[i].fec_pos = current * ctx->roots;
        args[i].start = current * ctx->rs_n;
        args[i].end = (current + rs_blocks_per_thread) * ctx->rs_n;

        args[i].rs = init_rs_char(FEC_PARAMS(ctx->roots));

        if (!args[i].rs) {
            FATAL("failed to initialize encoder for thread %d\n", i);
        }

        if (args[i].end > end) {
            args[i].end = end;
        } else if (i == threads && args[i].end + rs_blocks_per_thread *
                                        ctx->rs_n > end) {
            args[i].end = end;
        }

        if (ctx->verbose) {
            INFO("thread %d: [%" PRIu64 ", %" PRIu64 ")\n",
                i, args[i].start, args[i].end);
        }

        assert(args[i].start < args[i].end);
        assert((args[i].end - args[i].start) % ctx->rs_n == 0);

        if (pthread_create(&pthreads[i], nullptr, process, &args[i]) != 0) {
            FATAL("failed to create thread %d\n", i);
        }

        current += rs_blocks_per_thread;
    }

    ctx->rv = 0;

    for (int i = 0; i < threads; ++i) {
        if (pthread_join(pthreads[i], nullptr) != 0) {
            FATAL("failed to join thread %d: %s\n", i, strerror(errno));
        }

        ctx->rv += args[i].rv;

        if (args[i].rs) {
            free_rs_char(args[i].rs);
            args[i].rs = nullptr;
        }
    }

    return true;
}

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

#include <future>
#include "fec_private.h"

struct process_info {
    int id;
    fec_handle* f;
    uint8_t* buf;
    size_t count;
    uint64_t offset;
    read_func func;
    ssize_t rc;
    size_t errors;
};

/* thread function  */
static process_info* __process(process_info* p) {
    debug("thread %d: [%" PRIu64 ", %" PRIu64 ")", p->id, p->offset, p->offset + p->count);

    p->rc = p->func(p->f, p->buf, p->count, p->offset, &p->errors);
    return p;
}

/* launches a maximum number of threads to process a read */
ssize_t process(fec_handle* f, uint8_t* buf, size_t count, uint64_t offset, read_func func) {
    check(f);
    check(buf);
    check(func);

    if (count == 0) {
        return 0;
    }

    int threads = sysconf(_SC_NPROCESSORS_ONLN);

    if (threads < WORK_MIN_THREADS) {
        threads = WORK_MIN_THREADS;
    } else if (threads > WORK_MAX_THREADS) {
        threads = WORK_MAX_THREADS;
    }

    uint64_t start = (offset / FEC_BLOCKSIZE) * FEC_BLOCKSIZE;
    size_t blocks = fec_div_round_up(offset + count - start, FEC_BLOCKSIZE);

    /* start at most one thread per block we're accessing */
    if ((size_t)threads > blocks) {
        threads = (int)blocks;
    }

    size_t count_per_thread = fec_div_round_up(blocks, threads) * FEC_BLOCKSIZE;
    size_t left = count;
    uint64_t pos = offset;
    uint64_t end = start + count_per_thread;

    debug("max %d threads, %zu bytes per thread (total %zu spanning %zu blocks)", threads,
          count_per_thread, count, blocks);

    std::vector<std::future<process_info*>> handles;
    process_info info[threads];
    ssize_t rc = 0;

    /* start threads to process queue */
    for (int i = 0; i < threads && left > 0; ++i) {
        info[i].id = i;
        info[i].f = f;
        info[i].buf = &buf[pos - offset];
        info[i].count = (size_t)(end - pos);
        info[i].offset = pos;
        info[i].func = func;
        info[i].rc = -1;
        info[i].errors = 0;

        if (info[i].count > left) {
            info[i].count = left;
        }

        handles.push_back(std::async(std::launch::async, __process, &info[i]));

        pos = end;
        end += count_per_thread;
        left -= info[i].count;
    }

    ssize_t nread = 0;

    /* wait for all threads to complete */
    for (auto&& future : handles) {
        process_info* p = future.get();
        if (!p || p->rc == -1) {
            rc = -1;
        } else {
            nread += p->rc;
            f->errors += p->errors;
        }
    }

    if (left > 0 || rc == -1) {
        errno = EIO;
        return -1;
    }

    return nread;
}

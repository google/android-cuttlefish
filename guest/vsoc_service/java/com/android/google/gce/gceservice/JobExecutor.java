/*
 * Copyright (C) 2017 The Android Open Source Project
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
package com.android.google.gce.gceservice;

import android.util.Log;
import com.android.google.gce.gceservice.JobBase;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.TimeUnit;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Set;
import java.util.HashSet;

public class JobExecutor {
    private static final int THREAD_POOL_SIZE = 8;
    private static final String LOG_TAG = "GceExecutor";
    private static final int ITERATION_PERIOD_MS = 10 * 1000;
    private final ScheduledExecutorService mExecutor;
    private final Set<String> mStartedJobs;

    public JobExecutor() {
        mExecutor = Executors.newScheduledThreadPool(THREAD_POOL_SIZE);
        mStartedJobs = new HashSet<String>();
    }

    /** Schedule job for (periodic) execution.
     *
     * First execution is performed at the earliest possibility.
     * Execution stops when a call to execute() returns a nonpositive number.
     *
     * Note: iteration time is |delaySeconds| + total time needed to execute job.
     */
    public void schedule(final JobBase job, final GceFuture<?>... futures) {
        mExecutor.schedule(new Runnable() {
            private boolean mDependenciesReady = false;
            private ArrayList<GceFuture<?>> mFutures =
                    new ArrayList<GceFuture<?>>(Arrays.asList(futures));

            public void run() {
                if (!mDependenciesReady) {
                    while (!mFutures.isEmpty()) {
                        ArrayList<GceFuture<?>> stragglers = new ArrayList<GceFuture<?>>();
                        long endTime = System.currentTimeMillis() + ITERATION_PERIOD_MS;
                        for (GceFuture<?> future : mFutures) {
                            try {
                                // Wait for at most ITERATION_PERIOD_MS. Check all futures,
                                // collect only those that still failed to complete.
                                future.get(
                                        Math.max(0, endTime - System.currentTimeMillis()),
                                        TimeUnit.MILLISECONDS);
                            } catch (TimeoutException e) {
                                stragglers.add(future);
                            } catch (InterruptedException e) {
                                // In theory we should re-try this one.
                                // In practice this hardly ever happens, so let's just
                                // give it another go in the second loop.
                                stragglers.add(future);
                            } catch (Exception e) {
                                Log.e(LOG_TAG, String.format(
                                        "Could not start job %s.", job.getClass().getName()),
                                        e);
                                job.onDependencyFailed(e);
                                return;
                            }
                        }

                        mFutures = stragglers;
                        if (!mFutures.isEmpty()) {
                            job.onDependencyStraggling(mFutures);
                        }
                    }
                    mDependenciesReady = true;
                }

                try {
                    String jobName = job.getClass().getName();
                    if (!mStartedJobs.contains(jobName)) {
                        mStartedJobs.add(jobName);
                    }
                    int delaySeconds = job.execute();
                    if (delaySeconds > 0) {
                        mExecutor.schedule(this, delaySeconds, TimeUnit.SECONDS);
                    } else {
                        mStartedJobs.remove(jobName);
                    }
                } catch (Exception e) {
                    Log.e(LOG_TAG, String.format("Job %s threw an exception and will not be re-scheduled.",
                                job.getClass().getName()), e);
                }
            }
        }, 0, TimeUnit.SECONDS);
    }
}

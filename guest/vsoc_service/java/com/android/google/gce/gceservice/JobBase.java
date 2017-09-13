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
import java.util.ArrayList;

public abstract class JobBase {
    private final String mTag;

    JobBase(String tag) {
        mTag = tag;
    }


    /** Invoked, when job could not be started due to dependency failure.
     *
     * Supplied exception describes reason of failure.
     */
    public abstract void onDependencyFailed(Exception exception);


    /** Invoked, when one or more tasks have taken substantial time to complete.
     *
     * This is not a direct indication of an error, but merely an information
     * about what stops current job from being executed.
     *
     * @param stragglingDependencies list of dependencies that have not completed
     *        in last 10 seconds.
     */
    public void onDependencyStraggling(ArrayList<GceFuture<?>> stragglingDependencies) {
        Log.i(mTag, "Waiting for: " + GceFuture.toString(stragglingDependencies));
    }


    /** Invoked, when all dependencies are ready and job is clear to commence.
     *
     * Returns number of second before re-execution, or 0, if job is done.
     */
    public abstract int execute();
}

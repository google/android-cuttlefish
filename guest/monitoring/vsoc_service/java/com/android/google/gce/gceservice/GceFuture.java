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
import java.util.concurrent.CancellationException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.TimeUnit;

public class GceFuture<T> implements Future<T> {
    private static final String LOG_TAG = "GceFuture";
    private boolean mDone = false;
    private Exception mException = null;
    private T mResult = null;
    private final String mName;


    public GceFuture(String name) {
        mName = name;
    }


    public String getName() {
        return mName;
    }


    public void set(T value) {
        synchronized(this) {
            if (mDone) {
                Exception e = new Exception();
                Log.e(LOG_TAG, mName + ": Multiple return values from a future object.", e);
                return;
            }

            mResult = value;
            mDone = true;
            notifyAll();
        }
    }


    public void set(Exception e) {
        synchronized(this) {
            if (mDone) {
                Log.w(LOG_TAG, mName + ": Discarding execution exception -- job done.", e);
                return;
            }

            Log.w(LOG_TAG, mName + ": Could not complete job: " + e.getMessage(), e);
            mException = e;
            mDone = true;
            notifyAll();
        }
    }


    @Override
    public boolean cancel(boolean canInterrupt) {
        // We do not support interrupting jobs on purpose:
        // this offers us little benefit (stripping maybe a second or two), at the expense
        // of killing something that may cascade, like BroadcastReceiver.
        synchronized(this) {
            if (mDone) return false;
            set(new CancellationException("cancelled"));
        }

        return true;
    }


    @Override
    public boolean isCancelled() {
        synchronized(this) {
            return (mException != null) && (mException instanceof CancellationException);
        }
    }


    @Override
    public boolean isDone() {
        synchronized(this) {
            return mDone;
        }
    }


    @Override
    public T get() throws CancellationException, ExecutionException, InterruptedException {
        try {
            return get(-1, TimeUnit.SECONDS);
        } catch (TimeoutException e) {
            // This is a really interesting case to consider.
            // Fatal error to add to the drama.
            Log.wtf(LOG_TAG, mName + ": Unexpected condition: Infinite wait timed out.");
            return null;
        }
    }


    @Override
    public T get(long timeout, TimeUnit units)
    throws CancellationException, ExecutionException, InterruptedException, TimeoutException {
        waitDone(timeout, units);

        if (mException != null) {
            if (mException instanceof CancellationException)
                throw (CancellationException)mException;
            throw new ExecutionException(mException);
        }

        return mResult;
    }


    /** Wait for final result.
     *
     * Result is considered available, when:
     * - provider returned value,
     * - this object was cancelled,
     * - provider threw an exception.
     */
    private void waitDone(long timeout, TimeUnit units)
            throws InterruptedException, TimeoutException {
        while (!mDone) {
            synchronized(this) {
                if (timeout >= 0) {
                    this.wait(units.toMillis(timeout));
                    if (!mDone) throw new InterruptedException();
                } else {
                    this.wait();
                }
            }
        }
    }


    /** Convert list of GceFuture objects to string representation.
     */
    public static String toString(ArrayList<GceFuture<?>> futures) {
        StringBuilder b = new StringBuilder();
        for (GceFuture<?> dep : futures) {
            if (b.length() > 0) b.append(", ");
            b.append(dep.getName());
        }

        return b.toString();
    }
}

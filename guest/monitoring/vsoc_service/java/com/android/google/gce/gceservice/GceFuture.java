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
import com.google.common.util.concurrent.AbstractFuture;

public class GceFuture<T> extends AbstractFuture<T> {
    private static final String LOG_TAG = "GceFuture";
    private final String mName;


    public GceFuture(String name) {
        mName = name;
    }


    public String getName() {
        return mName;
    }


    @Override
    public boolean set(T value) {
        if (super.set(value)) {
            return true;
        } else {
            Exception e = new Exception();
            Log.e(LOG_TAG, mName + ": Multiple return values from a future object.", e);
            return false;
        }
    }


    public void set(Exception e) {
        if (!super.setException(e)) {
            Log.w(LOG_TAG, mName + ": Discarding execution exception -- job done.", e);
            return;
        }
        Log.w(LOG_TAG, mName + ": Could not complete job: " + e.getMessage(), e);
    }


    @Override
    public boolean cancel(boolean canInterrupt) {
        // TODO(schuffelen): See if this can be deleted, since GceFuture.cancel is never invoked
        // directly.
        // We do not support interrupting jobs on purpose:
        // this offers us little benefit (stripping maybe a second or two), at the expense
        // of killing something that may cascade, like BroadcastReceiver.
        return super.setException(new CancellationException("cancelled"));
    }


    @Override
    public boolean isCancelled() {
        if (isDone()) {
            try {
                get();
            } catch (ExecutionException ex) {
                return ex.getCause() instanceof CancellationException;
            } catch (InterruptedException ex) {
                return false;
            } catch (CancellationException ex) {
                return true;
            }
        }
        return false;
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

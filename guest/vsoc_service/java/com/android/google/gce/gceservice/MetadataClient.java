/*
 * Copyright (C) 2016 The Android Open Source Project
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

import android.os.Handler;
import android.util.Log;
import com.android.google.gce.gceservice.GceFuture;
import com.android.google.gce.gceservice.JobBase;
import com.android.google.gce.gceservice.Metadata;
import com.android.google.gce.gceservice.MetadataAttribute;
import com.android.google.gce.gceservice.MetadataParser;
import java.lang.Runnable;
import java.lang.Thread;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Poll Metadata Service for instance metadata and updates.
 * This class creates a new background thread, that polls Metadata Service for
 * metadata updates.
 */
public class MetadataClient extends JobBase {
    private static final String LOG_TAG = "GceMetadataClient";
    private static final int MAX_FAILED_ATTEMPTS_TO_READ_METADATA = 10;

    private final MetadataProxyClient mClient = new MetadataProxyClient();
    private final GceFuture<Boolean> mMetadataReady = new GceFuture<Boolean>("Metadata Ready");

    /* Service Handler ensures sequential processing of MetadataServer updates. */
    private Metadata mMetadata = new Metadata();
    private Map<String, List<OnAttributeUpdateListener>> mAttributeUpdateListeners =
            new HashMap<String, List<OnAttributeUpdateListener>>();
    private boolean mReadInitialMetadata = false;
    private int mFailedAttemptsToReadMetadata = 0;
    private String mJsonMetadata = null;
    private Object mJsonMetadataLock = new Object();
    private Date mLastFetch = null;


    /** Interface receiving callback, when requested instance.attribute key is
     * updated by Metadata server.
     */
    public interface OnAttributeUpdateListener {
        /** Callback invoked after registration and upon every change
         * to associated metadata value.
         *
         * This callback is executed:
         * - shortly after being registered with currently known value
         *   of corresponding metadata attribute, and
         * - upon every change that follows.
         * Callback is always invoked with valid MetadataAttribute,
         * however its value may be null. This is a valid case and
         * occurs, when Attribute value:
         * - has not yet been read, or
         * - is not present in Metadata.
         */
        public void onAttributeUpdate(MetadataAttribute attribute);
    };


    /** Constructor.
    */
    public MetadataClient() {
        super(LOG_TAG);
    }


    /** Start fetching metadata.
     * Fetches metadata from metadata proxy.
     * First update always carries initial metadata.
     *
     * Loops forever.
     */
    public int execute() {
        // Read everything we have ready on the socket.
        while (true) {
            if (mFailedAttemptsToReadMetadata >= MAX_FAILED_ATTEMPTS_TO_READ_METADATA) {
                Log.e(LOG_TAG, String.format("Still no metadata after %d attempts",
                            mFailedAttemptsToReadMetadata));
                if (!mMetadataReady.isDone()) {
                    mMetadataReady.set(new Exception(
                            String.format("No metadata after %d attempts.",
                                mFailedAttemptsToReadMetadata)));
                }
            }

            String metadata = null;
            try {
                metadata = mClient.read();
                mFailedAttemptsToReadMetadata = 0;
            } catch (Exception e) {
                ++mFailedAttemptsToReadMetadata;
            }

            if (metadata == null) return 3;
            readAndProcessMetadata(metadata, !mReadInitialMetadata);

            // Report we're ready only after we read both initial- and current metadata.
            if (!mMetadataReady.isDone() && mReadInitialMetadata) mMetadataReady.set(true);
            mReadInitialMetadata = true;
        }
    }


    public void onDependencyFailed(Exception e) {
        Log.e(LOG_TAG, "Could not start MetadataClient.", e);
        mMetadataReady.set(e);
    }


    public GceFuture<Boolean> getMetadataReady() {
        return mMetadataReady;
    }


    /** Wait for initial metadata to be available.
     *
     * This method returns as soon as initial metadata is read and processed.
     */
    public void waitMetadataReady() {
        try {
            mMetadataReady.get();
        } catch (Exception e) {
            Log.e(LOG_TAG, "Call to waitMetadataReady() failed.", e);
        }
    }


    /** Add an Instance Attribute change listener.
     *
     * No thread restrictions.
     */
    public synchronized void addOnAttributeUpdateListener(
            final String key, final OnAttributeUpdateListener listener) {
        List<OnAttributeUpdateListener> listeners;
        listeners = mAttributeUpdateListeners.get(key);
        if (listeners == null) {
            listeners = new ArrayList<OnAttributeUpdateListener>();
            mAttributeUpdateListeners.put(key, listeners);
        }
        listeners.add(listener);

        if (mMetadataReady.isDone()) {
            // Dispatch callback immediately, if we already have metadata.
            listener.onAttributeUpdate(mMetadata.getAttribute(key));
        }
    }


    /** Remove an Instance Attribute change listener.
     *
     * No thread restrictions.
     */
    public synchronized void removeOnAttributeUpdateListener(
            String key, OnAttributeUpdateListener listener) {
        List<OnAttributeUpdateListener> listeners;
        listeners = mAttributeUpdateListeners.get(key);
        if (listeners != null) {
            listeners.remove(listener);
        }
    }


    /** Read and process metadata from supplied InputStream.
     * Returns true, if metadata was read and successfully processed.
     */
    private boolean readAndProcessMetadata(String jsonMetadata, boolean isInitialMetadata) {
        if (!MetadataParser.parseFromJson(jsonMetadata, mMetadata, isInitialMetadata)) {
            return false;
        }
        synchronized(mJsonMetadataLock) {
            mLastFetch = Calendar.getInstance().getTime();
            mJsonMetadata = jsonMetadata;
        }

        for (MetadataAttribute attribute : mMetadata.getAttributes()) {
            /* Detect changed and new keys.
             * This code assumes, that keys are deleted by assigning a null
             * value to them.
             */
            if (attribute.isModified()) {
                List<OnAttributeUpdateListener> list =
                        mAttributeUpdateListeners.get(attribute.getName());
                if (list != null) {
                    synchronized(this) {
                        for (OnAttributeUpdateListener listener : list) {
                            listener.onAttributeUpdate(attribute);
                        }
                    }
                }
                attribute.apply();
            }
        }

        return true;
    }

    public String getLastFetchTime() {
        DateFormat df = new SimpleDateFormat("MM-dd HH:mm:ss.SSS");
        synchronized(mJsonMetadataLock) {
            return df.format(mLastFetch);
        }
    }

    public String getJsonMetadata() {
        synchronized(mJsonMetadataLock) {
            return mJsonMetadata;
        }
    }
};


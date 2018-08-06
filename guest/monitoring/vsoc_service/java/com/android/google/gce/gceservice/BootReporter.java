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

import android.os.Handler;
import android.util.Log;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintWriter;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.List;

/**
 * Report boot status to console.
 *
 * This class sends messages to kernel log (and serial console) directly by
 * writing to /dev/kmsg.
 */
public class BootReporter extends JobBase {
    private static final String LOG_TAG = "GceBootReporter";
    private static final int KLOG_NOTICE = 5;
    private static final String KLOG_OUTPUT = "/dev/kmsg";
    private static final String KLOG_FORMAT = "<%d>%s: %s\n";
    private static final String VIRTUAL_DEVICE_BOOT_STARTED = "VIRTUAL_DEVICE_BOOT_STARTED";
    private static final String VIRTUAL_DEVICE_BOOT_PENDING = "VIRTUAL_DEVICE_BOOT_PENDING";
    private static final String VIRTUAL_DEVICE_BOOT_COMPLETED = "VIRTUAL_DEVICE_BOOT_COMPLETED";
    private static final String VIRTUAL_DEVICE_BOOT_FAILED = "VIRTUAL_DEVICE_BOOT_FAILED";
    private FileOutputStream mKmsgStream = null;
    private PrintWriter mKmsgWriter = null;
    private List<String> mMessageList = new ArrayList<String>();


    /** Constructor. */
    public BootReporter() {
        super(LOG_TAG);

        try {
            mKmsgStream = new FileOutputStream(KLOG_OUTPUT);
            mKmsgWriter = new PrintWriter(mKmsgStream);
        } catch (IOException e) {
            Log.e(LOG_TAG, "Could not open output stream.", e);
        }
    }


    /** Report boot failure.
     *
     * Send message to kernel log and serial console explaining boot failure.
     */
    @Override
    public void onDependencyFailed(Exception e) {
        reportMessage(String.format("%s: %s", VIRTUAL_DEVICE_BOOT_FAILED, e.getMessage()));
    }


    /** Report straggling jobs.
     *
     * Reports boot pending, if any of the parent jobs is still awaiting completion
     * and reschedules itself for re-execution.
     *
     * If all jobs have completed, reports boot completed and stops.
     */
    @Override
    public void onDependencyStraggling(ArrayList<GceFuture<?>> deps) {
        reportMessage(String.format("%s: %s", VIRTUAL_DEVICE_BOOT_PENDING,
                    GceFuture.toString(deps)));
    }


    /** Report successful boot completion.
     *
     * Issue message to serial port confirming successful boot completion and
     * custom boot completion message, if specified by the user prior to reboot.
     */
    @Override
    public int execute() {
        // We suspect that something is throttling our messages and preventing
        // the following message from being logged to bugreport.
        // The log is present in logcat log (that we collect independently), yet
        // occasionally most of the GCEService logs never make it to show up on
        // bug report.
        // This may or may not prove to be effective. We need to monitor bugreports
        // for VIRTUAL_DEVICE_BOOT_COMPLETED messages are being dropped.
        //
        // Number chosen at random - yet guaranteed to be prime.
        try {
            Thread.sleep(937);
        } catch (InterruptedException e) {}

        reportMessage(VIRTUAL_DEVICE_BOOT_COMPLETED);
        return 0;
    }


    public void reportMessage(String message) {
        Log.i(LOG_TAG, message);
        mKmsgWriter.printf(KLOG_FORMAT, KLOG_NOTICE, LOG_TAG, message);
        mKmsgWriter.flush();
        DateFormat df = new SimpleDateFormat("MM-dd HH:mm:ss.SSS");
        String date = df.format(Calendar.getInstance().getTime());
        mMessageList.add("[" + date + "] "  + message);
    }


    public void reportBootStarted() {
        reportMessage(VIRTUAL_DEVICE_BOOT_STARTED);
    }

    /** Get the list of reported messages so far.
     */
    public List<String> getMessageList() {
      return mMessageList;
    }
}

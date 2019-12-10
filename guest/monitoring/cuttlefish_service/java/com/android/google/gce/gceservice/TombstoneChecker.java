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
import java.io.File;
import java.io.FileNotFoundException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Scanner;

/** A job that checks for any new tombstones before reporting VIRTUAL_DEVICE_BOOT_COMPLETED.
 *
 */
public class TombstoneChecker extends JobBase {
    private static final String LOG_TAG = "GceTombstoneChecker";
    private static final String sSnapshotDir = "/data/tombstones";
    private static final String sSnapshotFile = "/ts_snap.txt";
    private static final String sTsExceptionMessage = "GceTombstoneChecker internal error. ";
    private static final String sTsFilePrefix = "tombstone";
    private final GceFuture<Boolean> mPassed = new GceFuture<Boolean>("GceTombstoneChecker");
    private ArrayList<Record> mPreBootRecords = new ArrayList<Record>();
    private ArrayList<Record> mPostBootRecords = new ArrayList<Record>();

    public TombstoneChecker() {
        super(LOG_TAG);
    }


    @Override
    public int execute() {
        if (mPassed.isDone()) {
            return 0;
        }

        try {
            readPreBootSnapshot();
            capturePostBootSnapshot();
            if (seenNewTombstones()) {
                Log.e(LOG_TAG, "Tombstones created during boot. ");
                for (int i = 0; i < mPostBootRecords.size(); i++) {
                    Log.i(LOG_TAG, mPostBootRecords.get(i).getFileName());
                }
                mPassed.set(new Exception("Tombstones created. "));
            } else {
                mPassed.set(true);
            }
        } catch(Exception e) {
            Log.e(LOG_TAG, sTsExceptionMessage + e);
            mPassed.set(new Exception(sTsExceptionMessage, e));
        }

        return 0;
    }

    @Override
    public void onDependencyFailed(Exception e) {
        mPassed.set(e);
    }

    public GceFuture<Boolean> getTombstoneResult() {
        return mPassed;
    }

    private void capturePostBootSnapshot() throws Exception {
        File dir = new File(sSnapshotDir);
        File[] files = dir.listFiles();

        // In K & L, /data/tombstones directory is not created during boot. So
        // dir.listFiles() can return null.
        if (files == null) {
            return;
        }

        for (int i = 0; i < files.length; i++) {
            if (files[i].isFile() && files[i].getName().startsWith(sTsFilePrefix)) {
                long ctime = files[i].lastModified() / 1000;
                mPostBootRecords.add(new Record(files[i].getName(), ctime));
            }
        }
        Collections.sort(mPostBootRecords);

        return;
    }

    private void readPreBootSnapshot() throws Exception {
        File file = new File(sSnapshotFile);
        if (!file.isFile()) {
            throw new FileNotFoundException(sSnapshotFile);
        }

        Scanner scanner = new Scanner(file);
        while (scanner.hasNext()) {
            String[] fields = scanner.nextLine().split(" ");
            mPreBootRecords.add(new Record(fields[0], Long.parseLong(fields[1])));
        }
        Collections.sort(mPreBootRecords);

        return;
    }

    private boolean seenNewTombstones() {
        return !isEqual(mPreBootRecords, mPostBootRecords);
    }

    private boolean isEqual(ArrayList<Record> preBoot, ArrayList<Record> postBoot) {
        postBoot.removeAll(preBoot);
        if (postBoot.size() != 0) {
            return false;
        }

        return true;
    }

    private class Record implements Comparable<Record> {
        private String mFilename;
        private long mCtime;

        public Record(String filename, long ctime) {
            this.mFilename = filename;
            this.mCtime = ctime;
        }

        public String getFileName() {
            return mFilename;
        }

        public int compareTo(Record r) {
            if (this == r) {
                return 0;
            }

            return (mFilename.compareTo(r.mFilename));
        }

        public boolean equals(Object o) {
            if (o == null) {
                return false;
            }

            if (this == o) {
                return true;
            }

            Record r = (Record) o;
            return (mFilename.equals(r.mFilename) && (mCtime == r.mCtime));
        }

        public String toString() {
            StringBuilder sb = new StringBuilder();
            sb.append(mFilename).append(" ").append(String.valueOf(mCtime));
            return sb.toString();
        }
    }
}

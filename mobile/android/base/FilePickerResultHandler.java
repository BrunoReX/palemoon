/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import org.mozilla.gecko.mozglue.GeckoLoader;
import org.mozilla.gecko.util.ActivityResultHandler;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.Queue;

abstract class FilePickerResultHandler implements ActivityResultHandler {
    private static final String LOGTAG = "GeckoFilePickerResultHandler";

    protected final Queue<String> mFilePickerResult;
    protected final ActivityHandlerHelper.FileResultHandler mHandler;

    protected FilePickerResultHandler(Queue<String> resultQueue, ActivityHandlerHelper.FileResultHandler handler) {
        mFilePickerResult = resultQueue;
        mHandler = handler;
    }

    protected String handleActivityResult(int resultCode, Intent data) {
        if (data == null || resultCode != Activity.RESULT_OK)
            return "";
        Uri uri = data.getData();
        if (uri == null)
            return "";
        if ("file".equals(uri.getScheme())) {
            String path = uri.getPath();
            return path == null ? "" : path;
        }
        try {
            ContentResolver cr = GeckoAppShell.getContext().getContentResolver();
            Cursor cursor = cr.query(uri, new String[] { OpenableColumns.DISPLAY_NAME },
                                     null, null, null);
            String name = null;
            if (cursor != null) {
                try {
                    if (cursor.moveToNext()) {
                        name = cursor.getString(0);
                    }
                } finally {
                    cursor.close();
                }
            }

            // tmp filenames must be at least 3 characters long. Add a prefix to make sure that happens
            String fileName = "tmp_";
            String fileExt = null;
            int period;
            if (name == null || (period = name.lastIndexOf('.')) == -1) {
                String mimeType = cr.getType(uri);
                fileExt = "." + GeckoAppShell.getExtensionFromMimeType(mimeType);
            } else {
                fileExt = name.substring(period);
                fileName += name.substring(0, period);
            }
            Log.i(LOGTAG, "Filename: " + fileName + " . " + fileExt);
            File file = File.createTempFile(fileName, fileExt, GeckoLoader.getGREDir(GeckoAppShell.getContext()));
            FileOutputStream fos = new FileOutputStream(file);
            InputStream is = cr.openInputStream(uri);
            byte[] buf = new byte[4096];
            int len = is.read(buf);
            while (len != -1) {
                fos.write(buf, 0, len);
                len = is.read(buf);
            }
            fos.close();
            String path = file.getAbsolutePath();
            return path == null ? "" : path;
        } catch (Exception e) {
            Log.e(LOGTAG, "showing file picker", e);
        }
        return "";
    }
}

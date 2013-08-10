/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.app.Application;

import java.util.ArrayList;

public class GeckoApplication extends Application {

    private boolean mInited;
    private boolean mInBackground;
    private boolean mPausedGecko;

    private LightweightTheme mLightweightTheme;

    protected void initialize() {
        if (mInited)
            return;

        // workaround for http://code.google.com/p/android/issues/detail?id=20915
        try {
            Class.forName("android.os.AsyncTask");
        } catch (ClassNotFoundException e) {}

        mLightweightTheme = new LightweightTheme(this);

        GeckoConnectivityReceiver.getInstance().init(getApplicationContext());
        GeckoBatteryManager.getInstance().init(getApplicationContext());
        GeckoBatteryManager.getInstance().start();
        GeckoNetworkManager.getInstance().init(getApplicationContext());
        MemoryMonitor.getInstance().init(getApplicationContext());
        mInited = true;
    }

    protected void onActivityPause(GeckoActivityStatus activity) {
        mInBackground = true;

        if ((activity.isFinishing() == false) &&
            (activity.isGeckoActivityOpened() == false)) {
            // Notify Gecko that we are pausing; the cache service will be
            // shutdown, closing the disk cache cleanly. If the android
            // low memory killer subsequently kills us, the disk cache will
            // be left in a consistent state, avoiding costly cleanup and
            // re-creation. 
            GeckoAppShell.sendEventToGecko(GeckoEvent.createPauseEvent(true));
            mPausedGecko = true;
        }
        GeckoConnectivityReceiver.getInstance().stop();
        GeckoNetworkManager.getInstance().stop();
    }

    protected void onActivityResume(GeckoActivityStatus activity) {
        if (mPausedGecko) {
            GeckoAppShell.sendEventToGecko(GeckoEvent.createResumeEvent(true));
            mPausedGecko = false;
        }
        GeckoConnectivityReceiver.getInstance().start();
        GeckoNetworkManager.getInstance().start();

        mInBackground = false;
    }

    public boolean isApplicationInBackground() {
        return mInBackground;
    }

    public LightweightTheme getLightweightTheme() {
        return mLightweightTheme;
    }
}

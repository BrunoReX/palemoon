/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.widget;

import org.mozilla.gecko.BrowserApp;
import org.mozilla.gecko.Favicons;
import org.mozilla.gecko.R;
import org.mozilla.gecko.util.GamepadUtils;
import org.mozilla.gecko.util.ThreadUtils;
import org.mozilla.gecko.util.UiAsyncTask;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.text.SpannableString;
import android.text.TextUtils;
import android.text.style.TextAppearanceSpan;
import android.util.AttributeSet;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.MalformedURLException;
import java.net.URL;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

public class AddonsSection extends AboutHomeSection {
    private static final String LOGTAG = "GeckoAboutHomeAddons";

    private Context mContext;
    private BrowserApp mActivity;
    private AboutHome.UriLoadListener mUriLoadListener;

    private static Rect sIconBounds;
    private static TextAppearanceSpan sSubTitleSpan;

    public AddonsSection(Context context, AttributeSet attrs) {
        super(context, attrs);
        mContext = context;
        mActivity = (BrowserApp) context;

        int iconSize = mContext.getResources().getDimensionPixelSize(R.dimen.abouthome_addon_icon_size);
        sIconBounds = new Rect(0, 0, iconSize, iconSize);
        sSubTitleSpan = new TextAppearanceSpan(mContext, R.style.AboutHome_TextAppearance_SubTitle);

        setOnMoreTextClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mUriLoadListener != null)
                    mUriLoadListener.onAboutHomeUriLoad(mContext.getString(R.string.bookmarkdefaults_url_addons));
            }
        });
    }

    public void setUriLoadListener(AboutHome.UriLoadListener uriLoadListener) {
        mUriLoadListener = uriLoadListener;
    }

    private String readFromZipFile(String filename) {
        ZipFile zip = null;
        String str = null;
        try {
            InputStream fileStream = null;
            File applicationPackage = new File(mActivity.getApplication().getPackageResourcePath());
            zip = new ZipFile(applicationPackage);
            ZipEntry fileEntry = zip.getEntry(filename);
            if (fileEntry == null)
                return null;
            fileStream = zip.getInputStream(fileEntry);
            str = readStringFromStream(fileStream);
        } catch (IOException ioe) {
            Log.e(LOGTAG, "error reading zip file: " + filename, ioe);
        } finally {
            try {
                if (zip != null)
                    zip.close();
            } catch (IOException ioe) {
                // catch this here because we can continue even if the
                // close failed
                Log.e(LOGTAG, "error closing zip filestream", ioe);
            }
        }
        return str;
    }

    private String readStringFromStream(InputStream fileStream) {
        String str = null;
        try {
            byte[] buf = new byte[32768];
            StringBuffer jsonString = new StringBuffer();
            int read = 0;
            while ((read = fileStream.read(buf, 0, 32768)) != -1)
                jsonString.append(new String(buf, 0, read));
            str = jsonString.toString();
        } catch (IOException ioe) {
            Log.i(LOGTAG, "error reading filestream", ioe);
        } finally {
            try {
                if (fileStream != null)
                    fileStream.close();
            } catch (IOException ioe) {
                // catch this here because we can continue even if the
                // close failed
                Log.e(LOGTAG, "error closing filestream", ioe);
            }
        }
        return str;
    }

    private String getPageUrlFromIconUrl(String iconUrl) {
        // Addon icon URLs come with a query argument that is usually
        // used for expiration purposes. We want the "page URL" here to be
        // stable enough to avoid unnecessary duplicate records of the
        // same addon.
        String pageUrl = iconUrl;

        try {
            URL urlForIcon = new URL(iconUrl);
            URL urlForPage = new URL(urlForIcon.getProtocol(), urlForIcon.getAuthority(), urlForIcon.getPath());
            pageUrl = urlForPage.toString();
        } catch (MalformedURLException e) {
            // Defaults to pageUrl = iconUrl in case of error
        }

        return pageUrl;
    }

    public void readRecommendedAddons() {
        new UiAsyncTask<Void, Void, JSONArray>(ThreadUtils.getBackgroundHandler()) {
            @Override
            public JSONArray doInBackground(Void... params) {
                final String addonsFilename = "recommended-addons.json";
                String jsonString;
                try {
                    jsonString = mActivity.getProfile().readFile(addonsFilename);
                } catch (IOException ioe) {
                    Log.i(LOGTAG, "filestream is null");
                    jsonString = readFromZipFile(addonsFilename);
                }

                JSONArray addonsArray = null;
                if (jsonString != null) {
                    try {
                        addonsArray = new JSONObject(jsonString).getJSONArray("addons");
                    } catch (JSONException e) {
                        Log.i(LOGTAG, "error reading json file", e);
                    }
                }

                return addonsArray;
            }

            @Override
            public void onPostExecute(JSONArray addons) {
                if (addons == null || addons.length() == 0) {
                    hide();
                    return;
                }

                try {
                    for (int i = 0; i < addons.length(); i++) {
                        View addonView = createAddonView(addons.getJSONObject(i), getItemsContainer());
                        addItem(addonView);
                    }
                } catch (JSONException e) {
                    Log.e(LOGTAG, "Error reading JSON", e);
                    return;
                }

                show();
            }
        }.execute();
    }

    View createAddonView(JSONObject addonJSON, ViewGroup parent) throws JSONException {
        String name = addonJSON.getString("name");
        String version = addonJSON.getString("version");
        String text = name + " " + version;

        SpannableString spannable = new SpannableString(text);
        spannable.setSpan(sSubTitleSpan, name.length() + 1, text.length(), 0);

        final TextView row = (TextView) LayoutInflater.from(mContext).inflate(R.layout.abouthome_addon_row, getItemsContainer(), false);
        row.setText(spannable, TextView.BufferType.SPANNABLE);

        Drawable drawable = mContext.getResources().getDrawable(R.drawable.ic_addons_empty);
        drawable.setBounds(sIconBounds);
        row.setCompoundDrawables(drawable, null, null, null);

        String iconUrl = addonJSON.getString("iconURL");
        String pageUrl = getPageUrlFromIconUrl(iconUrl);

        // homepageURL may point to non-AMO installs. For now we use learnmoreURL instead
        // which is more likely to point to a mobile AMO page
        String homepageUrl = addonJSON.optString("learnmoreURL");
        if (TextUtils.isEmpty(homepageUrl)) {
            homepageUrl = addonJSON.getString("homepageURL");
        }
        final String addonUrl = homepageUrl;
        row.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mUriLoadListener != null)
                    mUriLoadListener.onAboutHomeUriLoad(addonUrl);
            }
        });
        row.setOnKeyListener(GamepadUtils.getClickDispatcher());

        Favicons favicons = Favicons.getInstance();
        favicons.loadFavicon(pageUrl, iconUrl, true,
                new Favicons.OnFaviconLoadedListener() {
            @Override
            public void onFaviconLoaded(String url, Bitmap favicon) {
                if (favicon != null) {
                    Drawable drawable = new BitmapDrawable(favicon);
                    drawable.setBounds(sIconBounds);
                    row.setCompoundDrawables(drawable, null, null, null);
                }
            }
        });

        return row;
    }
}

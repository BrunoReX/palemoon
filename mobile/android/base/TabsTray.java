/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Android code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Sriram Ramasubramanian <sriram@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

package org.mozilla.gecko;

import java.util.ArrayList;

import android.app.Activity;
import android.content.Context;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.os.Build;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.RelativeLayout;
import android.widget.TextView;

public class TabsTray extends Activity implements GeckoApp.OnTabsChangedListener {

    private static int sPreferredHeight;
    private static int sMaxHeight;
    private static int sListItemHeight;
    private static ListView mList;
    private TabsAdapter mTabsAdapter;
    private boolean mWaitingForClose;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.tabs_tray);

        if (Build.VERSION.SDK_INT >= 11) {
            GeckoActionBar.hide(this);
        }

        mWaitingForClose = false;

        mList = (ListView) findViewById(R.id.list);

        LinearLayout addTab = (LinearLayout) findViewById(R.id.add_tab);
        addTab.setOnClickListener(new Button.OnClickListener() {
            public void onClick(View v) {
                GeckoApp.mAppContext.addTab();
                finishActivity();
            }
        });
        
        LinearLayout container = (LinearLayout) findViewById(R.id.container);
        container.setOnClickListener(new Button.OnClickListener() {
            public void onClick(View v) {
                finishActivity();
            }
        });

        DisplayMetrics metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getMetrics(metrics);
        sPreferredHeight = (int) (0.67 * metrics.heightPixels);
        sListItemHeight = (int) (100 * metrics.density); 
        sMaxHeight = (int) (sPreferredHeight + (0.33 * sListItemHeight));

        GeckoApp.registerOnTabsChangedListener(this);
        Tabs.getInstance().refreshThumbnails();
        onTabsChanged(null);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        GeckoApp.unregisterOnTabsChangedListener(this);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        // This function is called after the initial list is populated
        // Scrolling to the selected tab can happen here
        if (hasFocus) {
            int position = mTabsAdapter.getPositionForTab(Tabs.getInstance().getSelectedTab());
            if (position == -1)
                return;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.FROYO) {
                mList.smoothScrollToPosition(position);
            } else {
                /* To Do: Find a way to scroll with Eclair's APIs */
            }
        }
    } 
   
    public void onTabsChanged(Tab tab) {
        if (Tabs.getInstance().getCount() == 1)
            finishActivity();

        if (mTabsAdapter == null) {
            mTabsAdapter = new TabsAdapter(this, Tabs.getInstance().getTabsInOrder());
            mList.setAdapter(mTabsAdapter);
            return;
        }
        
        int position = mTabsAdapter.getPositionForTab(tab);
        if (position == -1)
            return;

        if (Tabs.getInstance().getIndexOf(tab) == -1) {
            mWaitingForClose = false;
            mTabsAdapter = new TabsAdapter(this, Tabs.getInstance().getTabsInOrder());
            mList.setAdapter(mTabsAdapter);
        } else {
            View view = mList.getChildAt(position - mList.getFirstVisiblePosition());
            mTabsAdapter.assignValues(view, tab);
        }
    }

    void finishActivity() {
        finish();
        overridePendingTransition(0, R.anim.shrink_fade_out);
        GeckoAppShell.sendEventToGecko(new GeckoEvent("Tab:Screenshot:Cancel",""));
    }

    // Tabs List Container holds the ListView and the New Tab button
    public static class TabsListContainer extends LinearLayout {
        public TabsListContainer(Context context, AttributeSet attrs) {
            super(context, attrs);
        }

        @Override
        protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
            super.onSizeChanged(width, height, oldWidth, oldHeight);

            if ((height > sPreferredHeight) && (height != sMaxHeight)) {
                setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                                                              sPreferredHeight));

                // If the list ends perfectly on an item, increase the height of the container 
                if (mList.getHeight() % sListItemHeight == 0)
                    setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                                                                  sMaxHeight));
            }
        }
    }

    // Adapter to bind tabs into a list 
    private class TabsAdapter extends BaseAdapter {
        public TabsAdapter(Context context, ArrayList<Tab> tabs) {
            mContext = context;
            mInflater = LayoutInflater.from(mContext);
            mTabs = new ArrayList<Tab>();

            if (tabs == null)
                return;

            for (int i = 0; i < tabs.size(); i++) {
                mTabs.add(tabs.get(i));
            }
            
            mOnInfoClickListener = new View.OnClickListener() {
                public void onClick(View v) {
                    Tabs.getInstance().selectTab(Integer.parseInt((String) v.getTag()));
                    finishActivity();
                }
            };

            mOnCloseClickListener = new Button.OnClickListener() {
                public void onClick(View v) {
                    if (mWaitingForClose)
                        return;

                    mWaitingForClose = true;

                    String tabId = v.getTag().toString();
                    Tabs tabs = Tabs.getInstance();
                    Tab tab = tabs.getTab(Integer.parseInt(tabId));
                    tabs.closeTab(tab);
                }
            };
        }

        public int getCount() {
            return mTabs.size();
        }
    
        public Tab getItem(int position) {
            return mTabs.get(position);
        }

        public long getItemId(int position) {
            return position;
        }

        public int getPositionForTab(Tab tab) {
            if (mTabs == null || tab == null)
                return -1;

            return mTabs.indexOf(tab);
        }

        public void assignValues(View view, Tab tab) {
            if (view == null || tab == null)
                return;

            ImageView thumbnail = (ImageView) view.findViewById(R.id.thumbnail);

            Drawable thumbnailImage = tab.getThumbnail();
            if (thumbnailImage != null)
                thumbnail.setImageDrawable(thumbnailImage);
            else
                thumbnail.setImageResource(R.drawable.tab_thumbnail_default);

            if (Tabs.getInstance().isSelectedTab(tab))
                ((ImageView) view.findViewById(R.id.selected_indicator)).setVisibility(View.VISIBLE);

            TextView title = (TextView) view.findViewById(R.id.title);
            title.setText(tab.getDisplayTitle());
        }

        public View getView(int position, View convertView, ViewGroup parent) {
            convertView = mInflater.inflate(R.layout.tabs_row, null);

            Tab tab = mTabs.get(position);

            RelativeLayout info = (RelativeLayout) convertView.findViewById(R.id.info);
            info.setTag(String.valueOf(tab.getId()));
            info.setOnClickListener(mOnInfoClickListener);

            assignValues(convertView, tab);
            
            ImageButton close = (ImageButton) convertView.findViewById(R.id.close);
            if (mTabs.size() > 1) {
                close.setTag(String.valueOf(tab.getId()));
                close.setOnClickListener(mOnCloseClickListener);
            } else {
                close.setVisibility(View.GONE);
            }

            return convertView;
        }

        @Override
        public void notifyDataSetChanged() {
        }

        @Override
        public void notifyDataSetInvalidated() {
        }
    
        private Context mContext;
        private ArrayList<Tab> mTabs;
        private LayoutInflater mInflater;
        private View.OnClickListener mOnInfoClickListener;
        private Button.OnClickListener mOnCloseClickListener;
    }
}

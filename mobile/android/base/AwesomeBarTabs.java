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
 * Portions created by the Initial Developer are Copyright (C) 2009-2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Lucas Rocha <lucasr@mozilla.com>
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

import android.content.ContentResolver;
import android.content.Context;
import android.content.res.Resources;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.LightingColorFilter;
import android.graphics.drawable.Drawable;
import android.os.AsyncTask;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.util.Log;
import android.util.Pair;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodManager;
import android.widget.AdapterView;
import android.widget.ExpandableListView;
import android.widget.FilterQueryProvider;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.SimpleCursorAdapter;
import android.widget.SimpleExpandableListAdapter;
import android.widget.TabHost;
import android.widget.TextView;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.util.Date;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import org.mozilla.gecko.db.BrowserDB;
import org.mozilla.gecko.db.BrowserDB.URLColumns;

public class AwesomeBarTabs extends TabHost {
    private static final String LOGTAG = "GeckoAwesomeBarTabs";

    private static final String ALL_PAGES_TAB = "all";
    private static final String BOOKMARKS_TAB = "bookmarks";
    private static final String HISTORY_TAB = "history";

    private static enum HistorySection { TODAY, YESTERDAY, WEEK, OLDER };

    private Context mContext;
    private boolean mInflated;
    private OnUrlOpenListener mUrlOpenListener;
    private View.OnTouchListener mListTouchListener;
    private JSONArray mSearchEngines;

    private AwesomeBarCursorAdapter mAllPagesCursorAdapter;
    private SimpleCursorAdapter mBookmarksAdapter;
    private SimpleExpandableListAdapter mHistoryAdapter;

    // FIXME: This value should probably come from a
    // prefs key (just like XUL-based fennec)
    private static final int MAX_RESULTS = 100;

    public interface OnUrlOpenListener {
        public void onUrlOpen(String url);
        public void onSearch(String engine);
    }

    private class HistoryListAdapter extends SimpleExpandableListAdapter {
        public HistoryListAdapter(Context context, List<? extends Map<String, ?>> groupData,
                int groupLayout, String[] groupFrom, int[] groupTo,
                List<? extends List<? extends Map<String, ?>>> childData,
                int childLayout, String[] childFrom, int[] childTo) {

            super(context, groupData, groupLayout, groupFrom, groupTo,
                  childData, childLayout, childFrom, childTo);
        }

        @Override
        public View getChildView(int groupPosition, int childPosition, boolean isLastChild,
                View convertView, ViewGroup parent) {

            View childView =
                    super.getChildView(groupPosition, childPosition, isLastChild, convertView, parent); 

            @SuppressWarnings("unchecked")
            Map<String,Object> historyItem =
                    (Map<String,Object>) mHistoryAdapter.getChild(groupPosition, childPosition);

            byte[] b = (byte[]) historyItem.get(URLColumns.FAVICON);
            ImageView favicon = (ImageView) childView.findViewById(R.id.favicon);

            if (b == null) {
                favicon.setImageDrawable(null);
            } else {
                Bitmap bitmap = BitmapFactory.decodeByteArray(b, 0, b.length);
                favicon.setImageBitmap(bitmap);
            }

            return childView;
        }
    }

    private class AwesomeCursorViewBinder implements SimpleCursorAdapter.ViewBinder {
        private boolean updateFavicon(View view, Cursor cursor, int faviconIndex) {
            byte[] b = cursor.getBlob(faviconIndex);
            ImageView favicon = (ImageView) view;

            if (b == null) {
                favicon.setImageDrawable(null);
            } else {
                Bitmap bitmap = BitmapFactory.decodeByteArray(b, 0, b.length);
                favicon.setImageBitmap(bitmap);
            }

            return true;
        }

        private boolean updateTitle(View view, Cursor cursor, int titleIndex) {
            String title = cursor.getString(titleIndex);
            TextView titleView = (TextView)view;
            // Use the URL instead of an empty title for consistency with the normal URL
            // bar view - this is the equivalent of getDisplayTitle() in Tab.java
            if (title == null || title.length() == 0) {
                int urlIndex = cursor.getColumnIndexOrThrow(URLColumns.URL);
                title = cursor.getString(urlIndex);
            }

            titleView.setText(title);
            return true;
        }

        public boolean setViewValue(View view, Cursor cursor, int columnIndex) {
            int faviconIndex = cursor.getColumnIndexOrThrow(URLColumns.FAVICON);
            if (columnIndex == faviconIndex) {
                return updateFavicon(view, cursor, faviconIndex);
            }

            int titleIndex = cursor.getColumnIndexOrThrow(URLColumns.TITLE);
            if (columnIndex == titleIndex) {
                return updateTitle(view, cursor, titleIndex);
            }

            // Other columns are handled automatically
            return false;
        }
    }

    private class BookmarksQueryTask extends AsyncTask<Void, Void, Cursor> {
        protected Cursor doInBackground(Void... arg0) {
            ContentResolver resolver = mContext.getContentResolver();
            return BrowserDB.getAllBookmarks(resolver);
        }

        protected void onPostExecute(Cursor cursor) {
            // Load the list using a custom adapter so we can create the bitmaps
            mBookmarksAdapter = new SimpleCursorAdapter(
                mContext,
                R.layout.awesomebar_row,
                cursor,
                new String[] { URLColumns.TITLE,
                               URLColumns.URL,
                               URLColumns.FAVICON },
                new int[] { R.id.title, R.id.url, R.id.favicon }
            );

            mBookmarksAdapter.setViewBinder(new AwesomeCursorViewBinder());

            final ListView bookmarksList = (ListView) findViewById(R.id.bookmarks_list);

            bookmarksList.setOnItemClickListener(new AdapterView.OnItemClickListener() {
                public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                    handleItemClick(bookmarksList, position);
                }
            });

            bookmarksList.setAdapter(mBookmarksAdapter);
        }
    }

    private class HistoryQueryTask extends AsyncTask<Void, Void, Pair<List,List>> {
        private static final long MS_PER_DAY = 86400000;
        private static final long MS_PER_WEEK = MS_PER_DAY * 7;

        protected Pair<List,List> doInBackground(Void... arg0) {
            Pair<List,List> result = null;
            ContentResolver resolver = mContext.getContentResolver();
            Cursor cursor = BrowserDB.getRecentHistory(resolver, MAX_RESULTS);

            Date now = new Date();
            now.setHours(0);
            now.setMinutes(0);
            now.setSeconds(0);

            long today = now.getTime();

            // Split the list of urls into separate date range groups
            // and show it in an expandable list view.
            List<List<Map<String,?>>> childrenLists = null;
            List<Map<String,?>> children = null;
            List<Map<String,?>> groups = null;
            HistorySection section = null;

            // Move cursor before the first row in preparation
            // for the iteration.
            cursor.moveToPosition(-1);

            // Split the history query results into adapters per time
            // section (today, yesterday, week, older). Queries on content
            // Browser content provider don't support limitting the number
            // of returned rows so we limit it here.
            while (cursor.moveToNext()) {
                long time = cursor.getLong(cursor.getColumnIndexOrThrow(URLColumns.DATE_LAST_VISITED));
                HistorySection itemSection = getSectionForTime(time, today);

                if (groups == null)
                    groups = new LinkedList<Map<String,?>>();

                if (childrenLists == null)
                    childrenLists = new LinkedList<List<Map<String,?>>>();

                if (section != itemSection) {
                    if (section != null) {
                        groups.add(createGroupItem(section));
                        childrenLists.add(children);
                    }

                    section = itemSection;
                    children = new LinkedList<Map<String,?>>();
                }

                children.add(createHistoryItem(cursor));
            }

            // Add any remaining section to the list if it hasn't
            // been added to the list after the loop.
            if (section != null && children != null) {
                groups.add(createGroupItem(section));
                childrenLists.add(children);
            }

            // Close the query cursor as we won't use it anymore
            cursor.close();

            if (groups != null && childrenLists != null) {
                result = Pair.create((List) groups, (List) childrenLists);
            }

            return result;
        }

        public Map<String,?> createHistoryItem(Cursor cursor) {
            Map<String,Object> historyItem = new HashMap<String,Object>();

            String url = cursor.getString(cursor.getColumnIndexOrThrow(URLColumns.URL));
            String title = cursor.getString(cursor.getColumnIndexOrThrow(URLColumns.TITLE));
            byte[] favicon = cursor.getBlob(cursor.getColumnIndexOrThrow(URLColumns.FAVICON));

            // Use the URL instead of an empty title for consistency with the normal URL
            // bar view - this is the equivalent of getDisplayTitle() in Tab.java
            if (title == null || title.length() == 0)
                title = url;

            historyItem.put(URLColumns.URL, url);
            historyItem.put(URLColumns.TITLE, title);

            if (favicon != null)
                historyItem.put(URLColumns.FAVICON, favicon);

            return historyItem;
        }

        public Map<String,?> createGroupItem(HistorySection section) {
            Map<String,String> groupItem = new HashMap<String,String>();

            groupItem.put(URLColumns.TITLE, getSectionName(section));

            return groupItem;
        }

        private String getSectionName(HistorySection section) {
            Resources resources = mContext.getResources();

            switch (section) {
            case TODAY:
                return resources.getString(R.string.history_today_section);
            case YESTERDAY:
                return resources.getString(R.string.history_yesterday_section);
            case WEEK:
                return resources.getString(R.string.history_week_section);
            case OLDER:
                return resources.getString(R.string.history_older_section);
            }

            return null;
        }

        private void expandAllGroups(ExpandableListView historyList) {
            int groupCount = mHistoryAdapter.getGroupCount();

            for (int i = 0; i < groupCount; i++) {
                historyList.expandGroup(i);
            }
        }

        private HistorySection getSectionForTime(long time, long today) {
            long delta = today - time;

            if (delta < 0) {
                return HistorySection.TODAY;
            }

            if (delta < MS_PER_DAY) {
                return HistorySection.YESTERDAY;
            }

            if (delta < MS_PER_WEEK) {
                return HistorySection.WEEK;
            }

            return HistorySection.OLDER;
        }

        @SuppressWarnings("unchecked")
        protected void onPostExecute(Pair<List,List> result) {
            // FIXME: display some sort of message when there's no history
            if (result == null)
                return;

            mHistoryAdapter = new HistoryListAdapter(
                mContext,
                result.first,
                R.layout.awesomebar_header_row,
                new String[] { URLColumns.TITLE },
                new int[] { R.id.title },
                result.second,
                R.layout.awesomebar_row,
                new String[] { URLColumns.TITLE, URLColumns.URL },
                new int[] { R.id.title, R.id.url }
            );

            final ExpandableListView historyList =
                    (ExpandableListView) findViewById(R.id.history_list);

            historyList.setOnChildClickListener(new ExpandableListView.OnChildClickListener() {
                public boolean onChildClick(ExpandableListView parent, View view,
                        int groupPosition, int childPosition, long id) {
                    handleHistoryItemClick(groupPosition, childPosition);
                    return true;
                }
            });

            // This is to disallow collapsing the expandable groups in the
            // history expandable list view to mimic simpler sections. We should
            // Remove this if we decide to allow expanding/collapsing groups.
            historyList.setOnGroupClickListener(new ExpandableListView.OnGroupClickListener() {
                public boolean onGroupClick(ExpandableListView parent, View v,
                        int groupPosition, long id) {
                    return true;
                }
            });

            historyList.setAdapter(mHistoryAdapter);

            expandAllGroups(historyList);
        }
    }

    private class AwesomeBarCursorAdapter extends SimpleCursorAdapter {
        private String mSearchTerm;

        public AwesomeBarCursorAdapter(Context context, int layout, Cursor c, String[] from, int[] to) {
            super(context, layout, c, from, to);
            mSearchTerm = "";
        }

        public void filter(String searchTerm) {
            mSearchTerm = searchTerm;
            getFilter().filter(searchTerm);
        }

        // Add the search engines to the number of reported results.
        @Override
        public int getCount() {
            final int resultCount = super.getCount();

            // don't show additional search engines if search field is empty
            if (mSearchTerm.length() == 0)
                return resultCount;

            return resultCount + mSearchEngines.length();
        }

        // If an item is part of the cursor result set, return that entry.
        // Otherwise, return the search engine data.
        @Override
        public Object getItem(int position) {
            final int resultCount = super.getCount();
            if (position < resultCount)
                return super.getItem(position);

            JSONObject engine;
            String engineName = null;
            try {
                engine = mSearchEngines.getJSONObject(position - resultCount);
                engineName = engine.getString("name");
            } catch (JSONException e) {
                Log.e(LOGTAG, "error getting json arguments");
            }

            return engineName;
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            final int resultCount = super.getCount();
            if (position < resultCount)
                return super.getView(position, convertView, parent);

            View v;
            if (convertView == null)
                v = newView(mContext, null, parent);
            else
                v = convertView;
            bindSearchEngineView(position - resultCount, v);

            return v;
        }

        private Drawable getDrawableFromDataURI(String dataURI) {
            String base64 = dataURI.substring(dataURI.indexOf(',') + 1);
            Drawable drawable = null;
            try {
                byte[] bytes = GeckoAppShell.decodeBase64(base64);
                ByteArrayInputStream stream = new ByteArrayInputStream(bytes);
                drawable = Drawable.createFromStream(stream, "src");
                stream.close();
            } catch (IllegalArgumentException e) {
                Log.i(LOGTAG, "exception while decoding drawable: " + base64, e);
            } catch (IOException e) { }
            return drawable;
        }

        private void bindSearchEngineView(int position, View view) {
            String name;
            String iconURI;
            String searchText = getResources().getString(R.string.awesomebar_search_engine, mSearchTerm);
            try {
                JSONObject searchEngine = mSearchEngines.getJSONObject(position);
                name = searchEngine.getString("name");
                iconURI = searchEngine.getString("iconURI");
            } catch (JSONException e) {
                Log.e(LOGTAG, "error getting json arguments");
                return;
            }

            TextView titleView = (TextView) view.findViewById(R.id.title);
            TextView urlView = (TextView) view.findViewById(R.id.url);
            ImageView faviconView = (ImageView) view.findViewById(R.id.favicon);

            titleView.setText(name);
            urlView.setText(searchText);
            Drawable drawable = getDrawableFromDataURI(iconURI);
            if (drawable != null)
                faviconView.setImageDrawable(drawable);
        }
    };

    public AwesomeBarTabs(Context context, AttributeSet attrs) {
        super(context, attrs);

        Log.d(LOGTAG, "Creating AwesomeBarTabs");

        mContext = context;
        mInflated = false;
        mSearchEngines = new JSONArray();
    }

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        // HACK: Without this, the onFinishInflate is called twice
        // This issue is due to a bug when Android inflates a layout with a
        // parent. Fixed in Honeycomb
        if (mInflated)
            return;

        mInflated = true;

        // This should be called before adding any tabs
        // to the TabHost.
        setup();

        mListTouchListener = new View.OnTouchListener() {
            public boolean onTouch(View view, MotionEvent event) {
                hideSoftInput(view);
                return false;
            }
        };

        addAllPagesTab();
        addBookmarksTab();
        addHistoryTab();

        setOnTabChangedListener(new TabHost.OnTabChangeListener() {
            public void onTabChanged(String tabId) {
                boolean hideSoftInput = true;

                // Lazy load bookmarks and history lists. Only query the database
                // if those lists requested by user.
                if (tabId.equals(BOOKMARKS_TAB) && mBookmarksAdapter == null) {
                    new BookmarksQueryTask().execute();
                } else if (tabId.equals(HISTORY_TAB) && mHistoryAdapter == null) {
                    new HistoryQueryTask().execute();
                } else {
                    hideSoftInput = false;
                }

                // Always dismiss SKB when changing to lazy-loaded tabs
                if (hideSoftInput) {
                    View tabView = getCurrentTabView();
                    hideSoftInput(tabView);
                }
            }
        });

        // Initialize "App Pages" list with no filter
        filter("");
    }

    private TabSpec addAwesomeTab(String id, int titleId, int contentId) {
        TabSpec tab = newTabSpec(id);

        LayoutInflater inflater =
                (LayoutInflater) mContext.getSystemService(Context.LAYOUT_INFLATER_SERVICE);

        View indicatorView = inflater.inflate(R.layout.awesomebar_tab_indicator, null);
        Drawable background = indicatorView.getBackground();
        try {
            background.setColorFilter(new LightingColorFilter(Color.WHITE, GeckoApp.mBrowserToolbar.getHighlightColor()));
        } catch (Exception e) {
            Log.d(LOGTAG, "background.setColorFilter failed " + e);            
        }
        TextView title = (TextView) indicatorView.findViewById(R.id.title);
        title.setText(titleId);

        tab.setIndicator(indicatorView);
        tab.setContent(contentId);

        addTab(tab);

        return tab;
    }

    private void addAllPagesTab() {
        Log.d(LOGTAG, "Creating All Pages tab");

        addAwesomeTab(ALL_PAGES_TAB,
                      R.string.awesomebar_all_pages_title,
                      R.id.all_pages_list);

        // Load the list using a custom adapter so we can create the bitmaps
        mAllPagesCursorAdapter = new AwesomeBarCursorAdapter(
            mContext,
            R.layout.awesomebar_row,
            null,
            new String[] { URLColumns.TITLE,
                           URLColumns.URL,
                           URLColumns.FAVICON },
            new int[] { R.id.title, R.id.url, R.id.favicon }
        );

        mAllPagesCursorAdapter.setViewBinder(new AwesomeCursorViewBinder());

        mAllPagesCursorAdapter.setFilterQueryProvider(new FilterQueryProvider() {
            public Cursor runQuery(CharSequence constraint) {
                ContentResolver resolver = mContext.getContentResolver();
                long start = new Date().getTime();

                Cursor c = BrowserDB.filter(resolver, constraint, MAX_RESULTS);
                c.getCount(); // ensure the query runs at least once

                long end = new Date().getTime();
                Log.i(LOGTAG, "Got cursor in " + (end - start) + "ms");

                return c;
            }
        });

        final ListView allPagesList = (ListView) findViewById(R.id.all_pages_list);

        allPagesList.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                handleItemClick(allPagesList, position);
            }
        });

        allPagesList.setAdapter(mAllPagesCursorAdapter);
        allPagesList.setOnTouchListener(mListTouchListener);
    }

    private void addBookmarksTab() {
        Log.d(LOGTAG, "Creating Bookmarks tab");

        addAwesomeTab(BOOKMARKS_TAB,
                      R.string.awesomebar_bookmarks_title,
                      R.id.bookmarks_list);

        ListView bookmarksList = (ListView) findViewById(R.id.bookmarks_list);
        bookmarksList.setOnTouchListener(mListTouchListener);

        // Only load bookmark list when tab is actually used.
        // See OnTabChangeListener above.
    }

    private void addHistoryTab() {
        Log.d(LOGTAG, "Creating History tab");

        addAwesomeTab(HISTORY_TAB,
                      R.string.awesomebar_history_title,
                      R.id.history_list);

        ListView historyList = (ListView) findViewById(R.id.history_list);
        historyList.setOnTouchListener(mListTouchListener);

        // Only load history list when tab is actually used.
        // See OnTabChangeListener above.
    }

    private void hideSoftInput(View view) {
        InputMethodManager imm =
                (InputMethodManager) mContext.getSystemService(Context.INPUT_METHOD_SERVICE);

        imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
    }

    private void handleHistoryItemClick(int groupPosition, int childPosition) {
        @SuppressWarnings("unchecked")
        Map<String,Object> historyItem =
                (Map<String,Object>) mHistoryAdapter.getChild(groupPosition, childPosition);

        String url = (String) historyItem.get(URLColumns.URL);

        if (mUrlOpenListener != null)
            mUrlOpenListener.onUrlOpen(url);
    }

    private void handleItemClick(ListView list, int position) {
        Object item = list.getItemAtPosition(position);
        // If an AwesomeBar entry is clicked, item will be a Cursor containing
        // the entry's data.  Otherwise, a search engine entry was clicked, and
        // item will be a String containing the name of the search engine.
        if (item instanceof Cursor) {
            Cursor cursor = (Cursor) item;
            String url = cursor.getString(cursor.getColumnIndexOrThrow(URLColumns.URL));
            if (mUrlOpenListener != null)
                mUrlOpenListener.onUrlOpen(url);
        } else {
            if (mUrlOpenListener != null)
                mUrlOpenListener.onSearch((String)item);
        }
    }

    public void setOnUrlOpenListener(OnUrlOpenListener listener) {
        mUrlOpenListener = listener;
    }

    public void destroy() {
        Cursor allPagesCursor = mAllPagesCursorAdapter.getCursor();
        if (allPagesCursor != null)
            allPagesCursor.close();

        if (mBookmarksAdapter != null) {
            Cursor bookmarksCursor = mBookmarksAdapter.getCursor();
            if (bookmarksCursor != null)
                bookmarksCursor.close();
        }
    }

    public void filter(String searchTerm) {
        // Don't let the tab's content steal focus on tab switch
        setDescendantFocusability(ViewGroup.FOCUS_BLOCK_DESCENDANTS);

        // Ensure the 'All Pages' tab is selected
        setCurrentTabByTag(ALL_PAGES_TAB);

        // Restore normal focus behavior on tab host
        setDescendantFocusability(ViewGroup.FOCUS_AFTER_DESCENDANTS);

        // The tabs should only be visible if there's no on-going search
        int tabsVisibility = (searchTerm.length() == 0 ? View.VISIBLE : View.GONE);
        getTabWidget().setVisibility(tabsVisibility);

        // Perform the actual search
        mAllPagesCursorAdapter.filter(searchTerm);
    }

    public void setSearchEngines(JSONArray engines) {
        mSearchEngines = engines;
        GeckoAppShell.getMainHandler().post(new Runnable() {
            public void run() {
                mAllPagesCursorAdapter.notifyDataSetChanged();
            }
        });
    }
}

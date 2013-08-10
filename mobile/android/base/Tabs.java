/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import org.mozilla.gecko.db.BrowserDB;
import org.mozilla.gecko.util.GeckoEventListener;
import org.mozilla.gecko.sync.setup.SyncAccounts;

import org.json.JSONException;
import org.json.JSONObject;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.accounts.OnAccountsUpdateListener;
import android.content.ContentResolver;
import android.content.Intent;
import android.net.Uri;
import android.os.SystemClock;
import android.util.Log;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicInteger;

public class Tabs implements GeckoEventListener {
    private static final String LOGTAG = "GeckoTabs";

    // mOrder and mTabs are always of the same cardinality, and contain the same values.
    private final CopyOnWriteArrayList<Tab> mOrder = new CopyOnWriteArrayList<Tab>();

    // All writes to mSelectedTab must be synchronized on the Tabs instance.
    // In general, it's preferred to always use selectTab()).
    private volatile Tab mSelectedTab;

    // All accesses to mTabs must be synchronized on the Tabs instance.
    private final HashMap<Integer, Tab> mTabs = new HashMap<Integer, Tab>();

    // Keeps track of how much has happened since we last updated our persistent tab store.
    private volatile int mScore = 0;

    private AccountManager mAccountManager;
    private OnAccountsUpdateListener mAccountListener = null;

    public static final int LOADURL_NONE = 0;
    public static final int LOADURL_NEW_TAB = 1;
    public static final int LOADURL_USER_ENTERED = 2;
    public static final int LOADURL_PRIVATE = 4;
    public static final int LOADURL_PINNED = 8;
    public static final int LOADURL_DELAY_LOAD = 16;
    public static final int LOADURL_DESKTOP = 32;

    private static final int SCORE_INCREMENT_TAB_LOCATION_CHANGE = 5;
    private static final int SCORE_INCREMENT_TAB_SELECTED = 10;
    private static final int SCORE_THRESHOLD = 30;

    private static AtomicInteger sTabId = new AtomicInteger(0);
    private volatile boolean mInitialTabsAdded;

    private GeckoApp mActivity;

    private Tabs() {
        registerEventListener("SessionHistory:New");
        registerEventListener("SessionHistory:Back");
        registerEventListener("SessionHistory:Forward");
        registerEventListener("SessionHistory:Goto");
        registerEventListener("SessionHistory:Purge");
        registerEventListener("Tab:Added");
        registerEventListener("Tab:Close");
        registerEventListener("Tab:Select");
        registerEventListener("Content:LocationChange");
        registerEventListener("Session:RestoreEnd");
        registerEventListener("Reader:Added");
        registerEventListener("Reader:Removed");
        registerEventListener("Reader:Share");
    }

    public synchronized void attachToActivity(GeckoApp activity) {
        if (mActivity == activity) {
            return;
        }

        if (mActivity != null) {
            detachFromActivity(mActivity);
        }

        mActivity = activity;
        mAccountManager = AccountManager.get(mActivity);

        mAccountListener = new OnAccountsUpdateListener() {
            public void onAccountsUpdated(Account[] accounts) {
                persistAllTabs();
            }
        };

        // The listener will run on the background thread (see 2nd argument).
        mAccountManager.addOnAccountsUpdatedListener(mAccountListener, GeckoAppShell.getHandler(), false);
    }

    // Ideally, this would remove the reference to the activity once it's
    // detached; however, we have lifecycle issues with GeckoApp and Tabs that
    // requires us to keep it around (see
    // https://bugzilla.mozilla.org/show_bug.cgi?id=844407).
    public synchronized void detachFromActivity(GeckoApp activity) {
        if (mAccountListener != null) {
            mAccountManager.removeOnAccountsUpdatedListener(mAccountListener);
            mAccountListener = null;
        }
    }

    /**
     * Gets the tab count corresponding to the private state of the selected
     * tab.
     *
     * If the selected tab is a non-private tab, this will return the number of
     * non-private tabs; likewise, if this is a private tab, this will return
     * the number of private tabs.
     *
     * @return the number of tabs in the current private state
     */
    public synchronized int getDisplayCount() {
        // Once mSelectedTab is non-null, it cannot be null for the remainder
        // of the object's lifetime.
        boolean getPrivate = mSelectedTab != null && mSelectedTab.isPrivate();
        int count = 0;
        for (Tab tab : mOrder) {
            if (tab.isPrivate() == getPrivate) {
                count++;
            }
        }
        return count;
    }

    private Tab addTab(int id, String url, boolean external, int parentId, String title, boolean isPrivate) {
        final Tab tab = isPrivate ? new PrivateTab(id, url, external, parentId, title) :
                                    new Tab(id, url, external, parentId, title);
        synchronized (this) {
            mTabs.put(id, tab);
            mOrder.add(tab);
        }

        // Suppress the ADDED event to prevent animation of tabs created via session restore.
        if (mInitialTabsAdded) {
            notifyListeners(tab, TabEvents.ADDED);
        }

        return tab;
    }

    public synchronized void removeTab(int id) {
        if (mTabs.containsKey(id)) {
            Tab tab = getTab(id);
            mOrder.remove(tab);
            mTabs.remove(id);
        }
    }

    public synchronized Tab selectTab(int id) {
        if (!mTabs.containsKey(id))
            return null;

        final Tab oldTab = getSelectedTab();
        final Tab tab = mTabs.get(id);

        // This avoids a NPE below, but callers need to be careful to
        // handle this case.
        if (tab == null || oldTab == tab) {
            return null;
        }

        mSelectedTab = tab;
        mActivity.runOnUiThread(new Runnable() { 
            @Override
            public synchronized void run() {
                getActivity().hideFormAssistPopup();
                if (isSelectedTab(tab)) {
                    notifyListeners(tab, TabEvents.SELECTED);
                }
            }
        });

        if (oldTab != null) {
            notifyListeners(oldTab, TabEvents.UNSELECTED);
        }

        // Pass a message to Gecko to update tab state in BrowserApp.
        GeckoAppShell.sendEventToGecko(GeckoEvent.createBroadcastEvent("Tab:Selected", String.valueOf(tab.getId())));
        return tab;
    }

    private int getIndexOf(Tab tab) {
        return mOrder.lastIndexOf(tab);
    }

    private Tab getNextTabFrom(Tab tab, boolean getPrivate) {
        int numTabs = mOrder.size();
        int index = getIndexOf(tab);
        for (int i = index + 1; i < numTabs; i++) {
            Tab next = mOrder.get(i);
            if (next.isPrivate() == getPrivate) {
                return next;
            }
        }
        return null;
    }

    private Tab getPreviousTabFrom(Tab tab, boolean getPrivate) {
        int numTabs = mOrder.size();
        int index = getIndexOf(tab);
        for (int i = index - 1; i >= 0; i--) {
            Tab prev = mOrder.get(i);
            if (prev.isPrivate() == getPrivate) {
                return prev;
            }
        }
        return null;
    }

    /**
     * Gets the selected tab.
     *
     * The selected tab can be null if we're doing a session restore after a
     * crash and Gecko isn't ready yet.
     *
     * @return the selected tab, or null if no tabs exist
     */
    public Tab getSelectedTab() {
        return mSelectedTab;
    }

    public boolean isSelectedTab(Tab tab) {
        return tab != null && tab == mSelectedTab;
    }

    public synchronized Tab getTab(int id) {
        if (mTabs.size() == 0)
            return null;

        if (!mTabs.containsKey(id))
           return null;

        return mTabs.get(id);
    }

    /** Close tab and then select the default next tab */
    public synchronized void closeTab(Tab tab) {
        closeTab(tab, getNextTab(tab));
    }

    /** Close tab and then select nextTab */
    public synchronized void closeTab(final Tab tab, Tab nextTab) {
        if (tab == null)
            return;

        if (nextTab == null)
            nextTab = loadUrl("about:home", LOADURL_NEW_TAB);

        selectTab(nextTab.getId());

        int tabId = tab.getId();
        removeTab(tabId);

        tab.onDestroy();

        // Pass a message to Gecko to update tab state in BrowserApp
        GeckoAppShell.sendEventToGecko(GeckoEvent.createBroadcastEvent("Tab:Closed", String.valueOf(tabId)));
    }

    /** Return the tab that will be selected by default after this one is closed */
    public Tab getNextTab(Tab tab) {
        Tab selectedTab = getSelectedTab();
        if (selectedTab != tab)
            return selectedTab;

        boolean getPrivate = tab.isPrivate();
        Tab nextTab = getNextTabFrom(tab, getPrivate);
        if (nextTab == null)
            nextTab = getPreviousTabFrom(tab, getPrivate);
        if (nextTab == null && getPrivate) {
            // If there are no private tabs remaining, get the last normal tab
            Tab lastTab = mOrder.get(mOrder.size() - 1);
            nextTab = getPreviousTabFrom(lastTab, false);
        }

        Tab parent = getTab(tab.getParentId());
        if (parent != null) {
            // If the next tab is a sibling, switch to it. Otherwise go back to the parent.
            if (nextTab != null && nextTab.getParentId() == tab.getParentId())
                return nextTab;
            else
                return parent;
        }
        return nextTab;
    }

    public Iterable<Tab> getTabsInOrder() {
        return mOrder;
    }

    /**
     * @return the current GeckoApp instance, or throws if
     *         we aren't correctly initialized.
     */
    private synchronized GeckoApp getActivity() {
        if (mActivity == null) {
            throw new IllegalStateException("Tabs not initialized with a GeckoApp instance.");
        }
        return mActivity;
    }

    public ContentResolver getContentResolver() {
        return getActivity().getContentResolver();
    }

    // Make Tabs a singleton class.
    private static class TabsInstanceHolder {
        private static final Tabs INSTANCE = new Tabs();
    }

    public static Tabs getInstance() {
       return Tabs.TabsInstanceHolder.INSTANCE;
    }

    // GeckoEventListener implementation

    public void handleMessage(String event, JSONObject message) {
        try {
            if (event.startsWith("SessionHistory:")) {
                Tab tab = getTab(message.getInt("tabID"));
                if (tab != null) {
                    event = event.substring("SessionHistory:".length());
                    tab.handleSessionHistoryMessage(event, message);
                }
            } else if (event.equals("Tab:Added")) {
                String url = message.isNull("uri") ? null : message.getString("uri");
                int id = message.getInt("tabID");
                Tab tab = null;

                if (message.getBoolean("stub")) {
                    if (mTabs.containsKey(id)) {
                        tab = mTabs.get(id);
                        tab.updateURL(url);
                    } else {
                        // Tab was already closed; abort
                        return;
                    }
                } else {
                    tab = addTab(id, url, message.getBoolean("external"),
                                          message.getInt("parentId"),
                                          message.getString("title"),
                                          message.getBoolean("isPrivate"));
                }

                if (message.getBoolean("selected"))
                    selectTab(tab.getId());
                if (message.getBoolean("delayLoad"))
                    tab.setState(Tab.STATE_DELAYED);
                if (message.getBoolean("desktopMode"))
                    tab.setDesktopMode(true);
            } else if (event.equals("Tab:Close")) {
                Tab tab = getTab(message.getInt("tabID"));
                closeTab(tab);
            } else if (event.equals("Tab:Select")) {
                selectTab(message.getInt("tabID"));
            } else if (event.equals("Content:LocationChange")) {
                Tab tab = getTab(message.getInt("tabID"));
                if (tab != null) {
                    tab.handleLocationChange(message);
                }
            } else if (event.equals("Session:RestoreEnd")) {
                notifyListeners(null, TabEvents.RESTORED);
            } else if (event.equals("Reader:Added")) {
                final boolean success = message.getBoolean("success");
                final String title = message.getString("title");
                final String url = message.getString("url");
                handleReaderAdded(success, title, url);
            } else if (event.equals("Reader:Removed")) {
                final String url = message.getString("url");
                handleReaderRemoved(url);
            } else if (event.equals("Reader:Share")) {
                final String title = message.getString("title");
                final String url = message.getString("url");

                GeckoAppShell.openUriExternal(url, "text/plain", "", "",
                                              Intent.ACTION_SEND, title);
            }
        } catch (Exception e) { 
            Log.w(LOGTAG, "handleMessage threw for " + event, e);
        }
    }

    void handleReaderAdded(boolean success, final String title, final String url) {
        if (!success) {
            mActivity.showToast(R.string.reading_list_failed, Toast.LENGTH_SHORT);
            return;
        }

        GeckoAppShell.getHandler().post(new Runnable() {
            public void run() {
                BrowserDB.addReadingListItem(getContentResolver(), title, url);
                mActivity.showToast(R.string.reading_list_added, Toast.LENGTH_SHORT);
            }
        });
    }

    void handleReaderRemoved(final String url) {
        GeckoAppShell.getHandler().post(new Runnable() {
            public void run() {
                BrowserDB.removeReadingListItemWithURL(getContentResolver(), url);
                mActivity.showToast(R.string.reading_list_removed, Toast.LENGTH_SHORT);
            }
        });
    }

    public void refreshThumbnails() {
        final ThumbnailHelper helper = ThumbnailHelper.getInstance();
        GeckoAppShell.getHandler().post(new Runnable() {
            @Override
            public void run() {
                for (final Tab tab : mOrder) {
                    helper.getAndProcessThumbnailFor(tab);
                }
            }
        });
    }

    public interface OnTabsChangedListener {
        public void onTabChanged(Tab tab, TabEvents msg, Object data);
    }

    private static List<OnTabsChangedListener> mTabsChangedListeners =
        Collections.synchronizedList(new ArrayList<OnTabsChangedListener>());

    public static void registerOnTabsChangedListener(OnTabsChangedListener listener) {
        mTabsChangedListeners.add(listener);
    }

    public static void unregisterOnTabsChangedListener(OnTabsChangedListener listener) {
        mTabsChangedListeners.remove(listener);
    }

    public enum TabEvents {
        CLOSED,
        START,
        LOADED,
        LOAD_ERROR,
        STOP,
        FAVICON,
        THUMBNAIL,
        TITLE,
        SELECTED,
        UNSELECTED,
        ADDED,
        RESTORED,
        LOCATION_CHANGE,
        MENU_UPDATED
    }

    public void notifyListeners(Tab tab, TabEvents msg) {
        notifyListeners(tab, msg, "");
    }

    // Throws if not initialized.
    public void notifyListeners(final Tab tab, final TabEvents msg, final Object data) {
        getActivity().runOnUiThread(new Runnable() {
            public void run() {
                onTabChanged(tab, msg, data);

                synchronized (mTabsChangedListeners) {
                    if (mTabsChangedListeners.isEmpty()) {
                        return;
                    }

                    Iterator<OnTabsChangedListener> items = mTabsChangedListeners.iterator();
                    while (items.hasNext()) {
                        items.next().onTabChanged(tab, msg, data);
                    }
                }
            }
        });
    }

    private void onTabChanged(Tab tab, Tabs.TabEvents msg, Object data) {
        switch (msg) {
            case LOCATION_CHANGE:
                mScore += SCORE_INCREMENT_TAB_LOCATION_CHANGE;
                break;
            case RESTORED:
                mInitialTabsAdded = true;
                break;

            // When one tab is deselected, another one is always selected, so only
            // increment the score once. When tabs are added/closed, they are also
            // selected/unselected, so it would be redundant to also listen
            // for ADDED/CLOSED events.
            case SELECTED:
                mScore += SCORE_INCREMENT_TAB_SELECTED;
            case UNSELECTED:
                tab.onChange();
                break;
            default:
                break;
        }

        if (mScore > SCORE_THRESHOLD) {
            persistAllTabs();
            mScore = 0;
        }
    }

    // This method persists the current ordered list of tabs in our tabs content provider.
    public void persistAllTabs() {
        final GeckoApp activity = getActivity();
        final Iterable<Tab> tabs = getTabsInOrder();
        GeckoAppShell.getHandler().post(new Runnable() {
            public void run() {
                boolean syncIsSetup = SyncAccounts.syncAccountsExist(activity);
                if (syncIsSetup) {
                    TabsAccessor.persistLocalTabs(getContentResolver(), tabs);
                }
            }
        });
    }

    private void registerEventListener(String event) {
        GeckoAppShell.getEventDispatcher().registerEventListener(event, this);
    }

    /**
     * Loads a tab with the given URL in the currently selected tab.
     *
     * @param url URL of page to load, or search term used if searchEngine is given
     */
    public void loadUrl(String url) {
        loadUrl(url, LOADURL_NONE);
    }

    /**
     * Loads a tab with the given URL.
     *
     * @param url   URL of page to load, or search term used if searchEngine is given
     * @param flags flags used to load tab
     *
     * @return      the Tab if a new one was created; null otherwise
     */
    public Tab loadUrl(String url, int flags) {
        return loadUrl(url, null, -1, flags);
    }

    /**
     * Loads a tab with the given URL.
     *
     * @param url          URL of page to load, or search term used if searchEngine is given
     * @param searchEngine if given, the search engine with this name is used
     *                     to search for the url string; if null, the URL is loaded directly
     * @param parentId     ID of this tab's parent, or -1 if it has no parent
     * @param flags        flags used to load tab
     *
     * @return             the Tab if a new one was created; null otherwise
     */
    public Tab loadUrl(String url, String searchEngine, int parentId, int flags) {
        JSONObject args = new JSONObject();
        Tab added = null;
        boolean delayLoad = (flags & LOADURL_DELAY_LOAD) != 0;

        try {
            boolean isPrivate = (flags & LOADURL_PRIVATE) != 0;
            boolean userEntered = (flags & LOADURL_USER_ENTERED) != 0;
            boolean desktopMode = (flags & LOADURL_DESKTOP) != 0;

            args.put("url", url);
            args.put("engine", searchEngine);
            args.put("parentId", parentId);
            args.put("userEntered", userEntered);
            args.put("newTab", (flags & LOADURL_NEW_TAB) != 0);
            args.put("isPrivate", isPrivate);
            args.put("pinned", (flags & LOADURL_PINNED) != 0);
            args.put("delayLoad", delayLoad);
            args.put("desktopMode", desktopMode);

            if ((flags & LOADURL_NEW_TAB) != 0) {
                int tabId = getNextTabId();
                args.put("tabID", tabId);

                // The URL is updated for the tab once Gecko responds with the
                // Tab:Added message. We can preliminarily set the tab's URL as
                // long as it's a valid URI.
                String tabUrl = (url != null && Uri.parse(url).getScheme() != null) ? url : null;

                added = addTab(tabId, tabUrl, false, parentId, url, isPrivate);
                added.setDesktopMode(desktopMode);
            }
        } catch (Exception e) {
            Log.w(LOGTAG, "Error building JSON arguments for loadUrl.", e);
        }

        GeckoAppShell.sendEventToGecko(GeckoEvent.createBroadcastEvent("Tab:Load", args.toString()));

        if ((added != null) && !delayLoad) {
            selectTab(added.getId());
        }

        return added;
    }

    /**
     * Open the url as a new tab, and mark the selected tab as its "parent".
     *
     * If the url is already open in a tab, the existing tab is selected.
     * Use this for tabs opened by the browser chrome, so users can press the
     * "Back" button to return to the previous tab.
     *
     * @param url URL of page to load
     */
    public void loadUrlInTab(String url) {
        Iterable<Tab> tabs = getTabsInOrder();
        for (Tab tab : tabs) {
            if (url.equals(tab.getURL())) {
                selectTab(tab.getId());
                return;
            }
        }

        // getSelectedTab() can return null if no tab has been created yet
        // (i.e., we're restoring a session after a crash). In these cases,
        // don't mark any tabs as a parent.
        int parentId = -1;
        Tab selectedTab = getSelectedTab();
        if (selectedTab != null) {
            parentId = selectedTab.getId();
        }

        loadUrl(url, null, parentId, LOADURL_NEW_TAB);
    }

    /**
     * Gets the next tab ID.
     *
     * This method is invoked via JNI.
     */
    public static int getNextTabId() {
        return sTabId.getAndIncrement();
    }
}

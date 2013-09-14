/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

XPCOMUtils.defineLazyGetter(this, "docShell", () => {
  return window.QueryInterface(Ci.nsIInterfaceRequestor)
               .getInterface(Ci.nsIWebNavigation)
               .QueryInterface(Ci.nsIDocShell);
});

const EXPECTED_REFLOWS = [
  // tabbrowser.adjustTabstrip() call after tabopen animation has finished
  "adjustTabstrip@chrome://browser/content/tabbrowser.xml|" +
    "_handleNewTab@chrome://browser/content/tabbrowser.xml|" +
    "onxbltransitionend@chrome://browser/content/tabbrowser.xml|",

  // switching focus in updateCurrentBrowser() causes reflows
  "updateCurrentBrowser@chrome://browser/content/tabbrowser.xml|" +
    "onselect@chrome://browser/content/browser.xul|",

  // switching focus in openLinkIn() causes reflows
  "openLinkIn@chrome://browser/content/utilityOverlay.js|" +
    "openUILinkIn@chrome://browser/content/utilityOverlay.js|" +
    "BrowserOpenTab@chrome://browser/content/browser.js|",

  // accessing element.scrollPosition in _fillTrailingGap() flushes layout
  "get_scrollPosition@chrome://global/content/bindings/scrollbox.xml|" +
    "_fillTrailingGap@chrome://browser/content/tabbrowser.xml|" +
    "_handleNewTab@chrome://browser/content/tabbrowser.xml|" +
    "onxbltransitionend@chrome://browser/content/tabbrowser.xml|",

  // The TabView iframe causes reflows in the parent document.
  "iQClass_height@chrome://browser/content/tabview.js|" +
    "GroupItem_getContentBounds@chrome://browser/content/tabview.js|" +
    "GroupItem_shouldStack@chrome://browser/content/tabview.js|" +
    "GroupItem_arrange@chrome://browser/content/tabview.js|" +
    "GroupItem_add@chrome://browser/content/tabview.js|" +
    "GroupItems_newTab@chrome://browser/content/tabview.js|" +
    "TabItem__reconnect@chrome://browser/content/tabview.js|" +
    "TabItem@chrome://browser/content/tabview.js|" +
    "TabItems_link@chrome://browser/content/tabview.js|" +
    "@chrome://browser/content/tabview.js|" +
    "addTab@chrome://browser/content/tabbrowser.xml|",

  // SessionStore.getWindowDimensions()
  "ssi_getWindowDimension@resource:///modules/sessionstore/SessionStore.jsm|" +
    "@resource:///modules/sessionstore/SessionStore.jsm|" +
    "ssi_updateWindowFeatures@resource:///modules/sessionstore/SessionStore.jsm|" +
    "ssi_collectWindowData@resource:///modules/sessionstore/SessionStore.jsm|" +
    "@resource:///modules/sessionstore/SessionStore.jsm|" +
    "ssi_forEachBrowserWindow@resource:///modules/sessionstore/SessionStore.jsm|" +
    "ssi_getCurrentState@resource:///modules/sessionstore/SessionStore.jsm|" +
    "ssi_saveState@resource:///modules/sessionstore/SessionStore.jsm|" +
    "ssi_onTimerCallback@resource:///modules/sessionstore/SessionStore.jsm|" +
    "ssi_observe@resource:///modules/sessionstore/SessionStore.jsm|",

  // tabPreviews.capture()
  "tabPreviews_capture@chrome://browser/content/browser.js|" +
    "tabPreviews_handleEvent/<@chrome://browser/content/browser.js|"
];

const PREF_PRELOAD = "browser.newtab.preload";

/*
 * This test ensures that there are no unexpected
 * uninterruptible reflows when opening new tabs.
 */
function test() {
  waitForExplicitFinish();

  Services.prefs.setBoolPref(PREF_PRELOAD, false);
  registerCleanupFunction(() => Services.prefs.clearUserPref(PREF_PRELOAD));

  // Add a reflow observer and open a new tab.
  docShell.addWeakReflowObserver(observer);
  BrowserOpenTab();

  // Wait until the tabopen animation has finished.
  waitForTransitionEnd(function () {
    // Remove reflow observer and clean up.
    docShell.removeWeakReflowObserver(observer);
    gBrowser.removeCurrentTab();

    finish();
  });
}

let observer = {
  reflow: function (start, end) {
    // Gather information about the current code path.
    let path = (new Error().stack).split("\n").slice(1).map(line => {
      return line.replace(/:\d+$/, "");
    }).join("|");

    // Stack trace is empty. Reflow was triggered by native code.
    if (path === "") {
      return;
    }

    // Check if this is an expected reflow.
    for (let stack of EXPECTED_REFLOWS) {
      if (path.startsWith(stack)) {
        ok(true, "expected uninterruptible reflow '" + stack + "'");
        return;
      }
    }

    ok(false, "unexpected uninterruptible reflow '" + path + "'");
  },

  reflowInterruptible: function (start, end) {
    // We're not interested in interruptible reflows.
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIReflowObserver,
                                         Ci.nsISupportsWeakReference])
};

function waitForTransitionEnd(callback) {
  let tab = gBrowser.selectedTab;
  tab.addEventListener("transitionend", function onEnd(event) {
    if (event.propertyName === "max-width") {
      tab.removeEventListener("transitionend", onEnd);
      executeSoon(callback);
    }
  });
}

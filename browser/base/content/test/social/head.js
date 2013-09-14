/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "Promise",
  "resource://gre/modules/commonjs/sdk/core/promise.js");
XPCOMUtils.defineLazyModuleGetter(this, "Task",
  "resource://gre/modules/Task.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PlacesUtils",
  "resource://gre/modules/PlacesUtils.jsm");

function waitForCondition(condition, nextTest, errorMsg) {
  var tries = 0;
  var interval = setInterval(function() {
    if (tries >= 30) {
      ok(false, errorMsg);
      moveOn();
    }
    if (condition()) {
      moveOn();
    }
    tries++;
  }, 100);
  var moveOn = function() { clearInterval(interval); nextTest(); };
}

// Check that a specified (string) URL hasn't been "remembered" (ie, is not
// in history, will not appear in about:newtab or auto-complete, etc.)
function promiseSocialUrlNotRemembered(url) {
  let deferred = Promise.defer();
  let uri = Services.io.newURI(url, null, null);
  PlacesUtils.asyncHistory.isURIVisited(uri, function(aURI, aIsVisited) {
    ok(!aIsVisited, "social URL " + url + " should not be in global history");
    deferred.resolve();
  });
  return deferred.promise;
}

let gURLsNotRemembered = [];


function checkProviderPrefsEmpty(isError) {
  let MANIFEST_PREFS = Services.prefs.getBranch("social.manifest.");
  let prefs = MANIFEST_PREFS.getChildList("", []);
  let c = 0;
  for (let pref of prefs) {
    if (MANIFEST_PREFS.prefHasUserValue(pref)) {
      info("provider [" + pref + "] manifest left installed from previous test");
      c++;
    }
  }
  is(c, 0, "all provider prefs uninstalled from previous test");
  is(Social.providers.length, 0, "all providers uninstalled from previous test " + Social.providers.length);
}

function defaultFinishChecks() {
  checkProviderPrefsEmpty(true);
  finish();
}

function runSocialTestWithProvider(manifest, callback, finishcallback) {
  let SocialService = Cu.import("resource://gre/modules/SocialService.jsm", {}).SocialService;

  let manifests = Array.isArray(manifest) ? manifest : [manifest];

  // Check that none of the provider's content ends up in history.
  function finishCleanUp() {
    for (let i = 0; i < manifests.length; i++) {
      let m = manifests[i];
      for (let what of ['sidebarURL', 'workerURL', 'iconURL']) {
        if (m[what]) {
          yield promiseSocialUrlNotRemembered(m[what]);
        }
      };
    }
    for (let i = 0; i < gURLsNotRemembered.length; i++) {
      yield promiseSocialUrlNotRemembered(gURLsNotRemembered[i]);
    }
    gURLsNotRemembered = [];
  }

  info("runSocialTestWithProvider: " + manifests.toSource());

  let finishCount = 0;
  function finishIfDone(callFinish) {
    finishCount++;
    if (finishCount == manifests.length)
      Task.spawn(finishCleanUp).then(finishcallback || defaultFinishChecks);
  }
  function removeAddedProviders(cleanup) {
    manifests.forEach(function (m) {
      // If we're "cleaning up", don't call finish when done.
      let callback = cleanup ? function () {} : finishIfDone;
      // Similarly, if we're cleaning up, catch exceptions from removeProvider
      let removeProvider = SocialService.removeProvider.bind(SocialService);
      if (cleanup) {
        removeProvider = function (origin, cb) {
          try {
            SocialService.removeProvider(origin, cb);
          } catch (ex) {
            // Ignore "provider doesn't exist" errors.
            if (ex.message.indexOf("SocialService.removeProvider: no provider with origin") == 0)
              return;
            info("Failed to clean up provider " + origin + ": " + ex);
          }
        }
      }
      removeProvider(m.origin, callback);
    });
  }
  function finishSocialTest(cleanup) {
    // disable social before removing the providers to avoid providers
    // being activated immediately before we get around to removing it.
    Services.prefs.clearUserPref("social.enabled");
    removeAddedProviders(cleanup);
  }

  let providersAdded = 0;
  let firstProvider;

  manifests.forEach(function (m) {
    SocialService.addProvider(m, function(provider) {

      providersAdded++;
      info("runSocialTestWithProvider: provider added");

      // we want to set the first specified provider as the UI's provider
      if (provider.origin == manifests[0].origin) {
        firstProvider = provider;
      }

      // If we've added all the providers we need, call the callback to start
      // the tests (and give it a callback it can call to finish them)
      if (providersAdded == manifests.length) {
        // Set the UI's provider (which enables the feature)
        Social.provider = firstProvider;

        registerCleanupFunction(function () {
          finishSocialTest(true);
        });
        callback(finishSocialTest);
      }
    });
  });
}

function runSocialTests(tests, cbPreTest, cbPostTest, cbFinish) {
  let testIter = Iterator(tests);
  let providersAtStart = Social.providers.length;
  info("runSocialTests: start test run with " + providersAtStart + " providers");

  if (cbPreTest === undefined) {
    cbPreTest = function(cb) {cb()};
  }
  if (cbPostTest === undefined) {
    cbPostTest = function(cb) {cb()};
  }

  function runNextTest() {
    let name, func;
    try {
      [name, func] = testIter.next();
    } catch (err if err instanceof StopIteration) {
      // out of items:
      (cbFinish || defaultFinishChecks)();
      is(providersAtStart, Social.providers.length,
         "runSocialTests: finish test run with " + Social.providers.length + " providers");
      return;
    }
    // We run on a timeout as the frameworker also makes use of timeouts, so
    // this helps keep the debug messages sane.
    executeSoon(function() {
      function cleanupAndRunNextTest() {
        info("sub-test " + name + " complete");
        cbPostTest(runNextTest);
      }
      cbPreTest(function() {
        is(providersAtStart, Social.providers.length, "pre-test: no new providers left enabled");
        info("sub-test " + name + " starting");
        try {
          func.call(tests, cleanupAndRunNextTest);
        } catch (ex) {
          ok(false, "sub-test " + name + " failed: " + ex.toString() +"\n"+ex.stack);
          cleanupAndRunNextTest();
        }
      })
    });
  }
  runNextTest();
}

// A fairly large hammer which checks all aspects of the SocialUI for
// internal consistency.
function checkSocialUI(win) {
  win = win || window;
  let doc = win.document;
  let provider = Social.provider;
  let enabled = win.SocialUI.enabled;
  let active = Social.providers.length > 0 && !win.SocialUI._chromeless &&
               !PrivateBrowsingUtils.isWindowPrivate(win);

  function isbool(a, b, msg) {
    is(!!a, !!b, msg);
  }
  isbool(win.SocialSidebar.canShow, enabled, "social sidebar active?");
  if (enabled)
    isbool(win.SocialSidebar.opened, enabled, "social sidebar open?");
  isbool(win.SocialChatBar.isAvailable, enabled && Social.haveLoggedInUser(), "chatbar available?");
  isbool(!win.SocialChatBar.chatbar.hidden, enabled && Social.haveLoggedInUser(), "chatbar visible?");

  let markVisible = enabled && provider.pageMarkInfo;
  let canMark = markVisible && win.SocialMark.canMarkPage(win.gBrowser.currentURI);
  isbool(!win.SocialMark.button.hidden, markVisible, "SocialMark button visible?");
  isbool(!win.SocialMark.button.disabled, canMark, "SocialMark button enabled?");
  isbool(!doc.getElementById("social-toolbar-item").hidden, active, "toolbar items visible?");
  if (active) {
    if (!enabled) {
      ok(!win.SocialToolbar.button.style.listStyleImage, "toolbar button is default icon");
    } else {
      is(win.SocialToolbar.button.style.listStyleImage, 'url("' + Social.defaultProvider.iconURL + '")', "toolbar button has provider icon");
    }
  }
  // the menus should always have the provider name
  if (provider) {
    for (let id of ["menu_socialSidebar", "menu_socialAmbientMenu"])
      is(document.getElementById(id).getAttribute("label"), Social.provider.name, "element has the provider name");
  }

  // and for good measure, check all the social commands.
  isbool(!doc.getElementById("Social:Toggle").hidden, active, "Social:Toggle visible?");
  isbool(!doc.getElementById("Social:ToggleNotifications").hidden, enabled, "Social:ToggleNotifications visible?");
  isbool(!doc.getElementById("Social:FocusChat").hidden, enabled && Social.haveLoggedInUser(), "Social:FocusChat visible?");
  isbool(doc.getElementById("Social:FocusChat").getAttribute("disabled"), enabled ? "false" : "true", "Social:FocusChat disabled?");
  is(doc.getElementById("Social:TogglePageMark").getAttribute("disabled"), canMark ? "false" : "true", "Social:TogglePageMark enabled?");

  // broadcasters.
  isbool(!doc.getElementById("socialActiveBroadcaster").hidden, active, "socialActiveBroadcaster hidden?");
}

// blocklist testing
function updateBlocklist(aCallback) {
  var blocklistNotifier = Cc["@mozilla.org/extensions/blocklist;1"]
                          .getService(Ci.nsITimerCallback);
  var observer = function() {
    Services.obs.removeObserver(observer, "blocklist-updated");
    if (aCallback)
      executeSoon(aCallback);
  };
  Services.obs.addObserver(observer, "blocklist-updated", false);
  blocklistNotifier.notify(null);
}

function setAndUpdateBlocklist(aURL, aCallback) {
  Services.prefs.setCharPref("extensions.blocklist.url", aURL);
  updateBlocklist(aCallback);
}

function resetBlocklist(aCallback) {
  Services.prefs.clearUserPref("extensions.blocklist.url");
  updateBlocklist(aCallback);
}

function setManifestPref(name, manifest) {
  let string = Cc["@mozilla.org/supports-string;1"].
               createInstance(Ci.nsISupportsString);
  string.data = JSON.stringify(manifest);
  Services.prefs.setComplexValue(name, Ci.nsISupportsString, string);
}

function getManifestPrefname(aManifest) {
  // is same as the generated name in SocialServiceInternal.getManifestPrefname
  let originUri = Services.io.newURI(aManifest.origin, null, null);
  return "social.manifest." + originUri.hostPort.replace('.','-');
}

function setBuiltinManifestPref(name, manifest) {
  // we set this as a default pref, it must not be a user pref
  manifest.builtin = true;
  let string = Cc["@mozilla.org/supports-string;1"].
               createInstance(Ci.nsISupportsString);
  string.data = JSON.stringify(manifest);
  Services.prefs.getDefaultBranch(null).setComplexValue(name, Ci.nsISupportsString, string);
  // verify this is set on the default branch
  let stored = Services.prefs.getComplexValue(name, Ci.nsISupportsString).data;
  is(stored, string.data, "manifest '"+name+"' stored in default prefs");
  // don't dirty our manifest, we'll need it without this flag later
  delete manifest.builtin;
  // verify we DO NOT have a user-level pref
  ok(!Services.prefs.prefHasUserValue(name), "manifest '"+name+"' is not in user-prefs");
}

function resetBuiltinManifestPref(name) {
  Services.prefs.getDefaultBranch(null).deleteBranch(name);
  is(Services.prefs.getDefaultBranch(null).getPrefType(name),
     Services.prefs.PREF_INVALID, "default manifest removed");
}

function addTab(url, callback) {
  let tab = gBrowser.selectedTab = gBrowser.addTab(url, {skipAnimation: true});
  tab.linkedBrowser.addEventListener("load", function tabLoad(event) {
    tab.linkedBrowser.removeEventListener("load", tabLoad, true);
    executeSoon(function() {callback(tab)});
  }, true);
}

function selectBrowserTab(tab, callback) {
  if (gBrowser.selectedTab == tab) {
    executeSoon(function() {callback(tab)});
    return;
  }
  gBrowser.tabContainer.addEventListener("TabSelect", function onTabSelect() {
    gBrowser.tabContainer.removeEventListener("TabSelect", onTabSelect, false);
    is(gBrowser.selectedTab, tab, "browser tab is selected");
    executeSoon(function() {callback(tab)});
  });
  gBrowser.selectedTab = tab;
}

function loadIntoTab(tab, url, callback) {
  tab.linkedBrowser.addEventListener("load", function tabLoad(event) {
    tab.linkedBrowser.removeEventListener("load", tabLoad, true);
    executeSoon(function() {callback(tab)});
  }, true);
  tab.linkedBrowser.loadURI(url);
}


// chat test help functions

// And lots of helpers for the resize tests.
function get3ChatsForCollapsing(mode, cb) {
  // We make one chat, then measure its size.  We then resize the browser to
  // ensure a second can be created fully visible but a third can not - then
  // create the other 2.  first will will be collapsed, second fully visible
  // and the third also visible and the "selected" one.
  // To make our life easier we don't go via the worker and ports so we get
  // more control over creation *and* to make the code much simpler.  We
  // assume the worker/port stuff is individually tested above.
  let chatbar = window.SocialChatBar.chatbar;
  let chatWidth = undefined;
  let num = 0;
  is(chatbar.childNodes.length, 0, "chatbar starting empty");
  is(chatbar.menupopup.childNodes.length, 0, "popup starting empty");

  makeChat(mode, "first chat", function() {
    // got the first one.
    checkPopup();
    ok(chatbar.menupopup.parentNode.collapsed, "menu selection isn't visible");
    // we kinda cheat here and get the width of the first chat, assuming
    // that all future chats will have the same width when open.
    chatWidth = chatbar.calcTotalWidthOf(chatbar.selectedChat);
    let desired = chatWidth * 2.5;
    resizeWindowToChatAreaWidth(desired, function(sizedOk) {
      ok(sizedOk, "can't do any tests without this width");
      checkPopup();
      makeChat(mode, "second chat", function() {
        is(chatbar.childNodes.length, 2, "now have 2 chats");
        checkPopup();
        // and create the third.
        makeChat(mode, "third chat", function() {
          is(chatbar.childNodes.length, 3, "now have 3 chats");
          checkPopup();
          // XXX - this is a hacky implementation detail around the order of
          // the chats.  Ideally things would be a little more sane wrt the
          // other in which the children were created.
          let second = chatbar.childNodes[2];
          let first = chatbar.childNodes[1];
          let third = chatbar.childNodes[0];
          ok(first.collapsed && !second.collapsed && !third.collapsed, "collapsed state as promised");
          is(chatbar.selectedChat, third, "third is selected as promised")
          info("have 3 chats for collapse testing - starting actual test...");
          cb(first, second, third);
        }, mode);
      }, mode);
    });
  }, mode);
}

function makeChat(mode, uniqueid, cb) {
  info("making a chat window '" + uniqueid +"'");
  const chatUrl = "https://example.com/browser/browser/base/content/test/social/social_chat.html";
  let provider = Social.provider;
  let isOpened = window.SocialChatBar.openChat(provider, chatUrl + "?id=" + uniqueid, function(chat) {
    info("chat window has opened");
    // we can't callback immediately or we might close the chat during
    // this event which upsets the implementation - it is only 1/2 way through
    // handling the load event.
    chat.document.title = uniqueid;
    executeSoon(cb);
  }, mode);
  if (!isOpened) {
    ok(false, "unable to open chat window, no provider? more failures to come");
    executeSoon(cb);
  }
}

function checkPopup() {
  // popup only showing if any collapsed popup children.
  let chatbar = window.SocialChatBar.chatbar;
  let numCollapsed = 0;
  for (let chat of chatbar.childNodes) {
    if (chat.collapsed) {
      numCollapsed += 1;
      // and it have a menuitem weakmap
      is(chatbar.menuitemMap.get(chat).nodeName, "menuitem", "collapsed chat has a menu item");
    } else {
      ok(!chatbar.menuitemMap.has(chat), "open chat has no menu item");
    }
  }
  is(chatbar.menupopup.parentNode.collapsed, numCollapsed == 0, "popup matches child collapsed state");
  is(chatbar.menupopup.childNodes.length, numCollapsed, "popup has correct count of children");
  // todo - check each individual elt is what we expect?
}
// Resize the main window so the chat area's boxObject is |desired| wide.
// Does a callback passing |true| if the window is now big enough or false
// if we couldn't resize large enough to satisfy the test requirement.
function resizeWindowToChatAreaWidth(desired, cb, count = 0) {
  let current = window.SocialChatBar.chatbar.getBoundingClientRect().width;
  let delta = desired - current;
  info(count + ": resizing window so chat area is " + desired + " wide, currently it is "
       + current + ".  Screen avail is " + window.screen.availWidth
       + ", current outer width is " + window.outerWidth);

  // WTF?  Sometimes we will get fractional values due to the - err - magic
  // of DevPointsPerCSSPixel etc, so we allow a couple of pixels difference.
  let widthDeltaCloseEnough = function(d) {
    return Math.abs(d) < 2;
  }

  // attempting to resize by (0,0), unsurprisingly, doesn't cause a resize
  // event - so just callback saying all is well.
  if (widthDeltaCloseEnough(delta)) {
    info(count + ": skipping this as screen width is close enough");
    executeSoon(function() {
      cb(true);
    });
    return;
  }
  // On lo-res screens we may already be maxed out but still smaller than the
  // requested size, so asking to resize up also will not cause a resize event.
  // So just callback now saying the test must be skipped.
  if (window.screen.availWidth - window.outerWidth < delta) {
    info(count + ": skipping this as screen available width is less than necessary");
    executeSoon(function() {
      cb(false);
    });
    return;
  }
  function resize_handler(event) {
    // for whatever reason, sometimes we get called twice for different event
    // phases, only handle one of them.
    if (event.eventPhase != event.AT_TARGET)
      return;
    // we did resize - but did we get far enough to be able to continue?
    let newSize = window.SocialChatBar.chatbar.getBoundingClientRect().width;
    let sizedOk = widthDeltaCloseEnough(newSize - desired);
    if (!sizedOk)
      return;
    window.removeEventListener("resize", resize_handler);
    info(count + ": resized window width is " + newSize);
    executeSoon(function() {
      cb(sizedOk);
    });
  }
  // Otherwise we request resize and expect a resize event
  window.addEventListener("resize", resize_handler);
  window.resizeBy(delta, 0);
}

function resizeAndCheckWidths(first, second, third, checks, cb) {
  if (checks.length == 0) {
    cb(); // nothing more to check!
    return;
  }
  let count = checks.length;
  let [width, numExpectedVisible, why] = checks.shift();
  info("<< Check " + count + ": " + why);
  info(count + ": " + "resizing window to " + width + ", expect " + numExpectedVisible + " visible items");
  resizeWindowToChatAreaWidth(width, function(sizedOk) {
    checkPopup();
    ok(sizedOk, count+": window resized correctly");
    if (sizedOk) {
      let numVisible = [first, second, third].filter(function(item) !item.collapsed).length;
      is(numVisible, numExpectedVisible, count + ": " + "correct number of chats visible");
    }
    info(">> Check " + count);
    resizeAndCheckWidths(first, second, third, checks, cb);
  }, count);
}

function getPopupWidth() {
  let popup = window.SocialChatBar.chatbar.menupopup;
  ok(!popup.parentNode.collapsed, "asking for popup width when it is visible");
  let cs = document.defaultView.getComputedStyle(popup.parentNode);
  let margins = parseInt(cs.marginLeft) + parseInt(cs.marginRight);
  return popup.parentNode.getBoundingClientRect().width + margins;
}

function closeAllChats() {
  let chatbar = window.SocialChatBar.chatbar;
  chatbar.removeAll();
}


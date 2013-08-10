/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

let tempScope = {};
Cu.import("resource://gre/modules/devtools/dbg-server.jsm", tempScope);
Cu.import("resource://gre/modules/devtools/dbg-client.jsm", tempScope);
Cu.import("resource://gre/modules/Services.jsm", tempScope);
let DebuggerServer = tempScope.DebuggerServer;
let DebuggerTransport = tempScope.DebuggerTransport;
let DebuggerClient = tempScope.DebuggerClient;
let Services = tempScope.Services;

const EXAMPLE_URL = "http://example.com/browser/browser/devtools/debugger/test/";
const TAB1_URL = EXAMPLE_URL + "browser_dbg_tab1.html";
const TAB2_URL = EXAMPLE_URL + "browser_dbg_tab2.html";
const STACK_URL = EXAMPLE_URL + "browser_dbg_stack.html";

// Enable logging and remote debugging for the relevant tests.
let gEnableRemote = Services.prefs.getBoolPref("devtools.debugger.remote-enabled");
let gEnableLogging = Services.prefs.getBoolPref("devtools.debugger.log");
Services.prefs.setBoolPref("devtools.debugger.remote-enabled", true);
Services.prefs.setBoolPref("devtools.debugger.log", true);

registerCleanupFunction(function() {
  Services.prefs.setBoolPref("devtools.debugger.remote-enabled", gEnableRemote);
  Services.prefs.setBoolPref("devtools.debugger.log", gEnableLogging);

  // Properly shut down the server to avoid memory leaks.
  DebuggerServer.destroy();
});

if (!DebuggerServer.initialized) {
  DebuggerServer.init(function() true);
  DebuggerServer.addBrowserActors();
}

waitForExplicitFinish();

function addWindow() {
  let windowReference = window.open();
  let chromeWindow = windowReference
    .QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIWebNavigation)
    .QueryInterface(Ci.nsIDocShellTreeItem).rootTreeItem
    .QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindow);

  return chromeWindow;
}

function addTab(aURL, aOnload, aWindow) {
  let targetWindow = aWindow || window;
  let targetBrowser = targetWindow.gBrowser;

  targetWindow.focus();
  targetBrowser.selectedTab = targetBrowser.addTab(aURL);

  let tab = targetBrowser.selectedTab;
  let win = tab.linkedBrowser.contentWindow;
  let expectedReadyState = aURL == "about:blank" ? ["interactive", "complete"]
                                                 : ["complete"];
  if (aOnload) {
    let handler = function() {
      if (tab.linkedBrowser.currentURI.spec != aURL ||
          win.document == null ||
          expectedReadyState.indexOf(win.document.readyState) == -1) {
        return;
      }
      tab.removeEventListener("load", handler, false);
      executeSoon(aOnload);
    }
    tab.addEventListener("load", handler, false);
  }

  return tab;
}

function removeTab(aTab, aWindow) {
  let targetWindow = aWindow || window;
  let targetBrowser = targetWindow.gBrowser;

  targetBrowser.removeTab(aTab);
}

function closeDebuggerAndFinish(aRemoteFlag, aCallback, aWindow) {
  let targetWindow = aWindow || window;
  let debuggerUI = targetWindow.DebuggerUI;

  let debuggerClosed = false;
  let debuggerDisconnected = false;

  function _maybeFinish() {
    if (debuggerClosed && debuggerDisconnected) {
      if (!aCallback)
        aCallback = finish;
      aCallback();
    }
  }

  debuggerUI.chromeWindow.addEventListener("Debugger:Shutdown", function cleanup() {
    debuggerUI.chromeWindow.removeEventListener("Debugger:Shutdown", cleanup, false);
    debuggerDisconnected = true;
    _maybeFinish();
  }, false);
  if (!aRemoteFlag) {
    debuggerUI.getDebugger().close(function() {
      debuggerClosed = true;
      _maybeFinish();
    });
  } else {
    debuggerClosed = true;
    debuggerUI.getRemoteDebugger().close();
  }
}

function get_tab_actor_for_url(aClient, aURL, aCallback) {
  aClient.listTabs(function(aResponse) {
    for each (let tab in aResponse.tabs) {
      if (tab.url == aURL) {
        aCallback(tab);
        return;
      }
    }
  });
}

function attach_tab_actor_for_url(aClient, aURL, aCallback) {
  get_tab_actor_for_url(aClient, aURL, function(actor) {
    aClient.request({ to: actor.actor, type: "attach" }, function(aResponse) {
      aCallback(actor, aResponse);
    });
  });
}

function attach_thread_actor_for_url(aClient, aURL, aCallback) {
  attach_tab_actor_for_url(aClient, aURL, function(aTabActor, aResponse) {
    aClient.request({ "to": actor.threadActor, "type": "attach" }, function(aResponse) {
      // We don't care about the pause right now (use
      // get_actor_for_url() if you do), so resume it.
      aClient.request({ to: actor.threadActor, type: "resume" }, function(aResponse) {
        aCallback(actor);
      });
    });
  });
}

function wait_for_connect_and_resume(aOnDebugging, aWindow) {
  let targetWindow = aWindow || window;
  let targetDocument = targetWindow.document;

  targetDocument.addEventListener("Debugger:Connected", function dbgConnected(aEvent) {
    targetDocument.removeEventListener("Debugger:Connected", dbgConnected, true);

    // Wait for the initial resume...
    aEvent.target.ownerDocument.defaultView.gClient.addOneTimeListener("resumed", function() {
      aOnDebugging();
    });
  }, true);
}

function debug_tab_pane(aURL, aOnDebugging) {
  let tab = addTab(aURL, function() {
    gBrowser.selectedTab = gTab;
    let debuggee = tab.linkedBrowser.contentWindow.wrappedJSObject;

    let pane = DebuggerUI.toggleDebugger();
    pane._frame.addEventListener("Debugger:Connected", function dbgConnected() {
      pane._frame.removeEventListener("Debugger:Connected", dbgConnected, true);

      // Wait for the initial resume...
      pane.contentWindow.gClient.addOneTimeListener("resumed", function() {
        pane.contentWindow.DebuggerView.Variables.lazyEmpty = false;
        aOnDebugging(tab, debuggee, pane);
      });
    }, true);
  });
}

function debug_remote(aURL, aOnDebugging, aBeforeTabAdded) {
  // Make any necessary preparations (start the debugger server etc.)
  aBeforeTabAdded();

  let tab = addTab(aURL, function() {
    gBrowser.selectedTab = gTab;
    let debuggee = tab.linkedBrowser.contentWindow.wrappedJSObject;

    let win = DebuggerUI.toggleRemoteDebugger();
    win._dbgwin.addEventListener("Debugger:Connected", function dbgConnected() {
      win._dbgwin.removeEventListener("Debugger:Connected", dbgConnected, true);

      // Wait for the initial resume...
      win.contentWindow.gClient.addOneTimeListener("resumed", function() {
        win._dbgwin.DebuggerView.Variables.lazyEmpty = false;
        aOnDebugging(tab, debuggee, win);
      });
    }, true);
  });
}

function debug_chrome(aURL, aOnClosing, aOnDebugging) {
  let tab = addTab(aURL, function() {
    gBrowser.selectedTab = gTab;
    let debuggee = tab.linkedBrowser.contentWindow.wrappedJSObject;

    DebuggerUI.toggleChromeDebugger(aOnClosing, function dbgRan(process) {

      // Wait for the remote debugging process to start...
      aOnDebugging(tab, debuggee, process);
    });
  });
}

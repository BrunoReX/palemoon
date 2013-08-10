/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = ["Social"];

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cu = Components.utils;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "SocialService",
  "resource://gre/modules/SocialService.jsm");

this.Social = {
  lastEventReceived: 0,
  provider: null,
  _disabledForSafeMode: false,
  init: function Social_init(callback) {
    this._disabledForSafeMode = Services.appinfo.inSafeMode && this.enabled;

    if (this.provider) {
      schedule(callback);
      return;
    }

    if (!this._addedPrivateBrowsingObserver) {
      Services.obs.addObserver(this, "private-browsing", false);
      this._addedPrivateBrowsingObserver = true;
    }

    // Eventually this might want to retrieve a specific provider, but for now
    // just use the first available.
    SocialService.getProviderList(function (providers) {
      if (providers.length)
        this.provider = providers[0];
      callback();
    }.bind(this));
  },

  observe: function(aSubject, aTopic, aData) {
    if (aTopic == "private-browsing") {
      if (aData == "enter") {
        this._enabledBeforePrivateBrowsing = this.enabled;
        this.enabled = false;
      } else if (aData == "exit") {
        // if the user has explicitly re-enabled social in PB mode, then upon
        // leaving we want to tear the world down then reenable to prevent
        // information leaks during this transition.
        // The next 2 lines rely on the fact that setting this.enabled to
        // its current value doesn't actually do anything...
        this.enabled = false;
        this.enabled = this._enabledBeforePrivateBrowsing;
      }
    }
  },

  get uiVisible() {
    return this.provider && this.provider.enabled;
  },

  set enabled(val) {
    if (!val) {
      delete this.errorState;
    }
    SocialService.enabled = val;
  },
  get enabled() {
    return SocialService.enabled;
  },

  get active() {
    return Services.prefs.getBoolPref("social.active");
  },
  set active(val) {
    this.enabled = !!val;
    Services.prefs.setBoolPref("social.active", !!val);
  },

  toggle: function Social_toggle() {
    this.enabled = this._disabledForSafeMode ? false : !this.enabled;
    this._disabledForSafeMode = false;
  },

  toggleSidebar: function SocialSidebar_toggle() {
    let prefValue = Services.prefs.getBoolPref("social.sidebar.open");
    Services.prefs.setBoolPref("social.sidebar.open", !prefValue);
  },

  toggleNotifications: function SocialNotifications_toggle() {
    let prefValue = Services.prefs.getBoolPref("social.toast-notifications.enabled");
    Services.prefs.setBoolPref("social.toast-notifications.enabled", !prefValue);
  },

  // Sharing functionality
  _getShareablePageUrl: function Social_getShareablePageUrl(aURI) {
    let uri = aURI.clone();
    try {
      // Setting userPass on about:config throws.
      uri.userPass = "";
    } catch (e) {}
    return uri.spec;
  },

  isPageShared: function Social_isPageShared(aURI) {
    let url = this._getShareablePageUrl(aURI);
    return this._sharedUrls.hasOwnProperty(url);
  },

  sharePage: function Social_sharePage(aURI) {
    // this should not be called if this.provider or the port is null
    if (!this.provider) {
      Cu.reportError("Can't share a page when no provider is current");
      return;
    }
    let port = this.provider.getWorkerPort();
    if (!port) {
      Cu.reportError("Can't share page as no provider port is available");
      return;
    }
    let url = this._getShareablePageUrl(aURI);
    this._sharedUrls[url] = true;
    port.postMessage({
      topic: "social.user-recommend",
      data: { url: url }
    });
    port.close();
  },

  unsharePage: function Social_unsharePage(aURI) {
    // this should not be called if this.provider or the port is null
    if (!this.provider) {
      Cu.reportError("Can't unshare a page when no provider is current");
      return;
    }
    let port = this.provider.getWorkerPort();
    if (!port) {
      Cu.reportError("Can't unshare page as no provider port is available");
      return;
    }
    let url = this._getShareablePageUrl(aURI);
    delete this._sharedUrls[url];
    port.postMessage({
      topic: "social.user-unrecommend",
      data: { url: url }
    });
    port.close();
  },

  _sharedUrls: {}
};

function schedule(callback) {
  Services.tm.mainThread.dispatch(callback, Ci.nsIThread.DISPATCH_NORMAL);
}

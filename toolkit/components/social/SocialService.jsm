/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

this.EXPORTED_SYMBOLS = ["SocialService"];

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "getFrameWorkerHandle", "resource://gre/modules/FrameWorker.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "WorkerAPI", "resource://gre/modules/WorkerAPI.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "MozSocialAPI", "resource://gre/modules/MozSocialAPI.jsm");

/**
 * The SocialService is the public API to social providers - it tracks which
 * providers are installed and enabled, and is the entry-point for access to
 * the provider itself.
 */

// Internal helper methods and state
let SocialServiceInternal = {
  enabled: Services.prefs.getBoolPref("social.enabled"),
  get providerArray() {
    return [p for ([, p] of Iterator(this.providers))];
  }
};

function initService() {
  // Add a pref observer for the enabled state
  function prefObserver(subject, topic, data) {
    SocialService._setEnabled(Services.prefs.getBoolPref("social.enabled"));
  }
  Services.prefs.addObserver("social.enabled", prefObserver, false);
  Services.obs.addObserver(function xpcomShutdown() {
    Services.obs.removeObserver(xpcomShutdown, "xpcom-shutdown");
    Services.prefs.removeObserver("social.enabled", prefObserver);
  }, "xpcom-shutdown", false);

  // Initialize the MozSocialAPI
  if (SocialServiceInternal.enabled)
    MozSocialAPI.enabled = true;
}

XPCOMUtils.defineLazyGetter(SocialServiceInternal, "providers", function () {
  initService();

  // Don't load any providers from prefs if the test pref is set
  let skipLoading = false;
  try {
    skipLoading = Services.prefs.getBoolPref("social.skipLoadingProviders");
  } catch (ex) {}

  if (skipLoading)
    return {};

  // Now retrieve the providers from prefs
  let providers = {};
  let MANIFEST_PREFS = Services.prefs.getBranch("social.manifest.");
  let prefs = MANIFEST_PREFS.getChildList("", {});
  let appinfo = Cc["@mozilla.org/xre/app-info;1"]
                  .getService(Ci.nsIXULRuntime);
  prefs.forEach(function (pref) {
    try {
      var manifest = JSON.parse(MANIFEST_PREFS.getCharPref(pref));
      if (manifest && typeof(manifest) == "object") {
        let provider = new SocialProvider(manifest, appinfo.inSafeMode ? false : SocialServiceInternal.enabled);
        providers[provider.origin] = provider;
      }
    } catch (err) {
      Cu.reportError("SocialService: failed to load provider: " + pref +
                     ", exception: " + err);
    }
  });

  return providers;
});

function schedule(callback) {
  Services.tm.mainThread.dispatch(callback, Ci.nsIThread.DISPATCH_NORMAL);
}

// Public API
this.SocialService = {
  get enabled() {
    return SocialServiceInternal.enabled;
  },
  set enabled(val) {
    let enable = !!val;

    // Allow setting to the same value when in safe mode so the
    // feature can be force enabled.
    if (enable == SocialServiceInternal.enabled &&
        !Services.appinfo.inSafeMode)
      return;

    Services.prefs.setBoolPref("social.enabled", enable);
    this._setEnabled(enable);
  },
  _setEnabled: function _setEnabled(enable) {
    if (enable == SocialServiceInternal.enabled)
      return;

    SocialServiceInternal.providerArray.forEach(function (p) p.enabled = enable);
    SocialServiceInternal.enabled = enable;
    MozSocialAPI.enabled = enable;
    Services.obs.notifyObservers(null, "social:pref-changed", enable ? "enabled" : "disabled");
    Services.telemetry.getHistogramById("SOCIAL_TOGGLED").add(enable);
  },

  // Adds a provider given a manifest, and returns the added provider.
  addProvider: function addProvider(manifest, onDone) {
    if (SocialServiceInternal.providers[manifest.origin])
      throw new Error("SocialService.addProvider: provider with this origin already exists");

    let provider = new SocialProvider(manifest, SocialServiceInternal.enabled);
    SocialServiceInternal.providers[provider.origin] = provider;

    schedule(function () {
      onDone(provider);
    });
  },

  // Removes a provider with the given origin, and notifies when the removal is
  // complete.
  removeProvider: function removeProvider(origin, onDone) {
    if (!(origin in SocialServiceInternal.providers))
      throw new Error("SocialService.removeProvider: no provider with this origin exists!");

    let provider = SocialServiceInternal.providers[origin];
    provider.enabled = false;

    delete SocialServiceInternal.providers[origin];

    if (onDone)
      schedule(onDone);
  },

  // Returns a single provider object with the specified origin.
  getProvider: function getProvider(origin, onDone) {
    schedule((function () {
      onDone(SocialServiceInternal.providers[origin] || null);
    }).bind(this));
  },

  // Returns an array of installed provider origins.
  getProviderList: function getProviderList(onDone) {
    schedule(function () {
      onDone(SocialServiceInternal.providerArray);
    });
  }
};

/**
 * The SocialProvider object represents a social provider, and allows
 * access to its FrameWorker (if it has one).
 *
 * @constructor
 * @param {jsobj} object representing the manifest file describing this provider
 * @param {bool} whether the provider should be initially enabled (defaults to true)
 */
function SocialProvider(input, enabled) {
  if (!input.name)
    throw new Error("SocialProvider must be passed a name");
  if (!input.origin)
    throw new Error("SocialProvider must be passed an origin");

  this.name = input.name;
  this.iconURL = input.iconURL;
  this.workerURL = input.workerURL;
  this.sidebarURL = input.sidebarURL;
  this.origin = input.origin;
  this.ambientNotificationIcons = {};

  // If enabled is |undefined|, default to true.
  this._enabled = !(enabled == false);
  if (this._enabled)
    this._activate();
}

SocialProvider.prototype = {
  // Provider enabled/disabled state. Disabled providers do not have active
  // connections to their FrameWorkers.
  _enabled: true,
  get enabled() {
    return this._enabled;
  },
  set enabled(val) {
    let enable = !!val;
    if (enable == this._enabled)
      return;

    this._enabled = enable;

    if (enable) {
      this._activate();
    } else {
      this._terminate();
    }
  },

  // Reference to a workerAPI object for this provider. Null if the provider has
  // no FrameWorker, or is disabled.
  workerAPI: null,

  // Contains information related to the user's profile. Populated by the
  // workerAPI via updateUserProfile.
  // Properties:
  //   iconURL, portrait, userName, displayName, profileURL
  // See https://github.com/mozilla/socialapi-dev/blob/develop/docs/socialAPI.md
  // A value of null or an empty object means 'user not logged in'.
  // A value of undefined means the service has not yet told us the status of
  // the profile (ie, the service is still loading/initing, or the provider has
  // no FrameWorker)
  // This distinction might be used to cache certain data between runs - eg,
  // browser-social.js caches the notification icons so they can be displayed
  // quickly at startup without waiting for the provider to initialize -
  // 'undefined' means 'ok to use cached values' versus 'null' meaning 'cached
  // values aren't to be used as the user is logged out'.
  profile: undefined,

  // Contains the information necessary to support our "recommend" feature.
  // null means no info yet provided (which includes the case of the provider
  // not supporting the feature) or the provided data is invalid.  Updated via
  // the 'recommendInfo' setter and returned via the getter.
  _recommendInfo: null,
  get recommendInfo() {
    return this._recommendInfo;
  },
  set recommendInfo(data) {
    // Accept *and validate* the user-recommend-prompt-response message from
    // the provider.
    let promptImages = {};
    let promptMessages = {};
    function reportError(reason) {
      Cu.reportError("Invalid recommend data from provider: " + reason + ": sharing is disabled for this provider");
      // and we explicitly reset the recommend data to null to avoid stale
      // data being used and notify our observers.
      this._recommendInfo = null;
      Services.obs.notifyObservers(null, "social:recommend-info-changed", this.origin);
    }
    if (!data ||
        !data.images || typeof data.images != "object" ||
        !data.messages || typeof data.messages != "object") {
      reportError("data is missing valid 'images' or 'messages' elements");
      return;
    }
    for (let sub of ["share", "unshare"]) {
      let url = data.images[sub];
      if (!url || typeof url != "string" || url.length == 0) {
        reportError('images["' + sub + '"] is missing or not a non-empty string');
        return;
      }
      // resolve potentially relative URLs then check the scheme is acceptable.
      url = Services.io.newURI(this.origin, null, null).resolve(url);
      let uri = Services.io.newURI(url, null, null);
      if (!uri.schemeIs("http") && !uri.schemeIs("https") && !uri.schemeIs("data")) {
        reportError('images["' + sub + '"] does not have a valid scheme');
        return;
      }
      promptImages[sub] = url;
    }
    for (let sub of ["shareTooltip", "unshareTooltip",
                     "sharedLabel", "unsharedLabel", "unshareLabel",
                     "portraitLabel",
                     "unshareConfirmLabel", "unshareConfirmAccessKey",
                     "unshareCancelLabel", "unshareCancelAccessKey"]) {
      if (typeof data.messages[sub] != "string" || data.messages[sub].length == 0) {
        reportError('messages["' + sub + '"] is not a valid string');
        return;
      }
      promptMessages[sub] = data.messages[sub];
    }
    this._recommendInfo = {images: promptImages, messages: promptMessages};
    Services.obs.notifyObservers(null, "social:recommend-info-changed", this.origin);
  },

  // Map of objects describing the provider's notification icons, whose
  // properties include:
  //   name, iconURL, counter, contentPanel
  // See https://developer.mozilla.org/en-US/docs/Social_API
  ambientNotificationIcons: null,

  // Called by the workerAPI to update our profile information.
  updateUserProfile: function(profile) {
    this.profile = profile;

    // Sanitize the portrait from any potential script-injection.
    if (profile.portrait) {
      try {
        let portraitUri = Services.io.newURI(profile.portrait, null, null);

        let scheme = portraitUri ? portraitUri.scheme : "";
        if (scheme != "data" && scheme != "http" && scheme != "https") {
          profile.portrait = "";
        }
      } catch (ex) {
        profile.portrait = "";
      }
    }

    if (profile.iconURL)
      this.iconURL = profile.iconURL;

    if (!profile.displayName)
      profile.displayName = profile.userName;

    // if no userName, consider this a logged out state, emtpy the
    // users ambient notifications.  notify both profile and ambient
    // changes to clear everything
    if (!profile.userName) {
      this.profile = {};
      this.ambientNotificationIcons = {};
      Services.obs.notifyObservers(null, "social:ambient-notification-changed", this.origin);
    }

    Services.obs.notifyObservers(null, "social:profile-changed", this.origin);
  },

  // Called by the workerAPI to add/update a notification icon.
  setAmbientNotification: function(notification) {
    if (!this.profile.userName)
      throw new Error("unable to set notifications while logged out");
    this.ambientNotificationIcons[notification.name] = notification;

    Services.obs.notifyObservers(null, "social:ambient-notification-changed", this.origin);
  },

  // Internal helper methods
  _activate: function _activate() {
    // Initialize the workerAPI and its port first, so that its initialization
    // occurs before any other messages are processed by other ports.
    let workerAPIPort = this.getWorkerPort();
    if (workerAPIPort)
      this.workerAPI = new WorkerAPI(this, workerAPIPort);
  },

  _terminate: function _terminate() {
    if (this.workerURL) {
      try {
        getFrameWorkerHandle(this.workerURL, null).terminate();
      } catch (e) {
        Cu.reportError("SocialProvider FrameWorker termination failed: " + e);
      }
    }
    if (this.workerAPI) {
      this.workerAPI.terminate();
    }
    this.workerAPI = null;
    this.profile = undefined;
  },

  /**
   * Instantiates a FrameWorker for the provider if one doesn't exist, and
   * returns a reference to a new port to that FrameWorker.
   *
   * Returns null if this provider has no workerURL, or is disabled.
   *
   * @param {DOMWindow} window (optional)
   */
  getWorkerPort: function getWorkerPort(window) {
    if (!this.workerURL || !this.enabled)
      return null;
    return getFrameWorkerHandle(this.workerURL, window).port;
  }
}

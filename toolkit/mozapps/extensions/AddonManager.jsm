/*
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is the Extension Manager.
#
# The Initial Developer of the Original Code is
# the Mozilla Foundation.
# Portions created by the Initial Developer are Copyright (C) 2009
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Dave Townsend <dtownsend@oxymoronical.com>
#   Blair McBride <bmcbride@mozilla.com>
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****
*/

"use strict";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;

const PREF_BLOCKLIST_PINGCOUNTVERSION = "extensions.blocklist.pingCountVersion";
const PREF_EM_UPDATE_ENABLED          = "extensions.update.enabled";
const PREF_EM_LAST_APP_VERSION        = "extensions.lastAppVersion";
const PREF_EM_LAST_PLATFORM_VERSION   = "extensions.lastPlatformVersion";
const PREF_EM_AUTOUPDATE_DEFAULT      = "extensions.update.autoUpdateDefault";
const PREF_EM_STRICT_COMPATIBILITY    = "extensions.strictCompatibility";
const PREF_EM_UPDATE_URL              = "extensions.update.url";
const PREF_APP_UPDATE_ENABLED         = "app.update.enabled";
const PREF_APP_UPDATE_AUTO            = "app.update.auto";
const PREF_EM_HOTFIX_ID               = "extensions.hotfix.id";
const PREF_EM_HOTFIX_LASTVERSION      = "extensions.hotfix.lastVersion";
const PREF_EM_HOTFIX_URL              = "extensions.hotfix.url";
const PREF_EM_CERT_CHECKATTRIBUTES    = "extensions.hotfix.cert.checkAttributes";
const PREF_EM_HOTFIX_CERTS            = "extensions.hotfix.certs.";
const PREF_MATCH_OS_LOCALE            = "intl.locale.matchOS";
const PREF_SELECTED_LOCALE            = "general.useragent.locale";

const UPDATE_REQUEST_VERSION          = 2;
const CATEGORY_UPDATE_PARAMS          = "extension-update-params";

// Note: This has to be kept in sync with the same constant in AddonRepository.jsm
const STRICT_COMPATIBILITY_DEFAULT    = true;

const TOOLKIT_ID                      = "toolkit@mozilla.org";

const VALID_TYPES_REGEXP = /^[\w\-]+$/;

Components.utils.import("resource://gre/modules/Services.jsm");
var CertUtils = {};
Components.utils.import("resource://gre/modules/CertUtils.jsm", CertUtils);

var EXPORTED_SYMBOLS = [ "AddonManager", "AddonManagerPrivate" ];

const CATEGORY_PROVIDER_MODULE = "addon-provider-module";

// A list of providers to load by default
const DEFAULT_PROVIDERS = [
  "resource://gre/modules/XPIProvider.jsm",
  "resource://gre/modules/LightweightThemeManager.jsm"
];

["LOG", "WARN", "ERROR"].forEach(function(aName) {
  this.__defineGetter__(aName, function() {
    Components.utils.import("resource://gre/modules/AddonLogging.jsm");

    LogManager.getLogger("addons.manager", this);
    return this[aName];
  });
}, this);

/**
 * Calls a callback method consuming any thrown exception. Any parameters after
 * the callback parameter will be passed to the callback.
 *
 * @param  aCallback
 *         The callback method to call
 */
function safeCall(aCallback) {
  var args = Array.slice(arguments, 1);

  try {
    aCallback.apply(null, args);
  }
  catch (e) {
    WARN("Exception calling callback", e);
  }
}

/**
 * Calls a method on a provider if it exists and consumes any thrown exception.
 * Any parameters after the dflt parameter are passed to the provider's method.
 *
 * @param  aProvider
 *         The provider to call
 * @param  aMethod
 *         The method name to call
 * @param  aDefault
 *         A default return value if the provider does not implement the named
 *         method or throws an error.
 * @return the return value from the provider or dflt if the provider does not
 *         implement method or throws an error
 */
function callProvider(aProvider, aMethod, aDefault) {
  if (!(aMethod in aProvider))
    return aDefault;

  var args = Array.slice(arguments, 3);

  try {
    return aProvider[aMethod].apply(aProvider, args);
  }
  catch (e) {
    ERROR("Exception calling provider " + aMethod, e);
    return aDefault;
  }
}

/**
 * Gets the currently selected locale for display.
 * @return  the selected locale or "en-US" if none is selected
 */
function getLocale() {
  try {
    if (Services.prefs.getBoolPref(PREF_MATCH_OS_LOCALE))
      return Services.locale.getLocaleComponentForUserAgent();
  }
  catch (e) { }

  try {
    let locale = Services.prefs.getComplexValue(PREF_SELECTED_LOCALE,
                                                Ci.nsIPrefLocalizedString);
    if (locale)
      return locale;
  }
  catch (e) { }

  try {
    return Services.prefs.getCharPref(PREF_SELECTED_LOCALE);
  }
  catch (e) { }

  return "en-US";
}

/**
 * A helper class to repeatedly call a listener with each object in an array
 * optionally checking whether the object has a method in it.
 *
 * @param  aObjects
 *         The array of objects to iterate through
 * @param  aMethod
 *         An optional method name, if not null any objects without this method
 *         will not be passed to the listener
 * @param  aListener
 *         A listener implementing nextObject and noMoreObjects methods. The
 *         former will be called with the AsyncObjectCaller as the first
 *         parameter and the object as the second. noMoreObjects will be passed
 *         just the AsyncObjectCaller
 */
function AsyncObjectCaller(aObjects, aMethod, aListener) {
  this.objects = aObjects.slice(0);
  this.method = aMethod;
  this.listener = aListener;

  this.callNext();
}

AsyncObjectCaller.prototype = {
  objects: null,
  method: null,
  listener: null,

  /**
   * Passes the next object to the listener or calls noMoreObjects if there
   * are none left.
   */
  callNext: function AOC_callNext() {
    if (this.objects.length == 0) {
      this.listener.noMoreObjects(this);
      return;
    }

    let object = this.objects.shift();
    if (!this.method || this.method in object)
      this.listener.nextObject(this, object);
    else
      this.callNext();
  }
};

/**
 * This represents an author of an add-on (e.g. creator or developer)
 *
 * @param  aName
 *         The name of the author
 * @param  aURL
 *         The URL of the author's profile page
 */
function AddonAuthor(aName, aURL) {
  this.name = aName;
  this.url = aURL;
}

AddonAuthor.prototype = {
  name: null,
  url: null,

  // Returns the author's name, defaulting to the empty string
  toString: function() {
    return this.name || "";
  }
}

/**
 * This represents an screenshot for an add-on
 *
 * @param  aURL
 *         The URL to the full version of the screenshot
 * @param  aWidth
 *         The width in pixels of the screenshot
 * @param  aHeight
 *         The height in pixels of the screenshot
 * @param  aThumbnailURL
 *         The URL to the thumbnail version of the screenshot
 * @param  aThumbnailWidth
 *         The width in pixels of the thumbnail version of the screenshot
 * @param  aThumbnailHeight
 *         The height in pixels of the thumbnail version of the screenshot
 * @param  aCaption
 *         The caption of the screenshot
 */
function AddonScreenshot(aURL, aWidth, aHeight, aThumbnailURL,
                         aThumbnailWidth, aThumbnailHeight, aCaption) {
  this.url = aURL;
  if (aWidth) this.width = aWidth;
  if (aHeight) this.height = aHeight;
  if (aThumbnailURL) this.thumbnailURL = aThumbnailURL;
  if (aThumbnailWidth) this.thumbnailWidth = aThumbnailWidth;
  if (aThumbnailHeight) this.thumbnailHeight = aThumbnailHeight;
  if (aCaption) this.caption = aCaption;
}

AddonScreenshot.prototype = {
  url: null,
  width: null,
  height: null,
  thumbnailURL: null,
  thumbnailWidth: null,
  thumbnailHeight: null,
  caption: null,

  // Returns the screenshot URL, defaulting to the empty string
  toString: function() {
    return this.url || "";
  }
}


/**
 * This represents a compatibility override for an addon.
 *
 * @param  aType
 *         Overrride type - "compatible" or "incompatible"
 * @param  aMinVersion
 *         Minimum version of the addon to match
 * @param  aMaxVersion
 *         Maximum version of the addon to match
 * @param  aAppID
 *         Application ID used to match appMinVersion and appMaxVersion
 * @param  aAppMinVersion
 *         Minimum version of the application to match
 * @param  aAppMaxVersion
 *         Maximum version of the application to match
 */
function AddonCompatibilityOverride(aType, aMinVersion, aMaxVersion, aAppID,
                                    aAppMinVersion, aAppMaxVersion) {
  this.type = aType;
  this.minVersion = aMinVersion;
  this.maxVersion = aMaxVersion;
  this.appID = aAppID;
  this.appMinVersion = aAppMinVersion;
  this.appMaxVersion = aAppMaxVersion;
}

AddonCompatibilityOverride.prototype = {
  /**
   * Type of override - "incompatible" or "compatible".
   * Only "incompatible" is supported for now.
   */
  type: null,

  /**
   * Min version of the addon to match.
   */
  minVersion: null,

  /**
   * Max version of the addon to match.
   */
  maxVersion: null,

  /**
   * Application ID to match.
   */
  appID: null,

  /**
   * Min version of the application to match.
   */
  appMinVersion: null,

  /**
   * Max version of the application to match.
   */
  appMaxVersion: null
};


/**
 * A type of add-on, used by the UI to determine how to display different types
 * of add-ons.
 *
 * @param  aId
 *         The add-on type ID
 * @param  aLocaleURI
 *         The URI of a localized properties file to get the displayable name
 *         for the type from
 * @param  aLocaleKey
 *         The key for the string in the properties file or the actual display
 *         name if aLocaleURI is null. Include %ID% to include the type ID in
 *         the key
 * @param  aViewType
 *         The optional type of view to use in the UI
 * @param  aUIPriority
 *         The priority is used by the UI to list the types in order. Lower
 *         values push the type higher in the list.
 * @param  aFlags
 *         An option set of flags that customize the display of the add-on in
 *         the UI.
 */
function AddonType(aId, aLocaleURI, aLocaleKey, aViewType, aUIPriority, aFlags) {
  if (!aId)
    throw new Error("An AddonType must have an ID");
  if (aViewType && aUIPriority === undefined)
    throw new Error("An AddonType with a defined view must have a set UI priority");
  if (!aLocaleKey)
    throw new Error("An AddonType must have a displayable name");

  this.id = aId;
  this.uiPriority = aUIPriority;
  this.viewType = aViewType;
  this.flags = aFlags;

  if (aLocaleURI) {
    this.__defineGetter__("name", function() {
      delete this.name;
      let bundle = Services.strings.createBundle(aLocaleURI);
      this.name = bundle.GetStringFromName(aLocaleKey.replace("%ID%", aId));
      return this.name;
    });
  }
  else {
    this.name = aLocaleKey;
  }
}

var gStarted = false;
var gStrictCompatibility = STRICT_COMPATIBILITY_DEFAULT;

/**
 * This is the real manager, kept here rather than in AddonManager to keep its
 * contents hidden from API users.
 */
var AddonManagerInternal = {
  installListeners: [],
  addonListeners: [],
  typeListeners: [],
  providers: [],
  types: {},
  startupChanges: {},

  // A read-only wrapper around the types dictionary
  typesProxy: Proxy.create({
    getOwnPropertyDescriptor: function(aName) {
      if (!(aName in AddonManagerInternal.types))
        return undefined;

      return {
        value: AddonManagerInternal.types[aName].type,
        writable: false,
        configurable: false,
        enumerable: true
      }
    },

    getPropertyDescriptor: function(aName) {
      return this.getOwnPropertyDescriptor(aName);
    },

    getOwnPropertyNames: function() {
      return Object.keys(AddonManagerInternal.types);
    },

    getPropertyNames: function() {
      return this.getOwnPropertyNames();
    },

    delete: function(aName) {
      // Not allowed to delete properties
      return false;
    },

    defineProperty: function(aName, aProperty) {
      // Ignore attempts to define properties
    },

    fix: function() {
      return undefined;
    },

    // Despite MDC's claims to the contrary, it is required that this trap
    // be defined
    enumerate: function() {
      // All properties are enumerable
      return this.getPropertyNames();
    }
  }),

  /**
   * Initializes the AddonManager, loading any known providers and initializing
   * them.
   */
  startup: function AMI_startup() {
    if (gStarted)
      return;

    let appChanged = undefined;

    let oldAppVersion = null;
    try {
      oldAppVersion = Services.prefs.getCharPref(PREF_EM_LAST_APP_VERSION);
      appChanged = Services.appinfo.version != oldAppVersion;
    }
    catch (e) { }

    let oldPlatformVersion = null;
    try {
      oldPlatformVersion = Services.prefs.getCharPref(PREF_EM_LAST_PLATFORM_VERSION);
    }
    catch (e) { }

    if (appChanged !== false) {
      LOG("Application has been upgraded");
      Services.prefs.setCharPref(PREF_EM_LAST_APP_VERSION,
                                 Services.appinfo.version);
      Services.prefs.setCharPref(PREF_EM_LAST_PLATFORM_VERSION,
                                 Services.appinfo.platformVersion);
      Services.prefs.setIntPref(PREF_BLOCKLIST_PINGCOUNTVERSION,
                                (appChanged === undefined ? 0 : -1));
    }

    try {
      gStrictCompatibility = Services.prefs.getBoolPref(PREF_EM_STRICT_COMPATIBILITY);
    } catch (e) {}
    Services.prefs.addObserver(PREF_EM_STRICT_COMPATIBILITY, this, false);

    // Ensure all default providers have had a chance to register themselves
    DEFAULT_PROVIDERS.forEach(function(url) {
      try {
        Components.utils.import(url, {});
      }
      catch (e) {
        ERROR("Exception loading default provider \"" + url + "\"", e);
      }
    });

    // Load any providers registered in the category manager
    let catman = Cc["@mozilla.org/categorymanager;1"].
                 getService(Ci.nsICategoryManager);
    let entries = catman.enumerateCategory(CATEGORY_PROVIDER_MODULE);
    while (entries.hasMoreElements()) {
      let entry = entries.getNext().QueryInterface(Ci.nsISupportsCString).data;
      let url = catman.getCategoryEntry(CATEGORY_PROVIDER_MODULE, entry);

      try {
        Components.utils.import(url, {});
      }
      catch (e) {
        ERROR("Exception loading provider " + entry + " from category \"" +
              url + "\"", e);
      }
    }

    this.providers.forEach(function(provider) {
      callProvider(provider, "startup", null, appChanged, oldAppVersion,
                   oldPlatformVersion);
    });

    // If this is a new profile just pretend that there were no changes
    if (appChanged === undefined) {
      for (let type in this.startupChanges)
        delete this.startupChanges[type];
    }

    gStarted = true;
  },

  /**
   * Registers a new AddonProvider.
   *
   * @param  aProvider
   *         The provider to register
   * @param  aTypes
   *         An array of add-on types
   */
  registerProvider: function AMI_registerProvider(aProvider, aTypes) {
    this.providers.push(aProvider);

    if (aTypes) {
      aTypes.forEach(function(aType) {
        if (!(aType.id in this.types)) {
          if (!VALID_TYPES_REGEXP.test(aType.id)) {
            WARN("Ignoring invalid type " + aType.id);
            return;
          }

          this.types[aType.id] = {
            type: aType,
            providers: [aProvider]
          };

          this.typeListeners.forEach(function(aListener) {
            safeCall(function() {
              aListener.onTypeAdded(aType);
            });
          });
        }
        else {
          this.types[aType.id].providers.push(aProvider);
        }
      }, this);
    }

    // If we're registering after startup call this provider's startup.
    if (gStarted)
      callProvider(aProvider, "startup");
  },

  /**
   * Unregisters an AddonProvider.
   *
   * @param  aProvider
   *         The provider to unregister
   */
  unregisterProvider: function AMI_unregisterProvider(aProvider) {
    let pos = 0;
    while (pos < this.providers.length) {
      if (this.providers[pos] == aProvider)
        this.providers.splice(pos, 1);
      else
        pos++;
    }

    for (let type in this.types) {
      this.types[type].providers = this.types[type].providers.filter(function(p) p != aProvider);
      if (this.types[type].providers.length == 0) {
        let oldType = this.types[type].type;
        delete this.types[type];

        this.typeListeners.forEach(function(aListener) {
          safeCall(function() {
            aListener.onTypeRemoved(oldType);
          });
        });
      }
    }

    // If we're unregistering after startup call this provider's shutdown.
    if (gStarted)
      callProvider(aProvider, "shutdown");
  },

  /**
   * Shuts down the addon manager and all registered providers, this must clean
   * up everything in order for automated tests to fake restarts.
   */
  shutdown: function AMI_shutdown() {
    Services.prefs.removeObserver(PREF_EM_STRICT_COMPATIBILITY, this);

    this.providers.forEach(function(provider) {
      callProvider(provider, "shutdown");
    });

    this.installListeners.splice(0, this.installListeners.length);
    this.addonListeners.splice(0, this.addonListeners.length);
    this.typeListeners.splice(0, this.typeListeners.length);
    for (let type in this.startupChanges)
      delete this.startupChanges[type];
    gStarted = false;
  },

  /**
   * Notified when a preference we're interested in has changed.
   *
   * @see nsIObserver
   */
  observe: function AMI_observe(aSubject, aTopic, aData) {
    switch (aData) {
    case PREF_EM_STRICT_COMPATIBILITY:
      let oldValue = gStrictCompatibility;
      try {
        gStrictCompatibility = Services.prefs.getBoolPref(PREF_EM_STRICT_COMPATIBILITY);
      } catch(e) {
        gStrictCompatibility = STRICT_COMPATIBILITY_DEFAULT;
      }

      // XXXunf Currently, this won't notify listeners that an addon's
      // compatibility status has changed if the addon's appDisabled state
      // doesn't change.
      if (gStrictCompatibility != oldValue)
        this.updateAddonAppDisabledStates();

      break;
    }
  },

  /**
   * Replaces %...% strings in an addon url (update and updateInfo) with
   * appropriate values.
   *
   * @param  aAddon
   *         The AddonInternal representing the add-on
   * @param  aUri
   *         The uri to escape
   * @param  aAppVersion
   *         The optional application version to use for %APP_VERSION%
   * @return the appropriately escaped uri.
   */
  escapeAddonURI: function AMI_escapeAddonURI(aAddon, aUri, aAppVersion)
  {
    var addonStatus = aAddon.userDisabled || aAddon.softDisabled ? "userDisabled"
                                                                 : "userEnabled";

    if (!aAddon.isCompatible)
      addonStatus += ",incompatible";
    if (aAddon.blocklistState == Ci.nsIBlocklistService.STATE_BLOCKED)
      addonStatus += ",blocklisted";
    if (aAddon.blocklistState == Ci.nsIBlocklistService.STATE_SOFTBLOCKED)
      addonStatus += ",softblocked";

    try {
      var xpcomABI = Services.appinfo.XPCOMABI;
    } catch (ex) {
      xpcomABI = UNKNOWN_XPCOM_ABI;
    }

    let uri = aUri.replace(/%ITEM_ID%/g, aAddon.id);
    uri = uri.replace(/%ITEM_VERSION%/g, aAddon.version);
    uri = uri.replace(/%ITEM_STATUS%/g, addonStatus);
    uri = uri.replace(/%APP_ID%/g, Services.appinfo.ID);
    uri = uri.replace(/%APP_VERSION%/g, aAppVersion ? aAppVersion :
                                                      Services.appinfo.version);
    uri = uri.replace(/%REQ_VERSION%/g, UPDATE_REQUEST_VERSION);
    uri = uri.replace(/%APP_OS%/g, Services.appinfo.OS);
    uri = uri.replace(/%APP_ABI%/g, xpcomABI);
    uri = uri.replace(/%APP_LOCALE%/g, getLocale());
    uri = uri.replace(/%CURRENT_APP_VERSION%/g, Services.appinfo.version);

    // Replace custom parameters (names of custom parameters must have at
    // least 3 characters to prevent lookups for something like %D0%C8)
    var catMan = null;
    uri = uri.replace(/%(\w{3,})%/g, function(aMatch, aParam) {
      if (!catMan) {
        catMan = Cc["@mozilla.org/categorymanager;1"].
                 getService(Ci.nsICategoryManager);
      }

      try {
        var contractID = catMan.getCategoryEntry(CATEGORY_UPDATE_PARAMS, aParam);
        var paramHandler = Cc[contractID].getService(Ci.nsIPropertyBag2);
        return paramHandler.getPropertyAsAString(aParam);
      }
      catch(e) {
        return aMatch;
      }
    });

    // escape() does not properly encode + symbols in any embedded FVF strings.
    return uri.replace(/\+/g, "%2B");
  },

  /**
   * Performs a background update check by starting an update for all add-ons
   * that can be updated.
   */
  backgroundUpdateCheck: function AMI_backgroundUpdateCheck() {
    let hotfixID = null;
    if (Services.prefs.getPrefType(PREF_EM_HOTFIX_ID) == Ci.nsIPrefBranch.PREF_STRING)
      hotfixID = Services.prefs.getCharPref(PREF_EM_HOTFIX_ID);

    let checkHotfix = hotfixID &&
                      Services.prefs.getBoolPref(PREF_APP_UPDATE_ENABLED) &&
                      Services.prefs.getBoolPref(PREF_APP_UPDATE_AUTO);

    let checkAddons = Services.prefs.getBoolPref(PREF_EM_UPDATE_ENABLED);

    if (!checkAddons && !checkHotfix)
      return;

    Services.obs.notifyObservers(null, "addons-background-update-start", null);

    // Start this from one to ensure the whole of this function completes before
    // we can send the complete notification. Some parts can in some cases
    // complete synchronously before later parts have a chance to increment
    // pendingUpdates.
    let pendingUpdates = 1;

    function notifyComplete() {
      if (--pendingUpdates == 0) {
        Services.obs.notifyObservers(null,
                                     "addons-background-update-complete",
                                     null);
      }
    }

    if (checkAddons) {
      let scope = {};
      Components.utils.import("resource://gre/modules/AddonRepository.jsm", scope);
      Components.utils.import("resource://gre/modules/LightweightThemeManager.jsm", scope);
      scope.LightweightThemeManager.updateCurrentTheme();

      pendingUpdates++;
      this.getAllAddons(function getAddonsCallback(aAddons) {
        // If there is a known hotfix then exclude it from the list of add-ons to update.
        var ids = [a.id for each (a in aAddons) if (a.id != hotfixID)];

        // Repopulate repository cache first, to ensure compatibility overrides
        // are up to date before checking for addon updates.
        scope.AddonRepository.backgroundUpdateCheck(ids, function BUC_backgroundUpdateCheckCallback() {
          AddonManagerInternal.updateAddonRepositoryData(function BUC_updateAddonCallback() {

            pendingUpdates += aAddons.length;
            aAddons.forEach(function BUC_forEachCallback(aAddon) {
              if (aAddon.id == hotfixID) {
                notifyComplete();
                return;
              }

              // Check all add-ons for updates so that any compatibility updates will
              // be applied
              aAddon.findUpdates({
                onUpdateAvailable: function BUC_onUpdateAvailable(aAddon, aInstall) {
                  // Start installing updates when the add-on can be updated and
                  // background updates should be applied.
                  if (aAddon.permissions & AddonManager.PERM_CAN_UPGRADE &&
                      AddonManager.shouldAutoUpdate(aAddon)) {
                    aInstall.install();
                  }
                },

                onUpdateFinished: notifyComplete
              }, AddonManager.UPDATE_WHEN_PERIODIC_UPDATE);
            });

            notifyComplete();
          });
        });
      });
    }

    if (checkHotfix) {
      var hotfixVersion = "";
      try {
        hotfixVersion = Services.prefs.getCharPref(PREF_EM_HOTFIX_LASTVERSION);
      }
      catch (e) { }

      let url = null;
      if (Services.prefs.getPrefType(PREF_EM_HOTFIX_URL) == Ci.nsIPrefBranch.PREF_STRING)
        url = Services.prefs.getCharPref(PREF_EM_HOTFIX_URL);
      else
        url = Services.prefs.getCharPref(PREF_EM_UPDATE_URL);

      // Build the URI from a fake add-on data.
      url = AddonManager.escapeAddonURI({
        id: hotfixID,
        version: hotfixVersion,
        userDisabled: false,
        appDisabled: false
      }, url);

      pendingUpdates++;
      Components.utils.import("resource://gre/modules/AddonUpdateChecker.jsm");
      AddonUpdateChecker.checkForUpdates(hotfixID, "extension", null, url, {
        onUpdateCheckComplete: function(aUpdates) {
          let update = AddonUpdateChecker.getNewestCompatibleUpdate(aUpdates);
          if (!update) {
            notifyComplete();
            return;
          }

          // If the available version isn't newer than the last installed
          // version then ignore it.
          if (Services.vc.compare(hotfixVersion, update.version) >= 0) {
            notifyComplete();
            return;
          }

          LOG("Downloading hotfix version " + update.version);
          AddonManager.getInstallForURL(update.updateURL, function(aInstall) {
            aInstall.addListener({
              onDownloadEnded: function(aInstall) {
                try {
                  if (!Services.prefs.getBoolPref(PREF_EM_CERT_CHECKATTRIBUTES))
                    return;
                }
                catch (e) {
                  // By default don't do certificate checks.
                  return;
                }

                try {
                  CertUtils.validateCert(aInstall.certificate,
                                         CertUtils.readCertPrefs(PREF_EM_HOTFIX_CERTS));
                }
                catch (e) {
                  WARN("The hotfix add-on was not signed by the expected " +
                       "certificate and so will not be installed.");
                  aInstall.cancel();
                }
              },

              onInstallEnded: function(aInstall) {
                // Remember the last successfully installed version.
                Services.prefs.setCharPref(PREF_EM_HOTFIX_LASTVERSION,
                                           aInstall.version);
              },

              onInstallCancelled: function(aInstall) {
                // Revert to the previous version if the installation was
                // cancelled.
                Services.prefs.setCharPref(PREF_EM_HOTFIX_LASTVERSION,
                                           hotfixVersion);
              }
            });

            aInstall.install();

            notifyComplete();
          }, "application/x-xpinstall", update.updateHash, null,
             null, update.version);
        },

        onUpdateCheckError: notifyComplete
      });
    }

    notifyComplete();
  },

  /**
   * Adds a add-on to the list of detected changes for this startup. If
   * addStartupChange is called multiple times for the same add-on in the same
   * startup then only the most recent change will be remembered.
   *
   * @param  aType
   *         The type of change as a string. Providers can define their own
   *         types of changes or use the existing defined STARTUP_CHANGE_*
   *         constants
   * @param  aID
   *         The ID of the add-on
   */
  addStartupChange: function AMI_addStartupChange(aType, aID) {
    if (gStarted)
      return;

    // Ensure that an ID is only listed in one type of change
    for (let type in this.startupChanges)
      this.removeStartupChange(type, aID);

    if (!(aType in this.startupChanges))
      this.startupChanges[aType] = [];
    this.startupChanges[aType].push(aID);
  },

  /**
   * Removes a startup change for an add-on.
   *
   * @param  aType
   *         The type of change
   * @param  aID
   *         The ID of the add-on
   */
  removeStartupChange: function AMI_removeStartupChange(aType, aID) {
    if (gStarted)
      return;

    if (!(aType in this.startupChanges))
      return;

    this.startupChanges[aType] = this.startupChanges[aType].filter(function(aItem) aItem != aID);
  },

  /**
   * Calls all registered InstallListeners with an event. Any parameters after
   * the extraListeners parameter are passed to the listener.
   *
   * @param  aMethod
   *         The method on the listeners to call
   * @param  aExtraListeners
   *         An array of extra InstallListeners to also call
   * @return false if any of the listeners returned false, true otherwise
   */
  callInstallListeners: function AMI_callInstallListeners(aMethod, aExtraListeners) {
    let result = true;
    let listeners = this.installListeners;
    if (aExtraListeners)
      listeners = aExtraListeners.concat(listeners);
    let args = Array.slice(arguments, 2);

    listeners.forEach(function(listener) {
      try {
        if (aMethod in listener) {
          if (listener[aMethod].apply(listener, args) === false)
            result = false;
        }
      }
      catch (e) {
        WARN("InstallListener threw exception when calling " + aMethod, e);
      }
    });
    return result;
  },

  /**
   * Calls all registered AddonListeners with an event. Any parameters after
   * the method parameter are passed to the listener.
   *
   * @param  aMethod
   *         The method on the listeners to call
   */
  callAddonListeners: function AMI_callAddonListeners(aMethod) {
    var args = Array.slice(arguments, 1);
    this.addonListeners.forEach(function(listener) {
      try {
        if (aMethod in listener)
          listener[aMethod].apply(listener, args);
      }
      catch (e) {
        WARN("AddonListener threw exception when calling " + aMethod, e);
      }
    });
  },

  /**
   * Notifies all providers that an add-on has been enabled when that type of
   * add-on only supports a single add-on being enabled at a time. This allows
   * the providers to disable theirs if necessary.
   *
   * @param  aId
   *         The id of the enabled add-on
   * @param  aType
   *         The type of the enabled add-on
   * @param  aPendingRestart
   *         A boolean indicating if the change will only take place the next
   *         time the application is restarted
   */
  notifyAddonChanged: function AMI_notifyAddonChanged(aId, aType, aPendingRestart) {
    this.providers.forEach(function(provider) {
      callProvider(provider, "addonChanged", null, aId, aType, aPendingRestart);
    });
  },

  /**
   * Notifies all providers they need to update the appDisabled property for
   * their add-ons in response to an application change such as a blocklist
   * update.
   */
  updateAddonAppDisabledStates: function AMI_updateAddonAppDisabledStates() {
    this.providers.forEach(function(provider) {
      callProvider(provider, "updateAddonAppDisabledStates");
    });
  },
  
  /**
   * Notifies all providers that the repository has updated its data for
   * installed add-ons.
   *
   * @param  aCallback
   *         Function to call when operation is complete.
   */
  updateAddonRepositoryData: function AMI_updateAddonRepositoryData(aCallback) {
    if (!aCallback)
      throw Components.Exception("Must specify aCallback",
                                 Cr.NS_ERROR_INVALID_ARG);

    new AsyncObjectCaller(this.providers, "updateAddonRepositoryData", {
      nextObject: function(aCaller, aProvider) {
        callProvider(aProvider,
                     "updateAddonRepositoryData",
                     null,
                     aCaller.callNext.bind(aCaller));
      },
      noMoreObjects: function(aCaller) {
        safeCall(aCallback);
      }
    });
  },
  /**
   * Asynchronously gets an AddonInstall for a URL.
   *
   * @param  aUrl
   *         The url the add-on is located at
   * @param  aCallback
   *         A callback to pass the AddonInstall to
   * @param  aMimetype
   *         The mimetype of the add-on
   * @param  aHash
   *         An optional hash of the add-on
   * @param  aName
   *         An optional placeholder name while the add-on is being downloaded
   * @param  aIconURL
   *         An optional placeholder icon URL while the add-on is being downloaded
   * @param  aVersion
   *         An optional placeholder version while the add-on is being downloaded
   * @param  aLoadGroup
   *         An optional nsILoadGroup to associate any network requests with
   * @throws if the aUrl, aCallback or aMimetype arguments are not specified
   */
  getInstallForURL: function AMI_getInstallForURL(aUrl, aCallback, aMimetype,
                                                  aHash, aName, aIconURL,
                                                  aVersion, aLoadGroup) {
    if (!aUrl || !aMimetype || !aCallback)
      throw new TypeError("Invalid arguments");

    for (let i = 0; i < this.providers.length; i++) {
      if (callProvider(this.providers[i], "supportsMimetype", false, aMimetype)) {
        callProvider(this.providers[i], "getInstallForURL", null,
                     aUrl, aHash, aName, aIconURL, aVersion, aLoadGroup,
                     function(aInstall) {
          safeCall(aCallback, aInstall);
        });
        return;
      }
    }
    safeCall(aCallback, null);
  },

  /**
   * Asynchronously gets an AddonInstall for an nsIFile.
   *
   * @param  aFile
   *         the nsIFile where the add-on is located
   * @param  aCallback
   *         A callback to pass the AddonInstall to
   * @param  aMimetype
   *         An optional mimetype hint for the add-on
   * @throws if the aFile or aCallback arguments are not specified
   */
  getInstallForFile: function AMI_getInstallForFile(aFile, aCallback, aMimetype) {
    if (!aFile || !aCallback)
      throw Cr.NS_ERROR_INVALID_ARG;

    new AsyncObjectCaller(this.providers, "getInstallForFile", {
      nextObject: function(aCaller, aProvider) {
        callProvider(aProvider, "getInstallForFile", null, aFile,
                     function(aInstall) {
          if (aInstall)
            safeCall(aCallback, aInstall);
          else
            aCaller.callNext();
        });
      },

      noMoreObjects: function(aCaller) {
        safeCall(aCallback, null);
      }
    });
  },

  /**
   * Asynchronously gets all current AddonInstalls optionally limiting to a list
   * of types.
   *
   * @param  aTypes
   *         An optional array of types to retrieve. Each type is a string name
   * @param  aCallback
   *         A callback which will be passed an array of AddonInstalls
   * @throws if the aCallback argument is not specified
   */
  getInstallsByTypes: function AMI_getInstallsByTypes(aTypes, aCallback) {
    if (!aCallback)
      throw Cr.NS_ERROR_INVALID_ARG;

    let installs = [];

    new AsyncObjectCaller(this.providers, "getInstallsByTypes", {
      nextObject: function(aCaller, aProvider) {
        callProvider(aProvider, "getInstallsByTypes", null, aTypes,
                     function(aProviderInstalls) {
          installs = installs.concat(aProviderInstalls);
          aCaller.callNext();
        });
      },

      noMoreObjects: function(aCaller) {
        safeCall(aCallback, installs);
      }
    });
  },

  /**
   * Asynchronously gets all current AddonInstalls.
   *
   * @param  aCallback
   *         A callback which will be passed an array of AddonInstalls
   */
  getAllInstalls: function AMI_getAllInstalls(aCallback) {
    this.getInstallsByTypes(null, aCallback);
  },

  /**
   * Checks whether installation is enabled for a particular mimetype.
   *
   * @param  aMimetype
   *         The mimetype to check
   * @return true if installation is enabled for the mimetype
   */
  isInstallEnabled: function AMI_isInstallEnabled(aMimetype) {
    for (let i = 0; i < this.providers.length; i++) {
      if (callProvider(this.providers[i], "supportsMimetype", false, aMimetype) &&
          callProvider(this.providers[i], "isInstallEnabled"))
        return true;
    }
    return false;
  },

  /**
   * Checks whether a particular source is allowed to install add-ons of a
   * given mimetype.
   *
   * @param  aMimetype
   *         The mimetype of the add-on
   * @param  aURI
   *         The uri of the source, may be null
   * @return true if the source is allowed to install this mimetype
   */
  isInstallAllowed: function AMI_isInstallAllowed(aMimetype, aURI) {
    for (let i = 0; i < this.providers.length; i++) {
      if (callProvider(this.providers[i], "supportsMimetype", false, aMimetype) &&
          callProvider(this.providers[i], "isInstallAllowed", null, aURI))
        return true;
    }
    return false;
  },

  /**
   * Starts installation of an array of AddonInstalls notifying the registered
   * web install listener of blocked or started installs.
   *
   * @param  aMimetype
   *         The mimetype of add-ons being installed
   * @param  aSource
   *         The nsIDOMWindow that started the installs
   * @param  aURI
   *         the nsIURI that started the installs
   * @param  aInstalls
   *         The array of AddonInstalls to be installed
   */
  installAddonsFromWebpage: function AMI_installAddonsFromWebpage(aMimetype,
                                                                  aSource,
                                                                  aURI,
                                                                  aInstalls) {
    if (!("@mozilla.org/addons/web-install-listener;1" in Cc)) {
      WARN("No web installer available, cancelling all installs");
      aInstalls.forEach(function(aInstall) {
        aInstall.cancel();
      });
      return;
    }

    try {
      let weblistener = Cc["@mozilla.org/addons/web-install-listener;1"].
                        getService(Ci.amIWebInstallListener);

      if (!this.isInstallEnabled(aMimetype, aURI)) {
        weblistener.onWebInstallDisabled(aSource, aURI, aInstalls,
                                         aInstalls.length);
      }
      else if (!this.isInstallAllowed(aMimetype, aURI)) {
        if (weblistener.onWebInstallBlocked(aSource, aURI, aInstalls,
                                            aInstalls.length)) {
          aInstalls.forEach(function(aInstall) {
            aInstall.install();
          });
        }
      }
      else if (weblistener.onWebInstallRequested(aSource, aURI, aInstalls,
                                                   aInstalls.length)) {
        aInstalls.forEach(function(aInstall) {
          aInstall.install();
        });
      }
    }
    catch (e) {
      // In the event that the weblistener throws during instatiation or when
      // calling onWebInstallBlocked or onWebInstallRequested all of the
      // installs should get cancelled.
      WARN("Failure calling web installer", e);
      aInstalls.forEach(function(aInstall) {
        aInstall.cancel();
      });
    }
  },

  /**
   * Adds a new InstallListener if the listener is not already registered.
   *
   * @param  aListener
   *         The InstallListener to add
   */
  addInstallListener: function AMI_addInstallListener(aListener) {
    if (!this.installListeners.some(function(i) { return i == aListener; }))
      this.installListeners.push(aListener);
  },

  /**
   * Removes an InstallListener if the listener is registered.
   *
   * @param  aListener
   *         The InstallListener to remove
   */
  removeInstallListener: function AMI_removeInstallListener(aListener) {
    let pos = 0;
    while (pos < this.installListeners.length) {
      if (this.installListeners[pos] == aListener)
        this.installListeners.splice(pos, 1);
      else
        pos++;
    }
  },

  /**
   * Asynchronously gets an add-on with a specific ID.
   *
   * @param  aId
   *         The ID of the add-on to retrieve
   * @param  aCallback
   *         The callback to pass the retrieved add-on to
   * @throws if the aId or aCallback arguments are not specified
   */
  getAddonByID: function AMI_getAddonByID(aId, aCallback) {
    if (!aId || !aCallback)
      throw Cr.NS_ERROR_INVALID_ARG;

    new AsyncObjectCaller(this.providers, "getAddonByID", {
      nextObject: function(aCaller, aProvider) {
        callProvider(aProvider, "getAddonByID", null, aId, function(aAddon) {
          if (aAddon)
            safeCall(aCallback, aAddon);
          else
            aCaller.callNext();
        });
      },

      noMoreObjects: function(aCaller) {
        safeCall(aCallback, null);
      }
    });
  },

  /**
   * Asynchronously get an add-on with a specific Sync GUID.
   *
   * @param  aGUID
   *         String GUID of add-on to retrieve
   * @param  aCallback
   *         The callback to pass the retrieved add-on to.
   * @throws if the aGUID or aCallback arguments are not specified
   */
  getAddonBySyncGUID: function AMI_getAddonBySyncGUID(aGUID, aCallback) {
    if (!aGUID || !aCallback) {
      throw Cr.NS_ERROR_INVALID_ARG;
    }

    new AsyncObjectCaller(this.providers, "getAddonBySyncGUID", {
      nextObject: function(aCaller, aProvider) {
        callProvider(aProvider, "getAddonBySyncGUID", null, aGUID, function(aAddon) {
          if (aAddon) {
            safeCall(aCallback, aAddon);
          } else {
            aCaller.callNext();
          }
        });
      },

      noMoreObjects: function(aCaller) {
        safeCall(aCallback, null);
      }
    });
  },

  /**
   * Asynchronously gets an array of add-ons.
   *
   * @param  aIds
   *         The array of IDs to retrieve
   * @param  aCallback
   *         The callback to pass an array of Addons to
   * @throws if the aId or aCallback arguments are not specified
   */
  getAddonsByIDs: function AMI_getAddonsByIDs(aIds, aCallback) {
    if (!aIds || !aCallback)
      throw Cr.NS_ERROR_INVALID_ARG;

    let addons = [];

    new AsyncObjectCaller(aIds, null, {
      nextObject: function(aCaller, aId) {
        AddonManagerInternal.getAddonByID(aId, function(aAddon) {
          addons.push(aAddon);
          aCaller.callNext();
        });
      },

      noMoreObjects: function(aCaller) {
        safeCall(aCallback, addons);
      }
    });
  },

  /**
   * Asynchronously gets add-ons of specific types.
   *
   * @param  aTypes
   *         An optional array of types to retrieve. Each type is a string name
   * @param  aCallback
   *         The callback to pass an array of Addons to.
   * @throws if the aCallback argument is not specified
   */
  getAddonsByTypes: function AMI_getAddonsByTypes(aTypes, aCallback) {
    if (!aCallback)
      throw Cr.NS_ERROR_INVALID_ARG;

    let addons = [];

    new AsyncObjectCaller(this.providers, "getAddonsByTypes", {
      nextObject: function(aCaller, aProvider) {
        callProvider(aProvider, "getAddonsByTypes", null, aTypes,
                     function(aProviderAddons) {
          addons = addons.concat(aProviderAddons);
          aCaller.callNext();
        });
      },

      noMoreObjects: function(aCaller) {
        safeCall(aCallback, addons);
      }
    });
  },

  /**
   * Asynchronously gets all installed add-ons.
   *
   * @param  aCallback
   *         A callback which will be passed an array of Addons
   */
  getAllAddons: function AMI_getAllAddons(aCallback) {
    this.getAddonsByTypes(null, aCallback);
  },

  /**
   * Asynchronously gets add-ons that have operations waiting for an application
   * restart to complete.
   *
   * @param  aTypes
   *         An optional array of types to retrieve. Each type is a string name
   * @param  aCallback
   *         The callback to pass the array of Addons to
   * @throws if the aCallback argument is not specified
   */
  getAddonsWithOperationsByTypes:
  function AMI_getAddonsWithOperationsByTypes(aTypes, aCallback) {
    if (!aCallback)
      throw Cr.NS_ERROR_INVALID_ARG;

    let addons = [];

    new AsyncObjectCaller(this.providers, "getAddonsWithOperationsByTypes", {
      nextObject: function(aCaller, aProvider) {
        callProvider(aProvider, "getAddonsWithOperationsByTypes", null, aTypes,
                     function(aProviderAddons) {
          addons = addons.concat(aProviderAddons);
          aCaller.callNext();
        });
      },

      noMoreObjects: function(caller) {
        safeCall(aCallback, addons);
      }
    });
  },

  /**
   * Adds a new AddonListener if the listener is not already registered.
   *
   * @param  aListener
   *         The listener to add
   */
  addAddonListener: function AMI_addAddonListener(aListener) {
    if (!this.addonListeners.some(function(i) { return i == aListener; }))
      this.addonListeners.push(aListener);
  },

  /**
   * Removes an AddonListener if the listener is registered.
   *
   * @param  aListener
   *         The listener to remove
   */
  removeAddonListener: function AMI_removeAddonListener(aListener) {
    let pos = 0;
    while (pos < this.addonListeners.length) {
      if (this.addonListeners[pos] == aListener)
        this.addonListeners.splice(pos, 1);
      else
        pos++;
    }
  },

  addTypeListener: function AMI_addTypeListener(aListener) {
    if (!this.typeListeners.some(function(i) { return i == aListener; }))
      this.typeListeners.push(aListener);
  },

  removeTypeListener: function AMI_removeTypeListener(aListener) {
    let pos = 0;
    while (pos < this.typeListeners.length) {
      if (this.typeListeners[pos] == aListener)
        this.typeListeners.splice(pos, 1);
      else
        pos++;
    }
  },

  get addonTypes() {
    return this.typesProxy;
  },

  get autoUpdateDefault() {
    try {
      return Services.prefs.getBoolPref(PREF_EM_AUTOUPDATE_DEFAULT);
    } catch(e) { }
    return true;
  },

  get strictCompatibility() {
    return gStrictCompatibility;
  }
};

/**
 * Should not be used outside of core Mozilla code. This is a private API for
 * the startup and platform integration code to use. Refer to the methods on
 * AddonManagerInternal for documentation however note that these methods are
 * subject to change at any time.
 */
var AddonManagerPrivate = {
  startup: function AMP_startup() {
    AddonManagerInternal.startup();
  },

  registerProvider: function AMP_registerProvider(aProvider, aTypes) {
    AddonManagerInternal.registerProvider(aProvider, aTypes);
  },

  unregisterProvider: function AMP_unregisterProvider(aProvider) {
    AddonManagerInternal.unregisterProvider(aProvider);
  },

  shutdown: function AMP_shutdown() {
    AddonManagerInternal.shutdown();
  },

  backgroundUpdateCheck: function AMP_backgroundUpdateCheck() {
    AddonManagerInternal.backgroundUpdateCheck();
  },

  addStartupChange: function AMP_addStartupChange(aType, aID) {
    AddonManagerInternal.addStartupChange(aType, aID);
  },

  removeStartupChange: function AMP_removeStartupChange(aType, aID) {
    AddonManagerInternal.removeStartupChange(aType, aID);
  },

  notifyAddonChanged: function AMP_notifyAddonChanged(aId, aType, aPendingRestart) {
    AddonManagerInternal.notifyAddonChanged(aId, aType, aPendingRestart);
  },

  updateAddonAppDisabledStates: function AMP_updateAddonAppDisabledStates() {
    AddonManagerInternal.updateAddonAppDisabledStates();
  },

  updateAddonRepositoryData: function AMP_updateAddonRepositoryData(aCallback) {
    AddonManagerInternal.updateAddonRepositoryData(aCallback);
  },

  callInstallListeners: function AMP_callInstallListeners(aMethod) {
    return AddonManagerInternal.callInstallListeners.apply(AddonManagerInternal,
                                                           arguments);
  },

  callAddonListeners: function AMP_callAddonListeners(aMethod) {
    AddonManagerInternal.callAddonListeners.apply(AddonManagerInternal, arguments);
  },

  AddonAuthor: AddonAuthor,

  AddonScreenshot: AddonScreenshot,

  AddonCompatibilityOverride: AddonCompatibilityOverride,

  AddonType: AddonType
};

/**
 * This is the public API that UI and developers should be calling. All methods
 * just forward to AddonManagerInternal.
 */
var AddonManager = {
  // Constants for the AddonInstall.state property
  // The install is available for download.
  STATE_AVAILABLE: 0,
  // The install is being downloaded.
  STATE_DOWNLOADING: 1,
  // The install is checking for compatibility information.
  STATE_CHECKING: 2,
  // The install is downloaded and ready to install.
  STATE_DOWNLOADED: 3,
  // The download failed.
  STATE_DOWNLOAD_FAILED: 4,
  // The add-on is being installed.
  STATE_INSTALLING: 5,
  // The add-on has been installed.
  STATE_INSTALLED: 6,
  // The install failed.
  STATE_INSTALL_FAILED: 7,
  // The install has been cancelled.
  STATE_CANCELLED: 8,

  // Constants representing different types of errors while downloading an
  // add-on.
  // The download failed due to network problems.
  ERROR_NETWORK_FAILURE: -1,
  // The downloaded file did not match the provided hash.
  ERROR_INCORRECT_HASH: -2,
  // The downloaded file seems to be corrupted in some way.
  ERROR_CORRUPT_FILE: -3,
  // An error occured trying to write to the filesystem.
  ERROR_FILE_ACCESS: -4,

  // These must be kept in sync with AddonUpdateChecker.
  // No error was encountered.
  UPDATE_STATUS_NO_ERROR: 0,
  // The update check timed out
  UPDATE_STATUS_TIMEOUT: -1,
  // There was an error while downloading the update information.
  UPDATE_STATUS_DOWNLOAD_ERROR: -2,
  // The update information was malformed in some way.
  UPDATE_STATUS_PARSE_ERROR: -3,
  // The update information was not in any known format.
  UPDATE_STATUS_UNKNOWN_FORMAT: -4,
  // The update information was not correctly signed or there was an SSL error.
  UPDATE_STATUS_SECURITY_ERROR: -5,

  // Constants to indicate why an update check is being performed
  // Update check has been requested by the user.
  UPDATE_WHEN_USER_REQUESTED: 1,
  // Update check is necessary to see if the Addon is compatibile with a new
  // version of the application.
  UPDATE_WHEN_NEW_APP_DETECTED: 2,
  // Update check is necessary because a new application has been installed.
  UPDATE_WHEN_NEW_APP_INSTALLED: 3,
  // Update check is a regular background update check.
  UPDATE_WHEN_PERIODIC_UPDATE: 16,
  // Update check is needed to check an Addon that is being installed.
  UPDATE_WHEN_ADDON_INSTALLED: 17,

  // Constants for operations in Addon.pendingOperations
  // Indicates that the Addon has no pending operations.
  PENDING_NONE: 0,
  // Indicates that the Addon will be enabled after the application restarts.
  PENDING_ENABLE: 1,
  // Indicates that the Addon will be disabled after the application restarts.
  PENDING_DISABLE: 2,
  // Indicates that the Addon will be uninstalled after the application restarts.
  PENDING_UNINSTALL: 4,
  // Indicates that the Addon will be installed after the application restarts.
  PENDING_INSTALL: 8,
  PENDING_UPGRADE: 16,

  // Constants for operations in Addon.operationsRequiringRestart
  // Indicates that restart isn't required for any operation.
  OP_NEEDS_RESTART_NONE: 0,
  // Indicates that restart is required for enabling the addon.
  OP_NEEDS_RESTART_ENABLE: 1,
  // Indicates that restart is required for disabling the addon.
  OP_NEEDS_RESTART_DISABLE: 2,
  // Indicates that restart is required for uninstalling the addon.
  OP_NEEDS_RESTART_UNINSTALL: 4,
  // Indicates that restart is required for installing the addon.
  OP_NEEDS_RESTART_INSTALL: 8,

  // Constants for permissions in Addon.permissions.
  // Indicates that the Addon can be uninstalled.
  PERM_CAN_UNINSTALL: 1,
  // Indicates that the Addon can be enabled by the user.
  PERM_CAN_ENABLE: 2,
  // Indicates that the Addon can be disabled by the user.
  PERM_CAN_DISABLE: 4,
  // Indicates that the Addon can be upgraded.
  PERM_CAN_UPGRADE: 8,

  // General descriptions of where items are installed.
  // Installed in this profile.
  SCOPE_PROFILE: 1,
  // Installed for all of this user's profiles.
  SCOPE_USER: 2,
  // Installed and owned by the application.
  SCOPE_APPLICATION: 4,
  // Installed for all users of the computer.
  SCOPE_SYSTEM: 8,
  // The combination of all scopes.
  SCOPE_ALL: 15,

  // 1-15 are different built-in views for the add-on type
  VIEW_TYPE_LIST: "list",

  TYPE_UI_HIDE_EMPTY: 16,

  // Constants for Addon.applyBackgroundUpdates.
  // Indicates that the Addon should not update automatically.
  AUTOUPDATE_DISABLE: 0,
  // Indicates that the Addon should update automatically only if
  // that's the global default.
  AUTOUPDATE_DEFAULT: 1,
  // Indicates that the Addon should update automatically.
  AUTOUPDATE_ENABLE: 2,

  // Constants for how Addon options should be shown.
  // Options will be opened in a new window
  OPTIONS_TYPE_DIALOG: 1,
  // Options will be displayed within the AM detail view
  OPTIONS_TYPE_INLINE: 2,
  // Options will be displayed in a new tab, if possible
  OPTIONS_TYPE_TAB: 3,

  // Constants for getStartupChanges, addStartupChange and removeStartupChange
  // Add-ons that were detected as installed during startup. Doesn't include
  // add-ons that were pending installation the last time the application ran.
  STARTUP_CHANGE_INSTALLED: "installed",
  // Add-ons that were detected as changed during startup. This includes an
  // add-on moving to a different location, changing version or just having
  // been detected as possibly changed.
  STARTUP_CHANGE_CHANGED: "changed",
  // Add-ons that were detected as uninstalled during startup. Doesn't include
  // add-ons that were pending uninstallation the last time the application ran.
  STARTUP_CHANGE_UNINSTALLED: "uninstalled",
  // Add-ons that were detected as disabled during startup, normally because of
  // an application change making an add-on incompatible. Doesn't include
  // add-ons that were pending being disabled the last time the application ran.
  STARTUP_CHANGE_DISABLED: "disabled",
  // Add-ons that were detected as enabled during startup, normally because of
  // an application change making an add-on compatible. Doesn't include
  // add-ons that were pending being enabled the last time the application ran.
  STARTUP_CHANGE_ENABLED: "enabled",

  getInstallForURL: function AM_getInstallForURL(aUrl, aCallback, aMimetype,
                                                 aHash, aName, aIconURL,
                                                 aVersion, aLoadGroup) {
    AddonManagerInternal.getInstallForURL(aUrl, aCallback, aMimetype, aHash,
                                          aName, aIconURL, aVersion, aLoadGroup);
  },

  getInstallForFile: function AM_getInstallForFile(aFile, aCallback, aMimetype) {
    AddonManagerInternal.getInstallForFile(aFile, aCallback, aMimetype);
  },

  /**
   * Gets an array of add-on IDs that changed during the most recent startup.
   *
   * @param  aType
   *         The type of startup change to get
   * @return An array of add-on IDs
   */
  getStartupChanges: function AM_getStartupChanges(aType) {
    if (!(aType in AddonManagerInternal.startupChanges))
      return [];
    return AddonManagerInternal.startupChanges[aType].slice(0);
  },

  getAddonByID: function AM_getAddonByID(aId, aCallback) {
    AddonManagerInternal.getAddonByID(aId, aCallback);
  },

  getAddonBySyncGUID: function AM_getAddonBySyncGUID(aId, aCallback) {
    AddonManagerInternal.getAddonBySyncGUID(aId, aCallback);
  },

  getAddonsByIDs: function AM_getAddonsByIDs(aIds, aCallback) {
    AddonManagerInternal.getAddonsByIDs(aIds, aCallback);
  },

  getAddonsWithOperationsByTypes:
  function AM_getAddonsWithOperationsByTypes(aTypes, aCallback) {
    AddonManagerInternal.getAddonsWithOperationsByTypes(aTypes, aCallback);
  },

  getAddonsByTypes: function AM_getAddonsByTypes(aTypes, aCallback) {
    AddonManagerInternal.getAddonsByTypes(aTypes, aCallback);
  },

  getAllAddons: function AM_getAllAddons(aCallback) {
    AddonManagerInternal.getAllAddons(aCallback);
  },

  getInstallsByTypes: function AM_getInstallsByTypes(aTypes, aCallback) {
    AddonManagerInternal.getInstallsByTypes(aTypes, aCallback);
  },

  getAllInstalls: function AM_getAllInstalls(aCallback) {
    AddonManagerInternal.getAllInstalls(aCallback);
  },

  isInstallEnabled: function AM_isInstallEnabled(aType) {
    return AddonManagerInternal.isInstallEnabled(aType);
  },

  isInstallAllowed: function AM_isInstallAllowed(aType, aUri) {
    return AddonManagerInternal.isInstallAllowed(aType, aUri);
  },

  installAddonsFromWebpage: function AM_installAddonsFromWebpage(aType, aSource,
                                                                 aUri, aInstalls) {
    AddonManagerInternal.installAddonsFromWebpage(aType, aSource, aUri, aInstalls);
  },

  addInstallListener: function AM_addInstallListener(aListener) {
    AddonManagerInternal.addInstallListener(aListener);
  },

  removeInstallListener: function AM_removeInstallListener(aListener) {
    AddonManagerInternal.removeInstallListener(aListener);
  },

  addAddonListener: function AM_addAddonListener(aListener) {
    AddonManagerInternal.addAddonListener(aListener);
  },

  removeAddonListener: function AM_removeAddonListener(aListener) {
    AddonManagerInternal.removeAddonListener(aListener);
  },

  addTypeListener: function AM_addTypeListener(aListener) {
    AddonManagerInternal.addTypeListener(aListener);
  },

  removeTypeListener: function AM_removeTypeListener(aListener) {
    AddonManagerInternal.removeTypeListener(aListener);
  },

  get addonTypes() {
    return AddonManagerInternal.addonTypes;
  },

  get autoUpdateDefault() {
    return AddonManagerInternal.autoUpdateDefault;
  },

  shouldAutoUpdate: function AM_shouldAutoUpdate(aAddon) {
    if (!("applyBackgroundUpdates" in aAddon))
      return false;
    if (aAddon.applyBackgroundUpdates == AddonManager.AUTOUPDATE_ENABLE)
      return true;
    if (aAddon.applyBackgroundUpdates == AddonManager.AUTOUPDATE_DISABLE)
      return false;
    return this.autoUpdateDefault;
  },

  get strictCompatibility() {
    return AddonManagerInternal.strictCompatibility;
  },

  escapeAddonURI: function AM_escapeAddonURI(aAddon, aUri, aAppVersion) {
    return AddonManagerInternal.escapeAddonURI(aAddon, aUri, aAppVersion);
  }
};

Object.freeze(AddonManagerInternal);
Object.freeze(AddonManagerPrivate);
Object.freeze(AddonManager);

# -*- Mode: Java; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
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
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is
# Mike Connor.
# Portions created by the Initial Developer are Copyright (C) 2005
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Mike Connor <mconnor@steelgryphon.com>
#   Asaf Romano <mozilla.mano@sent.com>
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

function restartApp() {
  var appStartup = Components.classes["@mozilla.org/toolkit/app-startup;1"]
                             .getService(Components.interfaces.nsIAppStartup);
  appStartup.quit(appStartup.eForceQuit | appStartup.eRestart);
}

function clearAllPrefs() {
  var prefService = Components.classes["@mozilla.org/preferences-service;1"]
                              .getService(Components.interfaces.nsIPrefService);
  prefService.resetUserPrefs();

  // Remove the pref-overrides dir, if it exists
  try {
    var fileLocator = Components.classes["@mozilla.org/file/directory_service;1"]
                                .getService(Components.interfaces.nsIProperties);
    const NS_APP_PREFS_OVERRIDE_DIR = "PrefDOverride";
    var prefOverridesDir = fileLocator.get(NS_APP_PREFS_OVERRIDE_DIR,
                                           Components.interfaces.nsIFile);
    prefOverridesDir.remove(true);
  } catch (ex) {
    Components.utils.reportError(ex);
  }
}

function restoreDefaultBookmarks() {
  var prefBranch  = Components.classes["@mozilla.org/preferences-service;1"]
                              .getService(Components.interfaces.nsIPrefBranch);
  prefBranch.setBoolPref("browser.bookmarks.restore_default_bookmarks", true);
}

function deleteLocalstore() {
  const nsIDirectoryServiceContractID = "@mozilla.org/file/directory_service;1";
  const nsIProperties = Components.interfaces.nsIProperties;
  var directoryService =  Components.classes[nsIDirectoryServiceContractID]
                                    .getService(nsIProperties);
  var localstoreFile = directoryService.get("LStoreS", Components.interfaces.nsIFile);
  if (localstoreFile.exists())
    localstoreFile.remove(false);
}

function disableAddons() {
  // Disable addons
  const nsIUpdateItem = Components.interfaces.nsIUpdateItem;
  var em = Components.classes["@mozilla.org/extensions/manager;1"]
                     .getService(Components.interfaces.nsIExtensionManager);
  var type = nsIUpdateItem.TYPE_EXTENSION + nsIUpdateItem.TYPE_LOCALE;
  var items = em.getItemList(type, { });
  for (var i = 0; i < items.length; ++i)
    em.disableItem(items[i].id);

  // Select the default theme
  var prefB = Components.classes["@mozilla.org/preferences-service;1"]
                        .getService(Components.interfaces.nsIPrefBranch);
  if (prefB.prefHasUserValue("general.skins.selectedSkin"))
    prefB.clearUserPref("general.skins.selectedSkin");

  // Disable plugins
  var phs = Components.classes["@mozilla.org/plugin/host;1"]
                      .getService(Components.interfaces.nsIPluginHost);
  var plugins = phs.getPluginTags({ });
  for (i = 0; i < plugins.length; ++i)
    plugins[i].disabled = true;
}

function restoreDefaultSearchEngines() {
  var searchService = Components.classes["@mozilla.org/browser/search-service;1"]
                                .getService(Components.interfaces.nsIBrowserSearchService);

  searchService.restoreDefaultEngines();
}

function onOK() {
  try {
    if (document.getElementById("resetUserPrefs").checked)
      clearAllPrefs();
    if (document.getElementById("deleteBookmarks").checked)
      restoreDefaultBookmarks();
    if (document.getElementById("resetToolbars").checked)
      deleteLocalstore();
    if (document.getElementById("disableAddons").checked)
      disableAddons();
    if (document.getElementById("restoreSearch").checked)
      restoreDefaultSearchEngines();
  } catch(e) {
  }

  restartApp();
}

function onCancel() {
  var appStartup = Components.classes["@mozilla.org/toolkit/app-startup;1"]
                             .getService(Components.interfaces.nsIAppStartup);
  appStartup.quit(appStartup.eForceQuit);
}

function onLoad() {
  document.getElementById("tasks")
          .addEventListener("CheckboxStateChange", UpdateOKButtonState, false);
}

function UpdateOKButtonState() {
  document.documentElement.getButton("accept").disabled = 
    !document.getElementById("resetUserPrefs").checked &&
    !document.getElementById("deleteBookmarks").checked &&
    !document.getElementById("resetToolbars").checked &&
    !document.getElementById("disableAddons").checked &&
    !document.getElementById("restoreSearch").checked;
}

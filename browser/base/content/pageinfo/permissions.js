/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const UNKNOWN = nsIPermissionManager.UNKNOWN_ACTION;   // 0
const ALLOW = nsIPermissionManager.ALLOW_ACTION;       // 1
const BLOCK = nsIPermissionManager.DENY_ACTION;        // 2
const SESSION = nsICookiePermission.ACCESS_SESSION;    // 8

const nsIQuotaManager = Components.interfaces.nsIQuotaManager;

var gPermURI;
var gPrefs;
var gUsageRequest;

var gPermObj = {
  image: function getImageDefaultPermission()
  {
    if (gPrefs.getIntPref("permissions.default.image") == 2)
      return BLOCK;
    return ALLOW;
  },
  cookie: function getCookieDefaultPermission()
  {
    if (gPrefs.getIntPref("network.cookie.cookieBehavior") == 2)
      return BLOCK;

    if (gPrefs.getIntPref("network.cookie.lifetimePolicy") == 2)
      return SESSION;
    return ALLOW;
  },
  "desktop-notification": function getNotificationDefaultPermission()
  {
    return BLOCK;
  },
  popup: function getPopupDefaultPermission()
  {
    if (gPrefs.getBoolPref("dom.disable_open_during_load"))
      return BLOCK;
    return ALLOW;
  },
  install: function getInstallDefaultPermission()
  {
    try {
      if (!gPrefs.getBoolPref("xpinstall.whitelist.required"))
        return ALLOW;
    }
    catch (e) {
    }
    return BLOCK;
  },
  geo: function getGeoDefaultPermissions()
  {
    return BLOCK;
  },
  indexedDB: function getIndexedDBDefaultPermissions()
  {
    return UNKNOWN;
  },
  plugins: function getPluginsDefaultPermissions()
  {
    return UNKNOWN;
  },
  fullscreen: function getFullscreenDefaultPermissions()
  {
    return UNKNOWN;  
  },
  pointerLock: function getPointerLockPermissions()
  {
    return BLOCK;
  },
};

var permissionObserver = {
  observe: function (aSubject, aTopic, aData)
  {
    if (aTopic == "perm-changed") {
      var permission = aSubject.QueryInterface(Components.interfaces.nsIPermission);
      if (permission.host == gPermURI.host) {
        if (permission.type in gPermObj)
          initRow(permission.type);
        else if (permission.type.startsWith("plugin"))
          setPluginsRadioState();
      }
    }
  }
};

function onLoadPermission()
{
  gPrefs = Components.classes[PREFERENCES_CONTRACTID]
                     .getService(Components.interfaces.nsIPrefBranch);

  var uri = gDocument.documentURIObject;
  var permTab = document.getElementById("permTab");
  if (/^https?$/.test(uri.scheme)) {
    gPermURI = uri;
    var hostText = document.getElementById("hostText");
    hostText.value = gPermURI.host;

    for (var i in gPermObj)
      initRow(i);
    var os = Components.classes["@mozilla.org/observer-service;1"]
                       .getService(Components.interfaces.nsIObserverService);
    os.addObserver(permissionObserver, "perm-changed", false);
    onUnloadRegistry.push(onUnloadPermission);
    permTab.hidden = false;
  }
  else
    permTab.hidden = true;
}

function onUnloadPermission()
{
  var os = Components.classes["@mozilla.org/observer-service;1"]
                     .getService(Components.interfaces.nsIObserverService);
  os.removeObserver(permissionObserver, "perm-changed");

  if (gUsageRequest) {
    gUsageRequest.cancel();
    gUsageRequest = null;
  }
}

function initRow(aPartId)
{
  if (aPartId == "plugins") {
    initPluginsRow();
    return;
  }

  var permissionManager = Components.classes[PERMISSION_CONTRACTID]
                                    .getService(nsIPermissionManager);

  var checkbox = document.getElementById(aPartId + "Def");
  var command  = document.getElementById("cmd_" + aPartId + "Toggle");
  // Geolocation and PointerLock permission consumers use testExactPermission, not testPermission.
  var perm;
  if (aPartId == "geo" || aPartId == "pointerLock")
    perm = permissionManager.testExactPermission(gPermURI, aPartId);
  else
    perm = permissionManager.testPermission(gPermURI, aPartId);

  if (perm) {
    checkbox.checked = false;
    command.removeAttribute("disabled");
  }
  else {
    checkbox.checked = true;
    command.setAttribute("disabled", "true");
    perm = gPermObj[aPartId]();
  }
  setRadioState(aPartId, perm);

  if (aPartId == "indexedDB") {
    initIndexedDBRow();
  }
}

function onCheckboxClick(aPartId)
{
  var permissionManager = Components.classes[PERMISSION_CONTRACTID]
                                    .getService(nsIPermissionManager);

  var command  = document.getElementById("cmd_" + aPartId + "Toggle");
  var checkbox = document.getElementById(aPartId + "Def");
  if (checkbox.checked) {
    permissionManager.remove(gPermURI.host, aPartId);
    command.setAttribute("disabled", "true");
    var perm = gPermObj[aPartId]();
    setRadioState(aPartId, perm);
  }
  else {
    onRadioClick(aPartId);
    command.removeAttribute("disabled");
  }
}

function onPluginRadioClick(aEvent) {
  onRadioClick(aEvent.originalTarget.getAttribute("id").split('#')[0]);
}

function onRadioClick(aPartId)
{
  var permissionManager = Components.classes[PERMISSION_CONTRACTID]
                                    .getService(nsIPermissionManager);

  var radioGroup = document.getElementById(aPartId + "RadioGroup");
  var id = radioGroup.selectedItem.id;
  var permission = id.split('#')[1];
  if (permission == UNKNOWN) {
    permissionManager.remove(gPermURI.host, aPartId);
  } else {
    permissionManager.add(gPermURI, aPartId, permission);
  }
  if (aPartId == "indexedDB" &&
      (permission == ALLOW || permission == BLOCK)) {
    permissionManager.remove(gPermURI.host, "indexedDB-unlimited");
  }
}

function setRadioState(aPartId, aValue)
{
  var radio = document.getElementById(aPartId + "#" + aValue);
  radio.radioGroup.selectedItem = radio;
}

function initIndexedDBRow()
{
  var quotaManager = Components.classes["@mozilla.org/dom/quota/manager;1"]
                               .getService(nsIQuotaManager);
  gUsageRequest =
    quotaManager.getUsageForURI(gPermURI, onIndexedDBUsageCallback);

  var status = document.getElementById("indexedDBStatus");
  var button = document.getElementById("indexedDBClear");

  status.value = "";
  status.setAttribute("hidden", "true");
  button.setAttribute("hidden", "true");
}

function onIndexedDBClear()
{
  Components.classes["@mozilla.org/dom/quota/manager;1"]
            .getService(nsIQuotaManager)
            .clearStoragesForURI(gPermURI);

  var permissionManager = Components.classes[PERMISSION_CONTRACTID]
                                    .getService(nsIPermissionManager);
  permissionManager.remove(gPermURI.host, "indexedDB-unlimited");
  initIndexedDBRow();
}

function onIndexedDBUsageCallback(uri, usage, fileUsage)
{
  if (!uri.equals(gPermURI)) {
    throw new Error("Callback received for bad URI: " + uri);
  }

  if (usage) {
    if (!("DownloadUtils" in window)) {
      Components.utils.import("resource://gre/modules/DownloadUtils.jsm");
    }

    var status = document.getElementById("indexedDBStatus");
    var button = document.getElementById("indexedDBClear");

    status.value =
      gBundle.getFormattedString("indexedDBUsage",
                                 DownloadUtils.convertByteUnits(usage));
    status.removeAttribute("hidden");
    button.removeAttribute("hidden");
  }
}

// XXX copied this from browser-plugins.js - is there a way to share?
function makeNicePluginName(aName) {
  if (aName == "Shockwave Flash")
    return "Adobe Flash";

  // Clean up the plugin name by stripping off any trailing version numbers
  // or "plugin". EG, "Foo Bar Plugin 1.23_02" --> "Foo Bar"
  // Do this by first stripping the numbers, etc. off the end, and then
  // removing "Plugin" (and then trimming to get rid of any whitespace).
  // (Otherwise, something like "Java(TM) Plug-in 1.7.0_07" gets mangled)
  let newName = aName.replace(/[\s\d\.\-\_\(\)]+$/, "").replace(/\bplug-?in\b/i, "").trim();
  return newName;
}

function fillInPluginPermissionTemplate(aPluginName, aPermissionString) {
  let permPluginTemplate = document.getElementById("permPluginTemplate").cloneNode(true);
  permPluginTemplate.setAttribute("permString", aPermissionString);
  let attrs = [
    [ ".permPluginTemplateLabel", "value", aPluginName ],
    [ ".permPluginTemplateRadioGroup", "id", aPermissionString + "RadioGroup" ],
    [ ".permPluginTemplateRadioDefault", "id", aPermissionString + "#0" ],
    [ ".permPluginTemplateRadioAsk", "id", aPermissionString + "#3" ],
    [ ".permPluginTemplateRadioAllow", "id", aPermissionString + "#1" ],
    [ ".permPluginTemplateRadioBlock", "id", aPermissionString + "#2" ]
  ];

  for (let attr of attrs) {
    permPluginTemplate.querySelector(attr[0]).setAttribute(attr[1], attr[2]);
  }

  return permPluginTemplate;
}

function clearPluginPermissionTemplate() {
  let permPluginTemplate = document.getElementById("permPluginTemplate");
  permPluginTemplate.hidden = true;
  permPluginTemplate.removeAttribute("permString");
  document.querySelector(".permPluginTemplateLabel").removeAttribute("value");
  document.querySelector(".permPluginTemplateRadioGroup").removeAttribute("id");
  document.querySelector(".permPluginTemplateRadioAsk").removeAttribute("id");
  document.querySelector(".permPluginTemplateRadioAllow").removeAttribute("id");
  document.querySelector(".permPluginTemplateRadioBlock").removeAttribute("id");
}

function initPluginsRow() {
  let vulnerableLabel = document.getElementById("browserBundle").getString("pluginActivateVulnerable.label");
  let pluginHost = Components.classes["@mozilla.org/plugin/host;1"].getService(Components.interfaces.nsIPluginHost);

  let permissionMap = Map();

  for (let plugin of pluginHost.getPluginTags()) {
    if (plugin.disabled) {
      continue;
    }
    for (let mimeType of plugin.getMimeTypes()) {
      let permString = pluginHost.getPermissionStringForType(mimeType);
      if (!permissionMap.has(permString)) {
        var name = makeNicePluginName(plugin.name);
        if (permString.startsWith("plugin-vulnerable:")) {
          name += " \u2014 " + vulnerableLabel;
        }
        permissionMap.set(permString, name);
      }
    }
  }

  let entries = [{name: item[1], permission: item[0]} for (item of permissionMap)];
  entries.sort(function(a, b) {
    return a.name < b.name ? -1 : (a.name == b.name ? 0 : 1);
  });

  let permissionEntries = [
    fillInPluginPermissionTemplate(p.name, p.permission) for (p of entries)
  ];

  let permPluginsRow = document.getElementById("permPluginsRow");
  clearPluginPermissionTemplate();
  if (permissionEntries.length < 1) {
    permPluginsRow.hidden = true;
    return;
  }

  for (let permissionEntry of permissionEntries) {
    permPluginsRow.appendChild(permissionEntry);
  }

  setPluginsRadioState();
}

function setPluginsRadioState() {
  var permissionManager = Components.classes[PERMISSION_CONTRACTID]
                                    .getService(nsIPermissionManager);
  let box = document.getElementById("permPluginsRow");
  for (let permissionEntry of box.childNodes) {
    if (permissionEntry.hasAttribute("permString")) {
      let permString = permissionEntry.getAttribute("permString");
      let permission = permissionManager.testPermission(gPermURI, permString);
      setRadioState(permString, permission);
    }
  }
}

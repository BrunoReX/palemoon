/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = [ "gDevTools", "DevTools", "gDevToolsBrowser" ];

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/commonjs/promise/core.js");
Cu.import("resource:///modules/devtools/EventEmitter.jsm");
Cu.import("resource:///modules/devtools/ToolDefinitions.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Toolbox",
  "resource:///modules/devtools/Toolbox.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "TargetFactory",
  "resource:///modules/devtools/Target.jsm");

const FORBIDDEN_IDS = new Set("toolbox", "");

/**
 * DevTools is a class that represents a set of developer tools, it holds a
 * set of tools and keeps track of open toolboxes in the browser.
 */
this.DevTools = function DevTools() {
  this._tools = new Map();
  this._toolboxes = new Map();

  // destroy() is an observer's handler so we need to preserve context.
  this.destroy = this.destroy.bind(this);

  EventEmitter.decorate(this);

  Services.obs.addObserver(this.destroy, "quit-application", false);

  // Register the set of default tools
  for (let definition of defaultTools) {
    this.registerTool(definition);
  }
}

DevTools.prototype = {
  /**
   * Register a new developer tool.
   *
   * A definition is a light object that holds different information about a
   * developer tool. This object is not supposed to have any operational code.
   * See it as a "manifest".
   * The only actual code lives in the build() function, which will be used to
   * start an instance of this tool.
   *
   * Each toolDefinition has the following properties:
   * - id: Unique identifier for this tool (string|required)
   * - killswitch: Property name to allow us to turn this tool on/off globally
   *               (string|required) (TODO: default to devtools.{id}.enabled?)
   * - icon: URL pointing to a graphic which will be used as the src for an
   *         16x16 img tag (string|required)
   * - url: URL pointing to a XUL/XHTML document containing the user interface
   *        (string|required)
   * - label: Localized name for the tool to be displayed to the user
   *          (string|required)
   * - build: Function that takes an iframe, which has been populated with the
   *          markup from |url|, and also the toolbox containing the panel.
   *          And returns an instance of ToolPanel (function|required)
   */
  registerTool: function DT_registerTool(toolDefinition) {
    let toolId = toolDefinition.id;

    if (!toolId || FORBIDDEN_IDS.has(toolId)) {
      throw new Error("Invalid definition.id");
    }

    toolDefinition.killswitch = toolDefinition.killswitch ||
        "devtools." + toolId + ".enabled";
    this._tools.set(toolId, toolDefinition);

    this.emit("tool-registered", toolId);
  },

  /**
   * Removes all tools that match the given |toolId|
   * Needed so that add-ons can remove themselves when they are deactivated
   *
   * @param {string} toolId
   *        id of the tool to unregister
   */
  unregisterTool: function DT_unregisterTool(toolId) {
    this._tools.delete(toolId);

    this.emit("tool-unregistered", toolId);
  },

  /**
   * Allow ToolBoxes to get at the list of tools that they should populate
   * themselves with.
   *
   * @return {Map} tools
   *         A map of the the tool definitions registered in this instance
   */
  getToolDefinitionMap: function DT_getToolDefinitionMap() {
    let tools = new Map();

    for (let [key, value] of this._tools) {
      let enabled;

      try {
        enabled = Services.prefs.getBoolPref(value.killswitch);
      } catch(e) {
        enabled = true;
      }

      if (enabled) {
        tools.set(key, value);
      }
    }
    return tools;
  },

  /**
   * Tools have an inherent ordering that can't be represented in a Map so
   * getToolDefinitionArray provides an alternative representation of the
   * definitions sorted by ordinal value.
   *
   * @return {Array} tools
   *         A sorted array of the tool definitions registered in this instance
   */
  getToolDefinitionArray: function DT_getToolDefinitionArray() {
    const MAX_ORDINAL = 99;

    let definitions = [];
    for (let [id, definition] of this.getToolDefinitionMap()) {
      definitions.push(definition);
    }

    definitions.sort(function(d1, d2) {
      let o1 = (typeof d1.ordinal == "number") ? d1.ordinal : MAX_ORDINAL;
      let o2 = (typeof d2.ordinal == "number") ? d2.ordinal : MAX_ORDINAL;
      return o1 - o2;
    });

    return definitions;
  },

  /**
   * Show a Toolbox for a target (either by creating a new one, or if a toolbox
   * already exists for the target, by bring to the front the existing one)
   * If |toolId| is specified then the displayed toolbox will have the
   * specified tool selected.
   * If |hostType| is specified then the toolbox will be displayed using the
   * specified HostType.
   *
   * @param {Target} target
   *         The target the toolbox will debug
   * @param {string} toolId
   *        The id of the tool to show
   * @param {Toolbox.HostType} hostType
   *        The type of host (bottom, window, side)
   *
   * @return {Toolbox} toolbox
   *        The toolbox that was opened
   */
  showToolbox: function(target, toolId, hostType) {
    let deferred = Promise.defer();

    let toolbox = this._toolboxes.get(target);
    if (toolbox) {

      let promise = (hostType != null && toolbox.hostType != hostType) ?
          toolbox.switchHost(hostType) :
          Promise.resolve(null);

      if (toolId != null && toolbox.currentToolId != toolId) {
        promise = promise.then(function() {
          return toolbox.selectTool(toolId);
        });
      }

      return promise.then(function() {
        toolbox.raise();
        return toolbox;
      });
    }
    else {
      // No toolbox for target, create one
      toolbox = new Toolbox(target, toolId, hostType);

      this._toolboxes.set(target, toolbox);

      toolbox.once("destroyed", function() {
        this._toolboxes.delete(target);
        this.emit("toolbox-destroyed", target);
      }.bind(this));

      // If we were asked for a specific tool then we need to wait for the
      // tool to be ready, otherwise we can just wait for toolbox open
      if (toolId != null) {
        toolbox.once(toolId + "-ready", function(event, panel) {
          this.emit("toolbox-ready", toolbox);
          deferred.resolve(toolbox);
        }.bind(this));
        toolbox.open();
      }
      else {
        toolbox.open().then(function() {
          deferred.resolve(toolbox);
          this.emit("toolbox-ready", toolbox);
        }.bind(this));
      }
    }

    return deferred.promise;
  },

  /**
   * Return the toolbox for a given target.
   *
   * @param  {object} target
   *         Target value e.g. the target that owns this toolbox
   *
   * @return {Toolbox} toolbox
   *         The toobox that is debugging the given target
   */
  getToolbox: function DT_getToolbox(target) {
    return this._toolboxes.get(target);
  },

  /**
   * Close the toolbox for a given target
   */
  closeToolbox: function DT_closeToolbox(target) {
    let toolbox = this._toolboxes.get(target);
    if (toolbox == null) {
      return;
    }
    return toolbox.destroy();
  },

  /**
   * All browser windows have been closed, tidy up remaining objects.
   */
  destroy: function() {
    Services.obs.removeObserver(this.destroy, "quit-application");

    delete this._trackedBrowserWindows;
    delete this._toolboxes;
  },
};

/**
 * gDevTools is a singleton that controls the Firefox Developer Tools.
 *
 * It is an instance of a DevTools class that holds a set of tools. It has the
 * same lifetime as the browser.
 */
let gDevTools = new DevTools();
this.gDevTools = gDevTools;

/**
 * gDevToolsBrowser exposes functions to connect the gDevTools instance with a
 * Firefox instance.
 */
let gDevToolsBrowser = {
  /**
   * A record of the windows whose menus we altered, so we can undo the changes
   * as the window is closed
   */
  _trackedBrowserWindows: new Set(),

  /**
   * This function is for the benefit of Tools:DevToolbox in
   * browser/base/content/browser-sets.inc and should not be used outside
   * of there
   */
  toggleToolboxCommand: function(gBrowser) {
    let target = TargetFactory.forTab(gBrowser.selectedTab);
    let toolbox = gDevTools.getToolbox(target);

    toolbox ? toolbox.destroy() : gDevTools.showToolbox(target);
  },

  /**
   * This function is for the benefit of Tools:{toolId} commands,
   * triggered from the WebDeveloper menu and keyboard shortcuts.
   *
   * selectToolCommand's behavior:
   * - if the toolbox is closed,
   *   we open the toolbox and select the tool
   * - if the toolbox is open, and the targetted tool is not selected,
   *   we select it
   * - if the toolbox is open, and the targetted tool is selected,
   *   and the host is NOT a window, we close the toolbox
   * - if the toolbox is open, and the targetted tool is selected,
   *   and the host is a window, we raise the toolbox window
   */
  selectToolCommand: function(gBrowser, toolId) {
    let target = TargetFactory.forTab(gBrowser.selectedTab);
    let toolbox = gDevTools.getToolbox(target);

    if (toolbox && toolbox.currentToolId == toolId) {
      if (toolbox.hostType == Toolbox.HostType.WINDOW) {
        toolbox.raise();
      } else {
        toolbox.destroy();
      }
    } else {
      gDevTools.showToolbox(target, toolId);
    }
  },

  /**
   * Open a tab to allow connects to a remote browser
   */
  openConnectScreen: function(gBrowser) {
    gBrowser.selectedTab = gBrowser.addTab("chrome://browser/content/devtools/connect.xhtml");
  },

  /**
   * Add this DevTools's presence to a browser window's document
   *
   * @param {XULDocument} doc
   *        The document to which menuitems and handlers are to be added
   */
  registerBrowserWindow: function DT_registerBrowserWindow(win) {
    gDevToolsBrowser._trackedBrowserWindows.add(win);
    gDevToolsBrowser._addAllToolsToMenu(win.document);

    let tabContainer = win.document.getElementById("tabbrowser-tabs")
    tabContainer.addEventListener("TabSelect",
                                  gDevToolsBrowser._updateMenuCheckbox, false);
  },

  /**
   * Add a <key> to <keyset id="devtoolsKeyset">.
   * Appending a <key> element is not always enough. The <keyset> needs
   * to be detached and reattached to make sure the <key> is taken into
   * account (see bug 832984).
   *
   * @param {XULDocument} doc
   *        The document to which keys are to be added
   * @param {XULElement} or {DocumentFragment} keys
   *        Keys to add
   */
  attachKeybindingsToBrowser: function DT_attachKeybindingsToBrowser(doc, keys) {
    let devtoolsKeyset = doc.getElementById("devtoolsKeyset");
    if (!devtoolsKeyset) {
      devtoolsKeyset = doc.createElement("keyset");
      devtoolsKeyset.setAttribute("id", "devtoolsKeyset");
    }
    devtoolsKeyset.appendChild(keys);
    let mainKeyset = doc.getElementById("mainKeyset");
    mainKeyset.parentNode.insertBefore(devtoolsKeyset, mainKeyset);
  },

  /**
   * Add the menuitem for a tool to all open browser windows.
   *
   * @param {object} toolDefinition
   *        properties of the tool to add
   */
  _addToolToWindows: function DT_addToolToWindows(toolDefinition) {
    // We need to insert the new tool in the right place, which means knowing
    // the tool that comes before the tool that we're trying to add
    let allDefs = gDevTools.getToolDefinitionArray();
    let prevDef;
    for (let def of allDefs) {
      if (def === toolDefinition) {
        break;
      }
      prevDef = def;
    }

    for (let win of gDevToolsBrowser._trackedBrowserWindows) {
      let doc = win.document;
      let elements = gDevToolsBrowser._createToolMenuElements(toolDefinition, doc);

      doc.getElementById("mainCommandSet").appendChild(elements.cmd);

      if (elements.key) {
        this.attachKeybindingsToBrowser(doc, elements.keys);
      }

      doc.getElementById("mainBroadcasterSet").appendChild(elements.bc);

      let amp = doc.getElementById("appmenu_webDeveloper_popup");
      if (amp) {
        let ref = (prevDef != null) ?
            doc.getElementById("appmenuitem_" + prevDef.id).nextSibling :
            doc.getElementById("appmenu_devtools_separator");

        amp.insertBefore(elements.appmenuitem, ref);
      }

      let mp = doc.getElementById("menuWebDeveloperPopup");
      let ref = (prevDef != null) ?
          doc.getElementById("menuitem_" + prevDef.id).nextSibling :
          doc.getElementById("menu_devtools_separator");
      mp.insertBefore(elements.menuitem, ref);
    }
  },

  /**
   * Add all tools to the developer tools menu of a window.
   *
   * @param {XULDocument} doc
   *        The document to which the tool items are to be added.
   */
  _addAllToolsToMenu: function DT_addAllToolsToMenu(doc) {
    let fragCommands = doc.createDocumentFragment();
    let fragKeys = doc.createDocumentFragment();
    let fragBroadcasters = doc.createDocumentFragment();
    let fragAppMenuItems = doc.createDocumentFragment();
    let fragMenuItems = doc.createDocumentFragment();

    for (let toolDefinition of gDevTools.getToolDefinitionArray()) {
      let elements = gDevToolsBrowser._createToolMenuElements(toolDefinition, doc);

      if (!elements) {
        return;
      }

      fragCommands.appendChild(elements.cmd);
      if (elements.key) {
        fragKeys.appendChild(elements.key);
      }
      fragBroadcasters.appendChild(elements.bc);
      fragAppMenuItems.appendChild(elements.appmenuitem);
      fragMenuItems.appendChild(elements.menuitem);
    }

    let mcs = doc.getElementById("mainCommandSet");
    mcs.appendChild(fragCommands);

    this.attachKeybindingsToBrowser(doc, fragKeys);

    let mbs = doc.getElementById("mainBroadcasterSet");
    mbs.appendChild(fragBroadcasters);

    let amp = doc.getElementById("appmenu_webDeveloper_popup");
    if (amp) {
      let amps = doc.getElementById("appmenu_devtools_separator");
      amp.insertBefore(fragAppMenuItems, amps);
    }

    let mp = doc.getElementById("menuWebDeveloperPopup");
    let mps = doc.getElementById("menu_devtools_separator");
    mp.insertBefore(fragMenuItems, mps);
  },

  /**
   * Add a menu entry for a tool definition
   *
   * @param {string} toolDefinition
   *        Tool definition of the tool to add a menu entry.
   * @param {XULDocument} doc
   *        The document to which the tool menu item is to be added.
   */
  _createToolMenuElements: function DT_createToolMenuElements(toolDefinition, doc) {
    let id = toolDefinition.id;

    // Prevent multiple entries for the same tool.
    if (doc.getElementById("Tools:" + id)) {
      return;
    }

    let cmd = doc.createElement("command");
    cmd.id = "Tools:" + id;
    cmd.setAttribute("oncommand",
        'gDevToolsBrowser.selectToolCommand(gBrowser, "' + id + '");');

    let key = null;
    if (toolDefinition.key) {
      key = doc.createElement("key");
      key.id = "key_" + id;

      if (toolDefinition.key.startsWith("VK_")) {
        key.setAttribute("keycode", toolDefinition.key);
      } else {
        key.setAttribute("key", toolDefinition.key);
      }

      key.setAttribute("command", cmd.id);
      key.setAttribute("modifiers", toolDefinition.modifiers);
    }

    let bc = doc.createElement("broadcaster");
    bc.id = "devtoolsMenuBroadcaster_" + id;
    bc.setAttribute("label", toolDefinition.label);
    bc.setAttribute("command", cmd.id);

    if (key) {
      bc.setAttribute("key", "key_" + id);
    }

    let appmenuitem = doc.createElement("menuitem");
    appmenuitem.id = "appmenuitem_" + id;
    appmenuitem.setAttribute("observes", "devtoolsMenuBroadcaster_" + id);

    let menuitem = doc.createElement("menuitem");
    menuitem.id = "menuitem_" + id;
    menuitem.setAttribute("observes", "devtoolsMenuBroadcaster_" + id);

    if (toolDefinition.accesskey) {
      menuitem.setAttribute("accesskey", toolDefinition.accesskey);
    }

    return {
      cmd: cmd,
      key: key,
      bc: bc,
      appmenuitem: appmenuitem,
      menuitem: menuitem
    };
  },

  /**
   * Update the "Toggle Tools" checkbox in the developer tools menu. This is
   * called when a toolbox is created or destroyed.
   */
  _updateMenuCheckbox: function DT_updateMenuCheckbox() {
    for (let win of gDevToolsBrowser._trackedBrowserWindows) {

      let hasToolbox = false;
      if (TargetFactory.isKnownTab(win.gBrowser.selectedTab)) {
        let target = TargetFactory.forTab(win.gBrowser.selectedTab);
        if (gDevTools._toolboxes.has(target)) {
          hasToolbox = true;
        }
      }

      let broadcaster = win.document.getElementById("devtoolsMenuBroadcaster_DevToolbox");
      if (hasToolbox) {
        broadcaster.setAttribute("checked", "true");
      } else {
        broadcaster.removeAttribute("checked");
      }
    }
  },

  /**
   * Remove the menuitem for a tool to all open browser windows.
   *
   * @param {object} toolId
   *        id of the tool to remove
   */
  _removeToolFromWindows: function DT_removeToolFromWindows(toolId) {
    for (let win of gDevToolsBrowser._trackedBrowserWindows) {
      gDevToolsBrowser._removeToolFromMenu(toolId, win.document);
    }
  },

  /**
   * Remove a tool's menuitem from a window
   *
   * @param {string} toolId
   *        Id of the tool to add a menu entry for
   * @param {XULDocument} doc
   *        The document to which the tool menu item is to be removed from
   */
  _removeToolFromMenu: function DT_removeToolFromMenu(toolId, doc) {
    let command = doc.getElementById("Tools:" + toolId);
    if (command) {
      command.parentNode.removeChild(command);
    }

    let key = doc.getElementById("key_" + toolId);
    if (key) {
      key.parentNode.removeChild(key);
    }

    let bc = doc.getElementById("devtoolsMenuBroadcaster_" + toolId);
    if (bc) {
      bc.parentNode.removeChild(bc);
    }

    let appmenuitem = doc.getElementById("appmenuitem_" + toolId);
    if (appmenuitem) {
      appmenuitem.parentNode.removeChild(appmenuitem);
    }

    let menuitem = doc.getElementById("menuitem_" + toolId);
    if (menuitem) {
      menuitem.parentNode.removeChild(menuitem);
    }
  },

  /**
   * Called on browser unload to remove menu entries, toolboxes and event
   * listeners from the closed browser window.
   *
   * @param  {XULWindow} win
   *         The window containing the menu entry
   */
  forgetBrowserWindow: function DT_forgetBrowserWindow(win) {
    if (!gDevToolsBrowser._trackedBrowserWindows) {
      return;
    }

    gDevToolsBrowser._trackedBrowserWindows.delete(win);

    // Destroy toolboxes for closed window
    for (let [target, toolbox] of gDevTools._toolboxes) {
      if (toolbox.frame.ownerDocument.defaultView == win) {
        toolbox.destroy();
      }
    }

    let tabContainer = win.document.getElementById("tabbrowser-tabs")
    tabContainer.removeEventListener("TabSelect",
                                     gDevToolsBrowser._updateMenuCheckbox, false);
  },

  /**
   * All browser windows have been closed, tidy up remaining objects.
   */
  destroy: function() {
    Services.obs.removeObserver(gDevToolsBrowser.destroy, "quit-application");
    delete gDevToolsBrowser._trackedBrowserWindows;
  },
}
this.gDevToolsBrowser = gDevToolsBrowser;

gDevTools.on("tool-registered", function(ev, toolId) {
  let toolDefinition = gDevTools._tools.get(toolId);
  gDevToolsBrowser._addToolToWindows(toolDefinition);
});

gDevTools.on("tool-unregistered", function(ev, toolId) {
  gDevToolsBrowser._removeToolFromWindows(toolId);
});

gDevTools.on("toolbox-ready", gDevToolsBrowser._updateMenuCheckbox);
gDevTools.on("toolbox-destroyed", gDevToolsBrowser._updateMenuCheckbox);

Services.obs.addObserver(gDevToolsBrowser.destroy, "quit-application", false);

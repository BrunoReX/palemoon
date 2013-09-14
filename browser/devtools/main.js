/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {Cc, Ci, Cu} = require("chrome");

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource:///modules/devtools/gDevTools.jsm");

Object.defineProperty(exports, "Toolbox", {
  get: () => require("devtools/framework/toolbox").Toolbox
});
Object.defineProperty(exports, "TargetFactory", {
  get: () => require("devtools/framework/target").TargetFactory
});

loader.lazyGetter(this, "osString", () => Cc["@mozilla.org/xre/app-info;1"].getService(Ci.nsIXULRuntime).OS);

// Panels
loader.lazyGetter(this, "OptionsPanel", function() require("devtools/framework/toolbox-options").OptionsPanel);
loader.lazyGetter(this, "InspectorPanel", function() require("devtools/inspector/inspector-panel").InspectorPanel);
loader.lazyImporter(this, "WebConsolePanel", "resource:///modules/WebConsolePanel.jsm");
loader.lazyImporter(this, "DebuggerPanel", "resource:///modules/devtools/DebuggerPanel.jsm");
loader.lazyImporter(this, "StyleEditorPanel", "resource:///modules/devtools/StyleEditorPanel.jsm");
loader.lazyImporter(this, "ProfilerPanel", "resource:///modules/devtools/ProfilerPanel.jsm");
loader.lazyImporter(this, "NetMonitorPanel", "resource:///modules/devtools/NetMonitorPanel.jsm");

// Strings
const toolboxProps = "chrome://browser/locale/devtools/toolbox.properties";
const inspectorProps = "chrome://browser/locale/devtools/inspector.properties";
const debuggerProps = "chrome://browser/locale/devtools/debugger.properties";
const styleEditorProps = "chrome://browser/locale/devtools/styleeditor.properties";
const webConsoleProps = "chrome://browser/locale/devtools/webconsole.properties";
const profilerProps = "chrome://browser/locale/devtools/profiler.properties";
const netMonitorProps = "chrome://browser/locale/devtools/netmonitor.properties";
loader.lazyGetter(this, "toolboxStrings", () => Services.strings.createBundle(toolboxProps));
loader.lazyGetter(this, "webConsoleStrings", () => Services.strings.createBundle(webConsoleProps));
loader.lazyGetter(this, "debuggerStrings", () => Services.strings.createBundle(debuggerProps));
loader.lazyGetter(this, "styleEditorStrings", () => Services.strings.createBundle(styleEditorProps));
loader.lazyGetter(this, "inspectorStrings", () => Services.strings.createBundle(inspectorProps));
loader.lazyGetter(this, "profilerStrings",() => Services.strings.createBundle(profilerProps));
loader.lazyGetter(this, "netMonitorStrings", () => Services.strings.createBundle(netMonitorProps));

let Tools = {};
exports.Tools = Tools;

// Definitions
Tools.options = {
  id: "options",
  ordinal: 0,
  url: "chrome://browser/content/devtools/framework/toolbox-options.xul",
  icon: "chrome://browser/skin/devtools/tool-options.png",
  tooltip: l10n("optionsButton.tooltip", toolboxStrings),
  isTargetSupported: function(target) {
    return true;
  },
  build: function(iframeWindow, toolbox) {
    let panel = new OptionsPanel(iframeWindow, toolbox);
    return panel.open();
  }
}

Tools.webConsole = {
  id: "webconsole",
  key: l10n("cmd.commandkey", webConsoleStrings),
  accesskey: l10n("webConsoleCmd.accesskey", webConsoleStrings),
  modifiers: Services.appinfo.OS == "Darwin" ? "accel,alt" : "accel,shift",
  ordinal: 1,
  icon: "chrome://browser/skin/devtools/tool-webconsole.png",
  url: "chrome://browser/content/devtools/webconsole.xul",
  label: l10n("ToolboxTabWebconsole.label", webConsoleStrings),
  menuLabel: l10n("MenuWebconsole.label", webConsoleStrings),
  tooltip: l10n("ToolboxWebconsole.tooltip", webConsoleStrings),

  isTargetSupported: function(target) {
    return true;
  },
  build: function(iframeWindow, toolbox) {
    let panel = new WebConsolePanel(iframeWindow, toolbox);
    return panel.open();
  }
};

Tools.inspector = {
  id: "inspector",
  accesskey: l10n("inspector.accesskey", inspectorStrings),
  key: l10n("inspector.commandkey", inspectorStrings),
  ordinal: 2,
  modifiers: osString == "Darwin" ? "accel,alt" : "accel,shift",
  icon: "chrome://browser/skin/devtools/tool-inspector.png",
  url: "chrome://browser/content/devtools/inspector/inspector.xul",
  label: l10n("inspector.label", inspectorStrings),
  tooltip: l10n("inspector.tooltip", inspectorStrings),

  isTargetSupported: function(target) {
    return !target.isRemote;
  },

  build: function(iframeWindow, toolbox) {
    let panel = new InspectorPanel(iframeWindow, toolbox);
    return panel.open();
  }
};

Tools.jsdebugger = {
  id: "jsdebugger",
  key: l10n("debuggerMenu.commandkey", debuggerStrings),
  accesskey: l10n("debuggerMenu.accesskey", debuggerStrings),
  modifiers: osString == "Darwin" ? "accel,alt" : "accel,shift",
  ordinal: 3,
  visibilityswitch: "devtools.debugger.enabled",
  icon: "chrome://browser/skin/devtools/tool-debugger.png",
  highlightedicon: "chrome://browser/skin/devtools/tool-debugger-paused.png",
  url: "chrome://browser/content/devtools/debugger.xul",
  label: l10n("ToolboxDebugger.label", debuggerStrings),
  tooltip: l10n("ToolboxDebugger.tooltip", debuggerStrings),

  isTargetSupported: function(target) {
    return true;
  },

  build: function(iframeWindow, toolbox) {
    let panel = new DebuggerPanel(iframeWindow, toolbox);
    return panel.open();
  }
};

Tools.styleEditor = {
  id: "styleeditor",
  key: l10n("open.commandkey", styleEditorStrings),
  ordinal: 4,
  accesskey: l10n("open.accesskey", styleEditorStrings),
  modifiers: "shift",
  icon: "chrome://browser/skin/devtools/tool-styleeditor.png",
  url: "chrome://browser/content/devtools/styleeditor.xul",
  label: l10n("ToolboxStyleEditor.label", styleEditorStrings),
  tooltip: l10n("ToolboxStyleEditor.tooltip", styleEditorStrings),

  isTargetSupported: function(target) {
    return true;
  },

  build: function(iframeWindow, toolbox) {
    let panel = new StyleEditorPanel(iframeWindow, toolbox);
    return panel.open();
  }
};

Tools.jsprofiler = {
  id: "jsprofiler",
  accesskey: l10n("profiler.accesskey", profilerStrings),
  key: l10n("profiler2.commandkey", profilerStrings),
  ordinal: 5,
  modifiers: "shift",
  visibilityswitch: "devtools.profiler.enabled",
  icon: "chrome://browser/skin/devtools/tool-profiler.png",
  url: "chrome://browser/content/devtools/profiler.xul",
  label: l10n("profiler.label", profilerStrings),
  tooltip: l10n("profiler.tooltip2", profilerStrings),

  isTargetSupported: function (target) {
    return true;
  },

  build: function (frame, target) {
    let panel = new ProfilerPanel(frame, target);
    return panel.open();
  }
};

Tools.netMonitor = {
  id: "netmonitor",
  accesskey: l10n("netmonitor.accesskey", netMonitorStrings),
  key: l10n("netmonitor.commandkey", netMonitorStrings),
  ordinal: 6,
  modifiers: osString == "Darwin" ? "accel,alt" : "accel,shift",
  visibilityswitch: "devtools.netmonitor.enabled",
  icon: "chrome://browser/skin/devtools/tool-network.png",
  url: "chrome://browser/content/devtools/netmonitor.xul",
  label: l10n("netmonitor.label", netMonitorStrings),
  tooltip: l10n("netmonitor.tooltip", netMonitorStrings),

  isTargetSupported: function(target) {
    return true;
  },

  build: function(iframeWindow, toolbox) {
    let panel = new NetMonitorPanel(iframeWindow, toolbox);
    return panel.open();
  }
};

let defaultTools = [
  Tools.options,
  Tools.styleEditor,
  Tools.webConsole,
  Tools.jsdebugger,
  Tools.inspector,
  Tools.jsprofiler,
  Tools.netMonitor
];

exports.defaultTools = defaultTools;

for (let definition of defaultTools) {
  gDevTools.registerTool(definition);
}

var unloadObserver = {
  observe: function(subject, topic, data) {
    if (subject.wrappedJSObject === require("@loader/unload")) {
      Services.obs.removeObserver(unloadObserver, "sdk:loader:destroy");
      for (let definition of gDevTools.getToolDefinitionArray()) {
        gDevTools.unregisterTool(definition.id);
      }
    }
  }
};
Services.obs.addObserver(unloadObserver, "sdk:loader:destroy", false);

/**
 * Lookup l10n string from a string bundle.
 *
 * @param {string} name
 *        The key to lookup.
 * @param {StringBundle} bundle
 *        The key to lookup.
 * @returns A localized version of the given key.
 */
function l10n(name, bundle)
{
  try {
    return bundle.GetStringFromName(name);
  } catch (ex) {
    Services.console.logStringMessage("Error reading '" + name + "'");
    throw new Error("l10n error with " + name);
  }
}

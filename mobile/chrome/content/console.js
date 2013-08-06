// -*- Mode: js2; tab-width: 2; indent-tabs-mode: nil; js2-basic-offset: 2; js2-skip-preprocessor-directives: t; -*-
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Mobile Browser.
 *
 * The Initial Developer of the Original Code is Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Nino D'Aversa <ninodaversa@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

let ConsoleView = {
  _list: null,
  _inited: false,
  _evalTextbox: null,
  _evalFrame: null,
  _evalCode: "",
  _bundle: null,
  _showChromeErrors: -1,
  _enabledPref: "devtools.errorconsole.enabled",

  init: function cv_init() {
    if (this._list)
      return;

    this._list = document.getElementById("console-box");
    this._evalTextbox = document.getElementById("console-eval-textbox");
    this._bundle = Strings.browser;

    this._count = 0;
    this.limit = 250;

    try {
      // update users using the legacy pref
      if (Services.prefs.getBoolPref("browser.console.showInPanel")) {
        Services.prefs.setBoolPref(this._enabledPref, true);
        Services.prefs.clearUserPref("browser.console.showInPanel");
      }
    } catch(ex) {
      // likely don't have an old pref
    }
    this.updateVisibility();
    Services.prefs.addObserver(this._enabledPref, this, false);
  },

  delayedInit: function cv__delayedInit() {
    if (this._inited)
      return;
    this._inited = true;

    this.init(); // In case the panel is selected before init has been called.

    Services.console.registerListener(this);

    this.appendInitialItems();

    // Delay creation of the iframe for startup performance
    this._evalFrame = document.createElement("iframe");
    this._evalFrame.id = "console-evaluator";
    this._evalFrame.collapsed = true;
    document.getElementById("console-container").appendChild(this._evalFrame);

    this._evalFrame.addEventListener("load", this.loadOrDisplayResult.bind(this), true);
  },

  uninit: function cv_uninit() {
    if (this._inited)
      Services.console.unregisterListener(this);

    Services.prefs.removeObserver(this._enabledPref, this, false);
  },

  get enabled() {
    return Services.prefs.getBoolPref(this._enabledPref);
  },

  updateVisibility: function ec_updateVisibility(aVal, aPref) {
    let button = document.getElementById("tool-console");
    button.hidden = !this.enabled;
  },

  observe: function(aSubject, aTopic, aData) {
    if (aTopic == "nsPref:changed")
      this.updateVisibility();
    else
      this.appendItem(aSubject);
  },

  showChromeErrors: function() {
    if (this._showChromeErrors != -1)
      return this._showChromeErrors;

    try {
      let pref = Services.prefs;
      return this._showChromeErrors = pref.getBoolPref("javascript.options.showInConsole");
    }
    catch(ex) {
      return this._showChromeErrors = false;
    }
  },

  appendItem: function cv_appendItem(aObject) {
    try {
      // Try to QI it to a script error to get more info
      let scriptError = aObject.QueryInterface(Ci.nsIScriptError);

      // filter chrome urls
      if (!this.showChromeErrors && scriptError.sourceName.substr(0, 9) == "chrome://")
        return;
      this.appendError(scriptError);
    }
    catch (ex) {
      try {
        // Try to QI it to a console message
        let msg = aObject.QueryInterface(Ci.nsIConsoleMessage);

        if (msg.message)
          this.appendMessage(msg.message);
        else // observed a null/"clear" message
          this.clearConsole();
      }
      catch (ex2) {
        // Give up and append the object itself as a string
        this.appendMessage(aObject);
      }
    }
  },

  appendError: function cv_appendError(aObject) {
    let row = this.createConsoleRow();
    let nsIScriptError = Ci.nsIScriptError;

    // Is this error actually just a non-fatal warning?
    let warning = aObject.flags & nsIScriptError.warningFlag != 0;

    let typetext = warning ? "typeWarning" : "typeError";
    row.setAttribute("typetext", this._bundle.GetStringFromName(typetext));
    row.setAttribute("type", warning ? "warning" : "error");
    row.setAttribute("msg", aObject.errorMessage);
    row.setAttribute("category", aObject.category);
    if (aObject.lineNumber || aObject.sourceName) {
      row.setAttribute("href", aObject.sourceName);
      row.setAttribute("line", aObject.lineNumber);
    }
    else {
      row.setAttribute("hideSource", "true");
    }
    if (aObject.sourceLine) {
      row.setAttribute("code", aObject.sourceLine.replace(/\s/g, " "));
      if (aObject.columnNumber) {
        row.setAttribute("col", aObject.columnNumber);
        row.setAttribute("errorDots", this.repeatChar(" ", aObject.columnNumber));
        row.setAttribute("errorCaret", " ");
      }
      else {
        row.setAttribute("hideCaret", "true");
      }
    }
    else {
      row.setAttribute("hideCode", "true");
    }

    let mode = document.getElementById("console-filter").value;
    if (mode != "all" && mode != row.getAttribute("type"))
      row.collapsed = true;

    this.appendConsoleRow(row);
  },

  appendMessage: function cv_appendMessage (aMessage) {
    let row = this.createConsoleRow();
    row.setAttribute("type", "message");
    row.setAttribute("msg", aMessage);

    let mode = document.getElementById("console-filter").value;
    if (mode != "all" && mode != "message")
      row.collapsed = true;

    this.appendConsoleRow(row);
  },

  createConsoleRow: function cv_createConsoleRow() {
    let row = document.createElement("richlistitem");
    row.setAttribute("class", "console-row");
    return row;
  },

  appendConsoleRow: function cv_appendConsoleRow(aRow) {
    this._list.appendChild(aRow);
    if (++this._count > this.limit)
      this.deleteFirst();
  },

  deleteFirst: function cv_deleteFirst() {
    let node = this._list.firstChild;
    this._list.removeChild(node);
    --this._count;
  },

  appendInitialItems: function cv_appendInitialItems() {
    let out = {}; // Throwaway references to support 'out' parameters.
    Services.console.getMessageArray(out, {});
    let messages = out.value;

    // In case getMessageArray returns 0-length array as null
    if (!messages)
      messages = [];

    let limit = messages.length - this.limit;
    if (limit < 0)
      limit = 0;

    // Checks if console ever been cleared
    for (var i = messages.length - 1; i >= limit; --i)
      if (!messages[i].message)
        break;

    // Populate with messages after latest "clear"
    while (++i < messages.length)
      this.appendItem(messages[i]);
  },

  clearConsole: function cv_clearConsole() {
    if (this._count == 0) // already clear
      return;
    this._count = 0;

    let newRows = this._list.cloneNode(false);
    this._list.parentNode.replaceChild(newRows, this._list);
    this._list = newRows;
    this.selectedItem = null;
  },

  changeMode: function cv_changeMode() {
    let mode = document.getElementById("console-filter").value;
    if (this._list.getAttribute("mode") != mode) {
      let rows = this._list.childNodes;
      for (let i=0; i < rows.length; i++) {
        let row = rows[i];
        if (mode == "all" || row.getAttribute ("type") == mode)
          row.collapsed = false;
        else
          row.collapsed = true;
      }
      this._list.mode = mode;
      this._list.scrollToIndex(0);
    }
  },

  onContextMenu: function cv_onContextMenu(aEvent) {
    let row = aEvent.target;
    let text = ["msg", "href", "line", "code", "col"].map(function(attr) row.getAttribute(attr))
               .filter(function(x) x).join("\n");

    ContextHelper.showPopup({
      target: row,
      json: {
        types: ["copy"],
        string: text
      }
    });
  },

  onEvalKeyPress: function cv_onEvalKeyPress(aEvent) {
    if (aEvent.keyCode == 13)
      this.evaluateTypein();
  },

  onConsoleBoxKeyPress: function cv_onConsoleBoxKeyPress(aEvent) {
    if ((aEvent.charCode == 99 || aEvent.charCode == 67) && aEvent.ctrlKey && this._list && this._list.selectedItem) {
      let clipboard = Cc["@mozilla.org/widget/clipboardhelper;1"].getService(Ci.nsIClipboardHelper);
      clipboard.copyString(this._list.selectedItem.getAttribute("msg"));
    }
  },

  evaluateTypein: function cv_evaluateTypein() {
    this._evalCode = this._evalTextbox.value;
    this.loadOrDisplayResult();
  },

  loadOrDisplayResult: function cv_loadOrDisplayResult() {
    if (this._evalCode) {
      this._evalFrame.contentWindow.location = "javascript: " + this._evalCode.replace(/%/g, "%25");
      this._evalCode = "";
      return;
    }

    let resultRange = this._evalFrame.contentDocument.createRange();
    resultRange.selectNode(this._evalFrame.contentDocument.documentElement);
    let result = resultRange.toString();
    if (result)
      Services.console.logStringMessage(result);
      // or could use appendMessage which doesn't persist
  },

  repeatChar: function cv_repeatChar(aChar, aCol) {
    if (--aCol <= 0)
      return "";

    for (let i = 2; i < aCol; i += i)
      aChar += aChar;

    return aChar + aChar.slice(0, aCol - aChar.length);
  }
};

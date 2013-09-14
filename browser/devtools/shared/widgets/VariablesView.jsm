/* -*- Mode: javascript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const Ci = Components.interfaces;
const Cu = Components.utils;

const DBG_STRINGS_URI = "chrome://browser/locale/devtools/debugger.properties";
const LAZY_EMPTY_DELAY = 150; // ms
const LAZY_EXPAND_DELAY = 50; // ms
const LAZY_APPEND_DELAY = 100; // ms
const LAZY_APPEND_BATCH = 100; // nodes
const PAGE_SIZE_SCROLL_HEIGHT_RATIO = 100;
const PAGE_SIZE_MAX_JUMPS = 30;
const SEARCH_ACTION_MAX_DELAY = 300; // ms

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource:///modules/devtools/ViewHelpers.jsm");
Cu.import("resource:///modules/devtools/shared/event-emitter.js");
Cu.import("resource://gre/modules/commonjs/sdk/core/promise.js");

XPCOMUtils.defineLazyModuleGetter(this, "NetworkHelper",
  "resource://gre/modules/devtools/NetworkHelper.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "WebConsoleUtils",
  "resource://gre/modules/devtools/WebConsoleUtils.jsm");

this.EXPORTED_SYMBOLS = ["VariablesView"];

/**
 * Debugger localization strings.
 */
const STR = Services.strings.createBundle(DBG_STRINGS_URI);

/**
 * A tree view for inspecting scopes, objects and properties.
 * Iterable via "for (let [id, scope] in instance) { }".
 * Requires the devtools common.css and debugger.css skin stylesheets.
 *
 * To allow replacing variable or property values in this view, provide an
 * "eval" function property. To allow replacing variable or property names,
 * provide a "switch" function. To handle deleting variables or properties,
 * provide a "delete" function.
 *
 * @param nsIDOMNode aParentNode
 *        The parent node to hold this view.
 * @param object aFlags [optional]
 *        An object contaning initialization options for this view.
 *        e.g. { lazyEmpty: true, searchEnabled: true ... }
 */
this.VariablesView = function VariablesView(aParentNode, aFlags = {}) {
  this._store = []; // Can't use a Map because Scope names needn't be unique.
  this._itemsByElement = new WeakMap();
  this._prevHierarchy = new Map();
  this._currHierarchy = new Map();

  this._parent = aParentNode;
  this._parent.classList.add("variables-view-container");
  this._appendEmptyNotice();

  this._onSearchboxInput = this._onSearchboxInput.bind(this);
  this._onSearchboxKeyPress = this._onSearchboxKeyPress.bind(this);
  this._onViewKeyPress = this._onViewKeyPress.bind(this);

  // Create an internal scrollbox container.
  this._list = this.document.createElement("scrollbox");
  this._list.setAttribute("orient", "vertical");
  this._list.addEventListener("keypress", this._onViewKeyPress, false);
  this._parent.appendChild(this._list);
  this._boxObject = this._list.boxObject.QueryInterface(Ci.nsIScrollBoxObject);

  for (let name in aFlags) {
    this[name] = aFlags[name];
  }

  EventEmitter.decorate(this);
};

VariablesView.prototype = {
  /**
   * Helper setter for populating this container with a raw object.
   *
   * @param object aObject
   *        The raw object to display. You can only provide this object
   *        if you want the variables view to work in sync mode.
   */
  set rawObject(aObject) {
    this.empty();
    this.addScope().addItem().populate(aObject, { sorted: true });
  },

  /**
   * Adds a scope to contain any inspected variables.
   *
   * @param string aName
   *        The scope's name (e.g. "Local", "Global" etc.).
   * @return Scope
   *         The newly created Scope instance.
   */
  addScope: function(aName = "") {
    this._removeEmptyNotice();
    this._toggleSearchVisibility(true);

    let scope = new Scope(this, aName);
    this._store.push(scope);
    this._itemsByElement.set(scope._target, scope);
    this._currHierarchy.set(aName, scope);
    scope.header = !!aName;
    return scope;
  },

  /**
   * Removes all items from this container.
   *
   * @param number aTimeout [optional]
   *        The number of milliseconds to delay the operation if
   *        lazy emptying of this container is enabled.
   */
  empty: function(aTimeout = this.lazyEmptyDelay) {
    // If there are no items in this container, emptying is useless.
    if (!this._store.length) {
      return;
    }
    // Check if this empty operation may be executed lazily.
    if (this.lazyEmpty && aTimeout > 0) {
      this._emptySoon(aTimeout);
      return;
    }

    let list = this._list;

    while (list.hasChildNodes()) {
      list.firstChild.remove();
    }

    this._store.length = 0;
    this._itemsByElement.clear();

    this._appendEmptyNotice();
    this._toggleSearchVisibility(false);
  },

  /**
   * Emptying this container and rebuilding it immediately afterwards would
   * result in a brief redraw flicker, because the previously expanded nodes
   * may get asynchronously re-expanded, after fetching the prototype and
   * properties from a server.
   *
   * To avoid such behaviour, a normal container list is rebuild, but not
   * immediately attached to the parent container. The old container list
   * is kept around for a short period of time, hopefully accounting for the
   * data fetching delay. In the meantime, any operations can be executed
   * normally.
   *
   * @see VariablesView.empty
   * @see VariablesView.commitHierarchy
   */
  _emptySoon: function(aTimeout) {
    let prevList = this._list;
    let currList = this._list = this.document.createElement("scrollbox");

    this._store.length = 0;
    this._itemsByElement.clear();

    this._emptyTimeout = this.window.setTimeout(() => {
      this._emptyTimeout = null;

      prevList.removeEventListener("keypress", this._onViewKeyPress, false);
      currList.addEventListener("keypress", this._onViewKeyPress, false);
      currList.setAttribute("orient", "vertical");

      this._parent.removeChild(prevList);
      this._parent.appendChild(currList);
      this._boxObject = currList.boxObject.QueryInterface(Ci.nsIScrollBoxObject);

      if (!this._store.length) {
        this._appendEmptyNotice();
        this._toggleSearchVisibility(false);
      }
    }, aTimeout);
  },

  /**
   * The controller for this VariablesView, if it has one.
   */
  controller: null,

  /**
   * The amount of time (in milliseconds) it takes to empty this view lazily.
   */
  lazyEmptyDelay: LAZY_EMPTY_DELAY,

  /**
   * Specifies if this view may be emptied lazily.
   * @see VariablesView.prototype.empty
   */
  lazyEmpty: false,

  /**
   * Specifies if nodes in this view may be added lazily.
   * @see Scope.prototype._lazyAppend
   */
  lazyAppend: true,

  /**
   * Specifies if nodes in this view may be expanded lazily.
   * @see Scope.prototype.expand
   */
  lazyExpand: true,

  /**
   * Function called each time a variable or property's value is changed via
   * user interaction. If null, then value changes are disabled.
   *
   * This property is applied recursively onto each scope in this view and
   * affects only the child nodes when they're created.
   */
  eval: null,

  /**
   * Function called each time a variable or property's name is changed via
   * user interaction. If null, then name changes are disabled.
   *
   * This property is applied recursively onto each scope in this view and
   * affects only the child nodes when they're created.
   */
  switch: null,

  /**
   * Function called each time a variable or property is deleted via
   * user interaction. If null, then deletions are disabled.
   *
   * This property is applied recursively onto each scope in this view and
   * affects only the child nodes when they're created.
   */
  delete: null,

  /**
   * Specifies if after an eval or switch operation, the variable or property
   * which has been edited should be disabled.
   */
  preventDisableOnChage: false,

  /**
   * Specifies if, whenever a variable or property descriptor is available,
   * configurable, enumerable, writable, frozen, sealed and extensible
   * attributes should not affect presentation.
   *
   * This flag is applied recursively onto each scope in this view and
   * affects only the child nodes when they're created.
   */
  preventDescriptorModifiers: false,

  /**
   * The tooltip text shown on a variable or property's value if an |eval|
   * function is provided, in order to change the variable or property's value.
   *
   * This flag is applied recursively onto each scope in this view and
   * affects only the child nodes when they're created.
   */
  editableValueTooltip: STR.GetStringFromName("variablesEditableValueTooltip"),

  /**
   * The tooltip text shown on a variable or property's name if a |switch|
   * function is provided, in order to change the variable or property's name.
   *
   * This flag is applied recursively onto each scope in this view and
   * affects only the child nodes when they're created.
   */
  editableNameTooltip: STR.GetStringFromName("variablesEditableNameTooltip"),

  /**
   * The tooltip text shown on a variable or property's edit button if an
   * |eval| function is provided and a getter/setter descriptor is present,
   * in order to change the variable or property to a plain value.
   *
   * This flag is applied recursively onto each scope in this view and
   * affects only the child nodes when they're created.
   */
  editButtonTooltip: STR.GetStringFromName("variablesEditButtonTooltip"),

  /**
   * The tooltip text shown on a variable or property's delete button if a
   * |delete| function is provided, in order to delete the variable or property.
   *
   * This flag is applied recursively onto each scope in this view and
   * affects only the child nodes when they're created.
   */
  deleteButtonTooltip: STR.GetStringFromName("variablesCloseButtonTooltip"),

  /**
   * Specifies the context menu attribute set on variables and properties.
   *
   * This flag is applied recursively onto each scope in this view and
   * affects only the child nodes when they're created.
   */
  contextMenuId: "",

  /**
   * The separator label between the variables or properties name and value.
   *
   * This flag is applied recursively onto each scope in this view and
   * affects only the child nodes when they're created.
   */
  separatorStr: STR.GetStringFromName("variablesSeparatorLabel"),

  /**
   * Specifies if enumerable properties and variables should be displayed.
   * These variables and properties are visible by default.
   * @param boolean aFlag
   */
  set enumVisible(aFlag) {
    this._enumVisible = aFlag;

    for (let scope of this._store) {
      scope._enumVisible = aFlag;
    }
  },

  /**
   * Specifies if non-enumerable properties and variables should be displayed.
   * These variables and properties are visible by default.
   * @param boolean aFlag
   */
  set nonEnumVisible(aFlag) {
    this._nonEnumVisible = aFlag;

    for (let scope of this._store) {
      scope._nonEnumVisible = aFlag;
    }
  },

  /**
   * Specifies if only enumerable properties and variables should be displayed.
   * Both types of these variables and properties are visible by default.
   * @param boolean aFlag
   */
  set onlyEnumVisible(aFlag) {
    if (aFlag) {
      this.enumVisible = true;
      this.nonEnumVisible = false;
    } else {
      this.enumVisible = true;
      this.nonEnumVisible = true;
    }
  },

  /**
   * Sets if the variable and property searching is enabled.
   * @param boolean aFlag
   */
  set searchEnabled(aFlag) aFlag ? this._enableSearch() : this._disableSearch(),

  /**
   * Gets if the variable and property searching is enabled.
   * @return boolean
   */
  get searchEnabled() !!this._searchboxContainer,

  /**
   * Sets the text displayed for the searchbox in this container.
   * @param string aValue
   */
  set searchPlaceholder(aValue) {
    if (this._searchboxNode) {
      this._searchboxNode.setAttribute("placeholder", aValue);
    }
    this._searchboxPlaceholder = aValue;
  },

  /**
   * Gets the text displayed for the searchbox in this container.
   * @return string
   */
  get searchPlaceholder() this._searchboxPlaceholder,

  /**
   * Enables variable and property searching in this view.
   * Use the "searchEnabled" setter to enable searching.
   */
  _enableSearch: function() {
    // If searching was already enabled, no need to re-enable it again.
    if (this._searchboxContainer) {
      return;
    }
    let document = this.document;
    let ownerView = this._parent.parentNode;

    let container = this._searchboxContainer = document.createElement("hbox");
    container.className = "devtools-toolbar";

    // Hide the variables searchbox container if there are no variables or
    // properties to display.
    container.hidden = !this._store.length;

    let searchbox = this._searchboxNode = document.createElement("textbox");
    searchbox.className = "variables-view-searchinput devtools-searchinput";
    searchbox.setAttribute("placeholder", this._searchboxPlaceholder);
    searchbox.setAttribute("type", "search");
    searchbox.setAttribute("flex", "1");
    searchbox.addEventListener("input", this._onSearchboxInput, false);
    searchbox.addEventListener("keypress", this._onSearchboxKeyPress, false);

    container.appendChild(searchbox);
    ownerView.insertBefore(container, this._parent);
  },

  /**
   * Disables variable and property searching in this view.
   * Use the "searchEnabled" setter to disable searching.
   */
  _disableSearch: function() {
    // If searching was already disabled, no need to re-disable it again.
    if (!this._searchboxContainer) {
      return;
    }
    this._searchboxContainer.remove();
    this._searchboxNode.removeEventListener("input", this._onSearchboxInput, false);
    this._searchboxNode.removeEventListener("keypress", this._onSearchboxKeyPress, false);

    this._searchboxContainer = null;
    this._searchboxNode = null;
  },

  /**
   * Sets the variables searchbox container hidden or visible.
   * It's hidden by default.
   *
   * @param boolean aVisibleFlag
   *        Specifies the intended visibility.
   */
  _toggleSearchVisibility: function(aVisibleFlag) {
    // If searching was already disabled, there's no need to hide it.
    if (!this._searchboxContainer) {
      return;
    }
    this._searchboxContainer.hidden = !aVisibleFlag;
  },

  /**
   * Listener handling the searchbox input event.
   */
  _onSearchboxInput: function() {
    this.performSearch(this._searchboxNode.value);
  },

  /**
   * Listener handling the searchbox key press event.
   */
  _onSearchboxKeyPress: function(e) {
    switch(e.keyCode) {
      case e.DOM_VK_RETURN:
      case e.DOM_VK_ENTER:
        this._onSearchboxInput();
        return;
      case e.DOM_VK_ESCAPE:
        this._searchboxNode.value = "";
        this._onSearchboxInput();
        return;
    }
  },

  /**
   * Allows searches to be scheduled and delayed to avoid redundant calls.
   */
  delayedSearch: true,

  /**
   * Schedules searching for variables or properties matching the query.
   *
   * @param string aQuery
   *        The variable or property to search for.
   */
  scheduleSearch: function(aQuery) {
    if (!this.delayedSearch) {
      this.performSearch(aQuery);
      return;
    }
    let delay = Math.max(SEARCH_ACTION_MAX_DELAY / aQuery.length, 0);

    this.window.clearTimeout(this._searchTimeout);
    this._searchFunction = this._startSearch.bind(this, aQuery);
    this._searchTimeout = this.window.setTimeout(this._searchFunction, delay);
  },

  /**
   * Immediately searches for variables or properties matching the query.
   *
   * @param string aQuery
   *        The variable or property to search for.
   */
  performSearch: function(aQuery) {
    this.window.clearTimeout(this._searchTimeout);
    this._searchFunction = null;
    this._startSearch(aQuery);
  },

  /**
   * Performs a case insensitive search for variables or properties matching
   * the query, and hides non-matched items.
   *
   * If aQuery is empty string, then all the scopes are unhidden and expanded,
   * while the available variables and properties inside those scopes are
   * just unhidden.
   *
   * If aQuery is null or undefined, then all the scopes are just unhidden,
   * and the available variables and properties inside those scopes are also
   * just unhidden.
   *
   * @param string aQuery
   *        The variable or property to search for.
   */
  _startSearch: function(aQuery) {
    for (let scope of this._store) {
      switch (aQuery) {
        case "":
          scope.expand();
          // fall through
        case null:
        case undefined:
          scope._performSearch("");
          break;
        default:
          scope._performSearch(aQuery.toLowerCase());
          break;
      }
    }
  },

  /**
   * Expands the first search results in this container.
   */
  expandFirstSearchResults: function() {
    for (let scope of this._store) {
      let match = scope._firstMatch;
      if (match) {
        match.expand();
      }
    }
  },

  /**
   * Find the first item in the tree of visible items in this container that
   * matches the predicate. Searches in visual order (the order seen by the
   * user). Descends into each scope to check the scope and its children.
   *
   * @param function aPredicate
   *        A function that returns true when a match is found.
   * @return Scope | Variable | Property
   *         The first visible scope, variable or property, or null if nothing
   *         is found.
   */
  _findInVisibleItems: function(aPredicate) {
    for (let scope of this._store) {
      let result = scope._findInVisibleItems(aPredicate);
      if (result) {
        return result;
      }
    }
    return null;
  },

  /**
   * Find the last item in the tree of visible items in this container that
   * matches the predicate. Searches in reverse visual order (opposite of the
   * order seen by the user). Descends into each scope to check the scope and
   * its children.
   *
   * @param function aPredicate
   *        A function that returns true when a match is found.
   * @return Scope | Variable | Property
   *         The last visible scope, variable or property, or null if nothing
   *         is found.
   */
  _findInVisibleItemsReverse: function(aPredicate) {
    for (let i = this._store.length - 1; i >= 0; i--) {
      let scope = this._store[i];
      let result = scope._findInVisibleItemsReverse(aPredicate);
      if (result) {
        return result;
      }
    }
    return null;
  },

  /**
   * Searches for the scope in this container displayed by the specified node.
   *
   * @param nsIDOMNode aNode
   *        The node to search for.
   * @return Scope
   *         The matched scope, or null if nothing is found.
   */
  getScopeForNode: function(aNode) {
    let item = this._itemsByElement.get(aNode);
    // Match only Scopes, not Variables or Properties.
    if (item && !(item instanceof Variable)) {
      return item;
    }
    return null;
  },

  /**
   * Recursively searches this container for the scope, variable or property
   * displayed by the specified node.
   *
   * @param nsIDOMNode aNode
   *        The node to search for.
   * @return Scope | Variable | Property
   *         The matched scope, variable or property, or null if nothing is found.
   */
  getItemForNode: function(aNode) {
    return this._itemsByElement.get(aNode);
  },

  /**
   * Gets the currently focused scope, variable or property in this view.
   *
   * @return Scope | Variable | Property
   *         The focused scope, variable or property, or null if nothing is found.
   */
  getFocusedItem: function() {
    let focused = this.document.commandDispatcher.focusedElement;
    return this.getItemForNode(focused);
  },

  /**
   * Focuses the first visible scope, variable, or property in this container.
   */
  focusFirstVisibleItem: function() {
    let focusableItem = this._findInVisibleItems(item => item.focusable);
    if (focusableItem) {
      this._focusItem(focusableItem);
    }
    this._parent.scrollTop = 0;
    this._parent.scrollLeft = 0;
  },

  /**
   * Focuses the last visible scope, variable, or property in this container.
   */
  focusLastVisibleItem: function() {
    let focusableItem = this._findInVisibleItemsReverse(item => item.focusable);
    if (focusableItem) {
      this._focusItem(focusableItem);
    }
    this._parent.scrollTop = this._parent.scrollHeight;
    this._parent.scrollLeft = 0;
  },

  /**
   * Focuses the next scope, variable or property in this view.
   */
  focusNextItem: function() {
    this.focusItemAtDelta(+1);
  },

  /**
   * Focuses the previous scope, variable or property in this view.
   */
  focusPrevItem: function() {
    this.focusItemAtDelta(-1);
  },

  /**
   * Focuses another scope, variable or property in this view, based on
   * the index distance from the currently focused item.
   *
   * @param number aDelta
   *        A scalar specifying by how many items should the selection change.
   */
  focusItemAtDelta: function(aDelta) {
    let direction = aDelta > 0 ? "advanceFocus" : "rewindFocus";
    let distance = Math.abs(Math[aDelta > 0 ? "ceil" : "floor"](aDelta));
    while (distance--) {
      if (!this._focusChange(direction)) {
        break; // Out of bounds.
      }
    }
  },

  /**
   * Focuses the next or previous scope, variable or property in this view.
   *
   * @param string aDirection
   *        Either "advanceFocus" or "rewindFocus".
   * @return boolean
   *         False if the focus went out of bounds and the first or last element
   *         in this view was focused instead.
   */
  _focusChange: function(aDirection) {
    let commandDispatcher = this.document.commandDispatcher;
    let prevFocusedElement = commandDispatcher.focusedElement;
    let currFocusedItem = null;

    do {
      commandDispatcher.suppressFocusScroll = true;
      commandDispatcher[aDirection]();

      // Make sure the newly focused item is a part of this view.
      // If the focus goes out of bounds, revert the previously focused item.
      if (!(currFocusedItem = this.getFocusedItem())) {
        prevFocusedElement.focus();
        return false;
      }
    } while (!currFocusedItem.focusable);

    // Focus remained within bounds.
    return true;
  },

  /**
   * Focuses a scope, variable or property and makes sure it's visible.
   *
   * @param aItem Scope | Variable | Property
   *        The item to focus.
   * @param boolean aCollapseFlag
   *        True if the focused item should also be collapsed.
   * @return boolean
   *         True if the item was successfully focused.
   */
  _focusItem: function(aItem, aCollapseFlag) {
    if (!aItem.focusable) {
      return false;
    }
    if (aCollapseFlag) {
      aItem.collapse();
    }
    aItem._target.focus();
    this._boxObject.ensureElementIsVisible(aItem._arrow);
    return true;
  },

  /**
   * Listener handling a key press event on the view.
   */
  _onViewKeyPress: function(e) {
    let item = this.getFocusedItem();

    // Prevent scrolling when pressing navigation keys.
    ViewHelpers.preventScrolling(e);

    switch (e.keyCode) {
      case e.DOM_VK_UP:
        // Always rewind focus.
        this.focusPrevItem(true);
        return;

      case e.DOM_VK_DOWN:
        // Always advance focus.
        this.focusNextItem(true);
        return;

      case e.DOM_VK_LEFT:
        // Collapse scopes, variables and properties before rewinding focus.
        if (item._isExpanded && item._isArrowVisible) {
          item.collapse();
        } else {
          this._focusItem(item.ownerView);
        }
        return;

      case e.DOM_VK_RIGHT:
        // Nothing to do here if this item never expands.
        if (!item._isArrowVisible) {
          return;
        }
        // Expand scopes, variables and properties before advancing focus.
        if (!item._isExpanded) {
          item.expand();
        } else {
          this.focusNextItem(true);
        }
        return;

      case e.DOM_VK_PAGE_UP:
        // Rewind a certain number of elements based on the container height.
        this.focusItemAtDelta(-(this.pageSize || Math.min(Math.floor(this._list.scrollHeight /
          PAGE_SIZE_SCROLL_HEIGHT_RATIO),
          PAGE_SIZE_MAX_JUMPS)));
        return;

      case e.DOM_VK_PAGE_DOWN:
        // Advance a certain number of elements based on the container height.
        this.focusItemAtDelta(+(this.pageSize || Math.min(Math.floor(this._list.scrollHeight /
          PAGE_SIZE_SCROLL_HEIGHT_RATIO),
          PAGE_SIZE_MAX_JUMPS)));
        return;

      case e.DOM_VK_HOME:
        this.focusFirstVisibleItem();
        return;

      case e.DOM_VK_END:
        this.focusLastVisibleItem();
        return;

      case e.DOM_VK_RETURN:
      case e.DOM_VK_ENTER:
        // Start editing the value or name of the Variable or Property.
        if (item instanceof Variable) {
          if (e.metaKey || e.altKey || e.shiftKey) {
            item._activateNameInput();
          } else {
            item._activateValueInput();
          }
        }
        return;

      case e.DOM_VK_DELETE:
      case e.DOM_VK_BACK_SPACE:
        // Delete the Variable or Property if allowed.
        if (item instanceof Variable) {
          item._onDelete(e);
        }
        return;
    }
  },

  /**
   * The number of elements in this container to jump when Page Up or Page Down
   * keys are pressed. If falsy, then the page size will be based on the
   * container height.
   */
  pageSize: 0,

  /**
   * Sets the text displayed in this container when there are no available items.
   * @param string aValue
   */
  set emptyText(aValue) {
    if (this._emptyTextNode) {
      this._emptyTextNode.setAttribute("value", aValue);
    }
    this._emptyTextValue = aValue;
    this._appendEmptyNotice();
  },

  /**
   * Creates and appends a label signaling that this container is empty.
   */
  _appendEmptyNotice: function() {
    if (this._emptyTextNode || !this._emptyTextValue) {
      return;
    }

    let label = this.document.createElement("label");
    label.className = "variables-view-empty-notice";
    label.setAttribute("value", this._emptyTextValue);

    this._parent.appendChild(label);
    this._emptyTextNode = label;
  },

  /**
   * Removes the label signaling that this container is empty.
   */
  _removeEmptyNotice: function() {
    if (!this._emptyTextNode) {
      return;
    }

    this._parent.removeChild(this._emptyTextNode);
    this._emptyTextNode = null;
  },

  /**
   * Gets the parent node holding this view.
   * @return nsIDOMNode
   */
  get parentNode() this._parent,

  /**
   * Gets the owner document holding this view.
   * @return nsIHTMLDocument
   */
  get document() this._document || (this._document = this._parent.ownerDocument),

  /**
   * Gets the default window holding this view.
   * @return nsIDOMWindow
   */
  get window() this._window || (this._window = this.document.defaultView),

  _document: null,
  _window: null,

  _store: null,
  _prevHierarchy: null,
  _currHierarchy: null,
  _enumVisible: true,
  _nonEnumVisible: true,
  _emptyTimeout: null,
  _searchTimeout: null,
  _searchFunction: null,
  _parent: null,
  _list: null,
  _boxObject: null,
  _searchboxNode: null,
  _searchboxContainer: null,
  _searchboxPlaceholder: "",
  _emptyTextNode: null,
  _emptyTextValue: ""
};

VariablesView.NON_SORTABLE_CLASSES = [
  "Array",
  "Int8Array",
  "Uint8Array",
  "Uint8ClampedArray",
  "Int16Array",
  "Uint16Array",
  "Int32Array",
  "Uint32Array",
  "Float32Array",
  "Float64Array"
];

/**
 * Determine whether an object's properties should be sorted based on its class.
 *
 * @param string aClassName
 *        The class of the object.
 */
VariablesView.isSortable = function(aClassName) {
  return VariablesView.NON_SORTABLE_CLASSES.indexOf(aClassName) == -1;
};

/**
 * Generates the string evaluated when performing simple value changes.
 *
 * @param Variable | Property aItem
 *        The current variable or property.
 * @param string aCurrentString
 *        The trimmed user inputted string.
 * @param string aPrefix [optional]
 *        Prefix for the symbolic name.
 * @return string
 *         The string to be evaluated.
 */
VariablesView.simpleValueEvalMacro = function(aItem, aCurrentString, aPrefix = "") {
  return aPrefix + aItem._symbolicName + "=" + aCurrentString;
};

/**
 * Generates the string evaluated when overriding getters and setters with
 * plain values.
 *
 * @param Property aItem
 *        The current getter or setter property.
 * @param string aCurrentString
 *        The trimmed user inputted string.
 * @param string aPrefix [optional]
 *        Prefix for the symbolic name.
 * @return string
 *         The string to be evaluated.
 */
VariablesView.overrideValueEvalMacro = function(aItem, aCurrentString, aPrefix = "") {
  let property = "\"" + aItem._nameString + "\"";
  let parent = aPrefix + aItem.ownerView._symbolicName || "this";

  return "Object.defineProperty(" + parent + "," + property + "," +
    "{ value: " + aCurrentString +
    ", enumerable: " + parent + ".propertyIsEnumerable(" + property + ")" +
    ", configurable: true" +
    ", writable: true" +
    "})";
};

/**
 * Generates the string evaluated when performing getters and setters changes.
 *
 * @param Property aItem
 *        The current getter or setter property.
 * @param string aCurrentString
 *        The trimmed user inputted string.
 * @param string aPrefix [optional]
 *        Prefix for the symbolic name.
 * @return string
 *         The string to be evaluated.
 */
VariablesView.getterOrSetterEvalMacro = function(aItem, aCurrentString, aPrefix = "") {
  let type = aItem._nameString;
  let propertyObject = aItem.ownerView;
  let parentObject = propertyObject.ownerView;
  let property = "\"" + propertyObject._nameString + "\"";
  let parent = aPrefix + parentObject._symbolicName || "this";

  switch (aCurrentString) {
    case "":
    case "null":
    case "undefined":
      let mirrorType = type == "get" ? "set" : "get";
      let mirrorLookup = type == "get" ? "__lookupSetter__" : "__lookupGetter__";

      // If the parent object will end up without any getter or setter,
      // morph it into a plain value.
      if ((type == "set" && propertyObject.getter.type == "undefined") ||
          (type == "get" && propertyObject.setter.type == "undefined")) {
        // Make sure the right getter/setter to value override macro is applied to the target object.
        return propertyObject.evaluationMacro(propertyObject, "undefined", aPrefix);
      }

      // Construct and return the getter/setter removal evaluation string.
      // e.g: Object.defineProperty(foo, "bar", {
      //   get: foo.__lookupGetter__("bar"),
      //   set: undefined,
      //   enumerable: true,
      //   configurable: true
      // })
      return "Object.defineProperty(" + parent + "," + property + "," +
        "{" + mirrorType + ":" + parent + "." + mirrorLookup + "(" + property + ")" +
        "," + type + ":" + undefined +
        ", enumerable: " + parent + ".propertyIsEnumerable(" + property + ")" +
        ", configurable: true" +
        "})";

    default:
      // Wrap statements inside a function declaration if not already wrapped.
      if (!aCurrentString.startsWith("function")) {
        let header = "function(" + (type == "set" ? "value" : "") + ")";
        let body = "";
        // If there's a return statement explicitly written, always use the
        // standard function definition syntax
        if (aCurrentString.contains("return ")) {
          body = "{" + aCurrentString + "}";
        }
        // If block syntax is used, use the whole string as the function body.
        else if (aCurrentString.startsWith("{")) {
          body = aCurrentString;
        }
        // Prefer an expression closure.
        else {
          body = "(" + aCurrentString + ")";
        }
        aCurrentString = header + body;
      }

      // Determine if a new getter or setter should be defined.
      let defineType = type == "get" ? "__defineGetter__" : "__defineSetter__";

      // Make sure all quotes are escaped in the expression's syntax,
      let defineFunc = "eval(\"(" + aCurrentString.replace(/"/g, "\\$&") + ")\")";

      // Construct and return the getter/setter evaluation string.
      // e.g: foo.__defineGetter__("bar", eval("(function() { return 42; })"))
      return parent + "." + defineType + "(" + property + "," + defineFunc + ")";
  }
};

/**
 * Function invoked when a getter or setter is deleted.
 *
 * @param Property aItem
 *        The current getter or setter property.
 */
VariablesView.getterOrSetterDeleteCallback = function(aItem) {
  aItem._disable();

  // Make sure the right getter/setter to value override macro is applied
  // to the target object.
  aItem.ownerView.eval(aItem.evaluationMacro(aItem, ""));

  return true; // Don't hide the element.
};


/**
 * A Scope is an object holding Variable instances.
 * Iterable via "for (let [name, variable] in instance) { }".
 *
 * @param VariablesView aView
 *        The view to contain this scope.
 * @param string aName
 *        The scope's name.
 * @param object aFlags [optional]
 *        Additional options or flags for this scope.
 */
function Scope(aView, aName, aFlags = {}) {
  this.ownerView = aView;

  this._onClick = this._onClick.bind(this);
  this._openEnum = this._openEnum.bind(this);
  this._openNonEnum = this._openNonEnum.bind(this);
  this._batchAppend = this._batchAppend.bind(this);

  // Inherit properties and flags from the parent view. You can override
  // each of these directly onto any scope, variable or property instance.
  this.eval = aView.eval;
  this.switch = aView.switch;
  this.delete = aView.delete;
  this.editableValueTooltip = aView.editableValueTooltip;
  this.editableNameTooltip = aView.editableNameTooltip;
  this.editButtonTooltip = aView.editButtonTooltip;
  this.deleteButtonTooltip = aView.deleteButtonTooltip;
  this.preventDescriptorModifiers = aView.preventDescriptorModifiers;
  this.contextMenuId = aView.contextMenuId;
  this.separatorStr = aView.separatorStr;

  // Creating maps and arrays thousands of times for variables or properties
  // with a large number of children fills up a lot of memory. Make sure
  // these are instantiated only if needed.
  XPCOMUtils.defineLazyGetter(this, "_store", () => new Map());
  XPCOMUtils.defineLazyGetter(this, "_enumItems", () => []);
  XPCOMUtils.defineLazyGetter(this, "_nonEnumItems", () => []);
  XPCOMUtils.defineLazyGetter(this, "_batchItems", () => []);

  this._init(aName.trim(), aFlags);
}

Scope.prototype = {
  /**
   * Whether this Scope should be prefetched when it is remoted.
   */
  shouldPrefetch: true,

  /**
   * Create a new Variable that is a child of this Scope.
   *
   * @param string aName
   *        The name of the new Property.
   * @param object aDescriptor
   *        The variable's descriptor.
   * @return Variable
   *         The newly created child Variable.
   */
  _createChild: function(aName, aDescriptor) {
    return new Variable(this, aName, aDescriptor);
  },

  /**
   * Adds a child to contain any inspected properties.
   *
   * @param string aName
   *        The child's name.
   * @param object aDescriptor
   *        Specifies the value and/or type & class of the child,
   *        or 'get' & 'set' accessor properties. If the type is implicit,
   *        it will be inferred from the value.
   *        e.g. - { value: 42 }
   *             - { value: true }
   *             - { value: "nasu" }
   *             - { value: { type: "undefined" } }
   *             - { value: { type: "null" } }
   *             - { value: { type: "object", class: "Object" } }
   *             - { get: { type: "object", class: "Function" },
   *                 set: { type: "undefined" } }
   * @param boolean aRelaxed
   *        True if name duplicates should be allowed.
   * @return Variable
   *         The newly created Variable instance, null if it already exists.
   */
  addItem: function(aName = "", aDescriptor = {}, aRelaxed = false) {
    if (this._store.has(aName) && !aRelaxed) {
      return null;
    }

    let child = this._createChild(aName, aDescriptor);
    this._store.set(aName, child);
    this._variablesView._itemsByElement.set(child._target, child);
    this._variablesView._currHierarchy.set(child._absoluteName, child);
    child.header = !!aName;
    return child;
  },

  /**
   * Adds items for this variable.
   *
   * @param object aItems
   *        An object containing some { name: descriptor } data properties,
   *        specifying the value and/or type & class of the variable,
   *        or 'get' & 'set' accessor properties. If the type is implicit,
   *        it will be inferred from the value.
   *        e.g. - { someProp0: { value: 42 },
   *                 someProp1: { value: true },
   *                 someProp2: { value: "nasu" },
   *                 someProp3: { value: { type: "undefined" } },
   *                 someProp4: { value: { type: "null" } },
   *                 someProp5: { value: { type: "object", class: "Object" } },
   *                 someProp6: { get: { type: "object", class: "Function" },
   *                              set: { type: "undefined" } } }
   * @param object aOptions [optional]
   *        Additional options for adding the properties. Supported options:
   *        - sorted: true to sort all the properties before adding them
   *        - callback: function invoked after each item is added
   */
  addItems: function(aItems, aOptions = {}) {
    let names = Object.keys(aItems);

    // Sort all of the properties before adding them, if preferred.
    if (aOptions.sorted) {
      names.sort();
    }
    // Add the properties to the current scope.
    for (let name of names) {
      let descriptor = aItems[name];
      let item = this.addItem(name, descriptor);

      if (aOptions.callback) {
        aOptions.callback(item, descriptor.value);
      }
    }
  },

  /**
   * Gets the variable in this container having the specified name.
   *
   * @param string aName
   *        The name of the variable to get.
   * @return Variable
   *         The matched variable, or null if nothing is found.
   */
  get: function(aName) {
    return this._store.get(aName);
  },

  /**
   * Recursively searches for the variable or property in this container
   * displayed by the specified node.
   *
   * @param nsIDOMNode aNode
   *        The node to search for.
   * @return Variable | Property
   *         The matched variable or property, or null if nothing is found.
   */
  find: function(aNode) {
    for (let [, variable] of this._store) {
      let match;
      if (variable._target == aNode) {
        match = variable;
      } else {
        match = variable.find(aNode);
      }
      if (match) {
        return match;
      }
    }
    return null;
  },

  /**
   * Determines if this scope is a direct child of a parent variables view,
   * scope, variable or property.
   *
   * @param VariablesView | Scope | Variable | Property
   *        The parent to check.
   * @return boolean
   *         True if the specified item is a direct child, false otherwise.
   */
  isChildOf: function(aParent) {
    return this.ownerView == aParent;
  },

  /**
   * Determines if this scope is a descendant of a parent variables view,
   * scope, variable or property.
   *
   * @param VariablesView | Scope | Variable | Property
   *        The parent to check.
   * @return boolean
   *         True if the specified item is a descendant, false otherwise.
   */
  isDescendantOf: function(aParent) {
    if (this.isChildOf(aParent)) {
      return true;
    }

    // Recurse to parent if it is a Scope, Variable, or Property.
    if (this.ownerView instanceof Scope) {
      return this.ownerView.isDescendantOf(aParent);
    }

    return false;
  },

  /**
   * Shows the scope.
   */
  show: function() {
    this._target.hidden = false;
    this._isContentVisible = true;

    if (this.onshow) {
      this.onshow(this);
    }
  },

  /**
   * Hides the scope.
   */
  hide: function() {
    this._target.hidden = true;
    this._isContentVisible = false;

    if (this.onhide) {
      this.onhide(this);
    }
  },

  /**
   * Expands the scope, showing all the added details.
   */
  expand: function() {
    if (this._isExpanded || this._locked) {
      return;
    }
    // If there's a large number of enumerable or non-enumerable items
    // contained in this scope, painting them may take several seconds,
    // even if they were already displayed before. In this case, show a throbber
    // to suggest that this scope is expanding.
    if (!this._isExpanding &&
         this._variablesView.lazyExpand &&
         this._store.size > LAZY_APPEND_BATCH) {
      this._isExpanding = true;

      // Start spinning a throbber in this scope's title and allow a few
      // milliseconds for it to be painted.
      this._startThrobber();
      this.window.setTimeout(this.expand.bind(this), LAZY_EXPAND_DELAY);
      return;
    }

    if (this._variablesView._enumVisible) {
      this._openEnum();
    }
    if (this._variablesView._nonEnumVisible) {
      Services.tm.currentThread.dispatch({ run: this._openNonEnum }, 0);
    }
    this._isExpanding = false;
    this._isExpanded = true;

    if (this.onexpand) {
      this.onexpand(this);
    }
  },

  /**
   * Collapses the scope, hiding all the added details.
   */
  collapse: function() {
    if (!this._isExpanded || this._locked) {
      return;
    }
    this._arrow.removeAttribute("open");
    this._enum.removeAttribute("open");
    this._nonenum.removeAttribute("open");
    this._isExpanded = false;

    if (this.oncollapse) {
      this.oncollapse(this);
    }
  },

  /**
   * Toggles between the scope's collapsed and expanded state.
   */
  toggle: function(e) {
    if (e && e.button != 0) {
      // Only allow left-click to trigger this event.
      return;
    }
    this._wasToggled = true;
    this.expanded ^= 1;

    // Make sure the scope and its contents are visibile.
    for (let [, variable] of this._store) {
      variable.header = true;
      variable._matched = true;
    }
    if (this.ontoggle) {
      this.ontoggle(this);
    }
  },

  /**
   * Shows the scope's title header.
   */
  showHeader: function() {
    if (this._isHeaderVisible || !this._nameString) {
      return;
    }
    this._target.removeAttribute("non-header");
    this._isHeaderVisible = true;
  },

  /**
   * Hides the scope's title header.
   * This action will automatically expand the scope.
   */
  hideHeader: function() {
    if (!this._isHeaderVisible) {
      return;
    }
    this.expand();
    this._target.setAttribute("non-header", "");
    this._isHeaderVisible = false;
  },

  /**
   * Shows the scope's expand/collapse arrow.
   */
  showArrow: function() {
    if (this._isArrowVisible) {
      return;
    }
    this._arrow.removeAttribute("invisible");
    this._isArrowVisible = true;
  },

  /**
   * Hides the scope's expand/collapse arrow.
   */
  hideArrow: function() {
    if (!this._isArrowVisible) {
      return;
    }
    this._arrow.setAttribute("invisible", "");
    this._isArrowVisible = false;
  },

  /**
   * Gets the visibility state.
   * @return boolean
   */
  get visible() this._isContentVisible,

  /**
   * Gets the expanded state.
   * @return boolean
   */
  get expanded() this._isExpanded,

  /**
   * Gets the header visibility state.
   * @return boolean
   */
  get header() this._isHeaderVisible,

  /**
   * Gets the twisty visibility state.
   * @return boolean
   */
  get twisty() this._isArrowVisible,

  /**
   * Gets the expand lock state.
   * @return boolean
   */
  get locked() this._locked,

  /**
   * Sets the visibility state.
   * @param boolean aFlag
   */
  set visible(aFlag) aFlag ? this.show() : this.hide(),

  /**
   * Sets the expanded state.
   * @param boolean aFlag
   */
  set expanded(aFlag) aFlag ? this.expand() : this.collapse(),

  /**
   * Sets the header visibility state.
   * @param boolean aFlag
   */
  set header(aFlag) aFlag ? this.showHeader() : this.hideHeader(),

  /**
   * Sets the twisty visibility state.
   * @param boolean aFlag
   */
  set twisty(aFlag) aFlag ? this.showArrow() : this.hideArrow(),

  /**
   * Sets the expand lock state.
   * @param boolean aFlag
   */
  set locked(aFlag) this._locked = aFlag,

  /**
   * Specifies if this target node may be focused.
   * @return boolean
   */
  get focusable() {
    // Check if this target node is actually visibile.
    if (!this._nameString ||
        !this._isContentVisible ||
        !this._isHeaderVisible ||
        !this._isMatch) {
      return false;
    }
    // Check if all parent objects are expanded.
    let item = this;

    // Recurse while parent is a Scope, Variable, or Property
    while ((item = item.ownerView) && item instanceof Scope) {
      if (!item._isExpanded) {
        return false;
      }
    }
    return true;
  },

  /**
   * Focus this scope.
   */
  focus: function() {
    this._variablesView._focusItem(this);
  },

  /**
   * Adds an event listener for a certain event on this scope's title.
   * @param string aName
   * @param function aCallback
   * @param boolean aCapture
   */
  addEventListener: function(aName, aCallback, aCapture) {
    this._title.addEventListener(aName, aCallback, aCapture);
  },

  /**
   * Removes an event listener for a certain event on this scope's title.
   * @param string aName
   * @param function aCallback
   * @param boolean aCapture
   */
  removeEventListener: function(aName, aCallback, aCapture) {
    this._title.removeEventListener(aName, aCallback, aCapture);
  },

  /**
   * Gets the id associated with this item.
   * @return string
   */
  get id() this._idString,

  /**
   * Gets the name associated with this item.
   * @return string
   */
  get name() this._nameString,

  /**
   * Gets the displayed value for this item.
   * @return string
   */
  get displayValue() this._valueString,

  /**
   * Gets the class names used for the displayed value.
   * @return string
   */
  get displayValueClassName() this._valueClassName,

  /**
   * Gets the element associated with this item.
   * @return nsIDOMNode
   */
  get target() this._target,

  /**
   * Initializes this scope's id, view and binds event listeners.
   *
   * @param string aName
   *        The scope's name.
   * @param object aFlags [optional]
   *        Additional options or flags for this scope.
   */
  _init: function(aName, aFlags) {
    this._idString = generateId(this._nameString = aName);
    this._displayScope(aName, "variables-view-scope", "devtools-toolbar");
    this._addEventListeners();
    this.parentNode.appendChild(this._target);
  },

  /**
   * Creates the necessary nodes for this scope.
   *
   * @param string aName
   *        The scope's name.
   * @param string aClassName
   *        A custom class name for this scope.
   * @param string aTitleClassName [optional]
   *        A custom class name for this scope's title.
   */
  _displayScope: function(aName, aClassName, aTitleClassName) {
    let document = this.document;

    let element = this._target = document.createElement("vbox");
    element.id = this._idString;
    element.className = aClassName;

    let arrow = this._arrow = document.createElement("hbox");
    arrow.className = "arrow";

    let name = this._name = document.createElement("label");
    name.className = "plain name";
    name.setAttribute("value", aName);

    let title = this._title = document.createElement("hbox");
    title.className = "title " + (aTitleClassName || "");
    title.setAttribute("align", "center");

    let enumerable = this._enum = document.createElement("vbox");
    let nonenum = this._nonenum = document.createElement("vbox");
    enumerable.className = "variables-view-element-details enum";
    nonenum.className = "variables-view-element-details nonenum";

    title.appendChild(arrow);
    title.appendChild(name);

    element.appendChild(title);
    element.appendChild(enumerable);
    element.appendChild(nonenum);
  },

  /**
   * Adds the necessary event listeners for this scope.
   */
  _addEventListeners: function() {
    this._title.addEventListener("mousedown", this._onClick, false);
  },

  /**
   * The click listener for this scope's title.
   */
  _onClick: function(e) {
    if (e.target == this._inputNode ||
        e.target == this._editNode ||
        e.target == this._deleteNode) {
      return;
    }
    this.toggle();
    this.focus();
  },

  /**
   * Lazily appends a node to this scope's enumerable or non-enumerable
   * container. Once a certain number of nodes have been batched, they
   * will be appended.
   *
   * @param boolean aImmediateFlag
   *        Set to false if append calls should be dispatched synchronously
   *        on the current thread, to allow for a paint flush.
   * @param boolean aEnumerableFlag
   *        Specifies if the node to append is enumerable or non-enumerable.
   * @param nsIDOMNode aChild
   *        The child node to append.
   */
  _lazyAppend: function(aImmediateFlag, aEnumerableFlag, aChild) {
    // Append immediately, don't stage items and don't allow for a paint flush.
    if (aImmediateFlag || !this._variablesView.lazyAppend) {
      if (aEnumerableFlag) {
        this._enum.appendChild(aChild);
      } else {
        this._nonenum.appendChild(aChild);
      }
      return;
    }

    let window = this.window;
    let batchItems = this._batchItems;

    window.clearTimeout(this._batchTimeout);
    batchItems.push({ enumerableFlag: aEnumerableFlag, child: aChild });

    // If a certain number of nodes have been batched, append all the
    // staged items now.
    if (batchItems.length > LAZY_APPEND_BATCH) {
      // Allow for a paint flush.
      Services.tm.currentThread.dispatch({ run: this._batchAppend }, 1);
      return;
    }
    // Postpone appending the staged items for later, to allow batching
    // more nodes.
    this._batchTimeout = window.setTimeout(this._batchAppend, LAZY_APPEND_DELAY);
  },

  /**
   * Appends all the batched nodes to this scope's enumerable and non-enumerable
   * containers.
   */
  _batchAppend: function() {
    let document = this.document;
    let batchItems = this._batchItems;

    // Create two document fragments, one for enumerable nodes, and one
    // for non-enumerable nodes.
    let frags = [document.createDocumentFragment(), document.createDocumentFragment()];

    for (let item of batchItems) {
      frags[~~item.enumerableFlag].appendChild(item.child);
    }
    batchItems.length = 0;
    this._enum.appendChild(frags[1]);
    this._nonenum.appendChild(frags[0]);
  },

  /**
   * Starts spinning a throbber in this scope's title.
   */
  _startThrobber: function() {
    if (this._throbber) {
      this._throbber.hidden = false;
      return;
    }
    let throbber = this._throbber = this.document.createElement("hbox");
    throbber.className = "variables-view-throbber";
    this._title.appendChild(throbber);
  },

  /**
   * Stops spinning the throbber in this scope's title.
   */
  _stopThrobber: function() {
    if (!this._throbber) {
      return;
    }
    this._throbber.hidden = true;
  },

  /**
   * Opens the enumerable items container.
   */
  _openEnum: function() {
    this._arrow.setAttribute("open", "");
    this._enum.setAttribute("open", "");
    this._stopThrobber();
  },

  /**
   * Opens the non-enumerable items container.
   */
  _openNonEnum: function() {
    this._nonenum.setAttribute("open", "");
    this._stopThrobber();
  },

  /**
   * Specifies if enumerable properties and variables should be displayed.
   * @param boolean aFlag
   */
  set _enumVisible(aFlag) {
    for (let [, variable] of this._store) {
      variable._enumVisible = aFlag;

      if (!this._isExpanded) {
        continue;
      }
      if (aFlag) {
        this._enum.setAttribute("open", "");
      } else {
        this._enum.removeAttribute("open");
      }
    }
  },

  /**
   * Specifies if non-enumerable properties and variables should be displayed.
   * @param boolean aFlag
   */
  set _nonEnumVisible(aFlag) {
    for (let [, variable] of this._store) {
      variable._nonEnumVisible = aFlag;

      if (!this._isExpanded) {
        continue;
      }
      if (aFlag) {
        this._nonenum.setAttribute("open", "");
      } else {
        this._nonenum.removeAttribute("open");
      }
    }
  },

  /**
   * Performs a case insensitive search for variables or properties matching
   * the query, and hides non-matched items.
   *
   * @param string aLowerCaseQuery
   *        The lowercased name of the variable or property to search for.
   */
  _performSearch: function(aLowerCaseQuery) {
    for (let [, variable] of this._store) {
      let currentObject = variable;
      let lowerCaseName = variable._nameString.toLowerCase();
      let lowerCaseValue = variable._valueString.toLowerCase();

      // Non-matched variables or properties require a corresponding attribute.
      if (!lowerCaseName.contains(aLowerCaseQuery) &&
          !lowerCaseValue.contains(aLowerCaseQuery)) {
        variable._matched = false;
      }
      // Variable or property is matched.
      else {
        variable._matched = true;

        // If the variable was ever expanded, there's a possibility it may
        // contain some matched properties, so make sure they're visible
        // ("expand downwards").

        if (variable._wasToggled && aLowerCaseQuery) {
          variable.expand();
        }
        if (variable._isExpanded && !aLowerCaseQuery) {
          variable._wasToggled = true;
        }

        // If the variable is contained in another Scope, Variable, or Property,
        // the parent may not be a match, thus hidden. It should be visible
        // ("expand upwards").
        while ((variable = variable.ownerView) &&  /* Parent object exists. */
               variable instanceof Scope) {

          // Show and expand the parent, as it is certainly accessible.
          variable._matched = true;
          aLowerCaseQuery && variable.expand();
        }
      }

      // Proceed with the search recursively inside this variable or property.
      if (currentObject._wasToggled ||
          currentObject.getter ||
          currentObject.setter) {
        currentObject._performSearch(aLowerCaseQuery);
      }
    }
  },

  /**
   * Sets if this object instance is a matched or non-matched item.
   * @param boolean aStatus
   */
  set _matched(aStatus) {
    if (this._isMatch == aStatus) {
      return;
    }
    if (aStatus) {
      this._isMatch = true;
      this.target.removeAttribute("non-match");
    } else {
      this._isMatch = false;
      this.target.setAttribute("non-match", "");
    }
  },

  /**
   * Gets the first search results match in this scope.
   * @return Variable | Property
   */
  get _firstMatch() {
    for (let [, variable] of this._store) {
      let match;
      if (variable._isMatch) {
        match = variable;
      } else {
        match = variable._firstMatch;
      }
      if (match) {
        return match;
      }
    }
    return null;
  },

  /**
   * Find the first item in the tree of visible items in this item that matches
   * the predicate. Searches in visual order (the order seen by the user).
   * Tests itself, then descends into first the enumerable children and then
   * the non-enumerable children (since they are presented in separate groups).
   *
   * @param function aPredicate
   *        A function that returns true when a match is found.
   * @return Scope | Variable | Property
   *         The first visible scope, variable or property, or null if nothing
   *         is found.
   */
  _findInVisibleItems: function(aPredicate) {
    if (aPredicate(this)) {
      return this;
    }

    if (this._isExpanded) {
      if (this._variablesView._enumVisible) {
        for (let item of this._enumItems) {
          let result = item._findInVisibleItems(aPredicate);
          if (result) {
            return result;
          }
        }
      }

      if (this._variablesView._nonEnumVisible) {
        for (let item of this._nonEnumItems) {
          let result = item._findInVisibleItems(aPredicate);
          if (result) {
            return result;
          }
        }
      }
    }

    return null;
  },

  /**
   * Find the last item in the tree of visible items in this item that matches
   * the predicate. Searches in reverse visual order (opposite of the order
   * seen by the user). Descends into first the non-enumerable children, then
   * the enumerable children (since they are presented in separate groups), and
   * finally tests itself.
   *
   * @param function aPredicate
   *        A function that returns true when a match is found.
   * @return Scope | Variable | Property
   *         The last visible scope, variable or property, or null if nothing
   *         is found.
   */
  _findInVisibleItemsReverse: function(aPredicate) {
    if (this._isExpanded) {
      if (this._variablesView._nonEnumVisible) {
        for (let i = this._nonEnumItems.length - 1; i >= 0; i--) {
          let item = this._nonEnumItems[i];
          let result = item._findInVisibleItemsReverse(aPredicate);
          if (result) {
            return result;
          }
        }
      }

      if (this._variablesView._enumVisible) {
        for (let i = this._enumItems.length - 1; i >= 0; i--) {
          let item = this._enumItems[i];
          let result = item._findInVisibleItemsReverse(aPredicate);
          if (result) {
            return result;
          }
        }
      }
    }

    if (aPredicate(this)) {
      return this;
    }

    return null;
  },

  /**
   * Gets top level variables view instance.
   * @return VariablesView
   */
  get _variablesView() this._topView || (this._topView = (function(self) {
    let parentView = self.ownerView;
    let topView;

    while (topView = parentView.ownerView) {
      parentView = topView;
    }
    return parentView;
  })(this)),

  /**
   * Gets the parent node holding this scope.
   * @return nsIDOMNode
   */
  get parentNode() this.ownerView._list,

  /**
   * Gets the owner document holding this scope.
   * @return nsIHTMLDocument
   */
  get document() this._document || (this._document = this.ownerView.document),

  /**
   * Gets the default window holding this scope.
   * @return nsIDOMWindow
   */
  get window() this._window || (this._window = this.ownerView.window),

  _topView: null,
  _document: null,
  _window: null,

  ownerView: null,
  eval: null,
  switch: null,
  delete: null,
  editableValueTooltip: "",
  editableNameTooltip: "",
  editButtonTooltip: "",
  deleteButtonTooltip: "",
  preventDescriptorModifiers: false,
  contextMenuId: "",
  separatorStr: "",

  _store: null,
  _enumItems: null,
  _nonEnumItems: null,
  _fetched: false,
  _retrieved: false,
  _committed: false,
  _batchItems: null,
  _batchTimeout: null,
  _locked: false,
  _isExpanding: false,
  _isExpanded: false,
  _wasToggled: false,
  _isContentVisible: true,
  _isHeaderVisible: true,
  _isArrowVisible: true,
  _isMatch: true,
  _idString: "",
  _nameString: "",
  _target: null,
  _arrow: null,
  _name: null,
  _title: null,
  _enum: null,
  _nonenum: null,
  _throbber: null
};

/**
 * A Variable is a Scope holding Property instances.
 * Iterable via "for (let [name, property] in instance) { }".
 *
 * @param Scope aScope
 *        The scope to contain this variable.
 * @param string aName
 *        The variable's name.
 * @param object aDescriptor
 *        The variable's descriptor.
 */
function Variable(aScope, aName, aDescriptor) {
  this._setTooltips = this._setTooltips.bind(this);
  this._activateNameInput = this._activateNameInput.bind(this);
  this._activateValueInput = this._activateValueInput.bind(this);

  // Treat safe getter descriptors as descriptors with a value.
  if ("getterValue" in aDescriptor) {
    aDescriptor.value = aDescriptor.getterValue;
    delete aDescriptor.get;
    delete aDescriptor.set;
  }

  Scope.call(this, aScope, aName, this._initialDescriptor = aDescriptor);
  this.setGrip(aDescriptor.value);
  this._symbolicName = aName;
  this._absoluteName = aScope.name + "[\"" + aName + "\"]";
}

Variable.prototype = Heritage.extend(Scope.prototype, {
  /**
   * Whether this Scope should be prefetched when it is remoted.
   */
  get shouldPrefetch(){
    return this.name == "window" || this.name == "this";
  },

  /**
   * Create a new Property that is a child of Variable.
   *
   * @param string aName
   *        The name of the new Property.
   * @param object aDescriptor
   *        The property's descriptor.
   * @return Property
   *         The newly created child Property.
   */
  _createChild: function(aName, aDescriptor) {
    return new Property(this, aName, aDescriptor);
  },

  /**
   * Populates this variable to contain all the properties of an object.
   *
   * @param object aObject
   *        The raw object you want to display.
   * @param object aOptions [optional]
   *        Additional options for adding the properties. Supported options:
   *        - sorted: true to sort all the properties before adding them
   *        - expanded: true to expand all the properties after adding them
   */
  populate: function(aObject, aOptions = {}) {
    // Retrieve the properties only once.
    if (this._fetched) {
      return;
    }
    this._fetched = true;

    let propertyNames = Object.getOwnPropertyNames(aObject);
    let prototype = Object.getPrototypeOf(aObject);

    // Sort all of the properties before adding them, if preferred.
    if (aOptions.sorted) {
      propertyNames.sort();
    }
    // Add all the variable properties.
    for (let name of propertyNames) {
      let descriptor = Object.getOwnPropertyDescriptor(aObject, name);
      if (descriptor.get || descriptor.set) {
        let prop = this._addRawNonValueProperty(name, descriptor);
        if (aOptions.expanded) {
          prop.expanded = true;
        }
      } else {
        let prop = this._addRawValueProperty(name, descriptor, aObject[name]);
        if (aOptions.expanded) {
          prop.expanded = true;
        }
      }
    }
    // Add the variable's __proto__.
    if (prototype) {
      this._addRawValueProperty("__proto__", {}, prototype);
    }
  },

  /**
   * Populates a specific variable or property instance to contain all the
   * properties of an object
   *
   * @param Variable | Property aVar
   *        The target variable to populate.
   * @param object aObject [optional]
   *        The raw object you want to display. If unspecified, the object is
   *        assumed to be defined in a _sourceValue property on the target.
   */
  _populateTarget: function(aVar, aObject = aVar._sourceValue) {
    aVar.populate(aObject);
  },

  /**
   * Adds a property for this variable based on a raw value descriptor.
   *
   * @param string aName
   *        The property's name.
   * @param object aDescriptor
   *        Specifies the exact property descriptor as returned by a call to
   *        Object.getOwnPropertyDescriptor.
   * @param object aValue
   *        The raw property value you want to display.
   * @return Property
   *         The newly added property instance.
   */
  _addRawValueProperty: function(aName, aDescriptor, aValue) {
    let descriptor = Object.create(aDescriptor);
    descriptor.value = VariablesView.getGrip(aValue);

    let propertyItem = this.addItem(aName, descriptor);
    propertyItem._sourceValue = aValue;

    // Add an 'onexpand' callback for the property, lazily handling
    // the addition of new child properties.
    if (!VariablesView.isPrimitive(descriptor)) {
      propertyItem.onexpand = this._populateTarget;
    }
    return propertyItem;
  },

  /**
   * Adds a property for this variable based on a getter/setter descriptor.
   *
   * @param string aName
   *        The property's name.
   * @param object aDescriptor
   *        Specifies the exact property descriptor as returned by a call to
   *        Object.getOwnPropertyDescriptor.
   * @return Property
   *         The newly added property instance.
   */
  _addRawNonValueProperty: function(aName, aDescriptor) {
    let descriptor = Object.create(aDescriptor);
    descriptor.get = VariablesView.getGrip(aDescriptor.get);
    descriptor.set = VariablesView.getGrip(aDescriptor.set);

    return this.addItem(aName, descriptor);
  },

  /**
   * Gets this variable's path to the topmost scope.
   * For example, a symbolic name may look like "arguments['0']['foo']['bar']".
   * @return string
   */
  get symbolicName() this._symbolicName,

  /**
   * Returns this variable's value from the descriptor if available.
   * @return any
   */
  get value() this._initialDescriptor.value,

  /**
   * Returns this variable's getter from the descriptor if available.
   * @return object
   */
  get getter() this._initialDescriptor.get,

  /**
   * Returns this variable's getter from the descriptor if available.
   * @return object
   */
  get setter() this._initialDescriptor.set,

  /**
   * Sets the specific grip for this variable (applies the text content and
   * class name to the value label).
   *
   * The grip should contain the value or the type & class, as defined in the
   * remote debugger protocol. For convenience, undefined and null are
   * both considered types.
   *
   * @param any aGrip
   *        Specifies the value and/or type & class of the variable.
   *        e.g. - 42
   *             - true
   *             - "nasu"
   *             - { type: "undefined" }
   *             - { type: "null" }
   *             - { type: "object", class: "Object" }
   */
  setGrip: function(aGrip) {
    // Don't allow displaying grip information if there's no name available.
    if (!this._nameString || aGrip === undefined || aGrip === null) {
      return;
    }
    // Getters and setters should display grip information in sub-properties.
    if (!this._isUndefined && (this.getter || this.setter)) {
      this._valueLabel.setAttribute("value", "");
      return;
    }

    // Make sure the value is escaped unicode if it's a string.
    if (typeof aGrip == "string") {
      aGrip = NetworkHelper.convertToUnicode(unescape(aGrip));
    }

    let prevGrip = this._valueGrip;
    if (prevGrip) {
      this._valueLabel.classList.remove(VariablesView.getClass(prevGrip));
    }
    this._valueGrip = aGrip;
    this._valueString = VariablesView.getString(aGrip);
    this._valueClassName = VariablesView.getClass(aGrip);

    this._valueLabel.classList.add(this._valueClassName);
    this._valueLabel.setAttribute("value", this._valueString);
  },

  /**
   * Initializes this variable's id, view and binds event listeners.
   *
   * @param string aName
   *        The variable's name.
   * @param object aDescriptor
   *        The variable's descriptor.
   */
  _init: function(aName, aDescriptor) {
    this._idString = generateId(this._nameString = aName);
    this._displayScope(aName, "variables-view-variable variable-or-property");

    // Don't allow displaying variable information there's no name available.
    if (this._nameString) {
      this._displayVariable();
      this._customizeVariable();
      this._prepareTooltips();
      this._setAttributes();
      this._addEventListeners();
    }

    this._onInit(this.ownerView._store.size < LAZY_APPEND_BATCH);
  },

  /**
   * Called when this variable has finished initializing, and is ready to
   * be attached to the owner view.
   *
   * @param boolean aImmediateFlag
   *        @see Scope.prototype._lazyAppend
   */
  _onInit: function(aImmediateFlag) {
    if (this._initialDescriptor.enumerable ||
        this._nameString == "this" ||
        this._nameString == "<return>" ||
        this._nameString == "<exception>") {
      this.ownerView._lazyAppend(aImmediateFlag, true, this._target);
      this.ownerView._enumItems.push(this);
    } else {
      this.ownerView._lazyAppend(aImmediateFlag, false, this._target);
      this.ownerView._nonEnumItems.push(this);
    }
  },

  /**
   * Creates the necessary nodes for this variable.
   */
  _displayVariable: function() {
    let document = this.document;
    let descriptor = this._initialDescriptor;

    let separatorLabel = this._separatorLabel = document.createElement("label");
    separatorLabel.className = "plain separator";
    separatorLabel.setAttribute("value", this.ownerView.separatorStr);

    let valueLabel = this._valueLabel = document.createElement("label");
    valueLabel.className = "plain value";
    valueLabel.setAttribute("crop", "center");
    valueLabel.setAttribute('flex', "1");

    this._title.appendChild(separatorLabel);
    this._title.appendChild(valueLabel);

    let isPrimitive = this._isPrimitive = VariablesView.isPrimitive(descriptor);
    let isUndefined = this._isUndefined = VariablesView.isUndefined(descriptor);

    if (isPrimitive || isUndefined) {
      this.hideArrow();
    }
    if (!isUndefined && (descriptor.get || descriptor.set)) {
      separatorLabel.hidden = true;
      valueLabel.hidden = true;

      // Changing getter/setter names is never allowed.
      this.switch = null;

      // Getter/setter properties require special handling when it comes to
      // evaluation and deletion.
      if (this.ownerView.eval) {
        this.delete = VariablesView.getterOrSetterDeleteCallback;
        this.evaluationMacro = VariablesView.overrideValueEvalMacro;
      }
      // Deleting getters and setters individually is not allowed if no
      // evaluation method is provided.
      else {
        this.delete = null;
        this.evaluationMacro = null;
      }

      let getter = this.addItem("get", { value: descriptor.get });
      let setter = this.addItem("set", { value: descriptor.set });
      getter.evaluationMacro = VariablesView.getterOrSetterEvalMacro;
      setter.evaluationMacro = VariablesView.getterOrSetterEvalMacro;

      getter.hideArrow();
      setter.hideArrow();
      this.expand();
    }
  },

  /**
   * Adds specific nodes for this variable based on custom flags.
   */
  _customizeVariable: function() {
    let ownerView = this.ownerView;
    let descriptor = this._initialDescriptor;

    if (ownerView.eval) {
      if (!this._isUndefined && (this.getter || this.setter)) {
        let editNode = this._editNode = this.document.createElement("toolbarbutton");
        editNode.className = "plain variables-view-edit";
        editNode.addEventListener("mousedown", this._onEdit.bind(this), false);
        this._title.appendChild(editNode);
      }
    }
    if (ownerView.delete) {
      if (!this._isUndefined || !(ownerView.getter && ownerView.setter)) {
        let deleteNode = this._deleteNode = this.document.createElement("toolbarbutton");
        deleteNode.className = "plain variables-view-delete";
        deleteNode.setAttribute("ordinal", 2);
        deleteNode.addEventListener("click", this._onDelete.bind(this), false);
        this._title.appendChild(deleteNode);
      }
    }
    if (ownerView.contextMenuId) {
      this._title.setAttribute("context", ownerView.contextMenuId);
    }

    if (ownerView.preventDescriptorModifiers) {
      return;
    }

    if (!descriptor.writable && !ownerView.getter && !ownerView.setter) {
      let nonWritableIcon = this.document.createElement("hbox");
      nonWritableIcon.className = "variable-or-property-non-writable-icon";
      this._title.appendChild(nonWritableIcon);
    }
    if (descriptor.value && typeof descriptor.value == "object") {
      if (descriptor.value.frozen) {
        let frozenLabel = this.document.createElement("label");
        frozenLabel.className = "plain variable-or-property-frozen-label";
        frozenLabel.setAttribute("value", "F");
        this._title.appendChild(frozenLabel);
      }
      if (descriptor.value.sealed) {
        let sealedLabel = this.document.createElement("label");
        sealedLabel.className = "plain variable-or-property-sealed-label";
        sealedLabel.setAttribute("value", "S");
        this._title.appendChild(sealedLabel);
      }
      if (!descriptor.value.extensible) {
        let nonExtensibleLabel = this.document.createElement("label");
        nonExtensibleLabel.className = "plain variable-or-property-non-extensible-label";
        nonExtensibleLabel.setAttribute("value", "N");
        this._title.appendChild(nonExtensibleLabel);
      }
    }
  },

  /**
   * Prepares all tooltips for this variable.
   */
  _prepareTooltips: function() {
    this._target.addEventListener("mouseover", this._setTooltips, false);
  },

  /**
   * Sets all tooltips for this variable.
   */
  _setTooltips: function() {
    this._target.removeEventListener("mouseover", this._setTooltips, false);

    let ownerView = this.ownerView;
    if (ownerView.preventDescriptorModifiers) {
      return;
    }

    let tooltip = this.document.createElement("tooltip");
    tooltip.id = "tooltip-" + this._idString;
    tooltip.setAttribute("orient", "horizontal");

    let labels = [
      "configurable", "enumerable", "writable",
      "frozen", "sealed", "extensible", "WebIDL"];

    for (let label of labels) {
      let labelElement = this.document.createElement("label");
      labelElement.setAttribute("value", label);
      tooltip.appendChild(labelElement);
    }

    this._target.appendChild(tooltip);
    this._target.setAttribute("tooltip", tooltip.id);

    if (this._editNode && ownerView.eval) {
      this._editNode.setAttribute("tooltiptext", ownerView.editButtonTooltip);
    }
    if (this._valueLabel && ownerView.eval) {
      this._valueLabel.setAttribute("tooltiptext", ownerView.editableValueTooltip);
    }
    if (this._name && ownerView.switch) {
      this._name.setAttribute("tooltiptext", ownerView.editableNameTooltip);
    }
    if (this._deleteNode && ownerView.delete) {
      this._deleteNode.setAttribute("tooltiptext", ownerView.deleteButtonTooltip);
    }
  },

  /**
   * Sets a variable's configurable, enumerable and writable attributes,
   * and specifies if it's a 'this', '<exception>' or '__proto__' reference.
   */
  _setAttributes: function() {
    let ownerView = this.ownerView;
    if (ownerView.preventDescriptorModifiers) {
      return;
    }

    let descriptor = this._initialDescriptor;
    let target = this._target;
    let name = this._nameString;

    if (ownerView.eval) {
      target.setAttribute("editable", "");
    }

    if (!descriptor.configurable) {
      target.setAttribute("non-configurable", "");
    }
    if (!descriptor.enumerable) {
      target.setAttribute("non-enumerable", "");
    }
    if (!descriptor.writable && !ownerView.getter && !ownerView.setter) {
      target.setAttribute("non-writable", "");
    }

    if (descriptor.value && typeof descriptor.value == "object") {
      if (descriptor.value.frozen) {
        target.setAttribute("frozen", "");
      }
      if (descriptor.value.sealed) {
        target.setAttribute("sealed", "");
      }
      if (!descriptor.value.extensible) {
        target.setAttribute("non-extensible", "");
      }
    }

    if (descriptor && "getterValue" in descriptor) {
      target.setAttribute("safe-getter", "");
    }
    if (name == "this") {
      target.setAttribute("self", "");
    }

    else if (name == "<exception>") {
      target.setAttribute("exception", "");
    }
    else if (name == "<return>") {
      target.setAttribute("return", "");
    }
    else if (name == "__proto__") {
      target.setAttribute("proto", "");
    }
  },

  /**
   * Adds the necessary event listeners for this variable.
   */
  _addEventListeners: function() {
    this._name.addEventListener("dblclick", this._activateNameInput, false);
    this._valueLabel.addEventListener("mousedown", this._activateValueInput, false);
    this._title.addEventListener("mousedown", this._onClick, false);
  },

  /**
   * Creates a textbox node in place of a label.
   *
   * @param nsIDOMNode aLabel
   *        The label to be replaced with a textbox.
   * @param string aClassName
   *        The class to be applied to the textbox.
   * @param object aCallbacks
   *        An object containing the onKeypress and onBlur callbacks.
   */
  _activateInput: function(aLabel, aClassName, aCallbacks) {
    let initialString = aLabel.getAttribute("value");

    // Create a texbox input element which will be shown in the current
    // element's specified label location.
    let input = this.document.createElement("textbox");
    input.className = "plain " + aClassName;
    input.setAttribute("value", initialString);
    input.setAttribute("flex", "1");

    // Replace the specified label with a textbox input element.
    aLabel.parentNode.replaceChild(input, aLabel);
    this._variablesView._boxObject.ensureElementIsVisible(input);
    input.select();

    // When the value is a string (displayed as "value"), then we probably want
    // to change it to another string in the textbox, so to avoid typing the ""
    // again, tackle with the selection bounds just a bit.
    if (aLabel.getAttribute("value").match(/^".+"$/)) {
      input.selectionEnd--;
      input.selectionStart++;
    }

    input.addEventListener("keypress", aCallbacks.onKeypress, false);
    input.addEventListener("blur", aCallbacks.onBlur, false);

    this._prevExpandable = this.twisty;
    this._prevExpanded = this.expanded;
    this.collapse();
    this.hideArrow();
    this._locked = true;

    this._inputNode = input;
    this._stopThrobber();
  },

  /**
   * Removes the textbox node in place of a label.
   *
   * @param nsIDOMNode aLabel
   *        The label which was replaced with a textbox.
   * @param object aCallbacks
   *        An object containing the onKeypress and onBlur callbacks.
   */
  _deactivateInput: function(aLabel, aInput, aCallbacks) {
    aInput.parentNode.replaceChild(aLabel, aInput);
    this._variablesView._boxObject.scrollBy(-this._target.clientWidth, 0);

    aInput.removeEventListener("keypress", aCallbacks.onKeypress, false);
    aInput.removeEventListener("blur", aCallbacks.onBlur, false);

    this._locked = false;
    this.twisty = this._prevExpandable;
    this.expanded = this._prevExpanded;

    this._inputNode = null;
    this._stopThrobber();
  },

  /**
   * Makes this variable's name editable.
   */
  _activateNameInput: function(e) {
    if (e && e.button != 0) {
      // Only allow left-click to trigger this event.
      return;
    }
    if (!this.ownerView.switch) {
      return;
    }
    if (e) {
      e.preventDefault();
      e.stopPropagation();
    }

    this._onNameInputKeyPress = this._onNameInputKeyPress.bind(this);
    this._deactivateNameInput = this._deactivateNameInput.bind(this);

    this._activateInput(this._name, "element-name-input", {
      onKeypress: this._onNameInputKeyPress,
      onBlur: this._deactivateNameInput
    });
    this._separatorLabel.hidden = true;
    this._valueLabel.hidden = true;
  },

  /**
   * Deactivates this variable's editable name mode.
   */
  _deactivateNameInput: function(e) {
    this._deactivateInput(this._name, e.target, {
      onKeypress: this._onNameInputKeyPress,
      onBlur: this._deactivateNameInput
    });
    this._separatorLabel.hidden = false;
    this._valueLabel.hidden = false;
  },

  /**
   * Makes this variable's value editable.
   */
  _activateValueInput: function(e) {
    if (e && e.button != 0) {
      // Only allow left-click to trigger this event.
      return;
    }
    if (!this.ownerView.eval) {
      return;
    }
    if (e) {
      e.preventDefault();
      e.stopPropagation();
    }

    this._onValueInputKeyPress = this._onValueInputKeyPress.bind(this);
    this._deactivateValueInput = this._deactivateValueInput.bind(this);

    this._activateInput(this._valueLabel, "element-value-input", {
      onKeypress: this._onValueInputKeyPress,
      onBlur: this._deactivateValueInput
    });
  },

  /**
   * Deactivates this variable's editable value mode.
   */
  _deactivateValueInput: function(e) {
    this._deactivateInput(this._valueLabel, e.target, {
      onKeypress: this._onValueInputKeyPress,
      onBlur: this._deactivateValueInput
    });
  },

  /**
   * Disables this variable prior to a new name switch or value evaluation.
   */
  _disable: function() {
    this.hideArrow();
    this._separatorLabel.hidden = true;
    this._valueLabel.hidden = true;
    this._enum.hidden = true;
    this._nonenum.hidden = true;

    if (this._editNode) {
      this._editNode.hidden = true;
    }
    if (this._deleteNode) {
      this._deleteNode.hidden = true;
    }
  },

  /**
   * Deactivates this variable's editable mode and callbacks the new name.
   */
  _saveNameInput: function(e) {
    let input = e.target;
    let initialString = this._name.getAttribute("value");
    let currentString = input.value.trim();
    this._deactivateNameInput(e);

    if (initialString != currentString) {
      if (!this._variablesView.preventDisableOnChage) {
        this._disable();
        this._name.value = currentString;
      }
      this.ownerView.switch(this, currentString);
    }
  },

  /**
   * Deactivates this variable's editable mode and evaluates the new value.
   */
  _saveValueInput: function(e) {
    let input = e.target;
    let initialString = this._valueLabel.getAttribute("value");
    let currentString = input.value.trim();
    this._deactivateValueInput(e);

    if (initialString != currentString) {
      if (!this._variablesView.preventDisableOnChage) {
        this._disable();
      }
      this.ownerView.eval(this.evaluationMacro(this, currentString.trim()));
    }
  },

  /**
   * The current macro used to generate the string evaluated when performing
   * a variable or property value change.
   */
  evaluationMacro: VariablesView.simpleValueEvalMacro,

  /**
   * The key press listener for this variable's editable name textbox.
   */
  _onNameInputKeyPress: function(e) {
    e.stopPropagation();

    switch(e.keyCode) {
      case e.DOM_VK_RETURN:
      case e.DOM_VK_ENTER:
        this._saveNameInput(e);
        this.focus();
        return;
      case e.DOM_VK_ESCAPE:
        this._deactivateNameInput(e);
        this.focus();
        return;
    }
  },

  /**
   * The key press listener for this variable's editable value textbox.
   */
  _onValueInputKeyPress: function(e) {
    e.stopPropagation();

    switch(e.keyCode) {
      case e.DOM_VK_RETURN:
      case e.DOM_VK_ENTER:
        this._saveValueInput(e);
        this.focus();
        return;
      case e.DOM_VK_ESCAPE:
        this._deactivateValueInput(e);
        this.focus();
        return;
    }
  },

  /**
   * The click listener for the edit button.
   */
  _onEdit: function(e) {
    e.preventDefault();
    e.stopPropagation();
    this._activateValueInput();
  },

  /**
   * The click listener for the delete button.
   */
  _onDelete: function(e) {
    e.preventDefault();
    e.stopPropagation();

    if (this.ownerView.delete) {
      if (!this.ownerView.delete(this)) {
        this.hide();
      }
    }
  },

  _symbolicName: "",
  _absoluteName: "",
  _initialDescriptor: null,
  _isPrimitive: false,
  _isUndefined: false,
  _separatorLabel: null,
  _valueLabel: null,
  _inputNode: null,
  _editNode: null,
  _deleteNode: null,
  _tooltip: null,
  _valueGrip: null,
  _valueString: "",
  _valueClassName: "",
  _prevExpandable: false,
  _prevExpanded: false
});

/**
 * A Property is a Variable holding additional child Property instances.
 * Iterable via "for (let [name, property] in instance) { }".
 *
 * @param Variable aVar
 *        The variable to contain this property.
 * @param string aName
 *        The property's name.
 * @param object aDescriptor
 *        The property's descriptor.
 */
function Property(aVar, aName, aDescriptor) {
  Variable.call(this, aVar, aName, aDescriptor);
  this._symbolicName = aVar._symbolicName + "[\"" + aName + "\"]";
  this._absoluteName = aVar._absoluteName + "[\"" + aName + "\"]";
}

Property.prototype = Heritage.extend(Variable.prototype, {
  /**
   * Initializes this property's id, view and binds event listeners.
   *
   * @param string aName
   *        The property's name.
   * @param object aDescriptor
   *        The property's descriptor.
   */
  _init: function(aName, aDescriptor) {
    this._idString = generateId(this._nameString = aName);
    this._displayScope(aName, "variables-view-property variable-or-property");

    // Don't allow displaying property information there's no name available.
    if (this._nameString) {
      this._displayVariable();
      this._customizeVariable();
      this._prepareTooltips();
      this._setAttributes();
      this._addEventListeners();
    }

    this._onInit(this.ownerView._store.size < LAZY_APPEND_BATCH);
  },

  /**
   * Called when this property has finished initializing, and is ready to
   * be attached to the owner view.
   *
   * @param boolean aImmediateFlag
   *        @see Scope.prototype._lazyAppend
   */
  _onInit: function(aImmediateFlag) {
    if (this._initialDescriptor.enumerable) {
      this.ownerView._lazyAppend(aImmediateFlag, true, this._target);
      this.ownerView._enumItems.push(this);
    } else {
      this.ownerView._lazyAppend(aImmediateFlag, false, this._target);
      this.ownerView._nonEnumItems.push(this);
    }
  }
});

/**
 * A generator-iterator over the VariablesView, Scopes, Variables and Properties.
 */
VariablesView.prototype.__iterator__ =
Scope.prototype.__iterator__ =
Variable.prototype.__iterator__ =
Property.prototype.__iterator__ = function() {
  for (let item of this._store) {
    yield item;
  }
};

/**
 * Forget everything recorded about added scopes, variables or properties.
 * @see VariablesView.createHierarchy
 */
VariablesView.prototype.clearHierarchy = function() {
  this._prevHierarchy.clear();
  this._currHierarchy.clear();
};

/**
 * Start recording a hierarchy of any added scopes, variables or properties.
 * @see VariablesView.commitHierarchy
 */
VariablesView.prototype.createHierarchy = function() {
  this._prevHierarchy = this._currHierarchy;
  this._currHierarchy = new Map(); // Don't clear, this is just simple swapping.
};

/**
 * Briefly flash the variables that changed between the previous and current
 * scope/variable/property hierarchies and reopen previously expanded nodes.
 */
VariablesView.prototype.commitHierarchy = function() {
  let prevHierarchy = this._prevHierarchy;
  let currHierarchy = this._currHierarchy;

  for (let [absoluteName, currVariable] of currHierarchy) {
    // Ignore variables which were already commmitted.
    if (currVariable._committed) {
      continue;
    }
    // Avoid performing expensive operations.
    if (this.commitHierarchyIgnoredItems[currVariable._nameString]) {
      continue;
    }

    // Try to get the previous instance of the inspected variable to
    // determine the difference in state.
    let prevVariable = prevHierarchy.get(absoluteName);
    let expanded = false;
    let changed = false;

    // If the inspected variable existed in a previous hierarchy, check if
    // the displayed value (a representation of the grip) has changed and if
    // it was previously expanded.
    if (prevVariable) {
      expanded = prevVariable._isExpanded;

      // Only analyze Variables and Properties for displayed value changes.
      if (currVariable instanceof Variable) {
        changed = prevVariable._valueString != currVariable._valueString;
      }
    }

    // Make sure this variable is not handled in ulteror commits for the
    // same hierarchy.
    currVariable._committed = true;

    // Re-expand the variable if not previously collapsed.
    if (expanded) {
      currVariable._wasToggled = prevVariable._wasToggled;
      currVariable.expand();
    }
    // This variable was either not changed or removed, no need to continue.
    if (!changed) {
      continue;
    }

    // Apply an attribute determining the flash type and duration.
    // Dispatch this action after all the nodes have been drawn, so that
    // the transition efects can take place.
    this.window.setTimeout(function(aTarget) {
      aTarget.addEventListener("transitionend", function onEvent() {
        aTarget.removeEventListener("transitionend", onEvent, false);
        aTarget.removeAttribute("changed");
      }, false);
      aTarget.setAttribute("changed", "");
    }.bind(this, currVariable.target), this.lazyEmptyDelay + 1);
  }
};

// Some variables are likely to contain a very large number of properties.
// It would be a bad idea to re-expand them or perform expensive operations.
VariablesView.prototype.commitHierarchyIgnoredItems = Object.create(null, {
  "window": { value: true }
});

/**
 * Returns true if the descriptor represents an undefined, null or
 * primitive value.
 *
 * @param object aDescriptor
 *        The variable's descriptor.
 */
VariablesView.isPrimitive = function(aDescriptor) {
  // For accessor property descriptors, the getter and setter need to be
  // contained in 'get' and 'set' properties.
  let getter = aDescriptor.get;
  let setter = aDescriptor.set;
  if (getter || setter) {
    return false;
  }

  // As described in the remote debugger protocol, the value grip
  // must be contained in a 'value' property.
  let grip = aDescriptor.value;
  if (typeof grip != "object") {
    return true;
  }

  // For convenience, undefined, null and long strings are considered types.
  let type = grip.type;
  if (type == "undefined" || type == "null" || type == "longString") {
    return true;
  }

  return false;
};

/**
 * Returns true if the descriptor represents an undefined value.
 *
 * @param object aDescriptor
 *        The variable's descriptor.
 */
VariablesView.isUndefined = function(aDescriptor) {
  // For accessor property descriptors, the getter and setter need to be
  // contained in 'get' and 'set' properties.
  let getter = aDescriptor.get;
  let setter = aDescriptor.set;
  if (typeof getter == "object" && getter.type == "undefined" &&
      typeof setter == "object" && setter.type == "undefined") {
    return true;
  }

  // As described in the remote debugger protocol, the value grip
  // must be contained in a 'value' property.
  let grip = aDescriptor.value;
  if (typeof grip == "object" && grip.type == "undefined") {
    return true;
  }

  return false;
};

/**
 * Returns true if the descriptor represents a falsy value.
 *
 * @param object aDescriptor
 *        The variable's descriptor.
 */
VariablesView.isFalsy = function(aDescriptor) {
  // As described in the remote debugger protocol, the value grip
  // must be contained in a 'value' property.
  let grip = aDescriptor.value;
  if (typeof grip != "object") {
    return !grip;
  }

  // For convenience, undefined and null are both considered types.
  let type = grip.type;
  if (type == "undefined" || type == "null") {
    return true;
  }

  return false;
};

/**
 * Returns true if the value is an instance of Variable or Property.
 *
 * @param any aValue
 *        The value to test.
 */
VariablesView.isVariable = function(aValue) {
  return aValue instanceof Variable;
};

/**
 * Returns a standard grip for a value.
 *
 * @param any aValue
 *        The raw value to get a grip for.
 * @return any
 *         The value's grip.
 */
VariablesView.getGrip = function(aValue) {
  if (aValue === undefined) {
    return { type: "undefined" };
  }
  if (aValue === null) {
    return { type: "null" };
  }
  if (typeof aValue == "object" || typeof aValue == "function") {
    return { type: "object", class: WebConsoleUtils.getObjectClassName(aValue) };
  }
  return aValue;
};

/**
 * Returns a custom formatted property string for a grip.
 *
 * @param any aGrip
 *        @see Variable.setGrip
 * @param boolean aConciseFlag
 *        Return a concisely formatted property string.
 * @return string
 *         The formatted property string.
 */
VariablesView.getString = function(aGrip, aConciseFlag) {
  if (aGrip && typeof aGrip == "object") {
    switch (aGrip.type) {
      case "undefined":
        return "undefined";
      case "null":
        return "null";
      case "longString":
        return "\"" + aGrip.initial + "\"";
      default:
        if (!aConciseFlag) {
          return "[" + aGrip.type + " " + aGrip.class + "]";
        } else {
          return aGrip.class;
        }
    }
  } else {
    switch (typeof aGrip) {
      case "string":
        return "\"" + aGrip + "\"";
      case "boolean":
        return aGrip ? "true" : "false";
    }
  }
  return aGrip + "";
};

/**
 * Returns a custom class style for a grip.
 *
 * @param any aGrip
 *        @see Variable.setGrip
 * @return string
 *         The custom class style.
 */
VariablesView.getClass = function(aGrip) {
  if (aGrip && typeof aGrip == "object") {
    switch (aGrip.type) {
      case "undefined":
        return "token-undefined";
      case "null":
        return "token-null";
      case "longString":
        return "token-string";
    }
  } else {
    switch (typeof aGrip) {
      case "string":
        return "token-string";
      case "boolean":
        return "token-boolean";
      case "number":
        return "token-number";
    }
  }
  return "token-other";
};

/**
 * A monotonically-increasing counter, that guarantees the uniqueness of scope,
 * variables and properties ids.
 *
 * @param string aName
 *        An optional string to prefix the id with.
 * @return number
 *         A unique id.
 */
let generateId = (function() {
  let count = 0;
  return function(aName = "") {
    return aName.toLowerCase().trim().replace(/\s+/g, "-") + (++count);
  };
})();

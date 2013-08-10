/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const Cu = Components.utils;
const Cc = Components.classes;
const Ci = Components.interfaces;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource:///modules/devtools/LayoutHelpers.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

this.EXPORTED_SYMBOLS = ["Highlighter"];

const INSPECTOR_INVISIBLE_ELEMENTS = {
  "head": true,
  "base": true,
  "basefont": true,
  "isindex": true,
  "link": true,
  "meta": true,
  "script": true,
  "style": true,
  "title": true,
};

const PSEUDO_CLASSES = [":hover", ":active", ":focus"];
  // add ":visited" and ":link" after bug 713106 is fixed

/**
 * A highlighter mechanism.
 *
 * The highlighter is built dynamically into the browser element.
 * The caller is in charge of destroying the highlighter (ie, the highlighter
 * won't be destroyed if a new tab is selected for example).
 *
 * API:
 *
 *   // Constructor and destructor.
 *   // @param aWindow - browser.xul window.
 *   Highlighter(aWindow);
 *   void destroy();
 *
 *   // Highlight a node.
 *   // @param aNode - node to highlight
 *   // @param aScroll - scroll to ensure the node is visible
 *   void highlight(aNode, aScroll);
 *
 *   // Get the selected node.
 *   DOMNode getNode();
 *
 *   // Lock and unlock the select node.
 *   void lock();
 *   void unlock();
 *
 *   // Show and hide the highlighter
 *   void show();
 *   void hide();
 *   boolean isHidden();
 *
 *   // Redraw the highlighter if the visible portion of the node has changed.
 *   void invalidateSize(aScroll);
 *
 *   // Is a node highlightable.
 *   boolean isNodeHighlightable(aNode);
 *
 *   // Show/hide the outline and the infobar
 *   void showInfobar();
 *   void hideInfobar();
 *   void showOutline();
 *   void hideOutline();
 *
 *   // Add/Remove listeners
 *   // @param aEvent - event name
 *   // @param aListener - function callback
 *   void addListener(aEvent, aListener);
 *   void removeListener(aEvent, aListener);
 *
 * Events:
 *
 *   "closed" - Highlighter is closing
 *   "nodeselected" - A new node has been selected
 *   "highlighting" - Highlighter is highlighting
 *   "locked" - The selected node has been locked
 *   "unlocked" - The selected ndoe has been unlocked
 *   "pseudoclasstoggled" - A pseudo-class lock has changed on the selected node
 *
 * Structure:
 *  <stack id="highlighter-container">
 *    <box id="highlighter-outline-container">
 *      <box id="highlighter-outline" locked="true/false"/>
 *    </box>
 *    <box id="highlighter-controls">
 *      <box id="highlighter-nodeinfobar-container" position="top/bottom" locked="true/false">
 *        <box class="highlighter-nodeinfobar-arrow" id="highlighter-nodeinfobar-arrow-top"/>
 *        <hbox id="highlighter-nodeinfobar">
 *          <toolbarbutton id="highlighter-nodeinfobar-inspectbutton" class="highlighter-nodeinfobar-button"/>
 *          <hbox id="highlighter-nodeinfobar-text">tagname#id.class1.class2</hbox>
 *          <toolbarbutton id="highlighter-nodeinfobar-menu" class="highlighter-nodeinfobar-button">…</toolbarbutton>
 *        </hbox>
 *        <box class="highlighter-nodeinfobar-arrow" id="highlighter-nodeinfobar-arrow-bottom"/>
 *      </box>
 *    </box>
 *  </stack>
 *
 */


/**
 * Constructor.
 *
 * @param object aWindow
 */
this.Highlighter = function Highlighter(aWindow)
{
  this.chromeWin = aWindow;
  this.tabbrowser = aWindow.gBrowser;
  this.chromeDoc = aWindow.document;
  this.browser = aWindow.gBrowser.selectedBrowser;
  this.events = {};

  this._init();
}

Highlighter.prototype = {
  _init: function Highlighter__init()
  {
    let stack = this.browser.parentNode;
    this.win = this.browser.contentWindow;
    this._highlighting = false;

    this.highlighterContainer = this.chromeDoc.createElement("stack");
    this.highlighterContainer.id = "highlighter-container";

    this.outline = this.chromeDoc.createElement("box");
    this.outline.id = "highlighter-outline";

    let outlineContainer = this.chromeDoc.createElement("box");
    outlineContainer.appendChild(this.outline);
    outlineContainer.id = "highlighter-outline-container";

    // The controlsBox will host the different interactive
    // elements of the highlighter (buttons, toolbars, ...).
    let controlsBox = this.chromeDoc.createElement("box");
    controlsBox.id = "highlighter-controls";
    this.highlighterContainer.appendChild(outlineContainer);
    this.highlighterContainer.appendChild(controlsBox);

    // Insert the highlighter right after the browser
    stack.insertBefore(this.highlighterContainer, stack.childNodes[1]);

    this.buildInfobar(controlsBox);

    this.transitionDisabler = null;
    this.pageEventsMuter = null;

    this.unlock();

    this.hidden = true;
    this.show();
  },

  /**
   * Destroy the nodes. Remove listeners.
   */
  destroy: function Highlighter_destroy()
  {
    this.detachMouseListeners();
    this.detachPageListeners();

    this.chromeWin.clearTimeout(this.transitionDisabler);
    this.chromeWin.clearTimeout(this.pageEventsMuter);
    this.boundCloseEventHandler = null;
    this._contentRect = null;
    this._highlightRect = null;
    this._highlighting = false;
    this.outline = null;
    this.node = null;
    this.nodeInfo = null;
    this.highlighterContainer.parentNode.removeChild(this.highlighterContainer);
    this.highlighterContainer = null;
    this.win = null
    this.browser = null;
    this.chromeDoc = null;
    this.chromeWin = null;
    this.tabbrowser = null;

    this.emitEvent("closed");
    this.removeAllListeners();
  },

  /**
   * Show the outline, and select a node.
   * If no node is specified, the previous selected node is highlighted if any.
   * If no node was selected, the root element is selected.
   *
   * @param aNode [optional] - The node to be selected.
   * @param aScroll [optional] boolean
   *        Should we scroll to ensure that the selected node is visible.
   */
  highlight: function Highlighter_highlight(aNode, aScroll)
  {
    if (this.hidden)
      this.show();

    let oldNode = this.node;

    if (!aNode) {
      if (!this.node)
        this.node = this.win.document.documentElement;
    } else {
      this.node = aNode;
    }

    if (oldNode !== this.node) {
      this.updateInfobar();
    }

    this.invalidateSize(!!aScroll);

    if (oldNode !== this.node) {
      this.emitEvent("nodeselected");
    }
  },

  /**
   * Notify that a pseudo-class lock was toggled on the highlighted element
   *
   * @param aPseudo - The pseudo-class to toggle, e.g. ":hover".
   */
  pseudoClassLockToggled: function Highlighter_pseudoClassLockToggled(aPseudo)
  {
    this.emitEvent("pseudoclasstoggled", [aPseudo]);
    this.updateInfobar();
    this.moveInfobar();
  },

  /**
   * Update the highlighter size and position.
   */
  invalidateSize: function Highlighter_invalidateSize(aScroll)
  {
    let rect = null;

    if (this.node && this.isNodeHighlightable(this.node)) {

      if (aScroll &&
          this.node.scrollIntoView) { // XUL elements don't have such method
        this.node.scrollIntoView();
      }
      let clientRect = this.node.getBoundingClientRect();
      rect = LayoutHelpers.getDirtyRect(this.node);
    }

    this.highlightRectangle(rect);

    this.moveInfobar();

    if (this._highlighting) {
      this.showOutline();
      this.emitEvent("highlighting");
    }
  },

  /**
   * Returns the selected node.
   *
   * @returns node
   */
  getNode: function() {
    return this.node;
  },

  /**
   * Show the highlighter if it has been hidden.
   */
  show: function() {
    if (!this.hidden) return;
    this.showOutline();
    this.showInfobar();
    this.computeZoomFactor();
    this.attachPageListeners();
    this.invalidateSize();
    this.hidden = false;
  },

  /**
   * Hide the highlighter, the outline and the infobar.
   */
  hide: function() {
    if (this.hidden) return;
    this.hideOutline();
    this.hideInfobar();
    this.detachPageListeners();
    this.hidden = true;
  },

  /**
   * Is the highlighter visible?
   *
   * @return boolean
   */
  isHidden: function() {
    return this.hidden;
  },

  /**
   * Lock a node. Stops the inspection.
   */
  lock: function() {
    if (this.locked === true) return;
    this.outline.setAttribute("locked", "true");
    this.nodeInfo.container.setAttribute("locked", "true");
    this.detachMouseListeners();
    this.locked = true;
    this.emitEvent("locked");
  },

  /**
   * Start inspecting.
   * Unlock the current node (if any), and select any node being hovered.
   */
  unlock: function() {
    if (this.locked === false) return;
    this.outline.removeAttribute("locked");
    this.nodeInfo.container.removeAttribute("locked");
    this.attachMouseListeners();
    this.locked = false;
    this.showOutline();
    this.emitEvent("unlocked");
  },

  /**
   * Is the specified node highlightable?
   *
   * @param nsIDOMNode aNode
   *        the DOM element in question
   * @returns boolean
   *          True if the node is highlightable or false otherwise.
   */
  isNodeHighlightable: function Highlighter_isNodeHighlightable(aNode)
  {
    if (!LayoutHelpers.isNodeConnected(aNode)) {
      return false;
    }
    if (aNode.nodeType != aNode.ELEMENT_NODE) {
      return false;
    }
    let nodeName = aNode.nodeName.toLowerCase();
    return !INSPECTOR_INVISIBLE_ELEMENTS[nodeName];
  },

  /**
   * Hide the infobar
   */
   hideInfobar: function Highlighter_hideInfobar() {
     this.nodeInfo.container.setAttribute("force-transitions", "true");
     this.nodeInfo.container.setAttribute("hidden", "true");
   },

  /**
   * Show the infobar
   */
   showInfobar: function Highlighter_showInfobar() {
     this.nodeInfo.container.removeAttribute("hidden");
     this.moveInfobar();
     this.nodeInfo.container.removeAttribute("force-transitions");
   },

  /**
   * Hide the outline
   */
   hideOutline: function Highlighter_hideOutline() {
     this.outline.setAttribute("hidden", "true");
   },

  /**
   * Show the outline
   */
   showOutline: function Highlighter_showOutline() {
     if (this._highlighting)
       this.outline.removeAttribute("hidden");
   },

  /**
   * Build the node Infobar.
   *
   * <box id="highlighter-nodeinfobar-container">
   *   <box id="Highlighter-nodeinfobar-arrow-top"/>
   *   <hbox id="highlighter-nodeinfobar">
   *     <toolbarbutton class="highlighter-nodeinfobar-button" id="highlighter-nodeinfobar-inspectbutton"/>
   *     <hbox id="highlighter-nodeinfobar-text">
   *       <xhtml:span id="highlighter-nodeinfobar-tagname"/>
   *       <xhtml:span id="highlighter-nodeinfobar-id"/>
   *       <xhtml:span id="highlighter-nodeinfobar-classes"/>
   *       <xhtml:span id="highlighter-nodeinfobar-pseudo-classes"/>
   *     </hbox>
   *     <toolbarbutton class="highlighter-nodeinfobar-button" id="highlighter-nodeinfobar-menu"/>
   *   </hbox>
   *   <box id="Highlighter-nodeinfobar-arrow-bottom"/>
   * </box>
   *
   * @param nsIDOMElement aParent
   *        The container of the infobar.
   */
  buildInfobar: function Highlighter_buildInfobar(aParent)
  {
    let container = this.chromeDoc.createElement("box");
    container.id = "highlighter-nodeinfobar-container";
    container.setAttribute("position", "top");
    container.setAttribute("disabled", "true");

    let nodeInfobar = this.chromeDoc.createElement("hbox");
    nodeInfobar.id = "highlighter-nodeinfobar";

    nodeInfobar.addEventListener("mousedown", function(aEvent) {
      this.emitEvent("nodeselected");
    }.bind(this), true);

    let arrowBoxTop = this.chromeDoc.createElement("box");
    arrowBoxTop.className = "highlighter-nodeinfobar-arrow";
    arrowBoxTop.id = "highlighter-nodeinfobar-arrow-top";

    let arrowBoxBottom = this.chromeDoc.createElement("box");
    arrowBoxBottom.className = "highlighter-nodeinfobar-arrow";
    arrowBoxBottom.id = "highlighter-nodeinfobar-arrow-bottom";

    let tagNameLabel = this.chromeDoc.createElementNS("http://www.w3.org/1999/xhtml", "span");
    tagNameLabel.id = "highlighter-nodeinfobar-tagname";

    let idLabel = this.chromeDoc.createElementNS("http://www.w3.org/1999/xhtml", "span");
    idLabel.id = "highlighter-nodeinfobar-id";

    let classesBox = this.chromeDoc.createElementNS("http://www.w3.org/1999/xhtml", "span");
    classesBox.id = "highlighter-nodeinfobar-classes";

    let pseudoClassesBox = this.chromeDoc.createElementNS("http://www.w3.org/1999/xhtml", "span");
    pseudoClassesBox.id = "highlighter-nodeinfobar-pseudo-classes";

    // Add some content to force a better boundingClientRect down below.
    pseudoClassesBox.textContent = "&nbsp;";

    // Create buttons

    let inspect = this.chromeDoc.createElement("toolbarbutton");
    inspect.id = "highlighter-nodeinfobar-inspectbutton";
    inspect.className = "highlighter-nodeinfobar-button"
    let toolbarInspectButton =
      this.chromeDoc.getElementById("inspector-inspect-toolbutton");
    inspect.setAttribute("tooltiptext",
                         toolbarInspectButton.getAttribute("tooltiptext"));
    inspect.setAttribute("command", "Inspector:Inspect");

    let nodemenu = this.chromeDoc.createElement("toolbarbutton");
    nodemenu.setAttribute("type", "menu");
    nodemenu.id = "highlighter-nodeinfobar-menu";
    nodemenu.className = "highlighter-nodeinfobar-button"
    nodemenu.setAttribute("tooltiptext",
                          this.strings.GetStringFromName("nodeMenu.tooltiptext"));

    let menu = this.chromeDoc.getElementById("inspector-node-popup");
    menu = menu.cloneNode(true);
    menu.id = "highlighter-node-menu";

    let separator = this.chromeDoc.createElement("menuseparator");
    menu.appendChild(separator);

    menu.addEventListener("popupshowing", function() {
      let items = menu.getElementsByClassName("highlighter-pseudo-class-menuitem");
      let i = items.length;
      while (i--) {
        menu.removeChild(items[i]);
      }

      let fragment = this.buildPseudoClassMenu();
      menu.appendChild(fragment);
    }.bind(this), true);

    nodemenu.appendChild(menu);

    // <hbox id="highlighter-nodeinfobar-text"/>
    let texthbox = this.chromeDoc.createElement("hbox");
    texthbox.id = "highlighter-nodeinfobar-text";
    texthbox.setAttribute("align", "center");
    texthbox.setAttribute("flex", "1");

    texthbox.appendChild(tagNameLabel);
    texthbox.appendChild(idLabel);
    texthbox.appendChild(classesBox);
    texthbox.appendChild(pseudoClassesBox);

    nodeInfobar.appendChild(inspect);
    nodeInfobar.appendChild(texthbox);
    nodeInfobar.appendChild(nodemenu);

    container.appendChild(arrowBoxTop);
    container.appendChild(nodeInfobar);
    container.appendChild(arrowBoxBottom);

    aParent.appendChild(container);

    let barHeight = container.getBoundingClientRect().height;

    this.nodeInfo = {
      tagNameLabel: tagNameLabel,
      idLabel: idLabel,
      classesBox: classesBox,
      pseudoClassesBox: pseudoClassesBox,
      container: container,
      barHeight: barHeight,
    };
  },

  /**
   * Create the menuitems for toggling the selection's pseudo-class state
   *
   * @returns DocumentFragment. The menuitems for toggling pseudo-classes.
   */
  buildPseudoClassMenu: function IUI_buildPseudoClassesMenu()
  {
    let fragment = this.chromeDoc.createDocumentFragment();
    for (let i = 0; i < PSEUDO_CLASSES.length; i++) {
      let pseudo = PSEUDO_CLASSES[i];
      let item = this.chromeDoc.createElement("menuitem");
      item.id = "highlighter-pseudo-class-menuitem-" + pseudo;
      item.setAttribute("type", "checkbox");
      item.setAttribute("label", pseudo);
      item.className = "highlighter-pseudo-class-menuitem";
      item.setAttribute("checked", DOMUtils.hasPseudoClassLock(this.node,
                        pseudo));
      item.addEventListener("command",
                            this.pseudoClassLockToggled.bind(this, pseudo), false);
      fragment.appendChild(item);
    }
    return fragment;
  },

  /**
   * Highlight a rectangular region.
   *
   * @param object aRect
   *        The rectangle region to highlight.
   * @returns boolean
   *          True if the rectangle was highlighted, false otherwise.
   */
  highlightRectangle: function Highlighter_highlightRectangle(aRect)
  {
    if (!aRect) {
      this.unhighlight();
      return;
    }

    let oldRect = this._contentRect;

    if (oldRect && aRect.top == oldRect.top && aRect.left == oldRect.left &&
        aRect.width == oldRect.width && aRect.height == oldRect.height) {
      return; // same rectangle
    }

    let aRectScaled = LayoutHelpers.getZoomedRect(this.win, aRect);

    if (aRectScaled.left >= 0 && aRectScaled.top >= 0 &&
        aRectScaled.width > 0 && aRectScaled.height > 0) {

      this.showOutline();

      // The bottom div and the right div are flexibles (flex=1).
      // We don't need to resize them.
      let top = "top:" + aRectScaled.top + "px;";
      let left = "left:" + aRectScaled.left + "px;";
      let width = "width:" + aRectScaled.width + "px;";
      let height = "height:" + aRectScaled.height + "px;";
      this.outline.setAttribute("style", top + left + width + height);

      this._highlighting = true;
    } else {
      this.unhighlight();
    }

    this._contentRect = aRect; // save orig (non-scaled) rect
    this._highlightRect = aRectScaled; // and save the scaled rect.

    return;
  },

  /**
   * Clear the highlighter surface.
   */
  unhighlight: function Highlighter_unhighlight()
  {
    this._highlighting = false;
    this.hideOutline();
  },

  /**
   * Update node information (tagName#id.class)
   */
  updateInfobar: function Highlighter_updateInfobar()
  {
    // Tag name
    this.nodeInfo.tagNameLabel.textContent = this.node.tagName;

    // ID
    this.nodeInfo.idLabel.textContent = this.node.id ? "#" + this.node.id : "";

    // Classes
    let classes = this.nodeInfo.classesBox;

    classes.textContent = this.node.classList.length ?
                            "." + Array.join(this.node.classList, ".") : "";

    // Pseudo-classes
    let pseudos = PSEUDO_CLASSES.filter(function(pseudo) {
      return DOMUtils.hasPseudoClassLock(this.node, pseudo);
    }, this);

    let pseudoBox = this.nodeInfo.pseudoClassesBox;
    pseudoBox.textContent = pseudos.join("");
  },

  /**
   * Move the Infobar to the right place in the highlighter.
   */
  moveInfobar: function Highlighter_moveInfobar()
  {
    if (this._highlightRect) {
      let winHeight = this.win.innerHeight * this.zoom;
      let winWidth = this.win.innerWidth * this.zoom;

      let rect = {top: this._highlightRect.top,
                  left: this._highlightRect.left,
                  width: this._highlightRect.width,
                  height: this._highlightRect.height};

      rect.top = Math.max(rect.top, 0);
      rect.left = Math.max(rect.left, 0);
      rect.width = Math.max(rect.width, 0);
      rect.height = Math.max(rect.height, 0);

      rect.top = Math.min(rect.top, winHeight);
      rect.left = Math.min(rect.left, winWidth);

      this.nodeInfo.container.removeAttribute("disabled");
      // Can the bar be above the node?
      if (rect.top < this.nodeInfo.barHeight) {
        // No. Can we move the toolbar under the node?
        if (rect.top + rect.height +
            this.nodeInfo.barHeight > winHeight) {
          // No. Let's move it inside.
          this.nodeInfo.container.style.top = rect.top + "px";
          this.nodeInfo.container.setAttribute("position", "overlap");
        } else {
          // Yes. Let's move it under the node.
          this.nodeInfo.container.style.top = rect.top + rect.height + "px";
          this.nodeInfo.container.setAttribute("position", "bottom");
        }
      } else {
        // Yes. Let's move it on top of the node.
        this.nodeInfo.container.style.top =
          rect.top - this.nodeInfo.barHeight + "px";
        this.nodeInfo.container.setAttribute("position", "top");
      }

      let barWidth = this.nodeInfo.container.getBoundingClientRect().width;
      let left = rect.left + rect.width / 2 - barWidth / 2;

      // Make sure the whole infobar is visible
      if (left < 0) {
        left = 0;
        this.nodeInfo.container.setAttribute("hide-arrow", "true");
      } else {
        if (left + barWidth > winWidth) {
          left = winWidth - barWidth;
          this.nodeInfo.container.setAttribute("hide-arrow", "true");
        } else {
          this.nodeInfo.container.removeAttribute("hide-arrow");
        }
      }
      this.nodeInfo.container.style.left = left + "px";
    } else {
      this.nodeInfo.container.style.left = "0";
      this.nodeInfo.container.style.top = "0";
      this.nodeInfo.container.setAttribute("position", "top");
      this.nodeInfo.container.setAttribute("hide-arrow", "true");
    }
  },

  /**
   * Store page zoom factor.
   */
  computeZoomFactor: function Highlighter_computeZoomFactor() {
    this.zoom =
      this.win.QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIDOMWindowUtils)
      .fullZoom;
  },

  /////////////////////////////////////////////////////////////////////////
  //// Event Emitter Mechanism

  addListener: function Highlighter_addListener(aEvent, aListener)
  {
    if (!(aEvent in this.events))
      this.events[aEvent] = [];
    this.events[aEvent].push(aListener);
  },

  removeListener: function Highlighter_removeListener(aEvent, aListener)
  {
    if (!(aEvent in this.events))
      return;
    let idx = this.events[aEvent].indexOf(aListener);
    if (idx > -1)
      this.events[aEvent].splice(idx, 1);
  },

  emitEvent: function Highlighter_emitEvent(aEvent, aArgv)
  {
    if (!(aEvent in this.events))
      return;

    let listeners = this.events[aEvent];
    let highlighter = this;
    listeners.forEach(function(aListener) {
      try {
        aListener.apply(highlighter, aArgv);
      } catch(e) {}
    });
  },

  removeAllListeners: function Highlighter_removeAllIsteners()
  {
    for (let event in this.events) {
      delete this.events[event];
    }
  },

  /////////////////////////////////////////////////////////////////////////
  //// Event Handling

  attachMouseListeners: function Highlighter_attachMouseListeners()
  {
    this.browser.addEventListener("mousemove", this, true);
    this.browser.addEventListener("click", this, true);
    this.browser.addEventListener("dblclick", this, true);
    this.browser.addEventListener("mousedown", this, true);
    this.browser.addEventListener("mouseup", this, true);
  },

  detachMouseListeners: function Highlighter_detachMouseListeners()
  {
    this.browser.removeEventListener("mousemove", this, true);
    this.browser.removeEventListener("click", this, true);
    this.browser.removeEventListener("dblclick", this, true);
    this.browser.removeEventListener("mousedown", this, true);
    this.browser.removeEventListener("mouseup", this, true);
  },

  attachPageListeners: function Highlighter_attachPageListeners()
  {
    this.browser.addEventListener("resize", this, true);
    this.browser.addEventListener("scroll", this, true);
    this.browser.addEventListener("MozAfterPaint", this, true);
  },

  detachPageListeners: function Highlighter_detachPageListeners()
  {
    this.browser.removeEventListener("resize", this, true);
    this.browser.removeEventListener("scroll", this, true);
    this.browser.removeEventListener("MozAfterPaint", this, true);
  },

  /**
   * Generic event handler.
   *
   * @param nsIDOMEvent aEvent
   *        The DOM event object.
   */
  handleEvent: function Highlighter_handleEvent(aEvent)
  {
    switch (aEvent.type) {
      case "click":
        this.handleClick(aEvent);
        break;
      case "mousemove":
        this.brieflyIgnorePageEvents();
        this.handleMouseMove(aEvent);
        break;
      case "resize":
        this.computeZoomFactor();
        break;
      case "MozAfterPaint":
      case "scroll":
        this.brieflyDisableTransitions();
        this.invalidateSize();
        break;
      case "dblclick":
      case "mousedown":
      case "mouseup":
        aEvent.stopPropagation();
        aEvent.preventDefault();
        break;
    }
  },

  /**
   * Disable the CSS transitions for a short time to avoid laggy animations
   * during scrolling or resizing.
   */
  brieflyDisableTransitions: function Highlighter_brieflyDisableTransitions()
  {
    if (this.transitionDisabler) {
      this.chromeWin.clearTimeout(this.transitionDisabler);
    } else {
      this.outline.setAttribute("disable-transitions", "true");
      this.nodeInfo.container.setAttribute("disable-transitions", "true");
    }
    this.transitionDisabler =
      this.chromeWin.setTimeout(function() {
        this.outline.removeAttribute("disable-transitions");
        this.nodeInfo.container.removeAttribute("disable-transitions");
        this.transitionDisabler = null;
      }.bind(this), 500);
  },

  /**
   * Don't listen to page events while inspecting with the mouse.
   */
  brieflyIgnorePageEvents: function Highlighter_brieflyIgnorePageEvents()
  {
    // The goal is to keep smooth animations while inspecting.
    // CSS Transitions might be interrupted because of a MozAfterPaint
    // event that would triger an invalidateSize() call.
    // So we don't listen to events that would trigger an invalidateSize()
    // call.
    //
    // Side effect, zoom levels are not updated during this short period.
    // It's very unlikely this would happen, but just in case, we call
    // computeZoomFactor() when reattaching the events.
    if (this.pageEventsMuter) {
      this.chromeWin.clearTimeout(this.pageEventsMuter);
    } else {
      this.detachPageListeners();
    }
    this.pageEventsMuter =
      this.chromeWin.setTimeout(function() {
        this.attachPageListeners();
        // Just in case the zoom level changed while ignoring the paint events
        this.computeZoomFactor();
        this.pageEventsMuter = null;
      }.bind(this), 500);
  },

  /**
   * Handle clicks.
   *
   * @param nsIDOMEvent aEvent
   *        The DOM event.
   */
  handleClick: function Highlighter_handleClick(aEvent)
  {
    // Stop inspection when the user clicks on a node.
    if (aEvent.button == 0) {
      let win = aEvent.target.ownerDocument.defaultView;
      this.lock();
      win.focus();
      aEvent.preventDefault();
      aEvent.stopPropagation();
    }
  },

  /**
   * Handle mousemoves in panel.
   *
   * @param nsiDOMEvent aEvent
   *        The MouseEvent triggering the method.
   */
  handleMouseMove: function Highlighter_handleMouseMove(aEvent)
  {
    let doc = aEvent.target.ownerDocument;

    // This should never happen, but just in case, we don't let the
    // highlighter highlight browser nodes.
    if (doc && doc != this.chromeDoc) {
      let element = LayoutHelpers.getElementFromPoint(aEvent.target.ownerDocument,
        aEvent.clientX, aEvent.clientY);
      if (element && element != this.node) {
        this.highlight(element);
      }
    }
  },
};

///////////////////////////////////////////////////////////////////////////

XPCOMUtils.defineLazyGetter(this, "DOMUtils", function () {
  return Cc["@mozilla.org/inspector/dom-utils;1"].getService(Ci.inIDOMUtils)
});

XPCOMUtils.defineLazyGetter(Highlighter.prototype, "strings", function () {
    return Services.strings.createBundle(
            "chrome://browser/locale/devtools/inspector.properties");
});

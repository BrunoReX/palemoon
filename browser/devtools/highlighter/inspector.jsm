/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
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
 * The Original Code is the Mozilla Inspector Module.
 *
 * The Initial Developer of the Original Code is
 * The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Rob Campbell <rcampbell@mozilla.com> (original author)
 *   Mihai Șucan <mihai.sucan@gmail.com>
 *   Julian Viereck <jviereck@mozilla.com>
 *   Paul Rouget <paul@mozilla.com>
 *   Kyle Simpson <ksimpson@mozilla.com>
 *   Johan Charlez <johan.charlez@gmail.com>
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

const Cu = Components.utils;
const Ci = Components.interfaces;
const Cr = Components.results;

var EXPORTED_SYMBOLS = ["InspectorUI"];

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource:///modules/TreePanel.jsm");
Cu.import("resource:///modules/devtools/CssRuleView.jsm");

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

// Inspector notifications dispatched through the nsIObserverService.
const INSPECTOR_NOTIFICATIONS = {
  // Fires once the Inspector highlights an element in the page.
  HIGHLIGHTING: "inspector-highlighting",

  // Fires once the Inspector stops highlighting any element.
  UNHIGHLIGHTING: "inspector-unhighlighting",

  // Fires once the Inspector completes the initialization and opens up on
  // screen.
  OPENED: "inspector-opened",

  // Fires once the Inspector is closed.
  CLOSED: "inspector-closed",

  // Fires once the Inspector is destroyed. Not fired on tab switch.
  DESTROYED: "inspector-destroyed",

  // Fires when the Inspector is reopened after tab-switch.
  STATE_RESTORED: "inspector-state-restored",

  // Fires when the Tree Panel is opened and initialized.
  TREEPANELREADY: "inspector-treepanel-ready",

  // Fires when the CSS Rule View is opened and initialized.
  RULEVIEWREADY: "inspector-ruleview-ready",

  // Event notifications for the attribute-value editor
  EDITOR_OPENED: "inspector-editor-opened",
  EDITOR_CLOSED: "inspector-editor-closed",
  EDITOR_SAVED: "inspector-editor-saved",
};

///////////////////////////////////////////////////////////////////////////
//// Highlighter

/**
 * A highlighter mechanism.
 *
 * The highlighter is built dynamically once the Inspector is invoked:
 * <stack id="highlighter-container">
 *   <vbox id="highlighter-veil-container">...</vbox>
 *   <box id="highlighter-controls>...</vbox>
 * </stack>
 *
 * @param object aInspector
 *        The InspectorUI instance.
 */
function Highlighter(aInspector)
{
  this.IUI = aInspector;
  this._init();
}

Highlighter.prototype = {
  _init: function Highlighter__init()
  {
    this.browser = this.IUI.browser;
    this.chromeDoc = this.IUI.chromeDoc;

    let stack = this.browser.parentNode;
    this.win = this.browser.contentWindow;
    this._highlighting = false;

    this.highlighterContainer = this.chromeDoc.createElement("stack");
    this.highlighterContainer.id = "highlighter-container";

    this.veilContainer = this.chromeDoc.createElement("vbox");
    this.veilContainer.id = "highlighter-veil-container";

    // The controlsBox will host the different interactive
    // elements of the highlighter (buttons, toolbars, ...).
    let controlsBox = this.chromeDoc.createElement("box");
    controlsBox.id = "highlighter-controls";
    this.highlighterContainer.appendChild(this.veilContainer);
    this.highlighterContainer.appendChild(controlsBox);

    stack.appendChild(this.highlighterContainer);

    // The veil will make the whole page darker except
    // for the region of the selected box.
    this.buildVeil(this.veilContainer);

    this.buildInfobar(controlsBox);

    if (!this.IUI.store.getValue(this.winID, "inspecting")) {
      this.veilContainer.setAttribute("locked", true);
      this.nodeInfo.container.setAttribute("locked", true);
    }

    this.browser.addEventListener("resize", this, true);
    this.browser.addEventListener("scroll", this, true);

    this.transitionDisabler = null;

    this.handleResize();
  },

  /**
   * Build the veil:
   *
   * <vbox id="highlighter-veil-container">
   *   <box id="highlighter-veil-topbox" class="highlighter-veil"/>
   *   <hbox id="highlighter-veil-middlebox">
   *     <box id="highlighter-veil-leftbox" class="highlighter-veil"/>
   *     <box id="highlighter-veil-transparentbox"/>
   *     <box id="highlighter-veil-rightbox" class="highlighter-veil"/>
   *   </hbox>
   *   <box id="highlighter-veil-bottombox" class="highlighter-veil"/>
   * </vbox>
   *
   * @param nsIDOMElement aParent
   *        The container of the veil boxes.
   */
  buildVeil: function Highlighter_buildVeil(aParent)
  {
    // We will need to resize these boxes to surround a node.
    // See highlightRectangle().

    this.veilTopBox = this.chromeDoc.createElement("box");
    this.veilTopBox.id = "highlighter-veil-topbox";
    this.veilTopBox.className = "highlighter-veil";

    this.veilMiddleBox = this.chromeDoc.createElement("hbox");
    this.veilMiddleBox.id = "highlighter-veil-middlebox";

    this.veilLeftBox = this.chromeDoc.createElement("box");
    this.veilLeftBox.id = "highlighter-veil-leftbox";
    this.veilLeftBox.className = "highlighter-veil";

    this.veilTransparentBox = this.chromeDoc.createElement("box");
    this.veilTransparentBox.id = "highlighter-veil-transparentbox";

    // We don't need any references to veilRightBox and veilBottomBox.
    // These boxes are automatically resized (flex=1)

    let veilRightBox = this.chromeDoc.createElement("box");
    veilRightBox.id = "highlighter-veil-rightbox";
    veilRightBox.className = "highlighter-veil";

    let veilBottomBox = this.chromeDoc.createElement("box");
    veilBottomBox.id = "highlighter-veil-bottombox";
    veilBottomBox.className = "highlighter-veil";

    this.veilMiddleBox.appendChild(this.veilLeftBox);
    this.veilMiddleBox.appendChild(this.veilTransparentBox);
    this.veilMiddleBox.appendChild(veilRightBox);

    aParent.appendChild(this.veilTopBox);
    aParent.appendChild(this.veilMiddleBox);
    aParent.appendChild(veilBottomBox);
  },

  /**
   * Build the node Infobar.
   *
   * <box id="highlighter-nodeinfobar-container">
   *   <box id="Highlighter-nodeinfobar-arrow-top"/>
   *   <vbox id="highlighter-nodeinfobar">
   *     <label id="highlighter-nodeinfobar-tagname"/>
   *     <label id="highlighter-nodeinfobar-id"/>
   *     <vbox id="highlighter-nodeinfobar-classes"/>
   *   </vbox>
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

    let arrowBoxTop = this.chromeDoc.createElement("box");
    arrowBoxTop.className = "highlighter-nodeinfobar-arrow";
    arrowBoxTop.id = "highlighter-nodeinfobar-arrow-top";

    let arrowBoxBottom = this.chromeDoc.createElement("box");
    arrowBoxBottom.className = "highlighter-nodeinfobar-arrow";
    arrowBoxBottom.id = "highlighter-nodeinfobar-arrow-bottom";

    let tagNameLabel = this.chromeDoc.createElement("label");
    tagNameLabel.id = "highlighter-nodeinfobar-tagname";
    tagNameLabel.className = "plain";

    let idLabel = this.chromeDoc.createElement("label");
    idLabel.id = "highlighter-nodeinfobar-id";
    idLabel.className = "plain";

    let classesBox = this.chromeDoc.createElement("hbox");
    classesBox.id = "highlighter-nodeinfobar-classes";

    nodeInfobar.appendChild(tagNameLabel);
    nodeInfobar.appendChild(idLabel);
    nodeInfobar.appendChild(classesBox);
    container.appendChild(arrowBoxTop);
    container.appendChild(nodeInfobar);
    container.appendChild(arrowBoxBottom);

    aParent.appendChild(container);

    let barHeight = container.getBoundingClientRect().height;

    this.nodeInfo = {
      tagNameLabel: tagNameLabel,
      idLabel: idLabel,
      classesBox: classesBox,
      container: container,
      barHeight: barHeight,
    };
  },

  /**
   * Destroy the nodes.
   */
  destroy: function Highlighter_destroy()
  {
    this.IUI.win.clearTimeout(this.transitionDisabler);
    this.browser.removeEventListener("scroll", this, true);
    this.browser.removeEventListener("resize", this, true);
    this.boundCloseEventHandler = null;
    this._contentRect = null;
    this._highlightRect = null;
    this._highlighting = false;
    this.veilTopBox = null;
    this.veilLeftBox = null;
    this.veilMiddleBox = null;
    this.veilTransparentBox = null;
    this.veilContainer = null;
    this.node = null;
    this.nodeInfo = null;
    this.highlighterContainer.parentNode.removeChild(this.highlighterContainer);
    this.highlighterContainer = null;
    this.win = null
    this.browser = null;
    this.chromeDoc = null;
    this.IUI = null;
  },

  /**
   * Is the highlighter highlighting? Public method for querying the state
   * of the highlighter.
   */
  get isHighlighting() {
    return this._highlighting;
  },

  /**
   * Highlight this.node, unhilighting first if necessary.
   *
   * @param boolean aScroll
   *        Boolean determining whether to scroll or not.
   */
  highlight: function Highlighter_highlight(aScroll)
  {
    let rect = null;

    if (this.node && this.isNodeHighlightable(this.node)) {

      if (aScroll) {
        this.node.scrollIntoView();
      }

      let clientRect = this.node.getBoundingClientRect();

      // Go up in the tree of frames to determine the correct rectangle.
      // clientRect is read-only, we need to be able to change properties.
      rect = {top: clientRect.top,
              left: clientRect.left,
              width: clientRect.width,
              height: clientRect.height};

      let frameWin = this.node.ownerDocument.defaultView;

      // We iterate through all the parent windows.
      while (true) {

        // Does the selection overflow on the right of its window?
        let diffx = frameWin.innerWidth - (rect.left + rect.width);
        if (diffx < 0) {
          rect.width += diffx;
        }

        // Does the selection overflow on the bottom of its window?
        let diffy = frameWin.innerHeight - (rect.top + rect.height);
        if (diffy < 0) {
          rect.height += diffy;
        }

        // Does the selection overflow on the left of its window?
        if (rect.left < 0) {
          rect.width += rect.left;
          rect.left = 0;
        }

        // Does the selection overflow on the top of its window?
        if (rect.top < 0) {
          rect.height += rect.top;
          rect.top = 0;
        }

        // Selection has been clipped to fit in its own window.

        // Are we in the top-level window?
        if (frameWin.parent === frameWin || !frameWin.frameElement) {
          break;
        }

        // We are in an iframe.
        // We take into account the parent iframe position and its
        // offset (borders and padding).
        let frameRect = frameWin.frameElement.getBoundingClientRect();

        let [offsetTop, offsetLeft] =
          this.IUI.getIframeContentOffset(frameWin.frameElement);

        rect.top += frameRect.top + offsetTop;
        rect.left += frameRect.left + offsetLeft;

        frameWin = frameWin.parent;
      }
    }

    this.highlightRectangle(rect);

    this.moveInfobar();

    if (this._highlighting) {
      Services.obs.notifyObservers(null,
        INSPECTOR_NOTIFICATIONS.HIGHLIGHTING, null);
    }
  },

  /**
   * Highlight the given node.
   *
   * @param nsIDOMNode aNode
   *        a DOM element to be highlighted
   * @param object aParams
   *        extra parameters object
   */
  highlightNode: function Highlighter_highlightNode(aNode, aParams)
  {
    this.node = aNode;
    this.updateInfobar();
    this.highlight(aParams && aParams.scroll);
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
      return this._highlighting; // same rectangle
    }

    // get page zoom factor, if any
    let zoom =
      this.win.QueryInterface(Components.interfaces.nsIInterfaceRequestor)
      .getInterface(Components.interfaces.nsIDOMWindowUtils)
      .screenPixelsPerCSSPixel;

    // adjust rect for zoom scaling
    let aRectScaled = {};
    for (let prop in aRect) {
      aRectScaled[prop] = aRect[prop] * zoom;
    }

    if (aRectScaled.left >= 0 && aRectScaled.top >= 0 &&
        aRectScaled.width > 0 && aRectScaled.height > 0) {

      this.veilTransparentBox.style.visibility = "visible";

      // The bottom div and the right div are flexibles (flex=1).
      // We don't need to resize them.
      this.veilTopBox.style.height = aRectScaled.top + "px";
      this.veilLeftBox.style.width = aRectScaled.left + "px";
      this.veilMiddleBox.style.height = aRectScaled.height + "px";
      this.veilTransparentBox.style.width = aRectScaled.width + "px";

      this._highlighting = true;
    } else {
      this.unhighlight();
    }

    this._contentRect = aRect; // save orig (non-scaled) rect
    this._highlightRect = aRectScaled; // and save the scaled rect.

    return this._highlighting;
  },

  /**
   * Clear the highlighter surface.
   */
  unhighlight: function Highlighter_unhighlight()
  {
    this._highlighting = false;
    this.veilMiddleBox.style.height = 0;
    this.veilTransparentBox.style.width = 0;
    this.veilTransparentBox.style.visibility = "hidden";
    Services.obs.notifyObservers(null,
      INSPECTOR_NOTIFICATIONS.UNHIGHLIGHTING, null);
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
    while (classes.hasChildNodes()) {
      classes.removeChild(classes.firstChild);
    }

    if (this.node.className) {
      let fragment = this.chromeDoc.createDocumentFragment();
      for (let i = 0; i < this.node.classList.length; i++) {
        let classLabel = this.chromeDoc.createElement("label");
        classLabel.className = "highlighter-nodeinfobar-class plain";
        classLabel.textContent = "." + this.node.classList[i];
        fragment.appendChild(classLabel);
      }
      classes.appendChild(fragment);
    }
  },

  /**
   * Move the Infobar to the right place in the highlighter.
   */
  moveInfobar: function Highlighter_moveInfobar()
  {
    let rect = this._highlightRect;
    if (rect && this._highlighting) {
      this.nodeInfo.container.removeAttribute("disabled");
      // Can the bar be above the node?
      if (rect.top < this.nodeInfo.barHeight) {
        // No. Can we move the toolbar under the node?
        if (rect.top + rect.height +
            this.nodeInfo.barHeight > this.win.innerHeight) {
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
        if (left + barWidth > this.win.innerWidth) {
          left = this.win.innerWidth - barWidth;
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
   * Return the midpoint of a line from pointA to pointB.
   *
   * @param object aPointA
   *        An object with x and y properties.
   * @param object aPointB
   *        An object with x and y properties.
   * @returns object
   *          An object with x and y properties.
   */
  midPoint: function Highlighter_midPoint(aPointA, aPointB)
  {
    let pointC = { };
    pointC.x = (aPointB.x - aPointA.x) / 2 + aPointA.x;
    pointC.y = (aPointB.y - aPointA.y) / 2 + aPointA.y;
    return pointC;
  },

  /**
   * Return the node under the highlighter rectangle. Useful for testing.
   * Calculation based on midpoint of diagonal from top left to bottom right
   * of panel.
   *
   * @returns nsIDOMNode|null
   *          Returns the node under the current highlighter rectangle. Null is
   *          returned if there is no node highlighted.
   */
  get highlitNode()
  {
    // Not highlighting? Bail.
    if (!this._highlighting || !this._contentRect) {
      return null;
    }

    let a = {
      x: this._contentRect.left,
      y: this._contentRect.top
    };

    let b = {
      x: a.x + this._contentRect.width,
      y: a.y + this._contentRect.height
    };

    // Get midpoint of diagonal line.
    let midpoint = this.midPoint(a, b);

    return this.IUI.elementFromPoint(this.win.document, midpoint.x,
      midpoint.y);
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
    if (aNode.nodeType != aNode.ELEMENT_NODE) {
      return false;
    }
    let nodeName = aNode.nodeName.toLowerCase();
    return !INSPECTOR_INVISIBLE_ELEMENTS[nodeName];
  },

  /////////////////////////////////////////////////////////////////////////
  //// Event Handling

  attachInspectListeners: function Highlighter_attachInspectListeners()
  {
    this.browser.addEventListener("mousemove", this, true);
    this.browser.addEventListener("click", this, true);
    this.browser.addEventListener("dblclick", this, true);
    this.browser.addEventListener("mousedown", this, true);
    this.browser.addEventListener("mouseup", this, true);
  },

  detachInspectListeners: function Highlighter_detachInspectListeners()
  {
    this.browser.removeEventListener("mousemove", this, true);
    this.browser.removeEventListener("click", this, true);
    this.browser.removeEventListener("dblclick", this, true);
    this.browser.removeEventListener("mousedown", this, true);
    this.browser.removeEventListener("mouseup", this, true);
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
        this.handleMouseMove(aEvent);
        break;
      case "resize":
        this.brieflyDisableTransitions();
        this.handleResize(aEvent);
        break;
      case "dblclick":
      case "mousedown":
      case "mouseup":
        aEvent.stopPropagation();
        aEvent.preventDefault();
        break;
      case "scroll":
        this.brieflyDisableTransitions();
        this.highlight();
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
     this.IUI.win.clearTimeout(this.transitionDisabler);
   } else {
     this.veilContainer.setAttribute("disable-transitions", "true");
     this.nodeInfo.container.setAttribute("disable-transitions", "true");
   }
   this.transitionDisabler =
     this.IUI.win.setTimeout(function() {
       this.veilContainer.removeAttribute("disable-transitions");
       this.nodeInfo.container.removeAttribute("disable-transitions");
       this.transitionDisabler = null;
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
      this.IUI.stopInspecting();
      win.focus();
    }
    aEvent.preventDefault();
    aEvent.stopPropagation();
  },

  /**
   * Handle mousemoves in panel when InspectorUI.inspecting is true.
   *
   * @param nsiDOMEvent aEvent
   *        The MouseEvent triggering the method.
   */
  handleMouseMove: function Highlighter_handleMouseMove(aEvent)
  {
    let element = this.IUI.elementFromPoint(aEvent.target.ownerDocument,
      aEvent.clientX, aEvent.clientY);
    if (element && element != this.node) {
      this.IUI.inspectNode(element);
    }
  },

  /**
   * Handle window resize events.
   */
  handleResize: function Highlighter_handleResize()
  {
    this.highlight();
  },
};

///////////////////////////////////////////////////////////////////////////
//// InspectorUI

/**
 * Main controller class for the Inspector.
 *
 * @constructor
 * @param nsIDOMWindow aWindow
 *        The chrome window for which the Inspector instance is created.
 */
function InspectorUI(aWindow)
{
  this.chromeWin = aWindow;
  this.chromeDoc = aWindow.document;
  this.tabbrowser = aWindow.gBrowser;
  this.tools = {};
  this.toolEvents = {};
  this.store = new InspectorStore();
  this.INSPECTOR_NOTIFICATIONS = INSPECTOR_NOTIFICATIONS;
}

InspectorUI.prototype = {
  browser: null,
  tools: null,
  toolEvents: null,
  inspecting: false,
  treePanelEnabled: true,
  ruleViewEnabled: true,
  isDirty: false,
  store: null,

  /**
   * Toggle the inspector interface elements on or off.
   *
   * @param aEvent
   *        The event that requested the UI change. Toolbar button or menu.
   */
  toggleInspectorUI: function IUI_toggleInspectorUI(aEvent)
  {
    if (this.isInspectorOpen) {
      this.closeInspectorUI();
    } else {
      this.openInspectorUI();
    }
  },

  /**
   * Show the Sidebar.
   */
  showSidebar: function IUI_showSidebar()
  {
    this.sidebarBox.removeAttribute("hidden");
    this.sidebarSplitter.removeAttribute("hidden");
    this.stylingButton.checked = true;

    // Activate the first tool in the sidebar, only if none previously-
    // selected. We'll want to do a followup to remember selected tool-states.
    if (!Array.some(this.sidebarToolbar.children,
      function(btn) btn.hasAttribute("checked"))) {
        let firstButtonId = this.getToolbarButtonId(this.sidebarTools[0].id);
        this.chromeDoc.getElementById(firstButtonId).click();
    }
  },

  /**
   * Hide the Sidebar.
   */
  hideSidebar: function IUI_hideSidebar()
  {
    this.sidebarBox.setAttribute("hidden", "true");
    this.sidebarSplitter.setAttribute("hidden", "true");
    this.stylingButton.checked = false;
  },

  /**
   * Show or hide the sidebar. Called from the Styling button on the
   * highlighter toolbar.
   */
  toggleSidebar: function IUI_toggleSidebar()
  {
    if (!this.isSidebarOpen) {
      this.showSidebar();
    } else {
      this.hideSidebar();
    }
  },

  /**
   * Getter to test if the Sidebar is open or not.
   */
  get isSidebarOpen()
  {
    return this.stylingButton.checked &&
          !this.sidebarBox.hidden &&
          !this.sidebarSplitter.hidden;
  },

  /**
   * Toggle the status of the inspector, starting or stopping it. Invoked
   * from the toolbar's Inspect button.
   */
  toggleInspection: function IUI_toggleInspection()
  {
    if (this.inspecting) {
      this.stopInspecting();
    } else {
      this.startInspecting();
    }
  },

  /**
   * Is the inspector UI open? Simply check if the toolbar is visible or not.
   *
   * @returns boolean
   */
  get isInspectorOpen()
  {
    return this.toolbar && !this.toolbar.hidden && this.highlighter;
  },

  /**
   * Return the default selection element for the inspected document.
   */
  get defaultSelection()
  {
    let doc = this.win.document;
    return doc.documentElement ? doc.documentElement.lastElementChild : null;
  },

  /**
   * Open inspector UI and HTML tree. Add listeners for document scrolling,
   * resize, tabContainer.TabSelect and others. If a node is provided, then
   * start inspecting it.
   *
   * @param [optional] aNode
   *        The node to inspect.
   */
  openInspectorUI: function IUI_openInspectorUI(aNode)
  {
    // InspectorUI is already up and running. Lock a node if asked (via context).
    if (this.isInspectorOpen && aNode) {
      this.inspectNode(aNode);
      this.stopInspecting();
      return;
    }

    // Observer used to inspect the specified element from content after the
    // inspector UI has been opened (via the content context menu).
    function inspectObserver(aElement) {
      Services.obs.removeObserver(boundInspectObserver,
                                  INSPECTOR_NOTIFICATIONS.OPENED,
                                  false);
      this.inspectNode(aElement);
      this.stopInspecting();
    };

    var boundInspectObserver = inspectObserver.bind(this, aNode);

    if (aNode) {
      // Add the observer to inspect the node after initialization finishes.
      Services.obs.addObserver(boundInspectObserver,
                               INSPECTOR_NOTIFICATIONS.OPENED,
                               false);
    }
    // Start initialization.
    this.browser = this.tabbrowser.selectedBrowser;
    this.win = this.browser.contentWindow;
    this.winID = this.getWindowID(this.win);
    this.toolbar = this.chromeDoc.getElementById("inspector-toolbar");
    this.inspectMenuitem = this.chromeDoc.getElementById("Tools:Inspect");
    this.inspectToolbutton =
      this.chromeDoc.getElementById("inspector-inspect-toolbutton");

    this.initTools();
    this.chromeWin.Tilt.setup();

    if (this.treePanelEnabled) {
      this.treePanel = new TreePanel(this.chromeWin, this);
    }

    if (Services.prefs.getBoolPref("devtools.ruleview.enabled") &&
        !this.toolRegistered("ruleview")) {
      this.registerRuleView();
    }

    if (Services.prefs.getBoolPref("devtools.styleinspector.enabled") &&
        !this.toolRegistered("styleinspector")) {
      this.stylePanel = new StyleInspector(this.chromeWin, this);
    }

    this.toolbar.hidden = false;
    this.inspectMenuitem.setAttribute("checked", true);

    // initialize the HTML Breadcrumbs
    this.breadcrumbs = new HTMLBreadcrumbs(this);

    this.isDirty = false;

    this.progressListener = new InspectorProgressListener(this);

    // initialize the highlighter
    this.initializeHighlighter();
  },

  /**
   * Register the Rule View in the Sidebar.
   */
  registerRuleView: function IUI_registerRuleView()
  {
    let isOpen = this.isRuleViewOpen.bind(this);

    this.ruleViewObject = {
      id: "ruleview",
      label: this.strings.GetStringFromName("ruleView.label"),
      tooltiptext: this.strings.GetStringFromName("ruleView.tooltiptext"),
      accesskey: this.strings.GetStringFromName("ruleView.accesskey"),
      context: this,
      get isOpen() isOpen(),
      show: this.openRuleView,
      hide: this.closeRuleView,
      onSelect: this.selectInRuleView,
      panel: null,
      unregister: this.destroyRuleView,
      sidebar: true,
    };

    this.registerTool(this.ruleViewObject);
  },

  /**
   * Register and initialize any included tools.
   */
  initTools: function IUI_initTools()
  {
    // Extras go here.
  },

  /**
   * Initialize highlighter.
   */
  initializeHighlighter: function IUI_initializeHighlighter()
  {
    this.highlighter = new Highlighter(this);
    this.browser.addEventListener("keypress", this, true);
    this.highlighter.highlighterContainer.addEventListener("keypress", this, true);
    this.highlighterReady();
  },

  /**
   * Initialize the InspectorStore.
   */
  initializeStore: function IUI_initializeStore()
  {
    // First time opened, add the TabSelect listener
    if (this.store.isEmpty()) {
      this.tabbrowser.tabContainer.addEventListener("TabSelect", this, false);
    }

    // Has this windowID been inspected before?
    if (this.store.hasID(this.winID)) {
      let selectedNode = this.store.getValue(this.winID, "selectedNode");
      if (selectedNode) {
        this.inspectNode(selectedNode);
      }
      this.isDirty = this.store.getValue(this.winID, "isDirty");
    } else {
      // First time inspecting, set state to no selection + live inspection.
      this.store.addStore(this.winID);
      this.store.setValue(this.winID, "selectedNode", null);
      this.store.setValue(this.winID, "inspecting", true);
      this.store.setValue(this.winID, "isDirty", this.isDirty);
      this.win.addEventListener("pagehide", this, true);
    }
  },

  /**
   * Close inspector UI and associated panels. Unhighlight and stop inspecting.
   * Remove event listeners for document scrolling, resize,
   * tabContainer.TabSelect and others.
   *
   * @param boolean aKeepStore
   *        Tells if you want the store associated to the current tab/window to
   *        be cleared or not. Set this to true to not clear the store, or false
   *        otherwise.
   */
  closeInspectorUI: function IUI_closeInspectorUI(aKeepStore)
  {
    // if currently editing an attribute value, closing the
    // highlighter/HTML panel dismisses the editor
    if (this.treePanel && this.treePanel.editingContext)
      this.treePanel.closeEditor();

    if (this.closing || !this.win || !this.browser) {
      return;
    }

    let winId = new String(this.winID); // retain this to notify observers.

    this.closing = true;
    this.toolbar.hidden = true;

    this.progressListener.destroy();
    delete this.progressListener;

    if (!aKeepStore) {
      this.store.deleteStore(this.winID);
      this.win.removeEventListener("pagehide", this, true);
    } else {
      // Update the store before closing.
      if (this.selection) {
        this.store.setValue(this.winID, "selectedNode",
          this.selection);
      }
      this.store.setValue(this.winID, "inspecting", this.inspecting);
      this.store.setValue(this.winID, "isDirty", this.isDirty);
    }

    if (this.store.isEmpty()) {
      this.tabbrowser.tabContainer.removeEventListener("TabSelect", this, false);
    }

    this.stopInspecting();
    this.browser.removeEventListener("keypress", this, true);

    this.saveToolState(this.winID);
    this.toolsDo(function IUI_toolsHide(aTool) {
      this.unregisterTool(aTool);
    }.bind(this));

    // close the sidebar
    this.hideSidebar();

    if (this.highlighter) {
      this.highlighter.highlighterContainer.removeEventListener("keypress",
                                                                this,
                                                                true);
      this.highlighter.destroy();
      this.highlighter = null;
    }

    if (this.breadcrumbs) {
      this.breadcrumbs.destroy();
      this.breadcrumbs = null;
    }

    this.inspectMenuitem.setAttribute("checked", false);
    this.browser = this.win = null; // null out references to browser and window
    this.winID = null;
    this.selection = null;
    this.closing = false;
    this.isDirty = false;

    delete this.treePanel;
    delete this.stylePanel;
    delete this.toolbar;
    Services.obs.notifyObservers(null, INSPECTOR_NOTIFICATIONS.CLOSED, null);
    if (!aKeepStore)
      Services.obs.notifyObservers(null, INSPECTOR_NOTIFICATIONS.DESTROYED, winId);
  },

  /**
   * Begin inspecting webpage, attach page event listeners, activate
   * highlighter event listeners.
   */
  startInspecting: function IUI_startInspecting()
  {
    // if currently editing an attribute value, starting
    // "live inspection" mode closes the editor
    if (this.treePanel && this.treePanel.editingContext)
      this.treePanel.closeEditor();

    this.inspectToolbutton.checked = true;
    this.highlighter.attachInspectListeners();

    this.inspecting = true;
    this.toolsDim(true);
    this.highlighter.veilContainer.removeAttribute("locked");
    this.highlighter.nodeInfo.container.removeAttribute("locked");
  },

  /**
   * Stop inspecting webpage, detach page listeners, disable highlighter
   * event listeners.
   * @param aPreventScroll
   *        Prevent scroll in the HTML tree?
   */
  stopInspecting: function IUI_stopInspecting(aPreventScroll)
  {
    if (!this.inspecting) {
      return;
    }

    this.inspectToolbutton.checked = false;
    // Detach event listeners from content window and child windows to disable
    // highlighting. We still want to be notified if the user presses "ESCAPE"
    // to close the inspector, or "RETURN" to unlock the node, so we don't 
    // remove the "keypress" event until the highlighter is removed.
    this.highlighter.detachInspectListeners();

    this.inspecting = false;
    this.toolsDim(false);
    if (this.highlighter.node) {
      this.select(this.highlighter.node, true, true, !aPreventScroll);
    } else {
      this.select(null, true, true);
    }
    this.highlighter.veilContainer.setAttribute("locked", true);
    this.highlighter.nodeInfo.container.setAttribute("locked", true);
  },

  /**
   * Select an object in the tree view.
   * @param aNode
   *        node to inspect
   * @param forceUpdate
   *        force an update?
   * @param aScroll boolean
   *        scroll the tree panel?
   */
  select: function IUI_select(aNode, forceUpdate, aScroll)
  {
    // if currently editing an attribute value, using the
    // highlighter dismisses the editor
    if (this.treePanel && this.treePanel.editingContext)
      this.treePanel.closeEditor();

    if (!aNode)
      aNode = this.defaultSelection;

    if (forceUpdate || aNode != this.selection) {
      this.selection = aNode;
      if (!this.inspecting) {
        this.highlighter.highlightNode(this.selection);
      }
    }

    this.breadcrumbs.update();
    this.chromeWin.Tilt.update(aNode);

    this.toolsSelect(aScroll);
  },

  /**
   * Called when the highlighted node is changed by a tool.
   *
   * @param object aUpdater
   *        The tool that triggered the update (if any), that tool's
   *        onChanged will not be called.
   */
  nodeChanged: function IUI_nodeChanged(aUpdater)
  {
    this.highlighter.highlight();
    this.toolsOnChanged(aUpdater);
  },

  /////////////////////////////////////////////////////////////////////////
  //// Event Handling

  highlighterReady: function IUI_highlighterReady()
  {
    // Setup the InspectorStore or restore state
    this.initializeStore();

    if (this.store.getValue(this.winID, "inspecting")) {
      this.startInspecting();
    }

    this.restoreToolState(this.winID);

    this.win.focus();
    Services.obs.notifyObservers({wrappedJSObject: this},
                                 INSPECTOR_NOTIFICATIONS.OPENED, null);
  },

  /**
   * Main callback handler for events.
   *
   * @param event
   *        The event to be handled.
   */
  handleEvent: function IUI_handleEvent(event)
  {
    let winID = null;
    let win = null;
    let inspectorClosed = false;

    switch (event.type) {
      case "TabSelect":
        winID = this.getWindowID(this.tabbrowser.selectedBrowser.contentWindow);
        if (this.isInspectorOpen && winID != this.winID) {
          this.closeInspectorUI(true);
          inspectorClosed = true;
        }

        if (winID && this.store.hasID(winID)) {
          if (inspectorClosed && this.closing) {
            Services.obs.addObserver(function reopenInspectorForTab() {
              Services.obs.removeObserver(reopenInspectorForTab,
                INSPECTOR_NOTIFICATIONS.CLOSED, false);

              this.openInspectorUI();
            }.bind(this), INSPECTOR_NOTIFICATIONS.CLOSED, false);
          } else {
            this.openInspectorUI();
          }
        }

        if (this.store.isEmpty()) {
          this.tabbrowser.tabContainer.removeEventListener("TabSelect", this,
                                                         false);
        }
        break;
      case "pagehide":
        win = event.originalTarget.defaultView;
        // Skip iframes/frames.
        if (!win || win.frameElement || win.top != win) {
          break;
        }

        win.removeEventListener(event.type, this, true);

        winID = this.getWindowID(win);
        if (winID && winID != this.winID) {
          this.store.deleteStore(winID);
        }

        if (this.store.isEmpty()) {
          this.tabbrowser.tabContainer.removeEventListener("TabSelect", this,
                                                         false);
        }
        break;
      case "keypress":
        switch (event.keyCode) {
          case this.chromeWin.KeyEvent.DOM_VK_ESCAPE:
            this.closeInspectorUI(false);
            event.preventDefault();
            event.stopPropagation();
            break;
          case this.chromeWin.KeyEvent.DOM_VK_RETURN:
            this.toggleInspection();
            event.preventDefault();
            event.stopPropagation();
            break;
          case this.chromeWin.KeyEvent.DOM_VK_LEFT:
            let node;
            if (this.selection) {
              node = this.selection.parentNode;
            } else {
              node = this.defaultSelection;
            }
            if (node && this.highlighter.isNodeHighlightable(node)) {
              this.inspectNode(node, true);
            }
            event.preventDefault();
            event.stopPropagation();
            break;
          case this.chromeWin.KeyEvent.DOM_VK_RIGHT:
            if (this.selection) {
              // Find the first child that is highlightable.
              for (let i = 0; i < this.selection.childNodes.length; i++) {
                node = this.selection.childNodes[i];
                if (node && this.highlighter.isNodeHighlightable(node)) {
                  break;
                }
              }
            } else {
              node = this.defaultSelection;
            }
            if (node && this.highlighter.isNodeHighlightable(node)) {
              this.inspectNode(node, true);
            }
            event.preventDefault();
            event.stopPropagation();
            break;
          case this.chromeWin.KeyEvent.DOM_VK_UP:
            if (this.selection) {
              // Find a previous sibling that is highlightable.
              node = this.selection.previousSibling;
              while (node && !this.highlighter.isNodeHighlightable(node)) {
                node = node.previousSibling;
              }
            } else {
              node = this.defaultSelection;
            }
            if (node && this.highlighter.isNodeHighlightable(node)) {
              this.inspectNode(node, true);
            }
            event.preventDefault();
            event.stopPropagation();
            break;
          case this.chromeWin.KeyEvent.DOM_VK_DOWN:
            if (this.selection) {
              // Find a next sibling that is highlightable.
              node = this.selection.nextSibling;
              while (node && !this.highlighter.isNodeHighlightable(node)) {
                node = node.nextSibling;
              }
            } else {
              node = this.defaultSelection;
            }
            if (node && this.highlighter.isNodeHighlightable(node)) {
              this.inspectNode(node, true);
            }
            event.preventDefault();
            event.stopPropagation();
            break;
        }
        break;
    }
  },

  /////////////////////////////////////////////////////////////////////////
  //// CssRuleView methods

  /**
   * Is the cssRuleView open?
   */
  isRuleViewOpen: function IUI_isRuleViewOpen()
  {
    return this.isSidebarOpen && this.ruleButton.hasAttribute("checked") &&
      (this.sidebarDeck.selectedPanel == this.getToolIframe(this.ruleViewObject));
  },

  /**
   * Convenience getter to retrieve the Rule Button.
   */
  get ruleButton()
  {
    return this.chromeDoc.getElementById(
      this.getToolbarButtonId(this.ruleViewObject.id));
  },

  /**
   * Open the CssRuleView.
   */
  openRuleView: function IUI_openRuleView()
  {
    let iframe = this.getToolIframe(this.ruleViewObject);
    if (iframe.getAttribute("src")) {
      // We're already loading this tool, let it finish.
      return;
    }

    let boundLoadListener = function() {
      iframe.removeEventListener("load", boundLoadListener, true);
      let doc = iframe.contentDocument;

      let winID = this.winID;
      let ruleViewStore = this.store.getValue(winID, "ruleView");
      if (!ruleViewStore) {
        ruleViewStore = {};
        this.store.setValue(winID, "ruleView", ruleViewStore);
      }

      this.ruleView = new CssRuleView(doc, ruleViewStore);

      this.boundRuleViewChanged = this.ruleViewChanged.bind(this);
      this.ruleView.element.addEventListener("CssRuleViewChanged",
                                             this.boundRuleViewChanged);

      doc.documentElement.appendChild(this.ruleView.element);
      this.ruleView.highlight(this.selection);
      Services.obs.notifyObservers(null,
        INSPECTOR_NOTIFICATIONS.RULEVIEWREADY, null);
    }.bind(this);

    iframe.addEventListener("load", boundLoadListener, true);

    iframe.setAttribute("src", "chrome://browser/content/devtools/cssruleview.xul");
  },

  /**
   * Stub to Close the CSS Rule View. Does nothing currently because the
   * Rule View lives in the sidebar.
   */
  closeRuleView: function IUI_closeRuleView()
  {
    // do nothing for now
  },

  /**
   * Update the selected node in the Css Rule View.
   * @param {nsIDOMnode} the selected node.
   */
  selectInRuleView: function IUI_selectInRuleView(aNode)
  {
    if (this.ruleView)
      this.ruleView.highlight(aNode);
  },

  ruleViewChanged: function IUI_ruleViewChanged()
  {
    this.isDirty = true;
    this.nodeChanged(this.ruleViewObject);
  },

  /**
   * Destroy the rule view.
   */
  destroyRuleView: function IUI_destroyRuleView()
  {
    let iframe = this.getToolIframe(this.ruleViewObject);
    iframe.parentNode.removeChild(iframe);

    if (this.ruleView) {
      this.ruleView.element.removeEventListener("CssRuleViewChanged",
                                                this.boundRuleViewChanged);
      delete boundRuleViewChanged;
      this.ruleView.clear();
      delete this.ruleView;
    }
  },

  /////////////////////////////////////////////////////////////////////////
  //// Utility Methods

  /**
   * inspect the given node, highlighting it on the page and selecting the
   * correct row in the tree panel
   *
   * @param aNode
   *        the element in the document to inspect
   * @param aScroll
   *        force scroll?
   */
  inspectNode: function IUI_inspectNode(aNode, aScroll)
  {
    this.select(aNode, true, true);
    this.highlighter.highlightNode(aNode, { scroll: aScroll });
  },

  /**
   * Find an element from the given coordinates. This method descends through
   * frames to find the element the user clicked inside frames.
   *
   * @param DOMDocument aDocument the document to look into.
   * @param integer aX
   * @param integer aY
   * @returns Node|null the element node found at the given coordinates.
   */
  elementFromPoint: function IUI_elementFromPoint(aDocument, aX, aY)
  {
    let node = aDocument.elementFromPoint(aX, aY);
    if (node && node.contentDocument) {
      if (node instanceof Ci.nsIDOMHTMLIFrameElement) {
        let rect = node.getBoundingClientRect();

        // Gap between the iframe and its content window.
        let [offsetTop, offsetLeft] = this.getIframeContentOffset(node);

        aX -= rect.left + offsetLeft;
        aY -= rect.top + offsetTop;

        if (aX < 0 || aY < 0) {
          // Didn't reach the content document, still over the iframe.
          return node;
        }
      }
      if (node instanceof Ci.nsIDOMHTMLIFrameElement ||
          node instanceof Ci.nsIDOMHTMLFrameElement) {
        let subnode = this.elementFromPoint(node.contentDocument, aX, aY);
        if (subnode) {
          node = subnode;
        }
      }
    }
    return node;
  },

  ///////////////////////////////////////////////////////////////////////////
  //// Utility functions

  /**
   * Returns iframe content offset (iframe border + padding).
   * Note: this function shouldn't need to exist, had the platform provided a
   * suitable API for determining the offset between the iframe's content and
   * its bounding client rect. Bug 626359 should provide us with such an API.
   *
   * @param aIframe
   *        The iframe.
   * @returns array [offsetTop, offsetLeft]
   *          offsetTop is the distance from the top of the iframe and the
   *            top of the content document.
   *          offsetLeft is the distance from the left of the iframe and the
   *            left of the content document.
   */
  getIframeContentOffset: function IUI_getIframeContentOffset(aIframe)
  {
    let style = aIframe.contentWindow.getComputedStyle(aIframe, null);

    let paddingTop = parseInt(style.getPropertyValue("padding-top"));
    let paddingLeft = parseInt(style.getPropertyValue("padding-left"));

    let borderTop = parseInt(style.getPropertyValue("border-top-width"));
    let borderLeft = parseInt(style.getPropertyValue("border-left-width"));

    return [borderTop + paddingTop, borderLeft + paddingLeft];
  },

  /**
   * Retrieve the unique ID of a window object.
   *
   * @param nsIDOMWindow aWindow
   * @returns integer ID
   */
  getWindowID: function IUI_getWindowID(aWindow)
  {
    if (!aWindow) {
      return null;
    }

    let util = {};

    try {
      util = aWindow.QueryInterface(Ci.nsIInterfaceRequestor).
        getInterface(Ci.nsIDOMWindowUtils);
    } catch (ex) { }

    return util.currentInnerWindowID;
  },

  /**
   * @param msg
   *        text message to send to the log
   */
  _log: function LOG(msg)
  {
    Services.console.logStringMessage(msg);
  },

  /**
   * Debugging function.
   * @param msg
   *        text to show with the stack trace.
   */
  _trace: function TRACE(msg)
  {
    this._log("TRACE: " + msg);
    let frame = Components.stack.caller;
    while (frame = frame.caller) {
      if (frame.language == Ci.nsIProgrammingLanguage.JAVASCRIPT ||
          frame.language == Ci.nsIProgrammingLanguage.JAVASCRIPT2) {
        this._log("filename: " + frame.filename + " lineNumber: " + frame.lineNumber +
          " functionName: " + frame.name);
      }
    }
    this._log("END TRACE");
  },

  /**
   * Get the toolbar button name for a given id string. Used by the
   * registerTools API to retrieve a consistent name for toolbar buttons
   * based on the ID of the tool.
   * @param anId String
   *        id of the tool to be buttonized
   * @returns String
   */
  getToolbarButtonId: function IUI_createButtonId(anId)
  {
    return "inspector-" + anId + "-toolbutton";
  },

  /**
   * Save a registered tool's callback for a specified event.
   * @param aWidget xul:widget
   * @param aEvent a DOM event name
   * @param aCallback Function the click event handler for the button
   */
  bindToolEvent: function IUI_bindToolEvent(aWidget, aEvent, aCallback)
  {
    this.toolEvents[aWidget.id + "_" + aEvent] = aCallback;
    aWidget.addEventListener(aEvent, aCallback, false);
  },

  /**
   * Register an external tool with the inspector.
   *
   * aRegObj = {
   *   id: "toolname",
   *   context: myTool,
   *   label: "Button or tab label",
   *   icon: "chrome://somepath.png",
   *   tooltiptext: "Button tooltip",
   *   accesskey: "S",
   *   isOpen: object.property, (getter) returning true if tool is open.
   *   onSelect: object.method,
   *   show: object.method, called to show the tool when button is pressed.
   *   hide: object.method, called to hide the tool when button is pressed.
   *   dim: object.method, called to disable a tool during highlighting.
   *   unregister: object.method, called when tool should be destroyed.
   *   panel: myTool.panel, set if tool is in a separate panel, null otherwise.
   *   sidebar: boolean, true if tool lives in sidebar tab.
   * }
   *
   * @param aRegObj Object
   *        The Registration Object used to register this tool described
   *        above. The tool should cache this object for later deregistration.
   */
  registerTool: function IUI_registerTool(aRegObj)
  {
    if (this.toolRegistered(aRegObj.id)) {
      return;
    }

    this.tools[aRegObj.id] = aRegObj;

    let buttonContainer = this.chromeDoc.getElementById("inspector-tools");
    let btn;

    // if this is a sidebar tool, create the sidebar features for it and bail.
    if (aRegObj.sidebar) {
      this.createSidebarTool(aRegObj);
      return;
    }

    btn = this.chromeDoc.createElement("toolbarbutton");
    let buttonId = this.getToolbarButtonId(aRegObj.id);
    btn.setAttribute("id", buttonId);
    btn.setAttribute("class", "devtools-toolbarbutton");
    btn.setAttribute("label", aRegObj.label);
    btn.setAttribute("tooltiptext", aRegObj.tooltiptext);
    btn.setAttribute("accesskey", aRegObj.accesskey);
    btn.setAttribute("image", aRegObj.icon || "");
    buttonContainer.insertBefore(btn, this.stylingButton);

    this.bindToolEvent(btn, "click",
      function IUI_toolButtonClick(aEvent) {
        if (btn.checked) {
          this.toolHide(aRegObj);
        } else {
          this.toolShow(aRegObj);
        }
      }.bind(this));

    // if the tool has a panel, register the popuphiding event
    if (aRegObj.panel) {
      this.bindToolEvent(aRegObj.panel, "popuphiding",
        function IUI_toolPanelHiding() {
          btn.checked = false;
        });
    }
  },

  get sidebarBox()
  {
    return this.chromeDoc.getElementById("devtools-sidebar-box");
  },

  get sidebarToolbar()
  {
    return this.chromeDoc.getElementById("devtools-sidebar-toolbar");
  },

  get sidebarDeck()
  {
    return this.chromeDoc.getElementById("devtools-sidebar-deck");
  },

  get sidebarSplitter()
  {
    return this.chromeDoc.getElementById("devtools-side-splitter");
  },

  get stylingButton()
  {
    return this.chromeDoc.getElementById("inspector-style-button");
  },

  /**
   * Creates a tab and tabpanel for our tool to reside in.
   * @param {Object} aRegObj the Registration Object for our tool.
   */
  createSidebarTool: function IUI_createSidebarTab(aRegObj)
  {
    // toolbutton elements
    let btn = this.chromeDoc.createElement("toolbarbutton");
    let buttonId = this.getToolbarButtonId(aRegObj.id);

    btn.id = buttonId;
    btn.setAttribute("label", aRegObj.label);
    btn.setAttribute("class", "devtools-toolbarbutton");
    btn.setAttribute("tooltiptext", aRegObj.tooltiptext);
    btn.setAttribute("accesskey", aRegObj.accesskey);
    btn.setAttribute("image", aRegObj.icon || "");
    btn.setAttribute("type", "radio");
    btn.setAttribute("group", "sidebar-tools");
    this.sidebarToolbar.appendChild(btn);

    // create tool iframe
    let iframe = this.chromeDoc.createElement("iframe");
    iframe.id = "devtools-sidebar-iframe-" + aRegObj.id;
    iframe.setAttribute("flex", "1");
    this.sidebarDeck.appendChild(iframe);

    // wire up button to show the iframe
    this.bindToolEvent(btn, "click", function showIframe() {
      this.toolShow(aRegObj);
    }.bind(this));
  },

  /**
   * Return the registered object's iframe.
   * @param aRegObj see registerTool function.
   * @return iframe or null
   */
  getToolIframe: function IUI_getToolIFrame(aRegObj)
  {
    return this.chromeDoc.getElementById("devtools-sidebar-iframe-" + aRegObj.id);
  },

  /**
   * Show the specified tool.
   * @param aTool Object (see comment for IUI_registerTool)
   */
  toolShow: function IUI_toolShow(aTool)
  {
    let btn = this.chromeDoc.getElementById(this.getToolbarButtonId(aTool.id));
    btn.setAttribute("checked", "true");
    if (aTool.sidebar) {
      this.sidebarDeck.selectedPanel = this.getToolIframe(aTool);
      this.sidebarTools.forEach(function(other) {
        if (other != aTool)
          this.chromeDoc.getElementById(
            this.getToolbarButtonId(other.id)).removeAttribute("checked");
      }.bind(this));
    }

    aTool.show.call(aTool.context, this.selection);
  },

  /**
   * Hide the specified tool.
   * @param aTool Object (see comment for IUI_registerTool)
   */
  toolHide: function IUI_toolHide(aTool)
  {
    aTool.hide.call(aTool.context);

    let btn = this.chromeDoc.getElementById(this.getToolbarButtonId(aTool.id));
    btn.removeAttribute("checked");
  },

  /**
   * Unregister the events associated with the registered tool's widget.
   * @param aWidget XUL:widget (toolbarbutton|panel).
   * @param aEvent a DOM event.
   */
  unbindToolEvent: function IUI_unbindToolEvent(aWidget, aEvent)
  {
    let toolEvent = aWidget.id + "_" + aEvent;
    aWidget.removeEventListener(aEvent, this.toolEvents[toolEvent], false);
    delete this.toolEvents[toolEvent]
  },

  /**
   * Unregister the registered tool, unbinding click events for the buttons
   * and showing and hiding events for the panel.
   * @param aRegObj Object
   *        The registration object used to register the tool.
   */
  unregisterTool: function IUI_unregisterTool(aRegObj)
  {
    // if this is a sidebar tool, use the sidebar unregistration method
    if (aRegObj.sidebar) {
      this.unregisterSidebarTool(aRegObj);
      return;
    }

    let button = this.chromeDoc.getElementById(this.getToolbarButtonId(aRegObj.id));
    let buttonContainer = this.chromeDoc.getElementById("inspector-tools");

    // unbind click events on button
    this.unbindToolEvent(button, "click");

    // unbind panel popuphiding events if present.
    if (aRegObj.panel)
      this.unbindToolEvent(aRegObj.panel, "popuphiding");

    // remove the button from its container
    buttonContainer.removeChild(button);

    // call unregister callback and remove from collection
    if (aRegObj.unregister)
      aRegObj.unregister.call(aRegObj.context);

    delete this.tools[aRegObj.id];
  },

  /**
   * Unregister the registered sidebar tool, unbinding click events for the
   * button.
   * @param aRegObj Object
   *        The registration object used to register the tool.
   */
  unregisterSidebarTool: function IUI_unregisterSidebarTool(aRegObj)
  {
    // unbind tool button click event
    let buttonId = this.getToolbarButtonId(aRegObj.id);
    let btn = this.chromeDoc.getElementById(buttonId);
    this.unbindToolEvent(btn, "click");

    // remove sidebar buttons and tools
    this.sidebarToolbar.removeChild(btn);

    // call unregister callback and remove from collection, this also removes
    // the iframe.
    if (aRegObj.unregister)
      aRegObj.unregister.call(aRegObj.context);

    delete this.tools[aRegObj.id];
  },

  /**
   * Save a list of open tools to the inspector store.
   *
   * @param aWinID The ID of the window used to save the associated tools
   */
  saveToolState: function IUI_saveToolState(aWinID)
  {
    let openTools = {};
    this.toolsDo(function IUI_toolsSetId(aTool) {
      if (aTool.isOpen) {
        openTools[aTool.id] = true;
      }
    });
    this.store.setValue(aWinID, "openTools", openTools);
  },

  /**
   * Restore tools previously save using saveToolState().
   *
   * @param aWinID The ID of the window to which the associated tools are to be
   *               restored.
   */
  restoreToolState: function IUI_restoreToolState(aWinID)
  {
    let openTools = this.store.getValue(aWinID, "openTools");
    let activeSidebarTool;
    if (openTools) {
      this.toolsDo(function IUI_toolsOnShow(aTool) {
        if (aTool.id in openTools) {
          if (aTool.sidebar && !this.isSidebarOpen) {
            this.showSidebar();
            activeSidebarTool = aTool;
          }
          this.toolShow(aTool);
        }
      }.bind(this));
      this.sidebarTools.forEach(function(tool) {
        if (tool != activeSidebarTool)
          this.chromeDoc.getElementById(
            this.getToolbarButtonId(tool.id)).removeAttribute("checked");
      }.bind(this));
    }
    Services.obs.notifyObservers(null, INSPECTOR_NOTIFICATIONS.STATE_RESTORED, null);
  },

  /**
   * For each tool in the tools collection select the current node that is
   * selected in the highlighter
   * @param aScroll boolean
   *        Do you want to scroll the treepanel?
   */
  toolsSelect: function IUI_toolsSelect(aScroll)
  {
    let selection = this.selection;
    this.toolsDo(function IUI_toolsOnSelect(aTool) {
      if (aTool.isOpen) {
        aTool.onSelect.call(aTool.context, selection, aScroll);
      }
    });
  },

  /**
   * Dim or undim each tool in the tools collection
   * @param aState true = dim, false = undim
   */
  toolsDim: function IUI_toolsDim(aState)
  {
    this.toolsDo(function IUI_toolsDim(aTool) {
      if (aTool.isOpen && "dim" in aTool) {
        aTool.dim.call(aTool.context, aState);
      }
    });
  },

  /**
   * Notify registered tools of changes to the highlighted element.
   *
   * @param object aUpdater
   *        The tool that triggered the update (if any), that tool's
   *        onChanged will not be called.
   */
  toolsOnChanged: function IUI_toolsChanged(aUpdater)
  {
    this.toolsDo(function IUI_toolsOnChanged(aTool) {
      if (aTool.isOpen && ("onChanged" in aTool) && aTool != aUpdater) {
        aTool.onChanged.call(aTool.context);
      }
    });
  },

  /**
   * Loop through all registered tools and pass each into the provided function
   * @param aFunction The function to which each tool is to be passed
   */
  toolsDo: function IUI_toolsDo(aFunction)
  {
    for each (let tool in this.tools) {
      aFunction(tool);
    }
  },

  /**
   * Convenience getter to retrieve only the sidebar tools.
   */
  get sidebarTools()
  {
    let sidebarTools = [];
    for each (let tool in this.tools)
      if (tool.sidebar)
        sidebarTools.push(tool);
    return sidebarTools;
  },

  /**
   * Check if a tool is registered?
   * @param aId The id of the tool to check
   */
  toolRegistered: function IUI_toolRegistered(aId)
  {
    return aId in this.tools;
  },

  /**
   * Destroy the InspectorUI instance. This is called by the InspectorUI API
   * "user", see BrowserShutdown() in browser.js.
   */
  destroy: function IUI_destroy()
  {
    if (this.isInspectorOpen) {
      this.closeInspectorUI();
    }

    delete this.store;
    delete this.chromeDoc;
    delete this.chromeWin;
    delete this.tabbrowser;
  },
};

/**
 * The Inspector store is used for storing data specific to each tab window.
 * @constructor
 */
function InspectorStore()
{
  this.store = {};
}
InspectorStore.prototype = {
  length: 0,

  /**
   * Check if there is any data recorded for any tab/window.
   *
   * @returns boolean True if there are no stores for any window/tab, or false
   * otherwise.
   */
  isEmpty: function IS_isEmpty()
  {
    return this.length == 0 ? true : false;
  },

  /**
   * Add a new store.
   *
   * @param string aID The Store ID you want created.
   * @returns boolean True if the store was added successfully, or false
   * otherwise.
   */
  addStore: function IS_addStore(aID)
  {
    let result = false;

    if (!(aID in this.store)) {
      this.store[aID] = {};
      this.length++;
      result = true;
    }

    return result;
  },

  /**
   * Delete a store by ID.
   *
   * @param string aID The store ID you want deleted.
   * @returns boolean True if the store was removed successfully, or false
   * otherwise.
   */
  deleteStore: function IS_deleteStore(aID)
  {
    let result = false;

    if (aID in this.store) {
      delete this.store[aID];
      this.length--;
      result = true;
    }

    return result;
  },

  /**
   * Check store existence.
   *
   * @param string aID The store ID you want to check.
   * @returns boolean True if the store ID is registered, or false otherwise.
   */
  hasID: function IS_hasID(aID)
  {
    return (aID in this.store);
  },

  /**
   * Retrieve a value from a store for a given key.
   *
   * @param string aID The store ID you want to read the value from.
   * @param string aKey The key name of the value you want.
   * @returns mixed the value associated to your store and key.
   */
  getValue: function IS_getValue(aID, aKey)
  {
    if (!this.hasID(aID))
      return null;
    if (aKey in this.store[aID])
      return this.store[aID][aKey];
    return null;
  },

  /**
   * Set a value for a given key and store.
   *
   * @param string aID The store ID where you want to store the value into.
   * @param string aKey The key name for which you want to save the value.
   * @param mixed aValue The value you want stored.
   * @returns boolean True if the value was stored successfully, or false
   * otherwise.
   */
  setValue: function IS_setValue(aID, aKey, aValue)
  {
    let result = false;

    if (aID in this.store) {
      this.store[aID][aKey] = aValue;
      result = true;
    }

    return result;
  },

  /**
   * Delete a value for a given key and store.
   *
   * @param string aID The store ID where you want to store the value into.
   * @param string aKey The key name for which you want to save the value.
   * @returns boolean True if the value was removed successfully, or false
   * otherwise.
   */
  deleteValue: function IS_deleteValue(aID, aKey)
  {
    let result = false;

    if (aID in this.store && aKey in this.store[aID]) {
      delete this.store[aID][aKey];
      result = true;
    }

    return result;
  }
};

/**
 * The InspectorProgressListener object is an nsIWebProgressListener which
 * handles onStateChange events for the inspected browser. If the user makes
 * changes to the web page and he tries to navigate away, he is prompted to
 * confirm page navigation, such that he's given the chance to prevent the loss
 * of edits.
 *
 * @constructor
 * @param object aInspector
 *        InspectorUI instance object.
 */
function InspectorProgressListener(aInspector)
{
  this.IUI = aInspector;
  this.IUI.tabbrowser.addProgressListener(this);
}

InspectorProgressListener.prototype = {
  onStateChange:
  function IPL_onStateChange(aProgress, aRequest, aFlag, aStatus)
  {
    // Remove myself if the Inspector is no longer open.
    if (!this.IUI.isInspectorOpen) {
      this.destroy();
      return;
    }

    let isStart = aFlag & Ci.nsIWebProgressListener.STATE_START;
    let isDocument = aFlag & Ci.nsIWebProgressListener.STATE_IS_DOCUMENT;
    let isNetwork = aFlag & Ci.nsIWebProgressListener.STATE_IS_NETWORK;
    let isRequest = aFlag & Ci.nsIWebProgressListener.STATE_IS_REQUEST;

    // Skip non-interesting states.
    if (!isStart || !isDocument || !isRequest || !isNetwork) {
      return;
    }

    // If the request is about to happen in a new window, we are not concerned
    // about the request.
    if (aProgress.DOMWindow != this.IUI.win) {
      return;
    }

    if (this.IUI.isDirty) {
      this.showNotification(aRequest);
    } else {
      this.IUI.closeInspectorUI();
    }
  },

  /**
   * Show an asynchronous notification which asks the user to confirm or cancel
   * the page navigation request.
   *
   * @param nsIRequest aRequest
   *        The request initiated by the user or by the page itself.
   * @returns void
   */
  showNotification: function IPL_showNotification(aRequest)
  {
    aRequest.suspend();

    let notificationBox = this.IUI.tabbrowser.getNotificationBox(this.IUI.browser);
    let notification = notificationBox.
      getNotificationWithValue("inspector-page-navigation");

    if (notification) {
      notificationBox.removeNotification(notification, true);
    }

    let cancelRequest = function onCancelRequest() {
      if (aRequest) {
        aRequest.cancel(Cr.NS_BINDING_ABORTED);
        aRequest.resume(); // needed to allow the connection to be cancelled.
        aRequest = null;
      }
    };

    let eventCallback = function onNotificationCallback(aEvent) {
      if (aEvent == "removed") {
        cancelRequest();
      }
    };

    let buttons = [
      {
        id: "inspector.confirmNavigationAway.buttonLeave",
        label: this.IUI.strings.
          GetStringFromName("confirmNavigationAway.buttonLeave"),
        accessKey: this.IUI.strings.
          GetStringFromName("confirmNavigationAway.buttonLeaveAccesskey"),
        callback: function onButtonLeave() {
          if (aRequest) {
            aRequest.resume();
            aRequest = null;
            this.IUI.closeInspectorUI();
            return true;
          }
          return false;
        }.bind(this),
      },
      {
        id: "inspector.confirmNavigationAway.buttonStay",
        label: this.IUI.strings.
          GetStringFromName("confirmNavigationAway.buttonStay"),
        accessKey: this.IUI.strings.
          GetStringFromName("confirmNavigationAway.buttonStayAccesskey"),
        callback: cancelRequest
      },
    ];

    let message = this.IUI.strings.
      GetStringFromName("confirmNavigationAway.message");

    notification = notificationBox.appendNotification(message,
      "inspector-page-navigation", "chrome://browser/skin/Info.png",
      notificationBox.PRIORITY_WARNING_HIGH, buttons, eventCallback);

    // Make sure this not a transient notification, to avoid the automatic
    // transient notification removal.
    notification.persistence = -1;
  },

  /**
   * Destroy the progress listener instance.
   */
  destroy: function IPL_destroy()
  {
    this.IUI.tabbrowser.removeProgressListener(this);

    let notificationBox = this.IUI.tabbrowser.getNotificationBox(this.IUI.browser);
    let notification = notificationBox.
      getNotificationWithValue("inspector-page-navigation");

    if (notification) {
      notificationBox.removeNotification(notification, true);
    }

    delete this.IUI;
  },
};

///////////////////////////////////////////////////////////////////////////
//// HTML Breadcrumbs

/**
 * Display the ancestors of the current node and its children.
 * Only one "branch" of children are displayed (only one line).
 *
 * Mechanism:
 * . If no nodes displayed yet:
 *    then display the ancestor of the selected node and the selected node;
 *   else select the node;
 * . If the selected node is the last node displayed, append its first (if any).
 *
 * @param object aInspector
 *        The InspectorUI instance.
 */
function HTMLBreadcrumbs(aInspector)
{
  this.IUI = aInspector;
  this.DOMHelpers = new DOMHelpers(this.IUI.win);
  this._init();
}

HTMLBreadcrumbs.prototype = {
  _init: function BC__init()
  {
    this.container = this.IUI.chromeDoc.getElementById("inspector-breadcrumbs");
    this.container.addEventListener("mousedown", this, true);

    // We will save a list of already displayed nodes in this array.
    this.nodeHierarchy = [];

    // Last selected node in nodeHierarchy.
    this.currentIndex = -1;

    // Siblings menu
    this.menu = this.IUI.chromeDoc.createElement("menupopup");
    this.menu.id = "inspector-breadcrumbs-menu";

    let popupSet = this.IUI.chromeDoc.getElementById("mainPopupSet");
    popupSet.appendChild(this.menu);

    this.menu.addEventListener("popuphiding", (function() {
      while (this.menu.hasChildNodes()) {
        this.menu.removeChild(this.menu.firstChild);
      }
      let button = this.container.querySelector("button[siblings-menu-open]");
      button.removeAttribute("siblings-menu-open");
    }).bind(this), false);
  },

  /**
   * Build a string that represents the node: tagName#id.class1.class2.
   *
   * @param aNode The node to pretty-print
   * @returns a string
   */
  prettyPrintNodeAsText: function BC_prettyPrintNodeText(aNode)
  {
    let text = aNode.tagName.toLowerCase();
    if (aNode.id) {
      text += "#" + aNode.id;
    }
    for (let i = 0; i < aNode.classList.length; i++) {
      text += "." + aNode.classList[i];
    }
    return text;
  },


  /**
   * Build <label>s that represent the node:
   *   <label class="inspector-breadcrumbs-tag">tagName</label>
   *   <label class="inspector-breadcrumbs-id">#id</label>
   *   <label class="inspector-breadcrumbs-classes">.class1.class2</label>
   *
   * @param aNode The node to pretty-print
   * @returns a document fragment.
   */
  prettyPrintNodeAsXUL: function BC_prettyPrintNodeXUL(aNode)
  {
    let fragment = this.IUI.chromeDoc.createDocumentFragment();

    let tagLabel = this.IUI.chromeDoc.createElement("label");
    tagLabel.className = "inspector-breadcrumbs-tag plain";

    let idLabel = this.IUI.chromeDoc.createElement("label");
    idLabel.className = "inspector-breadcrumbs-id plain";

    let classesLabel = this.IUI.chromeDoc.createElement("label");
    classesLabel.className = "inspector-breadcrumbs-classes plain";

    tagLabel.textContent = aNode.tagName.toLowerCase();
    idLabel.textContent = aNode.id ? ("#" + aNode.id) : "";

    let classesText = "";
    for (let i = 0; i < aNode.classList.length; i++) {
      classesText += "." + aNode.classList[i];
    }
    classesLabel.textContent = classesText;

    fragment.appendChild(tagLabel);
    fragment.appendChild(idLabel);
    fragment.appendChild(classesLabel);

    return fragment;
  },

  /**
   * Open the sibling menu.
   *
   * @param aButton the button representing the node.
   * @param aNode the node we want the siblings from.
   */
  openSiblingMenu: function BC_openSiblingMenu(aButton, aNode)
  {
    let title = this.IUI.chromeDoc.createElement("menuitem");
    title.setAttribute("label",
      this.IUI.strings.GetStringFromName("breadcrumbs.siblings"));
    title.setAttribute("disabled", "true");

    let separator = this.IUI.chromeDoc.createElement("menuseparator");

    this.menu.appendChild(title);
    this.menu.appendChild(separator);

    let fragment = this.IUI.chromeDoc.createDocumentFragment();

    let nodes = aNode.parentNode.childNodes;
    for (let i = 0; i < nodes.length; i++) {
      if (nodes[i].nodeType == aNode.ELEMENT_NODE) {
        let item = this.IUI.chromeDoc.createElement("menuitem");
        let inspector = this.IUI;
        if (nodes[i] === aNode) {
          item.setAttribute("disabled", "true");
          item.setAttribute("checked", "true");
        }

        item.setAttribute("type", "radio");
        item.setAttribute("label", this.prettyPrintNodeAsText(nodes[i]));

        item.onmouseup = (function(aNode) {
          return function() {
            inspector.select(aNode, true, true);
          }
        })(nodes[i]);

        fragment.appendChild(item);
      }
    }
    this.menu.appendChild(fragment);
    this.menu.openPopup(aButton, "before_start", 0, 0, true, false);
  },

  /**
   * Generic event handler.
   *
   * @param nsIDOMEvent aEvent
   *        The DOM event object.
   */
  handleEvent: function BC_handleEvent(aEvent)
  {
    if (aEvent.type == "mousedown") {
      // on Click and Hold, open the Siblings menu

      let timer;
      let container = this.container;
      let window = this.IUI.win;

      function openMenu(aEvent) {
        cancelHold();
        let target = aEvent.originalTarget;
        if (target.tagName == "button") {
          target.onBreadcrumbsHold();
          target.setAttribute("siblings-menu-open", "true");
        }
      }

      function handleClick(aEvent) {
        cancelHold();
        let target = aEvent.originalTarget;
        if (target.tagName == "button") {
          target.onBreadcrumbsClick();
        }
      }

      function cancelHold(aEvent) {
        window.clearTimeout(timer);
        container.removeEventListener("mouseout", cancelHold, false);
        container.removeEventListener("mouseup", handleClick, false);
      }

      container.addEventListener("mouseout", cancelHold, false);
      container.addEventListener("mouseup", handleClick, false);
      timer = window.setTimeout(openMenu, 500, aEvent);
    }
  },

  /**
   * Remove nodes and delete properties.
   */
  destroy: function BC_destroy()
  {
    this.empty();
    this.container.removeEventListener("mousedown", this, true);
    this.menu.parentNode.removeChild(this.menu);
    this.container = null;
    this.nodeHierarchy = null;
  },

  /**
   * Empty the breadcrumbs container.
   */
  empty: function BC_empty()
  {
    while (this.container.hasChildNodes()) {
      this.container.removeChild(this.container.firstChild);
    }
  },

  /**
   * Re-init the cache and remove all the buttons.
   */
  invalidateHierarchy: function BC_invalidateHierarchy()
  {
    this.menu.hidePopup();
    this.nodeHierarchy = [];
    this.empty();
  },

  /**
   * Set which button represent the selected node.
   *
   * @param aIdx Index of the displayed-button to select
   */
  setCursor: function BC_setCursor(aIdx)
  {
    // Unselect the previously selected button
    if (this.currentIndex > -1 && this.currentIndex < this.nodeHierarchy.length) {
      this.nodeHierarchy[this.currentIndex].button.removeAttribute("checked");
    }
    if (aIdx > -1) {
      this.nodeHierarchy[aIdx].button.setAttribute("checked", "true");
    }
    this.currentIndex = aIdx;
  },

  /**
   * Get the index of the node in the cache.
   *
   * @param aNode
   * @returns integer the index, -1 if not found
   */
  indexOf: function BC_indexOf(aNode)
  {
    let i = this.nodeHierarchy.length - 1;
    for (let i = this.nodeHierarchy.length - 1; i >= 0; i--) {
      if (this.nodeHierarchy[i].node === aNode) {
        return i;
      }
    }
    return -1;
  },

  /**
   * Remove all the buttons and their references in the cache
   * after a given index.
   *
   * @param aIdx
   */
  cutAfter: function BC_cutAfter(aIdx)
  {
    while (this.nodeHierarchy.length > (aIdx + 1)) {
      let toRemove = this.nodeHierarchy.pop();
      this.container.removeChild(toRemove.button);
    }
  },

  /**
   * Build a button representing the node.
   *
   * @param aNode The node from the page.
   * @returns aNode The <button>.
   */
  buildButton: function BC_buildButton(aNode)
  {
    let button = this.IUI.chromeDoc.createElement("button");
    let inspector = this.IUI;
    button.appendChild(this.prettyPrintNodeAsXUL(aNode));
    button.className = "inspector-breadcrumbs-button";

    button.setAttribute("tooltiptext", this.prettyPrintNodeAsText(aNode));

    button.onBreadcrumbsClick = function onBreadcrumbsClick() {
      inspector.stopInspecting();
      inspector.select(aNode, true, true);
    };

    button.onclick = (function _onBreadcrumbsRightClick(aEvent) {
      if (aEvent.button == 2) {
        this.openSiblingMenu(button, aNode);
      }
    }).bind(this);

    button.onBreadcrumbsHold = (function _onBreadcrumbsHold() {
      this.openSiblingMenu(button, aNode);
    }).bind(this);
    return button;
  },

  /**
   * Connecting the end of the breadcrumbs to a node.
   *
   * @param aNode The node to reach.
   */
  expand: function BC_expand(aNode)
  {
      let fragment = this.IUI.chromeDoc.createDocumentFragment();
      let toAppend = aNode;
      let lastButtonInserted = null;
      let originalLength = this.nodeHierarchy.length;
      let stopNode = null;
      if (originalLength > 0) {
        stopNode = this.nodeHierarchy[originalLength - 1].node;
      }
      while (toAppend && toAppend.tagName && toAppend != stopNode) {
        let button = this.buildButton(toAppend);
        fragment.insertBefore(button, lastButtonInserted);
        lastButtonInserted = button;
        this.nodeHierarchy.splice(originalLength, 0, {node: toAppend, button: button});
        toAppend = this.DOMHelpers.getParentObject(toAppend);
      }
      this.container.appendChild(fragment, this.container.firstChild);
  },

  /**
   * Get a child of a node that can be displayed in the breadcrumbs.
   * By default, we want a node that can highlighted by the highlighter.
   * If no highlightable child is found, we return the first node of type
   * ELEMENT_NODE.
   *
   * @param aNode The parent node.
   * @returns nsIDOMNode|null
   */
  getFirstHighlightableChild: function BC_getFirstHighlightableChild(aNode)
  {
    let nextChild = this.DOMHelpers.getChildObject(aNode, 0);
    let fallback = null;

    while (nextChild) {
      if (this.IUI.highlighter.isNodeHighlightable(nextChild)) {
        return nextChild;
      }
      if (!fallback && nextChild.nodeType == aNode.ELEMENT_NODE) {
        fallback = nextChild;
      }
      nextChild = this.DOMHelpers.getNextSibling(nextChild);
    }
    return fallback;
  },

  /**
   * Find the "youngest" ancestor of a node which is already in the breadcrumbs.
   *
   * @param aNode
   * @returns Index of the ancestor in the cache
   */
  getCommonAncestor: function BC_getCommonAncestor(aNode)
  {
    let node = aNode;
    while (node) {
      let idx = this.indexOf(node);
      if (idx > -1) {
        return idx;
      } else {
        node = this.DOMHelpers.getParentObject(node);
      }
    }
    return -1;
  },

  /**
   * Make sure that the latest node in the breadcrumbs is not the selected node
   * if the selected node still has children.
   */
  ensureFirstChild: function BC_ensureFirstChild()
  {
    // If the last displayed node is the selected node
    if (this.currentIndex == this.nodeHierarchy.length - 1) {
      let node = this.nodeHierarchy[this.currentIndex].node;
      let child = this.getFirstHighlightableChild(node);
      // If the node has a child
      if (child) {
        // Show this child
        this.expand(child);
      }
    }
  },

  /**
   * Ensure the selected node is visible.
   */
  scroll: function BC_scroll()
  {
    // FIXME bug 684352: make sure its immediate neighbors are visible too.

    let scrollbox = this.container;
    let element = this.nodeHierarchy[this.currentIndex].button;
    scrollbox.ensureElementIsVisible(element);
  },

  /**
   * Update the breadcrumbs display when a new node is selected.
   */
  update: function BC_update()
  {
    this.menu.hidePopup();

    let selection = this.IUI.selection;
    let idx = this.indexOf(selection);

    // Is the node already displayed in the breadcrumbs?
    if (idx > -1) {
      // Yes. We select it.
      this.setCursor(idx);
    } else {
      // No. Is the breadcrumbs display empty?
      if (this.nodeHierarchy.length > 0) {
        // No. We drop all the element that are not direct ancestors
        // of the selection
        let parent = this.DOMHelpers.getParentObject(selection);
        let idx = this.getCommonAncestor(parent);
        this.cutAfter(idx);
      }
      // we append the missing button between the end of the breadcrumbs display
      // and the current node.
      this.expand(selection);

      // we select the current node button
      idx = this.indexOf(selection);
      this.setCursor(idx);
    }
    // Add the first child of the very last node of the breadcrumbs if possible.
    this.ensureFirstChild();

    // Make sure the selected node and its neighbours are visible.
    this.scroll();
  }
}

/////////////////////////////////////////////////////////////////////////
//// Initializers

XPCOMUtils.defineLazyGetter(InspectorUI.prototype, "strings",
  function () {
    return Services.strings.createBundle(
            "chrome://browser/locale/devtools/inspector.properties");
  });

XPCOMUtils.defineLazyGetter(this, "StyleInspector", function () {
  var obj = {};
  Cu.import("resource:///modules/devtools/StyleInspector.jsm", obj);
  return obj.StyleInspector;
});


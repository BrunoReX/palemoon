/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
#ifdef 0
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
#endif

#include insideOutBox.js

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
 * @param nsIDOMNode aBrowser
 *        The xul:browser object for the content window being highlighted.
 */
function Highlighter(aBrowser)
{
  this._init(aBrowser);
}

Highlighter.prototype = {

  _init: function Highlighter__init(aBrowser)
  {
    this.browser = aBrowser;
    let stack = this.browser.parentNode;
    this.win = this.browser.contentWindow;
    this._highlighting = false;

    this.highlighterContainer = document.createElement("stack");
    this.highlighterContainer.id = "highlighter-container";

    let veilBox = document.createElement("vbox");
    veilBox.id = "highlighter-veil-container";

    let controlsBox = document.createElement("box");
    controlsBox.id = "highlighter-controls";

    // The veil will make the whole page darker except
    // for the region of the selected box.
    this.buildVeil(veilBox);

    // The controlsBox will host the different interactive
    // elements of the highlighter (buttons, toolbars, ...).
    this.buildControls(controlsBox);

    this.highlighterContainer.appendChild(veilBox);
    this.highlighterContainer.appendChild(controlsBox);

    stack.appendChild(this.highlighterContainer);

    this.browser.addEventListener("resize", this, true);
    this.browser.addEventListener("scroll", this, true);

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
   * @param nsIDOMNode aParent
   */
  buildVeil: function Highlighter_buildVeil(aParent)
  {

    // We will need to resize these boxes to surround a node.
    // See highlightRectangle().

    this.veilTopBox = document.createElement("box");
    this.veilTopBox.id = "highlighter-veil-topbox";
    this.veilTopBox.className = "highlighter-veil";

    this.veilMiddleBox = document.createElement("hbox");
    this.veilMiddleBox.id = "highlighter-veil-middlebox";

    this.veilLeftBox = document.createElement("box");
    this.veilLeftBox.id = "highlighter-veil-leftbox";
    this.veilLeftBox.className = "highlighter-veil";

    this.veilTransparentBox = document.createElement("box");
    this.veilTransparentBox.id = "highlighter-veil-transparentbox";

    // We don't need any references to veilRightBox and veilBottomBox.
    // These boxes are automatically resized (flex=1)

    let veilRightBox = document.createElement("box");
    veilRightBox.id = "highlighter-veil-rightbox";
    veilRightBox.className = "highlighter-veil";

    let veilBottomBox = document.createElement("box");
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
   * Build the controls:
   *
   * <box id="highlighter-close-button"/>
   *
   * @param nsIDOMNode aParent
   */
  buildControls: function Highlighter_buildControls(aParent)
  {
    let closeButton = document.createElement("box");
    closeButton.id = "highlighter-close-button";
    closeButton.appendChild(document.createElement("image"));

    closeButton.setAttribute("onclick", "InspectorUI.closeInspectorUI(false);");

    aParent.appendChild(closeButton);
  },


  /**
   * Destroy the nodes.
   */
  destroy: function Highlighter_destroy()
  {
    this.browser.removeEventListener("scroll", this, true);
    this.browser.removeEventListener("resize", this, true);
    this._highlightRect = null;
    this._highlighting = false;
    this.veilTopBox = null;
    this.veilLeftBox = null;
    this.veilMiddleBox = null;
    this.veilTransparentBox = null;
    this.node = null;
    this.highlighterContainer.parentNode.removeChild(this.highlighterContainer);
    this.highlighterContainer = null;
    this.win = null
    this.browser = null;
    this.toolbar = null;
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
    // node is not set or node is not highlightable, bail
    if (!this.node || !this.isNodeHighlightable()) {
      return;
    }

    let clientRect = this.node.getBoundingClientRect();

    // clientRect is read-only, we need to be able to change properties.
    let rect = {top: clientRect.top,
                left: clientRect.left,
                width: clientRect.width,
                height: clientRect.height};

    if (aScroll) {
      this.node.scrollIntoView();
    }

    // Go up in the tree of frames to determine the correct rectangle
    // coordinates and size.
    let frameWin = this.node.ownerDocument.defaultView;
    do {
      let frameRect = frameWin.frameElement ?
                      frameWin.frameElement.getBoundingClientRect() :
                      {top: 0, left: 0};

      if (rect.top < 0) {
        rect.height += rect.top;
        rect.top = 0;
      }

      if (rect.left < 0) {
        rect.width += rect.left;
        rect.left = 0;
      }

      let diffx = frameWin.innerWidth - rect.left - rect.width;
      if (diffx < 0) {
        rect.width += diffx;
      }
      let diffy = frameWin.innerHeight - rect.top - rect.height;
      if (diffy < 0) {
        rect.height += diffy;
      }

      rect.left += frameRect.left;
      rect.top += frameRect.top;

      frameWin = frameWin.parent;
    } while (frameWin != this.win);

    this.highlightRectangle(rect);

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
    let oldRect = this._highlightRect;

    if (oldRect && aRect.top == oldRect.top && aRect.left == oldRect.left &&
        aRect.width == oldRect.width && aRect.height == oldRect.height) {
      return this._highlighting; // same rectangle
    }

    if (aRect.left >= 0 && aRect.top >= 0 &&
        aRect.width > 0 && aRect.height > 0) {
      // The bottom div and the right div are flexibles (flex=1).
      // We don't need to resize them.
      this.veilTopBox.style.height = aRect.top + "px";
      this.veilLeftBox.style.width = aRect.left + "px";
      this.veilMiddleBox.style.height = aRect.height + "px";
      this.veilTransparentBox.style.width = aRect.width + "px";

      this._highlighting = true;
    } else {
      this.unhighlight();
    }

    this._highlightRect = aRect;

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
    Services.obs.notifyObservers(null,
      INSPECTOR_NOTIFICATIONS.UNHIGHLIGHTING, null);
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
    if (!this._highlighting || !this._highlightRect) {
      return null;
    }

    let a = {
      x: this._highlightRect.left,
      y: this._highlightRect.top
    };

    let b = {
      x: a.x + this._highlightRect.width,
      y: a.y + this._highlightRect.height
    };

    // Get midpoint of diagonal line.
    let midpoint = this.midPoint(a, b);

    return InspectorUI.elementFromPoint(this.win.document, midpoint.x,
      midpoint.y);
  },

  /**
   * Is this.node highlightable?
   *
   * @returns boolean
   *          True if the node is highlightable or false otherwise.
   */
  isNodeHighlightable: function Highlighter_isNodeHighlightable()
  {
    if (!this.node || this.node.nodeType != Node.ELEMENT_NODE) {
      return false;
    }
    let nodeName = this.node.nodeName.toLowerCase();
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
        this.handleResize(aEvent);
        break;
      case "dblclick":
      case "mousedown":
      case "mouseup":
        aEvent.stopPropagation();
        aEvent.preventDefault();
        break;
      case "scroll":
        this.highlight();
        break;
    }
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
      InspectorUI.stopInspecting();
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
    let element = InspectorUI.elementFromPoint(aEvent.target.ownerDocument,
      aEvent.clientX, aEvent.clientY);
    if (element && element != this.node) {
      InspectorUI.inspectNode(element);
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
 */
var InspectorUI = {
  browser: null,
  tools: {},
  showTextNodesWithWhitespace: false,
  inspecting: false,
  treeLoaded: false,
  prefEnabledName: "devtools.inspector.enabled",

  /**
   * Toggle the inspector interface elements on or off.
   *
   * @param aEvent
   *        The event that requested the UI change. Toolbar button or menu.
   */
  toggleInspectorUI: function IUI_toggleInspectorUI(aEvent)
  {
    if (this.isTreePanelOpen) {
      this.closeInspectorUI();
    } else {
      this.openInspectorUI();
    }
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
   * Is the tree panel open?
   *
   * @returns boolean
   */
  get isTreePanelOpen()
  {
    return this.treePanel && this.treePanel.state == "open";
  },

  /**
   * Return the default selection element for the inspected document.
   */
  get defaultSelection()
  {
    let doc = this.win.document;
    return doc.documentElement.lastElementChild;
  },

  initializeTreePanel: function IUI_initializeTreePanel()
  {
    this.treeBrowserDocument = this.treeIFrame.contentDocument;
    this.treePanelDiv = this.treeBrowserDocument.createElement("div");
    this.treeBrowserDocument.body.appendChild(this.treePanelDiv);
    this.treePanelDiv.ownerPanel = this;
    this.ioBox = new InsideOutBox(this, this.treePanelDiv);
    this.ioBox.createObjectBox(this.win.document.documentElement);
    this.treeLoaded = true;

    // initialize the highlighter
    this.initializeHighlighter();
  },

  /**
   * Open the inspector's tree panel and initialize it.
   */
  openTreePanel: function IUI_openTreePanel()
  {
    if (!this.treePanel) {
      this.treePanel = document.getElementById("inspector-tree-panel");
      this.treePanel.hidden = false;
    }

    this.treeIFrame = document.getElementById("inspector-tree-iframe");
    if (!this.treeIFrame) {
      let resizerBox = document.getElementById("tree-panel-resizer-box");
      this.treeIFrame = document.createElement("iframe");
      this.treeIFrame.setAttribute("id", "inspector-tree-iframe");
      this.treeIFrame.setAttribute("flex", "1");
      this.treeIFrame.setAttribute("type", "content");
      this.treeIFrame.setAttribute("onclick", "InspectorUI.onTreeClick(event)");
      this.treeIFrame = this.treePanel.insertBefore(this.treeIFrame, resizerBox);
    }

    this.treePanel.addEventListener("popupshown", function treePanelShown() {
      InspectorUI.treePanel.removeEventListener("popupshown",
        treePanelShown, false);

        InspectorUI.treeIFrame.addEventListener("load",
          function loadedInitializeTreePanel() {
            InspectorUI.treeIFrame.removeEventListener("load",
              loadedInitializeTreePanel, true);
            InspectorUI.initializeTreePanel();
          }, true);

      let src = InspectorUI.treeIFrame.getAttribute("src");
      if (src != "chrome://browser/content/inspector.html") {
        InspectorUI.treeIFrame.setAttribute("src",
          "chrome://browser/content/inspector.html");
      } else {
        InspectorUI.treeIFrame.contentWindow.location.reload();
      }

    }, false);

    const panelWidthRatio = 7 / 8;
    const panelHeightRatio = 1 / 5;

    let width = parseInt(this.win.outerWidth * panelWidthRatio);
    let height = parseInt(this.win.outerHeight * panelHeightRatio);
    let y = Math.min(window.screen.availHeight - height, this.win.innerHeight);

    this.treePanel.openPopup(this.browser, "overlap", 0, 0,
      false, false);

    this.treePanel.moveTo(80, y);
    this.treePanel.sizeTo(width, height);
  },

  createObjectBox: function IUI_createObjectBox(object, isRoot)
  {
    let tag = this.domplateUtils.getNodeTag(object);
    if (tag)
      return tag.replace({object: object}, this.treeBrowserDocument);
  },

  getParentObject: function IUI_getParentObject(node)
  {
    let parentNode = node ? node.parentNode : null;

    if (!parentNode) {
      // Documents have no parentNode; Attr, Document, DocumentFragment, Entity,
      // and Notation. top level windows have no parentNode
      if (node && node == Node.DOCUMENT_NODE) {
        // document type
        if (node.defaultView) {
          let embeddingFrame = node.defaultView.frameElement;
          if (embeddingFrame)
            return embeddingFrame.parentNode;
        }
      }
      // a Document object without a parentNode or window
      return null;  // top level has no parent
    }

    if (parentNode.nodeType == Node.DOCUMENT_NODE) {
      if (parentNode.defaultView) {
        return parentNode.defaultView.frameElement;
      }
      // parent is document element, but no window at defaultView.
      return null;
    }
    if (!parentNode.localName) {
      return null;
    }
    return parentNode;
  },

  getChildObject: function IUI_getChildObject(node, index, previousSibling)
  {
    if (!node)
      return null;

    if (node.contentDocument) {
      // then the node is a frame
      if (index == 0) {
        return node.contentDocument.documentElement;  // the node's HTMLElement
      }
      return null;
    }

    if (node instanceof GetSVGDocument) {
      let svgDocument = node.getSVGDocument();
      if (svgDocument) {
        // then the node is a frame
        if (index == 0) {
          return svgDocument.documentElement;  // the node's SVGElement
        }
        return null;
      }
    }

    let child = null;
    if (previousSibling)  // then we are walking
      child = this.getNextSibling(previousSibling);
    else
      child = this.getFirstChild(node);

    if (this.showTextNodesWithWhitespace)
      return child;

    for (; child; child = this.getNextSibling(child)) {
      if (!this.domplateUtils.isWhitespaceText(child))
        return child;
    }

    return null;  // we have no children worth showing.
  },

  getFirstChild: function IUI_getFirstChild(node)
  {
    this.treeWalker = node.ownerDocument.createTreeWalker(node,
      NodeFilter.SHOW_ALL, null, false);
    return this.treeWalker.firstChild();
  },

  getNextSibling: function IUI_getNextSibling(node)
  {
    let next = this.treeWalker.nextSibling();

    if (!next)
      delete this.treeWalker;

    return next;
  },

  /**
   * Open inspector UI. tree. Add listeners for document scrolling,
   * resize, tabContainer.TabSelect and others.
   */
  openInspectorUI: function IUI_openInspectorUI()
  {
    // initialization
    this.browser = gBrowser.selectedBrowser;
    this.win = this.browser.contentWindow;
    this.winID = this.getWindowID(this.win);
    this.toolbar = document.getElementById("inspector-toolbar");

    if (!this.domplate) {
      Cu.import("resource:///modules/domplate.jsm", this);
      this.domplateUtils.setDOM(window);
    }

    this.openTreePanel();

    this.toolbar.hidden = false;
    this.inspectCmd.setAttribute("checked", true);
  },

  /**
   * Initialize highlighter.
   */
  initializeHighlighter: function IUI_initializeHighlighter()
  {
    this.highlighter = new Highlighter(this.browser);
    this.highlighterReady();
  },

  /**
   * Initialize the InspectorStore.
   */
  initializeStore: function IUI_initializeStore()
  {
    // First time opened, add the TabSelect listener
    if (InspectorStore.isEmpty())
      gBrowser.tabContainer.addEventListener("TabSelect", this, false);

    // Has this windowID been inspected before?
    if (InspectorStore.hasID(this.winID)) {
      let selectedNode = InspectorStore.getValue(this.winID, "selectedNode");
      if (selectedNode) {
        this.inspectNode(selectedNode);
      }
    } else {
      // First time inspecting, set state to no selection + live inspection.
      InspectorStore.addStore(this.winID);
      InspectorStore.setValue(this.winID, "selectedNode", null);
      InspectorStore.setValue(this.winID, "inspecting", true);
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
    if (this.closing || !this.win || !this.browser) {
      return;
    }

    this.closing = true;
    this.toolbar.hidden = true;

    if (!aKeepStore) {
      InspectorStore.deleteStore(this.winID);
      this.win.removeEventListener("pagehide", this, true);
    } else {
      // Update the store before closing.
      if (this.selection) {
        InspectorStore.setValue(this.winID, "selectedNode",
          this.selection);
      }
      InspectorStore.setValue(this.winID, "inspecting", this.inspecting);
    }

    if (InspectorStore.isEmpty()) {
      gBrowser.tabContainer.removeEventListener("TabSelect", this, false);
    }

    this.stopInspecting();
    if (this.highlighter) {
      this.highlighter.destroy();
      this.highlighter = null;
    }

    if (this.treePanelDiv) {
      this.treePanelDiv.ownerPanel = null;
      let parent = this.treePanelDiv.parentNode;
      parent.removeChild(this.treePanelDiv);
      delete this.treePanelDiv;
      delete this.treeBrowserDocument;
    }

    if (this.treeIFrame)
      delete this.treeIFrame;
    delete this.ioBox;

    if (this.domplate) {
      this.domplateUtils.setDOM(null);
      delete this.domplate;
      delete this.HTMLTemplates;
      delete this.domplateUtils;
    }

    this.saveToolState(this.winID);
    this.toolsDo(function IUI_toolsHide(aTool) {
      if (aTool.panel) {
        aTool.panel.hidePopup();
      }
    });

    this.inspectCmd.setAttribute("checked", false);
    this.browser = this.win = null; // null out references to browser and window
    this.winID = null;
    this.selection = null;
    this.treeLoaded = false;

    this.treePanel.addEventListener("popuphidden", function treePanelHidden() {
      InspectorUI.closing = false;
      Services.obs.notifyObservers(null, INSPECTOR_NOTIFICATIONS.CLOSED, null);
    }, false);

    this.treePanel.hidePopup();
    delete this.treePanel;
  },

  /**
   * Begin inspecting webpage, attach page event listeners, activate
   * highlighter event listeners.
   */
  startInspecting: function IUI_startInspecting()
  {
    document.getElementById("inspector-inspect-toolbutton").checked = true;
    this.attachPageListeners();
    this.inspecting = true;
    this.highlighter.veilTransparentBox.removeAttribute("locked");
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

    document.getElementById("inspector-inspect-toolbutton").checked = false;
    this.detachPageListeners();
    this.inspecting = false;
    if (this.highlighter.node) {
      this.select(this.highlighter.node, true, true, !aPreventScroll);
    } else {
      this.select(null, true, true);
    }
    this.highlighter.veilTransparentBox.setAttribute("locked", true);
  },

  /**
   * Select an object in the tree view.
   * @param aNode
   *        node to inspect
   * @param forceUpdate
   *        force an update?
   * @param aScroll
   *        force scroll?
   */
  select: function IUI_select(aNode, forceUpdate, aScroll)
  {
    if (!aNode)
      aNode = this.defaultSelection;

    if (forceUpdate || aNode != this.selection) {
      this.selection = aNode;
      if (!this.inspecting) {
        this.highlighter.highlightNode(this.selection);
      }
      this.ioBox.select(this.selection, true, true, aScroll);
    }
    this.toolsDo(function IUI_toolsOnSelect(aTool) {
      if (aTool.panel.state == "open") {
        aTool.onSelect.apply(aTool.context, [aNode]);
      }
    });
  },

  /////////////////////////////////////////////////////////////////////////
  //// Event Handling

  highlighterReady: function IUI_highlighterReady()
  {
    // Setup the InspectorStore or restore state
    this.initializeStore();

    if (InspectorStore.getValue(this.winID, "inspecting")) {
      this.startInspecting();
    }

    this.win.focus();
    Services.obs.notifyObservers(null, INSPECTOR_NOTIFICATIONS.OPENED, null);
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
        winID = this.getWindowID(gBrowser.selectedBrowser.contentWindow);
        if (this.isTreePanelOpen && winID != this.winID) {
          this.closeInspectorUI(true);
          inspectorClosed = true;
        }

        if (winID && InspectorStore.hasID(winID)) {
          if (inspectorClosed && this.closing) {
            Services.obs.addObserver(function reopenInspectorForTab() {
              Services.obs.removeObserver(reopenInspectorForTab,
                INSPECTOR_NOTIFICATIONS.CLOSED, false);

              InspectorUI.openInspectorUI();
            }, INSPECTOR_NOTIFICATIONS.CLOSED, false);
          } else {
            this.openInspectorUI();
            this.restoreToolState(winID);
          }
        }

        if (InspectorStore.isEmpty()) {
          gBrowser.tabContainer.removeEventListener("TabSelect", this, false);
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
          InspectorStore.deleteStore(winID);
        }

        if (InspectorStore.isEmpty()) {
          gBrowser.tabContainer.removeEventListener("TabSelect", this, false);
        }
        break;
      case "keypress":
        switch (event.keyCode) {
          case KeyEvent.DOM_VK_RETURN:
          case KeyEvent.DOM_VK_ESCAPE:
            if (this.inspecting) {
              this.stopInspecting();
              event.preventDefault();
              event.stopPropagation();
            }
            break;
        }
        break;
    }
  },

  /**
   * Handle click events in the html tree panel.
   * @param aEvent
   *        The mouse event.
   */
  onTreeClick: function IUI_onTreeClick(aEvent)
  {
    let node;
    let target = aEvent.target;
    let hitTwisty = false;
    if (this.hasClass(target, "twisty")) {
      node = this.getRepObject(aEvent.target.nextSibling);
      hitTwisty = true;
    } else {
      node = this.getRepObject(aEvent.target);
    }

    if (node) {
      if (hitTwisty) {
        this.ioBox.toggleObject(node);
      } else {
        if (this.inspecting) {
          this.stopInspecting(true);
        } else {
          this.select(node, true, false);
          this.highlighter.highlightNode(node);
        }
      }
    }
  },

  /**
   * Attach event listeners to content window and child windows to enable
   * highlighting and click to stop inspection.
   */
  attachPageListeners: function IUI_attachPageListeners()
  {
    this.browser.addEventListener("keypress", this, true);
    this.highlighter.attachInspectListeners();
  },

  /**
   * Detach event listeners from content window and child windows
   * to disable highlighting.
   */
  detachPageListeners: function IUI_detachPageListeners()
  {
    this.browser.removeEventListener("keypress", this, true);
    this.highlighter.detachInspectListeners();
  },

  /////////////////////////////////////////////////////////////////////////
  //// Utility Methods

  /**
   * inspect the given node, highlighting it on the page and selecting the
   * correct row in the tree panel
   *
   * @param aNode
   *        the element in the document to inspect
   */
  inspectNode: function IUI_inspectNode(aNode)
  {
    this.select(aNode, true, true);
    this.highlighter.highlightNode(aNode);
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
      switch (node.nodeName.toLowerCase()) {
        case "iframe":
          let rect = node.getBoundingClientRect();
          aX -= rect.left;
          aY -= rect.top;

        case "frame":
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
   * Does the given object have a class attribute?
   * @param aNode
   *        the DOM node.
   * @param aClass
   *        The class string.
   * @returns boolean
   */
  hasClass: function IUI_hasClass(aNode, aClass)
  {
    if (!(aNode instanceof Element))
      return false;
    return aNode.classList.contains(aClass);
  },

  /**
   * Add the class name to the given object.
   * @param aNode
   *        the DOM node.
   * @param aClass
   *        The class string.
   */
  addClass: function IUI_addClass(aNode, aClass)
  {
    if (aNode instanceof Element)
      aNode.classList.add(aClass);
  },

  /**
   * Remove the class name from the given object
   * @param aNode
   *        the DOM node.
   * @param aClass
   *        The class string.
   */
  removeClass: function IUI_removeClass(aNode, aClass)
  {
    if (aNode instanceof Element)
      aNode.classList.remove(aClass);
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
   * Get the "repObject" from the HTML panel's domplate-constructed DOM node.
   * In this system, a "repObject" is the Object being Represented by the box
   * object. It is the "real" object that we're building our facade around.
   *
   * @param element
   *        The element in the HTML panel the user clicked.
   * @returns either a real node or null
   */
  getRepObject: function IUI_getRepObject(element)
  {
    let target = null;
    for (let child = element; child; child = child.parentNode) {
      if (this.hasClass(child, "repTarget"))
        target = child;

      if (child.repObject) {
        if (!target && this.hasClass(child.repObject, "repIgnore"))
          break;
        else
          return child.repObject;
      }
    }
    return null;
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
   * Register an external tool with the inspector.
   *
   * aRegObj = {
   *   id: "toolname",
   *   context: myTool,
   *   label: "Button label",
   *   icon: "chrome://somepath.png",
   *   tooltiptext: "Button tooltip",
   *   accesskey: "S",
   *   onSelect: object.method,
   *   onShow: object.method,
   *   onHide: object.method,
   *   panel: myTool.panel
   * }
   *
   * @param aRegObj
   */
  registerTool: function IUI_RegisterTool(aRegObj) {
    if (this.tools[aRegObj.id]) {
      return;
    } else {
      let id = aRegObj.id;
      let buttonId = "inspector-" + id + "-toolbutton";
      aRegObj.buttonId = buttonId;

      aRegObj.panel.addEventListener("popuphiding",
        function IUI_toolPanelHiding() {
          btn.setAttribute("checked", "false");
        }, false);
      aRegObj.panel.addEventListener("popupshowing",
        function IUI_toolPanelShowing() {
          btn.setAttribute("checked", "true");
        }, false);

      this.tools[id] = aRegObj;
    }

    let toolbox = document.getElementById("inspector-tools");
    let btn = document.createElement("toolbarbutton");
    btn.setAttribute("id", aRegObj.buttonId);
    btn.setAttribute("label", aRegObj.label);
    btn.setAttribute("tooltiptext", aRegObj.tooltiptext);
    btn.setAttribute("accesskey", aRegObj.accesskey);
    btn.setAttribute("class", "toolbarbutton-text");
    btn.setAttribute("image", aRegObj.icon || "");
    toolbox.appendChild(btn);

    btn.addEventListener("click",
      function IUI_ToolButtonClick(aEvent) {
        if (btn.getAttribute("checked") == "true") {
          aRegObj.onHide.apply(aRegObj.context);
        } else {
          aRegObj.onShow.apply(aRegObj.context, [InspectorUI.selection]);
          aRegObj.onSelect.apply(aRegObj.context, [InspectorUI.selection]);
        }
      }, false);
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
      if (aTool.panel.state == "open") {
        openTools[aTool.id] = true;
      }
    });
    InspectorStore.setValue(aWinID, "openTools", openTools);
  },

/**
 * Restore tools previously save using saveToolState().
 *
 * @param aWinID The ID of the window to which the associated tools are to be
 *               restored.
 */
  restoreToolState: function IUI_restoreToolState(aWinID)
  {
    let openTools = InspectorStore.getValue(aWinID, "openTools");
    InspectorUI.selection = InspectorUI.selection;
    if (openTools) {
      this.toolsDo(function IUI_toolsOnShow(aTool) {
        if (aTool.id in openTools) {
          aTool.onShow.apply(aTool.context, [InspectorUI.selection]);
        }
      });
    }
  },
  
  /**
   * Loop through all registered tools and pass each into the provided function
   *
   * @param aFunction The function to which each tool is to be passed
   */
  toolsDo: function IUI_toolsDo(aFunction)
  {
    for each (let tool in this.tools) {
      aFunction(tool);
    }
  },
};

/**
 * The Inspector store is used for storing data specific to each tab window.
 */
var InspectorStore = {
  store: {},
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

/////////////////////////////////////////////////////////////////////////
//// Initializors

XPCOMUtils.defineLazyGetter(InspectorUI, "inspectCmd", function () {
  return document.getElementById("Tools:Inspect");
});

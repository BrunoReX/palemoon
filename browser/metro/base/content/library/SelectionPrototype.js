/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * SelectionPrototype - common base class used by both chrome and content selection logic.
 */

// selection node parameters for various apis
const kSelectionNodeAnchor = 1;
const kSelectionNodeFocus = 2;

// selection type property constants
const kChromeSelector = 1;
const kContentSelector = 2;

dump("### SelectionPrototype.js loaded\n");

/*
  http://mxr.mozilla.org/mozilla-central/source/docshell/base/nsIDocShell.idl
  http://mxr.mozilla.org/mozilla-central/source/content/base/public/nsISelectionDisplay.idl
  http://mxr.mozilla.org/mozilla-central/source/content/base/public/nsISelectionListener.idl
  http://mxr.mozilla.org/mozilla-central/source/content/base/public/nsISelectionPrivate.idl
  http://mxr.mozilla.org/mozilla-central/source/content/base/public/nsISelectionController.idl
  http://mxr.mozilla.org/mozilla-central/source/content/base/public/nsISelection.idl
  http://mxr.mozilla.org/mozilla-central/source/dom/interfaces/core/nsIDOMDocument.idl#372
    rangeCount
    getRangeAt
    containsNode
  http://www.w3.org/TR/DOM-Level-2-Traversal-Range/ranges.html
  http://mxr.mozilla.org/mozilla-central/source/dom/interfaces/range/nsIDOMRange.idl
  http://mxr.mozilla.org/mozilla-central/source/dom/interfaces/core/nsIDOMDocument.idl#80
    content.document.createRange()
    getBoundingClientRect
    isPointInRange
  http://mxr.mozilla.org/mozilla-central/source/dom/interfaces/core/nsIDOMNode.idl
  http://mxr.mozilla.org/mozilla-central/source/dom/interfaces/base/nsIDOMWindowUtils.idl
    setSelectionAtPoint
  http://mxr.mozilla.org/mozilla-central/source/dom/interfaces/core/nsIDOMElement.idl
    getClientRect
  http://mxr.mozilla.org/mozilla-central/source/layout/generic/nsFrameSelection.h
  http://mxr.mozilla.org/mozilla-central/source/editor/idl/nsIEditor.idl
  http://mxr.mozilla.org/mozilla-central/source/dom/interfaces/base/nsIFocusManager.idl
  http://mxr.mozilla.org/mozilla-central/source/toolkit/content/widgets/textbox.xml
  http://mxr.mozilla.org/mozilla-central/source/dom/interfaces/xul/nsIDOMXULTextboxElement.idl
    mEditor
*/

var SelectionPrototype = function() { }

SelectionPrototype.prototype = {
  _debugEvents: false,
  _cache: {},
  _targetElement: null,
  _targetIsEditable: true,
  _contentOffset: { x: 0, y: 0 },
  _contentWindow: null,
  _debugOptions: { dumpRanges: false, displayRanges: false },
  _domWinUtils: null,
  _selectionMoveActive: false,
  _snap: false,
  _type: 0,

  /*************************************************
   * Properties
   */

  get isActive() {
    return !!this._targetElement;
  },

  get targetIsEditable() {
    return this._targetIsEditable || false;
  },

  /*
   * snap - enable or disable word snap for the active marker when a
   * SelectionMoveEnd event is received. Typically you would disable
   * snap when zoom is < 1.0 for precision selection. defaults to off.
   */
  get snap() {
    return this._snap;
  },

  set snap(aValue) {
    this._snap = aValue;
  },

  /*
   * type - returns a constant indicating which module we're
   * loaded into - chrome or content.
   */
  set type(aValue) {
    this._type = aValue;
  },

  get type() {
    return this._type;
  },

  /*************************************************
   * Messaging wrapper
   */

  /*
   * sendAsync
   *
   * Wrapper over sendAsyncMessage in content, waps direct invocation on
   * SelectionHelperUI in chrome. Should be overriden by objects that inherit
   * our behavior. 
   */
  sendAsync: function sendAsync(aMsg, aJson) {
    Util.dumpLn("Base sendAsync called on SelectionPrototype. This is a no-op.");
  },

  /*************************************************
   * Common browser event handlers
   */

   /*
    * _onCaretPositionUpdate - sets the current caret location based on
    * a client coordinates. Messages back with updated monocle position
    * information.
    *
    * @param aX, aY drag location in client coordinates.
    */
  _onCaretPositionUpdate: function _onCaretPositionUpdate(aX, aY) {
    this._onCaretMove(aX, aY);

    // Update the position of our selection monocles
    this._updateSelectionUI("caret", false, false, true);
  },

   /*
    * _onCaretMove - updates the current caret location based on a client
    * coordinates.
    *
    * @param aX, aY drag location in client coordinates.
    */
  _onCaretMove: function _onCaretMove(aX, aY) {
    if (!this._targetIsEditable) {
      this._onFail("Unexpected, caret position isn't supported with non-inputs.");
      return;
    }

    // SelectionHelperUI sends text input tap coordinates and a caret move
    // event at the start of a monocle drag. caretPositionFromPoint isn't
    // going to give us correct info if the coord is outside the edit bounds,
    // so restrict the coordinates before we call cpfp.
    let containedCoords = this._restrictCoordinateToEditBounds(aX, aY);
    let cp = this._contentWindow.document.caretPositionFromPoint(containedCoords.xPos,
                                                                 containedCoords.yPos);
    let input = cp.offsetNode;
    let offset = cp.offset;
    input.selectionStart = input.selectionEnd = offset;
  },

  /*
   * Turning on or off various debug features.
   */
  _onSelectionDebug: function _onSelectionDebug(aMsg) {
    this._debugOptions = aMsg;
    this._debugEvents = aMsg.dumpEvents;
  },

  /*************************************************
   * Selection api
   */

  /*
   * _updateSelectionUI
   *
   * Informs SelectionHelperUI about selection marker position
   * so that our selection monocles can be positioned properly.
   *
   * @param aSrcMsg string the message type that triggered this update.
   * @param aUpdateStart bool update start marker position
   * @param aUpdateEnd bool update end marker position
   * @param aUpdateCaret bool update caret marker position, can be
   * undefined, defaults to false.
   */
  _updateSelectionUI: function _updateSelectionUI(aSrcMsg, aUpdateStart, aUpdateEnd,
                                                  aUpdateCaret) {
    let selection = this._getSelection();

    // If the range didn't have any text, let's bail
    if (!selection) {
      this._onFail("no selection was present");
      return;
    }

    // Updates this._cache content selection position data which we send over
    // to SelectionHelperUI. Note updateUIMarkerRects will fail if there isn't
    // any selection in the page. This can happen when we start a monocle drag
    // but haven't dragged enough to create selection. Just return.
    try {
      this._updateUIMarkerRects(selection);
    } catch (ex) {
      Util.dumpLn("_updateUIMarkerRects:", ex.message);
      return;
    }

    this._cache.src = aSrcMsg;
    this._cache.updateStart = aUpdateStart;
    this._cache.updateEnd = aUpdateEnd;
    this._cache.updateCaret = aUpdateCaret || false;
    this._cache.targetIsEditable = this._targetIsEditable;

    // Get monocles positioned correctly
    this.sendAsync("Content:SelectionRange", this._cache);
  },

  /*
   * _handleSelectionPoint(aMarker, aPoint, aEndOfSelection) 
   *
   * After a monocle moves to a new point in the document, determines
   * what the target is and acts on its selection accordingly. If the
   * monocle is within the bounds of the target, adds or subtracts selection
   * at the monocle coordinates appropriately and then merges selection ranges
   * into a single continuous selection. If the monocle is outside the bounds
   * of the target and the underlying target is editable, uses the selection
   * controller to advance selection and visibility within the control.
   */
  _handleSelectionPoint: function _handleSelectionPoint(aMarker, aClientPoint,
                                                        aEndOfSelection) {
    let selection = this._getSelection();

    let clientPoint = { xPos: aClientPoint.xPos, yPos: aClientPoint.yPos };

    if (selection.rangeCount == 0) {
      this._onFail("selection.rangeCount == 0");
      return;
    }

    // We expect continuous selection ranges.
    if (selection.rangeCount > 1) {
      this._setContinuousSelection();
    }

    // Adjust our y position up such that we are sending coordinates on
    // the text line vs. below it where the monocle is positioned.
    let halfLineHeight = this._queryHalfLineHeight(aMarker, selection);
    clientPoint.yPos -= halfLineHeight;

    // Modify selection based on monocle movement
    if (this._targetIsEditable) {
      this._adjustEditableSelection(aMarker, clientPoint, aEndOfSelection);
    } else {
      this._adjustSelectionAtPoint(aMarker, clientPoint, aEndOfSelection);
    }
  },

  /*
   * _handleSelectionPoint helper methods
   */

  /*
   * _adjustEditableSelection
   *
   * Based on a monocle marker and position, adds or subtracts from the
   * existing selection in editable controls. Handles auto-scroll as well.
   *
   * @param the marker currently being manipulated
   * @param aAdjustedClientPoint client point adjusted for line height.
   * @param aEndOfSelection indicates if this is the end of a selection
   * move, in which case we may want to snap to the end of a word or
   * sentence.
   */
  _adjustEditableSelection: function _adjustEditableSelection(aMarker,
                                                              aAdjustedClientPoint,
                                                              aEndOfSelection) {
    // Test to see if we need to handle auto-scroll in cases where the
    // monocle is outside the bounds of the control. This also handles
    // adjusting selection if out-of-bounds is true.
    let result = this.updateTextEditSelection(aAdjustedClientPoint);

    // If result.trigger is true, the monocle is outside the bounds of the
    // control.
    if (result.trigger) {
      // _handleSelectionPoint is triggered by input movement, so if we've
      // tested positive for out-of-bounds scrolling here, we need to set a
      // recurring timer to keep the expected selection behavior going as
      // long as the user keeps the monocle out of bounds.
      this._setTextEditUpdateInterval(result.speed);

      // Smooth the selection
      this._setContinuousSelection();

      // Update the other monocle's position if we've dragged off to one side
      this._updateSelectionUI("update", result.start, result.end);
    } else {
      // If we aren't out-of-bounds, clear the scroll timer if it exists.
      this._clearTimers();

      // Restrict the client point to the interior of the control.
      let constrainedPoint =
        this._constrainPointWithinControl(aAdjustedClientPoint);

      // For textareas we fall back on the selectAtPoint logic due to various
      // issues with caretPositionFromPoint (bug 882149).
      if (Util.isMultilineInput(this._targetElement)) {
        this._adjustSelectionAtPoint(aMarker, constrainedPoint, aEndOfSelection);
        return;
      }

      //  Add or subtract selection
      let cp =
        this._contentWindow.document.caretPositionFromPoint(constrainedPoint.xPos,
                                                            constrainedPoint.yPos);
      if (!cp || (cp.offsetNode != this._targetElement &&
          this._contentWindow.document.getBindingParent(cp.offsetNode) != this._targetElement)) {
        return;
      }
      if (aMarker == "start") {
        this._targetElement.selectionStart = cp.offset;
      } else {
        this._targetElement.selectionEnd = cp.offset;
      }
    }
  },

  /*
   * _adjustSelectionAtPoint
   *
   * Based on a monocle marker and position, adds or subtracts from the
   * existing selection at a point.
   *
   * Note: we are trying to move away from this api due to the overhead. 
   *
   * @param the marker currently being manipulated
   * @param aClientPoint the point designating the new start or end
   * position for the selection.
   * @param aEndOfSelection indicates if this is the end of a selection
   * move, in which case we may want to snap to the end of a word or
   * sentence.
   */
  _adjustSelectionAtPoint: function _adjustSelectionAtPoint(aMarker, aClientPoint,
                                                            aEndOfSelection) {
    // Make a copy of the existing range, we may need to reset it.
    this._backupRangeList();

    // shrinkSelectionFromPoint takes sub-frame relative coordinates.
    let framePoint = this._clientPointToFramePoint(aClientPoint);

    // Tests to see if the user is trying to shrink the selection, and if so
    // collapses it down to the appropriate side such that our calls below
    // will reset the selection to the proper range.
    let shrunk = this._shrinkSelectionFromPoint(aMarker, framePoint);

    let selectResult = false;
    try {
      // If we're at the end of a selection (touchend) snap to the word.
      let type = ((aEndOfSelection && this._snap) ?
        Ci.nsIDOMWindowUtils.SELECT_WORD :
        Ci.nsIDOMWindowUtils.SELECT_CHARACTER);

      // Select a character at the point.
      selectResult = 
        this._domWinUtils.selectAtPoint(framePoint.xPos,
                                        framePoint.yPos,
                                        type);
    } catch (ex) {
    }

    // If selectAtPoint failed (which can happen if there's nothing to select)
    // reset our range back before we shrunk it.
    if (!selectResult) {
      this._restoreRangeList();
    }

    this._freeRangeList();

    // Smooth over the selection between all existing ranges.
    this._setContinuousSelection();

    // Update the other monocle's position. We do this because the dragging
    // monocle may reset the static monocle to a new position if the dragging
    // monocle drags ahead or behind the other.
    this._updateSelectionUI("update", aMarker == "end", aMarker == "start");
  },

  /*
   * _backupRangeList, _restoreRangeList, and _freeRangeList
   *
   * Utilities that manage a cloned copy of the existing selection.
   */

  _backupRangeList: function _backupRangeList() {
    this._rangeBackup = new Array();
    for (let idx = 0; idx < this._getSelection().rangeCount; idx++) {
      this._rangeBackup.push(this._getSelection().getRangeAt(idx).cloneRange());
    }
  },

  _restoreRangeList: function _restoreRangeList() {
    if (this._rangeBackup == null)
      return;
    for (let idx = 0; idx < this._rangeBackup.length; idx++) {
      this._getSelection().addRange(this._rangeBackup[idx]);
    }
    this._freeRangeList();
  },

  _freeRangeList: function _restoreRangeList() {
    this._rangeBackup = null;
  },

  /*
   * Constrains a selection point within a text input control bounds.
   *
   * @param aPoint - client coordinate point
   * @param aMargin - added inner margin to respect, defaults to 0.
   * @param aOffset - amount to push the resulting point inward, defaults to 0.
   * @return new constrained point struct
   */
  _constrainPointWithinControl: function _cpwc(aPoint, aMargin, aOffset) {
    let margin = aMargin || 0;
    let offset = aOffset || 0;
    let bounds = this._getTargetBrowserRect();
    let point = { xPos: aPoint.xPos, yPos: aPoint.yPos };
    if (point.xPos <= (bounds.left + margin))
      point.xPos = bounds.left + offset;
    if (point.xPos >= (bounds.right - margin))
      point.xPos = bounds.right - offset;
    if (point.yPos <= (bounds.top + margin))
      point.yPos = bounds.top + offset;
    if (point.yPos >= (bounds.bottom - margin))
      point.yPos = bounds.bottom - offset;
    return point;
  },

  /*
   * _pointOrientationToRect(aPoint, aRect)
   *
   * Returns a table representing which sides of target aPoint is offset
   * from: { left: offset, top: offset, right: offset, bottom: offset }
   * Works on client coordinates.
   */
  _pointOrientationToRect: function _pointOrientationToRect(aPoint) {
    let bounds = this._getTargetBrowserRect();
    let result = { left: 0, right: 0, top: 0, bottom: 0 };
    if (aPoint.xPos <= bounds.left)
      result.left = bounds.left - aPoint.xPos;
    if (aPoint.xPos >= bounds.right)
      result.right = aPoint.xPos - bounds.right;
    if (aPoint.yPos <= bounds.top)
      result.top = bounds.top - aPoint.yPos;
    if (aPoint.yPos >= bounds.bottom)
      result.bottom = aPoint.yPos - bounds.bottom;
    return result;
  },

  /*
   * updateTextEditSelection(aPoint, aClientPoint)
   *
   * Checks to see if the monocle point is outside the bounds of the target
   * edit. If so, use the selection controller to select and scroll the edit
   * appropriately.
   *
   * @param aClientPoint raw pointer position
   * @return { speed: 0.0 -> 1.0,
   *           trigger: true/false if out of bounds,
   *           start: true/false if updated position,
   *           end: true/false if updated position }
   */
  updateTextEditSelection: function updateTextEditSelection(aClientPoint) {
    if (aClientPoint == undefined) {
      aClientPoint = this._rawSelectionPoint;
    }
    this._rawSelectionPoint = aClientPoint;

    let orientation = this._pointOrientationToRect(aClientPoint);
    let result = { speed: 1, trigger: false, start: false, end: false };
    let ml = Util.isMultilineInput(this._targetElement);

    // This could be improved such that we only select to the beginning of
    // the line when dragging left but not up.
    if (orientation.left || (ml && orientation.top)) {
      this._addEditSelection(kSelectionNodeAnchor);
      result.speed = orientation.left + orientation.top;
      result.trigger = true;
      result.end = true;
    } else if (orientation.right || (ml && orientation.bottom)) {
      this._addEditSelection(kSelectionNodeFocus);
      result.speed = orientation.right + orientation.bottom;
      result.trigger = true;
      result.start = true;
    }

    // 'speed' is just total pixels offset, so clamp it to something
    // reasonable callers can work with.
    if (result.speed > 100)
      result.speed = 100;
    if (result.speed < 1)
      result.speed = 1;
    result.speed /= 100;
    return result;
  },

  _setTextEditUpdateInterval: function _setTextEditUpdateInterval(aSpeedValue) {
    let timeout = (75 - (aSpeedValue * 75));
    if (!this._scrollTimer)
      this._scrollTimer = new Util.Timeout();
    this._scrollTimer.interval(timeout, this.scrollTimerCallback);
  },

  _clearTimers: function _clearTimers() {
    if (this._scrollTimer) {
      this._scrollTimer.clear();
    }
  },

  /*
   * _addEditSelection - selection control call wrapper for text inputs.
   * Adds selection on the anchor or focus side of selection in a text
   * input. Scrolls the location into view as well.
   *
   * @param const selection node identifier
   */
  _addEditSelection: function _addEditSelection(aLocation) {
    let selCtrl = this._getSelectController();
    try {
      if (aLocation == kSelectionNodeAnchor) {
        let start = Math.max(this._targetElement.selectionStart - 1, 0);
        this._targetElement.setSelectionRange(start, this._targetElement.selectionEnd,
                                              "backward");
      } else {
        let end = Math.min(this._targetElement.selectionEnd + 1,
                           this._targetElement.textLength);
        this._targetElement.setSelectionRange(this._targetElement.selectionStart,
                                              end,
                                              "forward");
      }
      selCtrl.scrollSelectionIntoView(Ci.nsISelectionController.SELECTION_NORMAL,
                                      Ci.nsISelectionController.SELECTION_FOCUS_REGION,
                                      Ci.nsISelectionController.SCROLL_SYNCHRONOUS);
    } catch (ex) { Util.dumpLn(ex);}
  },

  _updateInputFocus: function _updateInputFocus(aMarker) {
    try {
      let selCtrl = this._getSelectController();
      this._targetElement.setSelectionRange(this._targetElement.selectionStart,
                                            this._targetElement.selectionEnd,
                                            aMarker == "start" ?
                                              "backward" : "forward");
      selCtrl.scrollSelectionIntoView(Ci.nsISelectionController.SELECTION_NORMAL,
                                      Ci.nsISelectionController.SELECTION_FOCUS_REGION,
                                      Ci.nsISelectionController.SCROLL_SYNCHRONOUS);
    } catch (ex) {}
  },

  /*
   * _queryHalfLineHeight(aMarker, aSelection)
   *
   * Y offset applied to the coordinates of the selection position we send
   * to dom utils. The selection marker sits below text, but we want the
   * selection position to be on the text above the monocle. Since text
   * height can vary across the entire selection range, we need the correct
   * height based on the line the marker in question is moving on.
   */
  _queryHalfLineHeight: function _queryHalfLineHeight(aMarker, aSelection) {
    let rects = aSelection.getRangeAt(0).getClientRects();
    if (!rects.length) {
      return 0;
    }

    // We are assuming here that these rects are ordered correctly.
    // From looking at the range code it appears they will be.
    let height = 0;
    if (aMarker == "start") {
      // height of the first rect corresponding to the start marker:
      height = rects[0].bottom - rects[0].top;
    } else {
      // height of the last rect corresponding to the end marker:
      let len = rects.length - 1;
      height = rects[len].bottom - rects[len].top;
    }
    return height / 2;
  },

  /*
   * _setContinuousSelection()
   *
   * Smooths a selection with multiple ranges into a single
   * continuous range.
   */
  _setContinuousSelection: function _setContinuousSelection() {
    let selection = this._getSelection();
    try {
      if (selection.rangeCount > 1) {
        let startRange = selection.getRangeAt(0);
        if (this. _debugOptions.displayRanges) {
          let clientRect = startRange.getBoundingClientRect();
          this._setDebugRect(clientRect, "red", false);
        }
        let newStartNode = null;
        let newStartOffset = 0;
        let newEndNode = null;
        let newEndOffset = 0;
        for (let idx = 1; idx < selection.rangeCount; idx++) {
          let range = selection.getRangeAt(idx);
          switch (startRange.compareBoundaryPoints(Ci.nsIDOMRange.START_TO_START, range)) {
            case -1: // startRange is before
              newStartNode = startRange.startContainer;
              newStartOffset = startRange.startOffset;
              break;
            case 0: // startRange is equal
              newStartNode = startRange.startContainer;
              newStartOffset = startRange.startOffset;
              break;
            case 1: // startRange is after
              newStartNode = range.startContainer;
              newStartOffset = range.startOffset;
              break;
          }
          switch (startRange.compareBoundaryPoints(Ci.nsIDOMRange.END_TO_END, range)) {
            case -1: // startRange is before
              newEndNode = range.endContainer;
              newEndOffset = range.endOffset;
              break;
            case 0: // startRange is equal
              newEndNode = range.endContainer;
              newEndOffset = range.endOffset;
              break;
            case 1: // startRange is after
              newEndNode = startRange.endContainer;
              newEndOffset = startRange.endOffset;
              break;
          }
          if (this. _debugOptions.displayRanges) {
            let clientRect = range.getBoundingClientRect();
            this._setDebugRect(clientRect, "orange", false);
          }
        }
        let range = content.document.createRange();
        range.setStart(newStartNode, newStartOffset);
        range.setEnd(newEndNode, newEndOffset);
        selection.addRange(range);
      }
    } catch (ex) {
      Util.dumpLn("exception while modifying selection:", ex.message);
      this._onFail("_handleSelectionPoint failed.");
      return false;
    }
    return true;
  },

  /*
   * _shrinkSelectionFromPoint(aMarker, aFramePoint)
   *
   * Tests to see if aFramePoint intersects the current selection and if so,
   * collapses selection down to the opposite start or end point leaving a
   * character of selection at the collapse point.
   *
   * @param aMarker the marker that is being relocated. ("start" or "end")
   * @param aFramePoint position of the marker.
   */
  _shrinkSelectionFromPoint: function _shrinkSelectionFromPoint(aMarker, aFramePoint) {
    let result = false;
    try {
      let selection = this._getSelection();
      for (let range = 0; range < selection.rangeCount; range++) {
        // relative to frame
        let rects = selection.getRangeAt(range).getClientRects();
        for (let idx = 0; idx < rects.length; idx++) {
          // Util.dumpLn("[" + idx + "]", aFramePoint.xPos, aFramePoint.yPos, rects[idx].left,
          // rects[idx].top, rects[idx].right, rects[idx].bottom);
          if (Util.pointWithinDOMRect(aFramePoint.xPos, aFramePoint.yPos, rects[idx])) {
            result = true;
            if (aMarker == "start") {
              selection.collapseToEnd();
            } else {
              selection.collapseToStart();
            }
            // collapseToStart and collapseToEnd leave an empty range in the
            // selection at the collapse point. Therefore we need to add some
            // selection such that the selection added by selectAtPoint and
            // the current empty range will get merged properly when we smooth
            // the selection ranges out.
            let selCtrl = this._getSelectController();
            // Expand the collapsed range such that it occupies a little space.
            if (aMarker == "start") {
              // State: focus = anchor (collapseToEnd does this)
              selCtrl.characterMove(false, true);
              // State: focus = (anchor - 1)
              selection.collapseToStart();
              // State: focus = anchor and both are -1 from the original offset
              selCtrl.characterMove(true, true);
              // State: focus = anchor + 1, both have been moved back one char
            } else {
              selCtrl.characterMove(true, true);
            }
            break;
          }
        }
      }
    } catch (ex) {
      Util.dumpLn("error shrinking selection:", ex.message);
    }
    return result;
  },

  /*
   * _updateUIMarkerRects(aSelection)
   *
   * Extracts the rects of the current selection, clips them to any text
   * input bounds, and stores them in the cache table we send over to
   * SelectionHelperUI.
   */
  _updateUIMarkerRects: function _updateUIMarkerRects(aSelection) {
    this._cache = this._extractUIRects(aSelection.getRangeAt(0));
    if (this. _debugOptions.dumpRanges)  {
       Util.dumpLn("start:", "(" + this._cache.start.xPos + "," +
                   this._cache.start.yPos + ")");
       Util.dumpLn("end:", "(" + this._cache.end.xPos + "," +
                   this._cache.end.yPos + ")");
       Util.dumpLn("caret:", "(" + this._cache.caret.xPos + "," +
                   this._cache.caret.yPos + ")");
    }
    this._restrictSelectionRectToEditBounds();
  },

  /*
   * _extractUIRects - Extracts selection and target element information
   * used by SelectionHelperUI. Returns client relative coordinates.
   *
   * @return table containing various ui rects and information
   */
  _extractUIRects: function _extractUIRects(aRange) {
    let seldata = {
      start: {}, end: {}, caret: {},
      selection: { left: 0, top: 0, right: 0, bottom: 0 },
      element: { left: 0, top: 0, right: 0, bottom: 0 }
    };

    // When in an iframe, aRange coordinates are relative to the frame origin.
    let rects = aRange.getClientRects();

    if (rects && rects.length) {
      let startSet = false;
      for (let idx = 0; idx < rects.length; idx++) {
        if (this. _debugOptions.dumpRanges) Util.dumpDOMRect(idx, rects[idx]);
        if (!startSet && !Util.isEmptyDOMRect(rects[idx])) {
          seldata.start.xPos = rects[idx].left + this._contentOffset.x;
          seldata.start.yPos = rects[idx].bottom + this._contentOffset.y;
          seldata.caret = seldata.start;
          startSet = true;
          if (this. _debugOptions.dumpRanges) Util.dumpLn("start set");
        }
        if (!Util.isEmptyDOMRect(rects[idx])) {
          seldata.end.xPos = rects[idx].right + this._contentOffset.x;
          seldata.end.yPos = rects[idx].bottom + this._contentOffset.y;
          if (this. _debugOptions.dumpRanges) Util.dumpLn("end set");
        }
      }

      // Store the client rect of selection
      let r = aRange.getBoundingClientRect();
      seldata.selection.left = r.left + this._contentOffset.x;
      seldata.selection.top = r.top + this._contentOffset.y;
      seldata.selection.right = r.right + this._contentOffset.x;
      seldata.selection.bottom = r.bottom + this._contentOffset.y;
    }

    // Store the client rect of target element
    r = this._getTargetClientRect();
    seldata.element.left = r.left + this._contentOffset.x;
    seldata.element.top = r.top + this._contentOffset.y;
    seldata.element.right = r.right + this._contentOffset.x;
    seldata.element.bottom = r.bottom + this._contentOffset.y;

    // If we don't have a range we can attach to let SelectionHelperUI know.
    seldata.selectionRangeFound = !!rects.length;

    return seldata;
  },

  /*
   * Selection bounds will fall outside the bound of a control if the control
   * can scroll. Clip UI cache data to the bounds of the target so monocles
   * don't draw outside the control.
   */
  _restrictSelectionRectToEditBounds: function _restrictSelectionRectToEditBounds() {
    if (!this._targetIsEditable)
      return;

    let bounds = this._getTargetBrowserRect();
    if (this._cache.start.xPos < bounds.left)
      this._cache.start.xPos = bounds.left;
    if (this._cache.end.xPos < bounds.left)
      this._cache.end.xPos = bounds.left;
    if (this._cache.caret.xPos < bounds.left)
      this._cache.caret.xPos = bounds.left;
    if (this._cache.start.xPos > bounds.right)
      this._cache.start.xPos = bounds.right;
    if (this._cache.end.xPos > bounds.right)
      this._cache.end.xPos = bounds.right;
    if (this._cache.caret.xPos > bounds.right)
      this._cache.caret.xPos = bounds.right;

    if (this._cache.start.yPos < bounds.top)
      this._cache.start.yPos = bounds.top;
    if (this._cache.end.yPos < bounds.top)
      this._cache.end.yPos = bounds.top;
    if (this._cache.caret.yPos < bounds.top)
      this._cache.caret.yPos = bounds.top;
    if (this._cache.start.yPos > bounds.bottom)
      this._cache.start.yPos = bounds.bottom;
    if (this._cache.end.yPos > bounds.bottom)
      this._cache.end.yPos = bounds.bottom;
    if (this._cache.caret.yPos > bounds.bottom)
      this._cache.caret.yPos = bounds.bottom;
  },

  _restrictCoordinateToEditBounds: function _restrictCoordinateToEditBounds(aX, aY) {
    let result = {
      xPos: aX,
      yPos: aY
    };
    if (!this._targetIsEditable)
      return result;
    let bounds = this._getTargetBrowserRect();
    if (aX <= bounds.left)
      result.xPos = bounds.left + 1;
    if (aX >= bounds.right)
      result.xPos = bounds.right - 1;
    if (aY <= bounds.top)
      result.yPos = bounds.top + 1;
    if (aY >= bounds.bottom)
      result.yPos = bounds.bottom - 1;
    return result;
  },

  /*************************************************
   * Utilities
   */

  /*
   * Returns bounds of the element relative to the inner sub frame it sits
   * in.
   */
  _getTargetClientRect: function _getTargetClientRect() {
    return this._targetElement.getBoundingClientRect();
  },

  /*
   * Returns bounds of the element relative to the top level browser.
   */
  _getTargetBrowserRect: function _getTargetBrowserRect() {
    let client = this._getTargetClientRect();
    return {
      left: client.left +  this._contentOffset.x,
      top: client.top +  this._contentOffset.y,
      right: client.right +  this._contentOffset.x,
      bottom: client.bottom +  this._contentOffset.y
    };
  },

  _clientPointToFramePoint: function _clientPointToFramePoint(aClientPoint) {
    let point = {
      xPos: aClientPoint.xPos - this._contentOffset.x,
      yPos: aClientPoint.yPos - this._contentOffset.y
    };
    return point;
  },

  /*************************************************
   * Debug routines
   */

  _debugDumpSelection: function _debugDumpSelection(aNote, aSel) {
    Util.dumpLn("--" + aNote + "--");
    Util.dumpLn("anchor:", aSel.anchorNode, aSel.anchorOffset);
    Util.dumpLn("focus:", aSel.focusNode, aSel.focusOffset);
  },

  _debugDumpChildNodes: function _dumpChildNodes(aNode, aSpacing) {
    for (let idx = 0; idx < aNode.childNodes.length; idx++) {
      let node = aNode.childNodes.item(idx);
      for (let spaceIdx = 0; spaceIdx < aSpacing; spaceIdx++) dump(" ");
      Util.dumpLn("[" + idx + "]", node);
      this._debugDumpChildNodes(node, aSpacing + 1);
    }
  },

  _setDebugElementRect: function _setDebugElementRect(e, aScrollOffset, aColor) {
    try {
      if (e == null) {
        Util.dumpLn("_setDebugElementRect(): passed in null element");
        return;
      }
      if (e.offsetWidth == 0 || e.offsetHeight== 0) {
        Util.dumpLn("_setDebugElementRect(): passed in flat rect");
        return;
      }
      // e.offset values are positioned relative to the view.
      this.sendAsync("Content:SelectionDebugRect",
        { left:e.offsetLeft - aScrollOffset.x,
          top:e.offsetTop - aScrollOffset.y,
          right:e.offsetLeft + e.offsetWidth - aScrollOffset.x,
          bottom:e.offsetTop + e.offsetHeight - aScrollOffset.y,
          color:aColor, id: e.id });
    } catch(ex) {
      Util.dumpLn("_setDebugElementRect():", ex.message);
    }
  },

  /*
   * Adds a debug rect to the selection overlay, useful in identifying
   * locations for points and rects. Params are in client coordinates.
   *
   * Example:
   * let rect = { left: aPoint.xPos - 1, top: aPoint.yPos - 1,
   *              right: aPoint.xPos + 1, bottom: aPoint.yPos + 1 };
   * this._setDebugRect(rect, "red");
   *
   * In SelectionHelperUI, you'll need to turn on displayDebugLayer
   * in init().
   */
  _setDebugRect: function _setDebugRect(aRect, aColor, aFill, aId) {
    this.sendAsync("Content:SelectionDebugRect",
      { left:aRect.left, top:aRect.top,
        right:aRect.right, bottom:aRect.bottom,
        color:aColor, fill: aFill, id: aId });
  },

  /*
   * Adds a small debug rect at the point specified. Params are in
   * client coordinates.
   *
   * In SelectionHelperUI, you'll need to turn on displayDebugLayer
   * in init().
   */
  _setDebugPoint: function _setDebugPoint(aX, aY, aColor) {
    let rect = { left: aX - 2, top: aY - 2,
                 right: aX + 2, bottom: aY + 2 };
    this._setDebugRect(rect, aColor, true);
  },
};

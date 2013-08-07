/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mats Palmgren <mats.palmgren@bredband.net>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#ifndef nsEventStateManager_h__
#define nsEventStateManager_h__

#include "nsEvent.h"
#include "nsGUIEvent.h"
#include "nsIContent.h"
#include "nsIObserver.h"
#include "nsWeakReference.h"
#include "nsHashtable.h"
#include "nsITimer.h"
#include "nsCOMPtr.h"
#include "nsIDocument.h"
#include "nsCOMArray.h"
#include "nsIFrameLoader.h"
#include "nsIFrame.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIMarkupDocumentViewer.h"
#include "nsIScrollableFrame.h"
#include "nsFocusManager.h"
#include "nsIDocument.h"
#include "nsEventStates.h"
#include "mozilla/TimeStamp.h"
#include "nsContentUtils.h"

class nsIPresShell;
class nsIDocShell;
class nsIDocShellTreeNode;
class nsIDocShellTreeItem;
class imgIContainer;
class nsDOMDataTransfer;
class MouseEnterLeaveDispatcher;

namespace mozilla {
namespace dom {
class TabParent;
}
}

/*
 * Event listener manager
 */

class nsEventStateManager : public nsSupportsWeakReference,
                            public nsIObserver
{
  friend class nsMouseWheelTransaction;
public:

  typedef mozilla::TimeStamp TimeStamp;
  typedef mozilla::TimeDuration TimeDuration;

  nsEventStateManager();
  virtual ~nsEventStateManager();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIOBSERVER

  nsresult Init();
  nsresult Shutdown();

  /* The PreHandleEvent method is called before event dispatch to either
   * the DOM or frames.  Any processing which must not be prevented or
   * cancelled should occur here.  Any processing which is intended to
   * be conditional based on either DOM or frame processing should occur in
   * PostHandleEvent.  Any centralized event processing which must occur before
   * DOM or frame event handling should occur here as well.
   */
  nsresult PreHandleEvent(nsPresContext* aPresContext,
                          nsEvent *aEvent,
                          nsIFrame* aTargetFrame,
                          nsEventStatus* aStatus);

  /* The PostHandleEvent method should contain all system processing which
   * should occur conditionally based on DOM or frame processing.  It should
   * also contain any centralized event processing which must occur after
   * DOM and frame processing.
   */
  nsresult PostHandleEvent(nsPresContext* aPresContext,
                           nsEvent *aEvent,
                           nsIFrame* aTargetFrame,
                           nsEventStatus* aStatus);

  void NotifyDestroyPresContext(nsPresContext* aPresContext);
  void SetPresContext(nsPresContext* aPresContext);
  void ClearFrameRefs(nsIFrame* aFrame);

  nsIFrame* GetEventTarget();
  already_AddRefed<nsIContent> GetEventTargetContent(nsEvent* aEvent);

  /**
   * Notify that the given NS_EVENT_STATE_* bit has changed for this content.
   * @param aContent Content which has changed states
   * @param aState   Corresponding state flags such as NS_EVENT_STATE_FOCUS
   * @return  Whether the content was able to change all states. Returns false
   *                  if a resulting DOM event causes the content node passed in
   *                  to not change states. Note, the frame for the content may
   *                  change as a result of the content state change, because of
   *                  frame reconstructions that may occur, but this does not
   *                  affect the return value.
   */
  bool SetContentState(nsIContent *aContent, nsEventStates aState);
  void ContentRemoved(nsIDocument* aDocument, nsIContent* aContent);
  bool EventStatusOK(nsGUIEvent* aEvent);

  /**
   * Register accesskey on the given element. When accesskey is activated then
   * the element will be notified via nsIContent::PerformAccesskey() method.
   *
   * @param  aContent  the given element
   * @param  aKey      accesskey
   */
  void RegisterAccessKey(nsIContent* aContent, PRUint32 aKey);

  /**
   * Unregister accesskey for the given element.
   *
   * @param  aContent  the given element
   * @param  aKey      accesskey
   */
  void UnregisterAccessKey(nsIContent* aContent, PRUint32 aKey);

  /**
   * Get accesskey registered on the given element or 0 if there is none.
   *
   * @param  aContent  the given element
   * @return           registered accesskey
   */
  PRUint32 GetRegisteredAccessKey(nsIContent* aContent);

  bool GetAccessKeyLabelPrefix(nsAString& aPrefix);

  nsresult SetCursor(PRInt32 aCursor, imgIContainer* aContainer,
                     bool aHaveHotspot, float aHotspotX, float aHotspotY,
                     nsIWidget* aWidget, bool aLockCursor); 

  static void StartHandlingUserInput()
  {
    ++sUserInputEventDepth;
    if (sUserInputEventDepth == 1) {
      sHandlingInputStart = TimeStamp::Now();
    }
  }

  static void StopHandlingUserInput()
  {
    --sUserInputEventDepth;
    if (sUserInputEventDepth == 0) {
      sHandlingInputStart = TimeStamp();
    }
  }

  static bool IsHandlingUserInput()
  {
    if (sUserInputEventDepth <= 0) {
      return false;
    }
    TimeDuration timeout = nsContentUtils::HandlingUserInputTimeout();
    return timeout <= TimeDuration(0) ||
           (TimeStamp::Now() - sHandlingInputStart) <= timeout;
  }

  /**
   * Returns true if the current code is being executed as a result of user input.
   * This includes timers or anything else that is initiated from user input.
   * However, mouse hover events are not counted as user input, nor are
   * page load events. If this method is called from asynchronously executed code,
   * such as during layout reflows, it will return false. If more time has elapsed
   * since the user input than is specified by the
   * dom.event.handling-user-input-time-limit pref (default 1 second), this
   * function also returns false.
   */
  NS_IMETHOD_(bool) IsHandlingUserInputExternal() { return IsHandlingUserInput(); }
  
  nsPresContext* GetPresContext() { return mPresContext; }

  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsEventStateManager,
                                           nsIObserver)

  static nsIDocument* sMouseOverDocument;

  static nsEventStateManager* GetActiveEventStateManager() { return sActiveESM; }

  // Sets aNewESM to be the active event state manager, and
  // if aContent is non-null, marks the object as active.
  static void SetActiveManager(nsEventStateManager* aNewESM,
                               nsIContent* aContent);

  // Sets the full-screen event state on aElement to aIsFullScreen.
  static void SetFullScreenState(mozilla::dom::Element* aElement, bool aIsFullScreen);

  static bool IsRemoteTarget(nsIContent* aTarget);

protected:
  friend class MouseEnterLeaveDispatcher;

  void UpdateCursor(nsPresContext* aPresContext, nsEvent* aEvent, nsIFrame* aTargetFrame, nsEventStatus* aStatus);
  /**
   * Turn a GUI mouse event into a mouse event targeted at the specified
   * content.  This returns the primary frame for the content (or null
   * if it goes away during the event).
   */
  nsIFrame* DispatchMouseEvent(nsGUIEvent* aEvent, PRUint32 aMessage,
                               nsIContent* aTargetContent,
                               nsIContent* aRelatedContent);
  /**
   * Synthesize DOM and frame mouseover and mouseout events from this
   * MOUSE_MOVE or MOUSE_EXIT event.
   */
  void GenerateMouseEnterExit(nsGUIEvent* aEvent);
  /**
   * Tell this ESM and ESMs in parent documents that the mouse is
   * over some content in this document.
   */
  void NotifyMouseOver(nsGUIEvent* aEvent, nsIContent* aContent);
  /**
   * Tell this ESM and ESMs in affected child documents that the mouse
   * has exited this document's currently hovered content.
   * @param aEvent the event that triggered the mouseout
   * @param aMovingInto the content node we've moved into.  This is used to set
   *        the relatedTarget for mouseout events.  Also, if it's non-null
   *        NotifyMouseOut will NOT change the current hover content to null;
   *        in that case the caller is responsible for updating hover state.
   */
  void NotifyMouseOut(nsGUIEvent* aEvent, nsIContent* aMovingInto);
  void GenerateDragDropEnterExit(nsPresContext* aPresContext, nsGUIEvent* aEvent);
  /**
   * Fire the dragenter and dragexit/dragleave events when the mouse moves to a
   * new target.
   *
   * @param aRelatedTarget relatedTarget to set for the event
   * @param aTargetContent target to set for the event
   * @param aTargetFrame target frame for the event
   */
  void FireDragEnterOrExit(nsPresContext* aPresContext,
                           nsGUIEvent* aEvent,
                           PRUint32 aMsg,
                           nsIContent* aRelatedTarget,
                           nsIContent* aTargetContent,
                           nsWeakFrame& aTargetFrame);
  /**
   * Update the initial drag session data transfer with any changes that occur
   * on cloned data transfer objects used for events.
   */
  void UpdateDragDataTransfer(nsDragEvent* dragEvent);

  nsresult SetClickCount(nsPresContext* aPresContext, nsMouseEvent *aEvent, nsEventStatus* aStatus);
  nsresult CheckForAndDispatchClick(nsPresContext* aPresContext, nsMouseEvent *aEvent, nsEventStatus* aStatus);
  void EnsureDocument(nsPresContext* aPresContext);
  void FlushPendingEvents(nsPresContext* aPresContext);

  /**
   * The phases of HandleAccessKey processing. See below.
   */
  typedef enum {
    eAccessKeyProcessingNormal = 0,
    eAccessKeyProcessingUp,
    eAccessKeyProcessingDown
  } ProcessingAccessKeyState;

  /**
   * Access key handling.  If there is registered content for the accesskey
   * given by the key event and modifier mask then call
   * content.PerformAccesskey(), otherwise call HandleAccessKey() recursively,
   * on descendant docshells first, then on the ancestor (with |aBubbledFrom|
   * set to the docshell associated with |this|), until something matches.
   *
   * @param aPresContext the presentation context
   * @param aEvent the key event
   * @param aStatus the event status
   * @param aBubbledFrom is used by an ancestor to avoid calling HandleAccessKey()
   *        on the child the call originally came from, i.e. this is the child
   *        that recursively called us in its Up phase. The initial caller
   *        passes |nsnull| here. This is to avoid an infinite loop.
   * @param aAccessKeyState Normal, Down or Up processing phase (see enums
   *        above). The initial event receiver uses 'normal', then 'down' when
   *        processing children and Up when recursively calling its ancestor.
   * @param aModifierMask modifier mask for the key event
   */
  void HandleAccessKey(nsPresContext* aPresContext,
                       nsKeyEvent* aEvent,
                       nsEventStatus* aStatus,
                       nsIDocShellTreeItem* aBubbledFrom,
                       ProcessingAccessKeyState aAccessKeyState,
                       PRInt32 aModifierMask);

  bool ExecuteAccessKey(nsTArray<PRUint32>& aAccessCharCodes,
                          bool aIsTrustedEvent);

  //---------------------------------------------
  // DocShell Focus Traversal Methods
  //---------------------------------------------

  nsIContent* GetFocusedContent();
  bool IsShellVisible(nsIDocShell* aShell);

  // These functions are for mousewheel and pixel scrolling
  void SendLineScrollEvent(nsIFrame* aTargetFrame,
                           nsMouseScrollEvent* aEvent,
                           nsPresContext* aPresContext,
                           nsEventStatus* aStatus,
                           PRInt32 aNumLines);
  void SendPixelScrollEvent(nsIFrame* aTargetFrame,
                            nsMouseScrollEvent* aEvent,
                            nsPresContext* aPresContext,
                            nsEventStatus* aStatus);
  /**
   * @param aQueryEvent If you set vailid pointer for this, DoScrollText()
   *                    computes the line-height and page size of current
   *                    mouse wheel scroll target and sets it to the event.
   *                    And then, this method does NOT scroll any scrollable
   *                    elements.  I.e., you can just query the scroll target
   *                    information.
   */
  nsresult DoScrollText(nsIFrame* aTargetFrame,
                        nsMouseScrollEvent* aMouseEvent,
                        nsIScrollableFrame::ScrollUnit aScrollQuantity,
                        bool aAllowScrollSpeedOverride,
                        nsQueryContentEvent* aQueryEvent = nsnull);
  void DoScrollHistory(PRInt32 direction);
  void DoScrollZoom(nsIFrame *aTargetFrame, PRInt32 adjustment);
  nsresult GetMarkupDocumentViewer(nsIMarkupDocumentViewer** aMv);
  nsresult ChangeTextSize(PRInt32 change);
  nsresult ChangeFullZoom(PRInt32 change);
  /**
   * Computes actual delta value used for scrolling.  If user customized the
   * scrolling speed and/or direction, this would return the customized value.
   * Otherwise, it would return the original delta value of aMouseEvent.
   */
  PRInt32 ComputeWheelDeltaFor(nsMouseScrollEvent* aMouseEvent);
  /**
   * Computes the action for the aMouseEvent with prefs.  The result is
   * MOUSE_SCROLL_N_LINES, MOUSE_SCROLL_PAGE, MOUSE_SCROLL_HISTORY,
   * MOUSE_SCROLL_ZOOM, MOUSE_SCROLL_PIXELS or -1.
   * When the result is -1, nothing happens for the event.
   *
   * @param aUseSystemSettings    Set the result of UseSystemScrollSettingFor().
   */
  PRInt32 ComputeWheelActionFor(nsMouseScrollEvent* aMouseEvent,
                                bool aUseSystemSettings);
  /**
   * Gets the wheel action for the aMouseEvent ONLY with the pref.
   * When you actually do something for the event, probably you should use
   * ComputeWheelActionFor().
   */
  PRInt32 GetWheelActionFor(nsMouseScrollEvent* aMouseEvent);
  /**
   * Gets the pref value for line scroll amount for the aMouseEvent.
   * Note that this method doesn't check whether the aMouseEvent is line scroll
   * event and doesn't use system settings.
   */
  PRInt32 GetScrollLinesFor(nsMouseScrollEvent* aMouseEvent);
  /**
   * Whether use system scroll settings or settings in our prefs for the event.
   * TRUE, if use system scroll settings.  Otherwise, FALSE.
   */
  bool UseSystemScrollSettingFor(nsMouseScrollEvent* aMouseEvent);
  // end mousewheel functions

  /*
   * When a touch gesture is about to start, this function determines what
   * kind of gesture interaction we will want to use, based on what is
   * underneath the initial touch point.
   * Currently it decides between panning (finger scrolling) or dragging
   * the target element, as well as the orientation to trigger panning and
   * display visual boundary feedback. The decision is stored back in aEvent.
   */
  void DecideGestureEvent(nsGestureNotifyEvent* aEvent, nsIFrame* targetFrame);

  // routines for the d&d gesture tracking state machine
  void BeginTrackingDragGesture ( nsPresContext* aPresContext, nsMouseEvent* inDownEvent,
                                  nsIFrame* inDownFrame ) ;
  void StopTrackingDragGesture ( ) ;
  void GenerateDragGesture ( nsPresContext* aPresContext, nsMouseEvent *aEvent ) ;

  /**
   * Determine which node the drag should be targeted at.
   * This is either the node clicked when there is a selection, or, for HTML,
   * the element with a draggable property set to true.
   *
   * aSelectionTarget - target to check for selection
   * aDataTransfer - data transfer object that will contain the data to drag
   * aIsSelection - [out] set to true if a selection is being dragged
   * aIsInEditor - [out] set to true if the content is in an editor field
   * aTargetNode - [out] the draggable node, or null if there isn't one
   */
  void DetermineDragTarget(nsPresContext* aPresContext,
                           nsIContent* aSelectionTarget,
                           nsDOMDataTransfer* aDataTransfer,
                           bool* aIsSelection,
                           bool* aIsInEditor,
                           nsIContent** aTargetNode);

  /*
   * Perform the default handling for the dragstart/draggesture event and set up a
   * drag for aDataTransfer if it contains any data. Returns true if a drag has
   * started.
   *
   * aDragEvent - the dragstart/draggesture event
   * aDataTransfer - the data transfer that holds the data to be dragged
   * aDragTarget - the target of the drag
   * aIsSelection - true if a selection is being dragged
   */
  bool DoDefaultDragStart(nsPresContext* aPresContext,
                            nsDragEvent* aDragEvent,
                            nsDOMDataTransfer* aDataTransfer,
                            nsIContent* aDragTarget,
                            bool aIsSelection);

  bool IsTrackingDragGesture ( ) const { return mGestureDownContent != nsnull; }
  /**
   * Set the fields of aEvent to reflect the mouse position and modifier keys
   * that were set when the user first pressed the mouse button (stored by
   * BeginTrackingDragGesture). aEvent->widget must be
   * mCurrentTarget->GetNearestWidget().
   */
  void FillInEventFromGestureDown(nsMouseEvent* aEvent);

  nsresult DoContentCommandEvent(nsContentCommandEvent* aEvent);
  nsresult DoContentCommandScrollEvent(nsContentCommandEvent* aEvent);

  void DoQueryScrollTargetInfo(nsQueryContentEvent* aEvent,
                               nsIFrame* aTargetFrame);
  void DoQuerySelectedText(nsQueryContentEvent* aEvent);

  bool RemoteQueryContentEvent(nsEvent *aEvent);
  mozilla::dom::TabParent *GetCrossProcessTarget();
  bool IsTargetCrossProcess(nsGUIEvent *aEvent);

  void DispatchCrossProcessEvent(nsEvent* aEvent, nsIFrameLoader* remote);
  bool HandleCrossProcessEvent(nsEvent *aEvent,
                                 nsIFrame* aTargetFrame,
                                 nsEventStatus *aStatus);

private:
  static inline void DoStateChange(mozilla::dom::Element* aElement,
                                   nsEventStates aState, bool aAddState);
  static inline void DoStateChange(nsIContent* aContent, nsEventStates aState,
                                   bool aAddState);
  static void UpdateAncestorState(nsIContent* aStartNode,
                                  nsIContent* aStopBefore,
                                  nsEventStates aState,
                                  bool aAddState);

  PRInt32     mLockCursor;

  nsWeakFrame mCurrentTarget;
  nsCOMPtr<nsIContent> mCurrentTargetContent;
  nsWeakFrame mLastMouseOverFrame;
  nsCOMPtr<nsIContent> mLastMouseOverElement;
  static nsWeakFrame sLastDragOverFrame;

  // member variables for the d&d gesture state machine
  nsIntPoint mGestureDownPoint; // screen coordinates
  // The content to use as target if we start a d&d (what we drag).
  nsCOMPtr<nsIContent> mGestureDownContent;
  // The content of the frame where the mouse-down event occurred. It's the same
  // as the target in most cases but not always - for example when dragging
  // an <area> of an image map this is the image. (bug 289667)
  nsCOMPtr<nsIContent> mGestureDownFrameOwner;
  // State of keys when the original gesture-down happened
  bool mGestureDownShift;
  bool mGestureDownControl;
  bool mGestureDownAlt;
  bool mGestureDownMeta;

  nsCOMPtr<nsIContent> mLastLeftMouseDownContent;
  nsCOMPtr<nsIContent> mLastLeftMouseDownContentParent;
  nsCOMPtr<nsIContent> mLastMiddleMouseDownContent;
  nsCOMPtr<nsIContent> mLastMiddleMouseDownContentParent;
  nsCOMPtr<nsIContent> mLastRightMouseDownContent;
  nsCOMPtr<nsIContent> mLastRightMouseDownContentParent;

  nsCOMPtr<nsIContent> mActiveContent;
  nsCOMPtr<nsIContent> mHoverContent;
  static nsCOMPtr<nsIContent> sDragOverContent;
  nsCOMPtr<nsIContent> mURLTargetContent;

  // The last element on which we fired a mouseover event, or null if
  // the last mouseover event we fired has finished processing.
  nsCOMPtr<nsIContent> mFirstMouseOverEventElement;

  // The last element on which we fired a mouseout event, or null if
  // the last mouseout event we fired has finished processing.
  nsCOMPtr<nsIContent> mFirstMouseOutEventElement;

  nsPresContext* mPresContext;      // Not refcnted
  nsCOMPtr<nsIDocument> mDocument;   // Doesn't necessarily need to be owner

  PRUint32 mLClickCount;
  PRUint32 mMClickCount;
  PRUint32 mRClickCount;

  bool m_haveShutdown;

  // Time at which we began handling user input.
  static TimeStamp sHandlingInputStart;

public:
  static nsresult UpdateUserActivityTimer(void);
  // Array for accesskey support
  nsCOMArray<nsIContent> mAccessKeys;

  // Unlocks pixel scrolling
  bool mLastLineScrollConsumedX;
  bool mLastLineScrollConsumedY;

  static PRInt32 sUserInputEventDepth;
  
  static bool sNormalLMouseEventInProcess;

  static nsEventStateManager* sActiveESM;
  
  static void ClearGlobalActiveContent(nsEventStateManager* aClearer);

  // Functions used for click hold context menus
  bool mClickHoldContextMenu;
  nsCOMPtr<nsITimer> mClickHoldTimer;
  void CreateClickHoldTimer ( nsPresContext* aPresContext, nsIFrame* inDownFrame,
                              nsGUIEvent* inMouseDownEvent ) ;
  void KillClickHoldTimer ( ) ;
  void FireContextClick ( ) ;
  static void sClickHoldCallback ( nsITimer* aTimer, void* aESM ) ;
};

/**
 * This class is used while processing real user input. During this time, popups
 * are allowed. For mousedown events, mouse capturing is also permitted.
 */
class nsAutoHandlingUserInputStatePusher
{
public:
  nsAutoHandlingUserInputStatePusher(bool aIsHandlingUserInput,
                                     nsEvent* aEvent,
                                     nsIDocument* aDocument)
    : mIsHandlingUserInput(aIsHandlingUserInput),
      mIsMouseDown(aEvent && aEvent->message == NS_MOUSE_BUTTON_DOWN),
      mResetFMMouseDownState(false)
  {
    if (aIsHandlingUserInput) {
      nsEventStateManager::StartHandlingUserInput();
      if (mIsMouseDown) {
        nsIPresShell::SetCapturingContent(nsnull, 0);
        nsIPresShell::AllowMouseCapture(true);
        if (aDocument && NS_IS_TRUSTED_EVENT(aEvent)) {
          nsFocusManager* fm = nsFocusManager::GetFocusManager();
          if (fm) {
            fm->SetMouseButtonDownHandlingDocument(aDocument);
            mResetFMMouseDownState = true;
          }
        }
      }
    }
  }

  ~nsAutoHandlingUserInputStatePusher()
  {
    if (mIsHandlingUserInput) {
      nsEventStateManager::StopHandlingUserInput();
      if (mIsMouseDown) {
        nsIPresShell::AllowMouseCapture(false);
        if (mResetFMMouseDownState) {
          nsFocusManager* fm = nsFocusManager::GetFocusManager();
          if (fm) {
            fm->SetMouseButtonDownHandlingDocument(nsnull);
          }
        }
      }
    }
  }

protected:
  bool mIsHandlingUserInput;
  bool mIsMouseDown;
  bool mResetFMMouseDownState;

private:
  // Hide so that this class can only be stack-allocated
  static void* operator new(size_t /*size*/) CPP_THROW_NEW { return nsnull; }
  static void operator delete(void* /*memory*/) {}
};

#define NS_EVENT_NEEDS_FRAME(event) (!NS_IS_ACTIVATION_EVENT(event))

#endif // nsEventStateManager_h__

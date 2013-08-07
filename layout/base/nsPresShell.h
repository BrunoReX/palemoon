/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=2 sw=2 et tw=78:
 * ***** BEGIN LICENSE BLOCK *****
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
 *   Steve Clark <buster@netscape.com>
 *   Håkan Waara <hwaara@chello.se>
 *   Dan Rosen <dr@netscape.com>
 *   Daniel Glazman <glazman@netscape.com>
 *   Mats Palmgren <matspal@gmail.com>
 *   Mihai Sucan <mihai.sucan@gmail.com>
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
 * ***** END LICENSE BLOCK *****
 *
 * This Original Code has been modified by IBM Corporation.
 * Modifications made by IBM described herein are
 * Copyright (c) International Business Machines
 * Corporation, 2000
 *
 * Modifications to Mozilla code or documentation
 * identified per MPL Section 3.3
 *
 * Date         Modified by     Description of modification
 * 05/03/2000   IBM Corp.       Observer events for reflow states
 */

/* a presentation of a document, part 2 */

#ifndef nsPresShell_h_
#define nsPresShell_h_

#include "nsIPresShell.h"
#include "nsIViewObserver.h"
#include "nsStubDocumentObserver.h"
#include "nsISelectionController.h"
#include "nsIObserver.h"
#include "nsWeakReference.h"
#include "nsCRT.h"
#include "nsAutoPtr.h"
#include "nsIWidget.h"
#include "nsStyleSet.h"
#include "nsPresArena.h"
#include "nsFrameSelection.h"
#include "nsGUIEvent.h"
#include "nsContentUtils.h"
#include "nsRefreshDriver.h"

class nsIRange;
class nsIDragService;
class nsCSSStyleSheet;

struct RangePaintInfo;
struct nsCallbackEventRequest;
#ifdef MOZ_REFLOW_PERF
class ReflowCountMgr;
#endif

#define STACK_ARENA_MARK_INCREMENT 50
/* a bit under 4096, for malloc overhead */
#define STACK_ARENA_BLOCK_INCREMENT 4044

/**A block of memory that the stack will 
 * chop up and hand out
 */
struct StackBlock {
   
   // a block of memory.  Note that this must be first so that it will
   // be aligned.
   char mBlock[STACK_ARENA_BLOCK_INCREMENT];

   // another block of memory that would only be created
   // if our stack overflowed. Yes we have the ability
   // to grow on a stack overflow
   StackBlock* mNext;

   StackBlock() : mNext(nsnull) { }
   ~StackBlock() { }
};

/* we hold an array of marks. A push pushes a mark on the stack
 * a pop pops it off.
 */
struct StackMark {
   // the block of memory we are currently handing out chunks of
   StackBlock* mBlock;
   
   // our current position in the memory
   size_t mPos;
};


/* A stack arena allows a stack based interface to a block of memory.
 * It should be used when you need to allocate some temporary memory that
 * you will immediately return.
 */
class StackArena {
public:
  StackArena();
  ~StackArena();

  nsresult Init() { return mBlocks ? NS_OK : NS_ERROR_OUT_OF_MEMORY; }

  // Memory management functions
  void* Allocate(size_t aSize);
  void Push();
  void Pop();

  PRUint32 Size() {
    PRUint32 result = 0;
    StackBlock *block = mBlocks;
    while (block) {
      result += sizeof(StackBlock);
      block = block->mNext;
    }
    return result;
  }

private:
  // our current position in memory
  size_t mPos;

  // a list of memory block. Usually there is only one
  // but if we overrun our stack size we can get more memory.
  StackBlock* mBlocks;

  // the current block of memory we are passing our chucks of
  StackBlock* mCurBlock;

  // our stack of mark where push has been called
  StackMark* mMarks;

  // the current top of the mark list
  PRUint32 mStackTop;

  // the size of the mark array
  PRUint32 mMarkLength;
};

class nsPresShellEventCB;
class nsAutoCauseReflowNotifier;

class PresShell : public nsIPresShell, public nsIViewObserver,
                  public nsStubDocumentObserver,
                  public nsISelectionController, public nsIObserver,
                  public nsSupportsWeakReference
{
public:
  PresShell();

  NS_DECL_AND_IMPL_ZEROING_OPERATOR_NEW

  // nsISupports
  NS_DECL_ISUPPORTS

  // nsIPresShell
  virtual NS_HIDDEN_(nsresult) Init(nsIDocument* aDocument,
                                   nsPresContext* aPresContext,
                                   nsIViewManager* aViewManager,
                                   nsStyleSet* aStyleSet,
                                   nsCompatibility aCompatMode);
  virtual NS_HIDDEN_(void) Destroy();

  virtual NS_HIDDEN_(void*) AllocateFrame(nsQueryFrame::FrameIID aCode,
                                          size_t aSize);
  virtual NS_HIDDEN_(void)  FreeFrame(nsQueryFrame::FrameIID aCode,
                                      void* aChunk);

  virtual NS_HIDDEN_(void*) AllocateMisc(size_t aSize);
  virtual NS_HIDDEN_(void)  FreeMisc(size_t aSize, void* aChunk);

  // Dynamic stack memory allocation
  virtual NS_HIDDEN_(void) PushStackMemory();
  virtual NS_HIDDEN_(void) PopStackMemory();
  virtual NS_HIDDEN_(void*) AllocateStackMemory(size_t aSize);

  virtual NS_HIDDEN_(nsresult) SetPreferenceStyleRules(PRBool aForceReflow);

  NS_IMETHOD GetSelection(SelectionType aType, nsISelection** aSelection);
  virtual nsISelection* GetCurrentSelection(SelectionType aType);

  NS_IMETHOD SetDisplaySelection(PRInt16 aToggle);
  NS_IMETHOD GetDisplaySelection(PRInt16 *aToggle);
  NS_IMETHOD ScrollSelectionIntoView(SelectionType aType, SelectionRegion aRegion,
                                     PRInt16 aFlags);
  NS_IMETHOD RepaintSelection(SelectionType aType);

  virtual NS_HIDDEN_(void) BeginObservingDocument();
  virtual NS_HIDDEN_(void) EndObservingDocument();
  virtual NS_HIDDEN_(nsresult) InitialReflow(nscoord aWidth, nscoord aHeight);
  virtual NS_HIDDEN_(nsresult) ResizeReflow(nscoord aWidth, nscoord aHeight);
  virtual NS_HIDDEN_(nsresult) ResizeReflowOverride(nscoord aWidth, nscoord aHeight);
  virtual NS_HIDDEN_(void) StyleChangeReflow();
  virtual NS_HIDDEN_(nsIPageSequenceFrame*) GetPageSequenceFrame() const;
  virtual NS_HIDDEN_(nsIFrame*) GetRealPrimaryFrameFor(nsIContent* aContent) const;

  virtual NS_HIDDEN_(nsIFrame*) GetPlaceholderFrameFor(nsIFrame* aFrame) const;
  virtual NS_HIDDEN_(void) FrameNeedsReflow(nsIFrame *aFrame, IntrinsicDirty aIntrinsicDirty,
                                            nsFrameState aBitToAdd);
  virtual NS_HIDDEN_(void) FrameNeedsToContinueReflow(nsIFrame *aFrame);
  virtual NS_HIDDEN_(void) CancelAllPendingReflows();
  virtual NS_HIDDEN_(PRBool) IsSafeToFlush() const;
  virtual NS_HIDDEN_(void) FlushPendingNotifications(mozFlushType aType);

  /**
   * Recreates the frames for a node
   */
  virtual NS_HIDDEN_(nsresult) RecreateFramesFor(nsIContent* aContent);

  /**
   * Post a callback that should be handled after reflow has finished.
   */
  virtual NS_HIDDEN_(nsresult) PostReflowCallback(nsIReflowCallback* aCallback);
  virtual NS_HIDDEN_(void) CancelReflowCallback(nsIReflowCallback* aCallback);

  virtual NS_HIDDEN_(void) ClearFrameRefs(nsIFrame* aFrame);
  virtual NS_HIDDEN_(already_AddRefed<nsRenderingContext>) GetReferenceRenderingContext();
  virtual NS_HIDDEN_(nsresult) GoToAnchor(const nsAString& aAnchorName, PRBool aScroll);
  virtual NS_HIDDEN_(nsresult) ScrollToAnchor();

  virtual NS_HIDDEN_(nsresult) ScrollContentIntoView(nsIContent* aContent,
                                                     PRIntn      aVPercent,
                                                     PRIntn      aHPercent,
                                                     PRUint32    aFlags);
  virtual PRBool ScrollFrameRectIntoView(nsIFrame*     aFrame,
                                         const nsRect& aRect,
                                         PRIntn        aVPercent,
                                         PRIntn        aHPercent,
                                         PRUint32      aFlags);
  virtual nsRectVisibility GetRectVisibility(nsIFrame *aFrame,
                                             const nsRect &aRect,
                                             nscoord aMinTwips) const;

  virtual NS_HIDDEN_(void) SetIgnoreFrameDestruction(PRBool aIgnore);
  virtual NS_HIDDEN_(void) NotifyDestroyingFrame(nsIFrame* aFrame);

  virtual NS_HIDDEN_(nsresult) GetLinkLocation(nsIDOMNode* aNode, nsAString& aLocationString) const;

  virtual NS_HIDDEN_(nsresult) CaptureHistoryState(nsILayoutHistoryState** aLayoutHistoryState, PRBool aLeavingPage);

  virtual NS_HIDDEN_(void) UnsuppressPainting();

  virtual nsresult GetAgentStyleSheets(nsCOMArray<nsIStyleSheet>& aSheets);
  virtual nsresult SetAgentStyleSheets(const nsCOMArray<nsIStyleSheet>& aSheets);

  virtual nsresult AddOverrideStyleSheet(nsIStyleSheet *aSheet);
  virtual nsresult RemoveOverrideStyleSheet(nsIStyleSheet *aSheet);

  virtual NS_HIDDEN_(nsresult) HandleEventWithTarget(nsEvent* aEvent, nsIFrame* aFrame,
                                                     nsIContent* aContent,
                                                     nsEventStatus* aStatus);
  virtual NS_HIDDEN_(nsIFrame*) GetEventTargetFrame();
  virtual NS_HIDDEN_(already_AddRefed<nsIContent>) GetEventTargetContent(nsEvent* aEvent);


  virtual nsresult ReconstructFrames(void);
  virtual void Freeze();
  virtual void Thaw();
  virtual void FireOrClearDelayedEvents(PRBool aFireEvents);

  virtual nsIFrame* GetFrameForPoint(nsIFrame* aFrame, nsPoint aPt);

  virtual NS_HIDDEN_(nsresult) RenderDocument(const nsRect& aRect, PRUint32 aFlags,
                                              nscolor aBackgroundColor,
                                              gfxContext* aThebesContext);

  virtual already_AddRefed<gfxASurface> RenderNode(nsIDOMNode* aNode,
                                                   nsIntRegion* aRegion,
                                                   nsIntPoint& aPoint,
                                                   nsIntRect* aScreenRect);

  virtual already_AddRefed<gfxASurface> RenderSelection(nsISelection* aSelection,
                                                        nsIntPoint& aPoint,
                                                        nsIntRect* aScreenRect);

  virtual already_AddRefed<nsPIDOMWindow> GetRootWindow();

  virtual LayerManager* GetLayerManager();

  virtual void SetIgnoreViewportScrolling(PRBool aIgnore);

  virtual void SetDisplayPort(const nsRect& aDisplayPort);

  virtual nsresult SetResolution(float aXResolution, float aYResolution);

  //nsIViewObserver interface

  NS_IMETHOD Paint(nsIView* aViewToPaint,
                   nsIWidget* aWidget,
                   const nsRegion& aDirtyRegion,
                   const nsIntRegion& aIntDirtyRegion,
                   PRBool aPaintDefaultBackground,
                   PRBool aWillSendDidPaint);
  NS_IMETHOD HandleEvent(nsIView*        aView,
                         nsGUIEvent*     aEvent,
                         PRBool          aDontRetargetEvents,
                         nsEventStatus*  aEventStatus);
  virtual NS_HIDDEN_(nsresult) HandleDOMEventWithTarget(nsIContent* aTargetContent,
                                                        nsEvent* aEvent,
                                                        nsEventStatus* aStatus);
  virtual NS_HIDDEN_(nsresult) HandleDOMEventWithTarget(nsIContent* aTargetContent,
                                                        nsIDOMEvent* aEvent,
                                                        nsEventStatus* aStatus);
  NS_IMETHOD ResizeReflow(nsIView *aView, nscoord aWidth, nscoord aHeight);
  NS_IMETHOD_(PRBool) ShouldIgnoreInvalidation();
  NS_IMETHOD_(void) WillPaint(PRBool aWillSendDidPaint);
  NS_IMETHOD_(void) DidPaint();
  NS_IMETHOD_(void) DispatchSynthMouseMove(nsGUIEvent *aEvent,
                                           PRBool aFlushOnHoverChange);
  NS_IMETHOD_(void) ClearMouseCapture(nsIView* aView);

  // caret handling
  virtual NS_HIDDEN_(already_AddRefed<nsCaret>) GetCaret() const;
  virtual NS_HIDDEN_(void) MaybeInvalidateCaretPosition();
  NS_IMETHOD SetCaretEnabled(PRBool aInEnable);
  NS_IMETHOD SetCaretReadOnly(PRBool aReadOnly);
  NS_IMETHOD GetCaretEnabled(PRBool *aOutEnabled);
  NS_IMETHOD SetCaretVisibilityDuringSelection(PRBool aVisibility);
  NS_IMETHOD GetCaretVisible(PRBool *_retval);
  virtual void SetCaret(nsCaret *aNewCaret);
  virtual void RestoreCaret();

  NS_IMETHOD SetSelectionFlags(PRInt16 aInEnable);
  NS_IMETHOD GetSelectionFlags(PRInt16 *aOutEnable);

  // nsISelectionController

  NS_IMETHOD CharacterMove(PRBool aForward, PRBool aExtend);
  NS_IMETHOD CharacterExtendForDelete();
  NS_IMETHOD CharacterExtendForBackspace();
  NS_IMETHOD WordMove(PRBool aForward, PRBool aExtend);
  NS_IMETHOD WordExtendForDelete(PRBool aForward);
  NS_IMETHOD LineMove(PRBool aForward, PRBool aExtend);
  NS_IMETHOD IntraLineMove(PRBool aForward, PRBool aExtend);
  NS_IMETHOD PageMove(PRBool aForward, PRBool aExtend);
  NS_IMETHOD ScrollPage(PRBool aForward);
  NS_IMETHOD ScrollLine(PRBool aForward);
  NS_IMETHOD ScrollHorizontal(PRBool aLeft);
  NS_IMETHOD CompleteScroll(PRBool aForward);
  NS_IMETHOD CompleteMove(PRBool aForward, PRBool aExtend);
  NS_IMETHOD SelectAll();
  NS_IMETHOD CheckVisibility(nsIDOMNode *node, PRInt16 startOffset, PRInt16 EndOffset, PRBool *_retval);

  // nsIDocumentObserver
  NS_DECL_NSIDOCUMENTOBSERVER_BEGINUPDATE
  NS_DECL_NSIDOCUMENTOBSERVER_ENDUPDATE
  NS_DECL_NSIDOCUMENTOBSERVER_BEGINLOAD
  NS_DECL_NSIDOCUMENTOBSERVER_ENDLOAD
  NS_DECL_NSIDOCUMENTOBSERVER_CONTENTSTATECHANGED
  NS_DECL_NSIDOCUMENTOBSERVER_DOCUMENTSTATESCHANGED
  NS_DECL_NSIDOCUMENTOBSERVER_STYLESHEETADDED
  NS_DECL_NSIDOCUMENTOBSERVER_STYLESHEETREMOVED
  NS_DECL_NSIDOCUMENTOBSERVER_STYLESHEETAPPLICABLESTATECHANGED
  NS_DECL_NSIDOCUMENTOBSERVER_STYLERULECHANGED
  NS_DECL_NSIDOCUMENTOBSERVER_STYLERULEADDED
  NS_DECL_NSIDOCUMENTOBSERVER_STYLERULEREMOVED

  // nsIMutationObserver
  NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATACHANGED
  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTEWILLCHANGE
  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED

  NS_DECL_NSIOBSERVER

#ifdef MOZ_REFLOW_PERF
  virtual NS_HIDDEN_(void) DumpReflows();
  virtual NS_HIDDEN_(void) CountReflows(const char * aName, nsIFrame * aFrame);
  virtual NS_HIDDEN_(void) PaintCount(const char * aName,
                                      nsRenderingContext* aRenderingContext,
                                      nsPresContext* aPresContext,
                                      nsIFrame * aFrame,
                                      const nsPoint& aOffset,
                                      PRUint32 aColor);
  virtual NS_HIDDEN_(void) SetPaintFrameCount(PRBool aOn);
  virtual PRBool IsPaintingFrameCounts();
#endif

#ifdef DEBUG
  virtual void ListStyleContexts(nsIFrame *aRootFrame, FILE *out,
                                 PRInt32 aIndent = 0);

  virtual void ListStyleSheets(FILE *out, PRInt32 aIndent = 0);
  virtual void VerifyStyleTree();
#endif

#ifdef PR_LOGGING
  static PRLogModuleInfo* gLog;
#endif

  virtual NS_HIDDEN_(void) DisableNonTestMouseEvents(PRBool aDisable);

  virtual void UpdateCanvasBackground();

  virtual nsresult AddCanvasBackgroundColorItem(nsDisplayListBuilder& aBuilder,
                                                nsDisplayList& aList,
                                                nsIFrame* aFrame,
                                                const nsRect& aBounds,
                                                nscolor aBackstopColor,
                                                PRUint32 aFlags);

  virtual nsresult AddPrintPreviewBackgroundItem(nsDisplayListBuilder& aBuilder,
                                                 nsDisplayList& aList,
                                                 nsIFrame* aFrame,
                                                 const nsRect& aBounds);

  virtual nscolor ComputeBackstopColor(nsIView* aDisplayRoot);

  virtual NS_HIDDEN_(nsresult) SetIsActive(PRBool aIsActive);

  virtual PRBool GetIsViewportOverridden() { return mViewportOverridden; }

  virtual PRBool IsLayoutFlushObserver()
  {
    return GetPresContext()->RefreshDriver()->
      IsLayoutFlushObserver(this);
  }

protected:
  virtual ~PresShell();

  void HandlePostedReflowCallbacks(PRBool aInterruptible);
  void CancelPostedReflowCallbacks();

  void UnsuppressAndInvalidate();

  void WillCauseReflow() {
    nsContentUtils::AddScriptBlocker();
    ++mChangeNestCount;
  }
  nsresult DidCauseReflow();
  friend class nsAutoCauseReflowNotifier;

  void     WillDoReflow();
  void     DidDoReflow(PRBool aInterruptible);
  // ProcessReflowCommands returns whether we processed all our dirty roots
  // without interruptions.
  PRBool   ProcessReflowCommands(PRBool aInterruptible);
  // MaybeScheduleReflow checks if posting a reflow is needed, then checks if
  // the last reflow was interrupted. In the interrupted case ScheduleReflow is
  // called off a timer, otherwise it is called directly.
  void     MaybeScheduleReflow();
  // Actually schedules a reflow.  This should only be called by
  // MaybeScheduleReflow and the reflow timer ScheduleReflowOffTimer
  // sets up.
  void     ScheduleReflow();

  // Reflow regardless of whether the override bit has been set.
  nsresult ResizeReflowIgnoreOverride(nscoord aWidth, nscoord aHeight);

  // DoReflow returns whether the reflow finished without interruption
  PRBool DoReflow(nsIFrame* aFrame, PRBool aInterruptible);
#ifdef DEBUG
  void DoVerifyReflow();
  void VerifyHasDirtyRootAncestor(nsIFrame* aFrame);
#endif

  // Helper for ScrollContentIntoView
  void DoScrollContentIntoView(nsIContent* aContent,
                               PRIntn      aVPercent,
                               PRIntn      aHPercent,
                               PRUint32    aFlags);

  friend struct AutoRenderingStateSaveRestore;
  friend struct RenderingState;

  struct RenderingState {
    RenderingState(PresShell* aPresShell) 
      : mRenderFlags(aPresShell->mRenderFlags)
      , mXResolution(aPresShell->mXResolution)
      , mYResolution(aPresShell->mYResolution)
    { }
    PRUint32 mRenderFlags;
    float mXResolution;
    float mYResolution;
  };

  struct AutoSaveRestoreRenderingState {
    AutoSaveRestoreRenderingState(PresShell* aPresShell)
      : mPresShell(aPresShell)
      , mOldState(aPresShell)
    {}

    ~AutoSaveRestoreRenderingState()
    {
      mPresShell->mRenderFlags = mOldState.mRenderFlags;
      mPresShell->mXResolution = mOldState.mXResolution;
      mPresShell->mYResolution = mOldState.mYResolution;
    }

    PresShell* mPresShell;
    RenderingState mOldState;
  };

  void SetRenderingState(const RenderingState& aState);

  friend class nsPresShellEventCB;

  PRBool mCaretEnabled;
#ifdef NS_DEBUG
  nsStyleSet* CloneStyleSet(nsStyleSet* aSet);
  PRBool VerifyIncrementalReflow();
  PRBool mInVerifyReflow;
  void ShowEventTargetDebug();
#endif

    /**
    * methods that manage rules that are used to implement the associated preferences
    *  - initially created for bugs 31816, 20760, 22963
    */
  nsresult ClearPreferenceStyleRules(void);
  nsresult CreatePreferenceStyleSheet(void);
  nsresult SetPrefLinkRules(void);
  nsresult SetPrefFocusRules(void);
  nsresult SetPrefNoScriptRule();
  nsresult SetPrefNoFramesRule(void);

  // methods for painting a range to an offscreen buffer

  // given a display list, clip the items within the list to
  // the range
  nsRect ClipListToRange(nsDisplayListBuilder *aBuilder,
                         nsDisplayList* aList,
                         nsIRange* aRange);

  // create a RangePaintInfo for the range aRange containing the
  // display list needed to paint the range to a surface
  RangePaintInfo* CreateRangePaintInfo(nsIDOMRange* aRange,
                                       nsRect& aSurfaceRect,
                                       PRBool aForPrimarySelection);

  /*
   * Paint the items to a new surface and return it.
   *
   * aSelection - selection being painted, if any
   * aRegion - clip region, if any
   * aArea - area that the surface occupies, relative to the root frame
   * aPoint - reference point, typically the mouse position
   * aScreenRect - [out] set to the area of the screen the painted area should
   *               be displayed at
   */
  already_AddRefed<gfxASurface>
  PaintRangePaintInfo(nsTArray<nsAutoPtr<RangePaintInfo> >* aItems,
                      nsISelection* aSelection,
                      nsIntRegion* aRegion,
                      nsRect aArea,
                      nsIntPoint& aPoint,
                      nsIntRect* aScreenRect);

  /**
   * Methods to handle changes to user and UA sheet lists that we get
   * notified about.
   */
  void AddUserSheet(nsISupports* aSheet);
  void AddAgentSheet(nsISupports* aSheet);
  void RemoveSheet(nsStyleSet::sheetType aType, nsISupports* aSheet);

  // Hide a view if it is a popup
  void HideViewIfPopup(nsIView* aView);

  // Utility method to restore the root scrollframe state
  void RestoreRootScrollPosition();

  void MaybeReleaseCapturingContent()
  {
    nsRefPtr<nsFrameSelection> frameSelection = FrameSelection();
    if (frameSelection) {
      frameSelection->SetMouseDownState(PR_FALSE);
    }
    if (gCaptureInfo.mContent &&
        gCaptureInfo.mContent->GetOwnerDoc() == mDocument) {
      SetCapturingContent(nsnull, 0);
    }
  }

  nsresult HandleRetargetedEvent(nsEvent* aEvent, nsIView* aView,
                                 nsEventStatus* aStatus, nsIContent* aTarget)
  {
    PushCurrentEventInfo(nsnull, nsnull);
    mCurrentEventContent = aTarget;
    nsresult rv = NS_OK;
    if (GetCurrentEventFrame()) {
      rv = HandleEventInternal(aEvent, aView, aStatus);
    }
    PopCurrentEventInfo();
    return rv;
  }

  nsRefPtr<nsCSSStyleSheet> mPrefStyleSheet; // mStyleSet owns it but we
                                             // maintain a ref, may be null
#ifdef DEBUG
  PRUint32                  mUpdateCount;
#endif
  // reflow roots that need to be reflowed, as both a queue and a hashtable
  nsTArray<nsIFrame*> mDirtyRoots;

  PRPackedBool mDocumentLoading;

  PRPackedBool mIgnoreFrameDestruction;
  PRPackedBool mHaveShutDown;

  PRPackedBool mViewportOverridden;

  PRPackedBool mLastRootReflowHadUnconstrainedHeight;

  // This is used to protect ourselves from triggering reflow while in the
  // middle of frame construction and the like... it really shouldn't be
  // needed, one hopes, but it is for now.
  PRUint32  mChangeNestCount;
  
  nsIFrame*   mCurrentEventFrame;
  nsCOMPtr<nsIContent> mCurrentEventContent;
  nsTArray<nsIFrame*> mCurrentEventFrameStack;
  nsCOMArray<nsIContent> mCurrentEventContentStack;

  nsCOMPtr<nsIContent>          mLastAnchorScrolledTo;
  nscoord                       mLastAnchorScrollPositionY;
  nsRefPtr<nsCaret>             mCaret;
  nsRefPtr<nsCaret>             mOriginalCaret;
  nsPresArena                   mFrameArena;
  StackArena                    mStackArena;
  nsCOMPtr<nsIDragService>      mDragService;
  
#ifdef DEBUG
  // The reflow root under which we're currently reflowing.  Null when
  // not in reflow.
  nsIFrame* mCurrentReflowRoot;
#endif

  // Set of frames that we should mark with NS_FRAME_HAS_DIRTY_CHILDREN after
  // we finish reflowing mCurrentReflowRoot.
  nsTHashtable< nsPtrHashKey<nsIFrame> > mFramesToDirty;

  // Information needed to properly handle scrolling content into view if the
  // pre-scroll reflow flush can be interrupted.  mContentToScrollTo is
  // non-null between the initial scroll attempt and the first time we finish
  // processing all our dirty roots.  mContentScrollVPosition and
  // mContentScrollHPosition are only used when it's non-null.
  nsCOMPtr<nsIContent> mContentToScrollTo;
  PRIntn mContentScrollVPosition;
  PRIntn mContentScrollHPosition;
  PRUint32 mContentToScrollToFlags;

  class nsDelayedEvent
  {
  public:
    virtual ~nsDelayedEvent() {};
    virtual void Dispatch(PresShell* aShell) {}
  };

  class nsDelayedInputEvent : public nsDelayedEvent
  {
  public:
    virtual void Dispatch(PresShell* aShell)
    {
      if (mEvent && mEvent->widget) {
        nsCOMPtr<nsIWidget> w = mEvent->widget;
        nsEventStatus status;
        w->DispatchEvent(mEvent, status);
      }
    }

  protected:
    void Init(nsInputEvent* aEvent)
    {
      mEvent->time = aEvent->time;
      mEvent->refPoint = aEvent->refPoint;
      mEvent->isShift = aEvent->isShift;
      mEvent->isControl = aEvent->isControl;
      mEvent->isAlt = aEvent->isAlt;
      mEvent->isMeta = aEvent->isMeta;
    }

    nsDelayedInputEvent()
    : nsDelayedEvent(), mEvent(nsnull) {}

    nsInputEvent* mEvent;
  };

  class nsDelayedMouseEvent : public nsDelayedInputEvent
  {
  public:
    nsDelayedMouseEvent(nsMouseEvent* aEvent) : nsDelayedInputEvent()
    {
      mEvent = new nsMouseEvent(NS_IS_TRUSTED_EVENT(aEvent),
                                aEvent->message,
                                aEvent->widget,
                                aEvent->reason,
                                aEvent->context);
      Init(aEvent);
      static_cast<nsMouseEvent*>(mEvent)->clickCount = aEvent->clickCount;
    }

    virtual ~nsDelayedMouseEvent()
    {
      delete static_cast<nsMouseEvent*>(mEvent);
    }
  };

  class nsDelayedKeyEvent : public nsDelayedInputEvent
  {
  public:
    nsDelayedKeyEvent(nsKeyEvent* aEvent) : nsDelayedInputEvent()
    {
      mEvent = new nsKeyEvent(NS_IS_TRUSTED_EVENT(aEvent),
                              aEvent->message,
                              aEvent->widget);
      Init(aEvent);
      static_cast<nsKeyEvent*>(mEvent)->keyCode = aEvent->keyCode;
      static_cast<nsKeyEvent*>(mEvent)->charCode = aEvent->charCode;
      static_cast<nsKeyEvent*>(mEvent)->alternativeCharCodes =
        aEvent->alternativeCharCodes;
      static_cast<nsKeyEvent*>(mEvent)->isChar = aEvent->isChar;
    }

    virtual ~nsDelayedKeyEvent()
    {
      delete static_cast<nsKeyEvent*>(mEvent);
    }
  };

  PRPackedBool                         mNoDelayedMouseEvents;
  PRPackedBool                         mNoDelayedKeyEvents;
  nsTArray<nsAutoPtr<nsDelayedEvent> > mDelayedEvents;

  nsCallbackEventRequest* mFirstCallbackEventRequest;
  nsCallbackEventRequest* mLastCallbackEventRequest;

  PRPackedBool      mIsDocumentGone;      // We've been disconnected from the document.
                                          // We will refuse to paint the document until either
                                          // (a) our timer fires or (b) all frames are constructed.
  PRPackedBool      mShouldUnsuppressPainting;  // Indicates that it is safe to unlock painting once all pending
                                                // reflows have been processed.
  nsCOMPtr<nsITimer> mPaintSuppressionTimer; // This timer controls painting suppression.  Until it fires
                                             // or all frames are constructed, we won't paint anything but
                                             // our <body> background and scrollbars.
#define PAINTLOCK_EVENT_DELAY 250 // 250ms.  This is actually
                                  // pref-controlled, but we use this
                                  // value if we fail to get the pref
                                  // for any reason.

  static void sPaintSuppressionCallback(nsITimer* aTimer, void* aPresShell); // A callback for the timer.

  // At least on Win32 and Mac after interupting a reflow we need to post
  // the resume reflow event off a timer to avoid event starvation because
  // posted messages are processed before other messages when the modal
  // moving/sizing loop is running, see bug 491700 for details.
  nsCOMPtr<nsITimer> mReflowContinueTimer;
  static void sReflowContinueCallback(nsITimer* aTimer, void* aPresShell);
  PRBool ScheduleReflowOffTimer();
  
#ifdef MOZ_REFLOW_PERF
  ReflowCountMgr * mReflowCountMgr;
#endif

  static PRBool sDisableNonTestMouseEvents;

private:

  PRBool InZombieDocument(nsIContent *aContent);
  already_AddRefed<nsIPresShell> GetParentPresShell();
  nsresult RetargetEventToParent(nsGUIEvent* aEvent,
                                 nsEventStatus*  aEventStatus);

  //helper funcs for event handling
protected:
  //protected because nsPresShellEventCB needs this.
  nsIFrame* GetCurrentEventFrame();
private:
  void PushCurrentEventInfo(nsIFrame* aFrame, nsIContent* aContent);
  void PopCurrentEventInfo();
  nsresult HandleEventInternal(nsEvent* aEvent, nsIView* aView,
                               nsEventStatus *aStatus);
  nsresult HandlePositionedEvent(nsIView*       aView,
                                 nsIFrame*      aTargetFrame,
                                 nsGUIEvent*    aEvent,
                                 nsEventStatus* aEventStatus);
  // This returns the focused DOM window under our top level window.
  //  I.e., when we are deactive, this returns the *last* focused DOM window.
  already_AddRefed<nsPIDOMWindow> GetFocusedDOMWindowInOurWindow();

  /*
   * This and the next two helper methods are used to target and position the
   * context menu when the keyboard shortcut is used to open it.
   *
   * If another menu is open, the context menu is opened relative to the
   * active menuitem within the menu, or the menu itself if no item is active.
   * Otherwise, if the caret is visible, the menu is opened near the caret.
   * Otherwise, if a selectable list such as a listbox is focused, the
   * current item within the menu is opened relative to this item.
   * Otherwise, the context menu is opened at the topleft corner of the
   * view.
   *
   * Returns true if the context menu event should fire and false if it should
   * not.
   */
  PRBool AdjustContextMenuKeyEvent(nsMouseEvent* aEvent);

  // 
  PRBool PrepareToUseCaretPosition(nsIWidget* aEventWidget, nsIntPoint& aTargetPt);

  // Get the selected item and coordinates in device pixels relative to root
  // document's root view for element, first ensuring the element is onscreen
  void GetCurrentItemAndPositionForElement(nsIDOMElement *aCurrentEl,
                                           nsIContent **aTargetToUse,
                                           nsIntPoint& aTargetPt,
                                           nsIWidget *aRootWidget);

  void FireResizeEvent();
  void FireBeforeResizeEvent();
  static void AsyncResizeEventCallback(nsITimer* aTimer, void* aPresShell);
  nsRevocableEventPtr<nsRunnableMethod<PresShell> > mResizeEvent;
  nsCOMPtr<nsITimer> mAsyncResizeEventTimer;
  PRPackedBool mAsyncResizeTimerIsActive;
  PRPackedBool mInResize;

  virtual void SynthesizeMouseMove(PRBool aFromScroll);

  // Check if aEvent is a mouse event and record the mouse location for later
  // synth mouse moves.
  void RecordMouseLocation(nsGUIEvent* aEvent);
  // This is used for synthetic mouse events that are sent when what is under
  // the mouse pointer may have changed without the mouse moving (eg scrolling,
  // change to the document contents).
  // It is set only on a presshell for a root document, this value represents
  // the last observed location of the mouse relative to that root document. It
  // is set to (NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE) if the mouse isn't
  // over our window or there is no last observed mouse location for some
  // reason.
  nsPoint mMouseLocation;
  class nsSynthMouseMoveEvent : public nsRunnable {
  public:
    nsSynthMouseMoveEvent(PresShell* aPresShell, PRBool aFromScroll)
      : mPresShell(aPresShell), mFromScroll(aFromScroll) {
      NS_ASSERTION(mPresShell, "null parameter");
    }
    void Revoke() { mPresShell = nsnull; }
    NS_IMETHOD Run() {
      if (mPresShell)
        mPresShell->ProcessSynthMouseMoveEvent(mFromScroll);
      return NS_OK;
    }
  private:
    PresShell* mPresShell;
    PRBool mFromScroll;
  };
  nsRevocableEventPtr<nsSynthMouseMoveEvent> mSynthMouseMoveEvent;
  void ProcessSynthMouseMoveEvent(PRBool aFromScroll);

  PresShell* GetRootPresShell();

private:
#ifdef DEBUG
  // Ensure that every allocation from the PresArena is eventually freed.
  PRUint32 mPresArenaAllocCount;
#endif

public:

  PRUint32 EstimateMemoryUsed() {
    PRUint32 result = 0;

    result += sizeof(PresShell);
    result += mStackArena.Size();
    result += mFrameArena.Size();

    return result;
  }

  class MemoryReporter : public nsIMemoryMultiReporter
  {
  public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIMEMORYMULTIREPORTER
  protected:
    static PLDHashOperator SizeEnumerator(PresShellPtrKey *aEntry, void *userArg);
  };

protected:
  void QueryIsActive();
  nsresult UpdateImageLockingState();

private:
  nscolor GetDefaultBackgroundColorToDraw();
};

#endif /* !defined(nsPresShell_h_) */

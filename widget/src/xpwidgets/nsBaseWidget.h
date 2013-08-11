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
#ifndef nsBaseWidget_h__
#define nsBaseWidget_h__

#include "nsRect.h"
#include "nsIWidget.h"
#include "nsIEventListener.h"
#include "nsIToolkit.h"
#include "nsIAppShell.h"
#include "nsILocalFile.h"
#include "nsString.h"
#include "nsCOMPtr.h"
#include "nsGUIEvent.h"
#include "nsAutoPtr.h"

class nsIContent;
class nsAutoRollup;

/**
 * Common widget implementation used as base class for native
 * or crossplatform implementations of Widgets. 
 * All cross-platform behavior that all widgets need to implement 
 * should be placed in this class. 
 * (Note: widget implementations are not required to use this
 * class, but it gives them a head start.)
 */

class nsBaseWidget : public nsIWidget
{
  friend class nsAutoRollup;

public:
  nsBaseWidget();
  virtual ~nsBaseWidget();
  
  NS_DECL_ISUPPORTS
  
  // nsIWidget interface
  NS_IMETHOD              CaptureMouse(PRBool aCapture);
  NS_IMETHOD              Validate();
  NS_IMETHOD              GetClientData(void*& aClientData);
  NS_IMETHOD              SetClientData(void* aClientData);
  NS_IMETHOD              Destroy();
  NS_IMETHOD              SetParent(nsIWidget* aNewParent);
  virtual nsIWidget*      GetParent(void);
  virtual nsIWidget*      GetTopLevelWidget();
  virtual nsIWidget*      GetSheetWindowParent(void);
  virtual void            AddChild(nsIWidget* aChild);
  virtual void            RemoveChild(nsIWidget* aChild);

  NS_IMETHOD              SetZIndex(PRInt32 aZIndex);
  NS_IMETHOD              GetZIndex(PRInt32* aZIndex);
  NS_IMETHOD              PlaceBehind(nsTopLevelWidgetZPlacement aPlacement,
                                      nsIWidget *aWidget, PRBool aActivate);

  NS_IMETHOD              SetSizeMode(PRInt32 aMode);
  NS_IMETHOD              GetSizeMode(PRInt32* aMode);

  virtual nscolor         GetForegroundColor(void);
  NS_IMETHOD              SetForegroundColor(const nscolor &aColor);
  virtual nscolor         GetBackgroundColor(void);
  NS_IMETHOD              SetBackgroundColor(const nscolor &aColor);
  virtual nsCursor        GetCursor();
  NS_IMETHOD              SetCursor(nsCursor aCursor);
  NS_IMETHOD              SetCursor(imgIContainer* aCursor,
                                    PRUint32 aHotspotX, PRUint32 aHotspotY);
  NS_IMETHOD              GetWindowType(nsWindowType& aWindowType);
  virtual void            SetTransparencyMode(nsTransparencyMode aMode);
  virtual nsTransparencyMode GetTransparencyMode();
  virtual void            GetWindowClipRegion(nsTArray<nsIntRect>* aRects);
  NS_IMETHOD              SetWindowShadowStyle(PRInt32 aStyle);
  virtual void            SetShowsToolbarButton(PRBool aShow) {}
  NS_IMETHOD              HideWindowChrome(PRBool aShouldHide);
  NS_IMETHOD              MakeFullScreen(PRBool aFullScreen);
  virtual nsIRenderingContext* GetRenderingContext();
  virtual nsIDeviceContext* GetDeviceContext();
  virtual nsIToolkit*     GetToolkit();  
  virtual gfxASurface*    GetThebesSurface();
  NS_IMETHOD              SetModal(PRBool aModal); 
  NS_IMETHOD              SetWindowClass(const nsAString& xulWinType);
  NS_IMETHOD              SetBorderStyle(nsBorderStyle aBorderStyle); 
  NS_IMETHOD              AddEventListener(nsIEventListener * aListener);
  NS_IMETHOD              SetBounds(const nsIntRect &aRect);
  NS_IMETHOD              GetBounds(nsIntRect &aRect);
  NS_IMETHOD              GetClientBounds(nsIntRect &aRect);
  NS_IMETHOD              GetScreenBounds(nsIntRect &aRect);
  NS_IMETHOD              EnableDragDrop(PRBool aEnable);
  NS_IMETHOD              GetAttention(PRInt32 aCycleCount);
  virtual PRBool          HasPendingInputEvent();
  NS_IMETHOD              SetIcon(const nsAString &anIconSpec);
  NS_IMETHOD              BeginSecureKeyboardInput();
  NS_IMETHOD              EndSecureKeyboardInput();
  NS_IMETHOD              SetWindowTitlebarColor(nscolor aColor, PRBool aActive);
  virtual PRBool          ShowsResizeIndicator(nsIntRect* aResizerRect);
  virtual void            FreeNativeData(void * data, PRUint32 aDataType) {}
  NS_IMETHOD              BeginResizeDrag(nsGUIEvent* aEvent, PRInt32 aHorizontal, PRInt32 aVertical);
  virtual nsresult        ActivateNativeMenuItemAt(const nsAString& indexString) { return NS_ERROR_NOT_IMPLEMENTED; }
  virtual nsresult        ForceUpdateNativeMenuAt(const nsAString& indexString) { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD              ResetInputState() { return NS_OK; }
  NS_IMETHOD              SetIMEOpenState(PRBool aState) { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD              GetIMEOpenState(PRBool* aState) { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD              SetIMEEnabled(PRUint32 aState) { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD              GetIMEEnabled(PRUint32* aState) { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD              CancelIMEComposition() { return NS_OK; }
  NS_IMETHOD              GetToggledKeyState(PRUint32 aKeyCode, PRBool* aLEDState) { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD              OnIMEFocusChange(PRBool aFocus) { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD              OnIMETextChange(PRUint32 aStart, PRUint32 aOldEnd, PRUint32 aNewEnd) { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD              OnIMESelectionChange(void) { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD              OnDefaultButtonLoaded(const nsIntRect &aButtonRect) { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD              OverrideSystemMouseScrollSpeed(PRInt32 aOriginalDelta, PRBool aIsHorizontal, PRInt32 &aOverriddenDelta);

protected:

  virtual void            ResolveIconName(const nsAString &aIconName,
                                          const nsAString &aIconSuffix,
                                          nsILocalFile **aResult);
  virtual void            OnDestroy();
  virtual void            BaseCreate(nsIWidget *aParent,
                                     const nsIntRect &aRect,
                                     EVENT_CALLBACK aHandleEventFunction,
                                     nsIDeviceContext *aContext,
                                     nsIAppShell *aAppShell,
                                     nsIToolkit *aToolkit,
                                     nsWidgetInitData *aInitData);

  virtual nsIContent* GetLastRollup()
  {
    return mLastRollup;
  }

  virtual nsresult SynthesizeNativeKeyEvent(PRInt32 aNativeKeyboardLayout,
                                            PRInt32 aNativeKeyCode,
                                            PRUint32 aModifierFlags,
                                            const nsAString& aCharacters,
                                            const nsAString& aUnmodifiedCharacters)
  { return NS_ERROR_UNEXPECTED; }

  // Stores the clip rectangles in aRects into mClipRects. Returns true
  // if the new rectangles are different from the old rectangles.
  PRBool StoreWindowClipRegion(const nsTArray<nsIntRect>& aRects);

protected: 
  void*             mClientData;
  EVENT_CALLBACK    mEventCallback;
  nsIDeviceContext  *mContext;
  nsIToolkit        *mToolkit;
  nsIEventListener  *mEventListener;
  nscolor           mBackground;
  nscolor           mForeground;
  nsCursor          mCursor;
  nsWindowType      mWindowType;
  nsBorderStyle     mBorderStyle;
  PRPackedBool      mOnDestroyCalled;
  nsIntRect         mBounds;
  nsIntRect*        mOriginalBounds;
  // When this pointer is null, the widget is not clipped
  nsAutoArrayPtr<nsIntRect> mClipRects;
  PRUint32          mClipRectCount;
  PRInt32           mZIndex;
  nsSizeMode        mSizeMode;

  // the last rolled up popup. Only set this when an nsAutoRollup is in scope,
  // so it can be cleared automatically.
  static nsIContent* mLastRollup;
    
#ifdef DEBUG
protected:
  static nsAutoString debug_GuiEventToString(nsGUIEvent * aGuiEvent);
  static PRBool debug_WantPaintFlashing();

  static void debug_DumpInvalidate(FILE *                aFileOut,
                                   nsIWidget *           aWidget,
                                   const nsIntRect *     aRect,
                                   PRBool                aIsSynchronous,
                                   const nsCAutoString & aWidgetName,
                                   PRInt32               aWindowID);

  static void debug_DumpEvent(FILE *                aFileOut,
                              nsIWidget *           aWidget,
                              nsGUIEvent *          aGuiEvent,
                              const nsCAutoString & aWidgetName,
                              PRInt32               aWindowID);
  
  static void debug_DumpPaintEvent(FILE *                aFileOut,
                                   nsIWidget *           aWidget,
                                   nsPaintEvent *        aPaintEvent,
                                   const nsCAutoString & aWidgetName,
                                   PRInt32               aWindowID);

  static PRBool debug_GetCachedBoolPref(const char* aPrefName);
#endif
};

// A situation can occur when a mouse event occurs over a menu label while the
// menu popup is already open. The expected behaviour is to close the popup.
// This happens by calling nsIRollupListener::Rollup before the mouse event is
// processed. However, in cases where the mouse event is not consumed, this
// event will then get targeted at the menu label causing the menu to open
// again. To prevent this, we store in mLastRollup a reference to the popup
// that was closed during the Rollup call, and prevent this popup from
// reopening while processing the mouse event.
// mLastRollup should only be set while an nsAutoRollup is in scope;
// when it goes out of scope mLastRollup is cleared automatically.
// As mLastRollup is static, it can be retrieved by calling
// nsIWidget::GetLastRollup on any widget.
class nsAutoRollup
{
  PRBool wasClear;

  public:

  nsAutoRollup();
  ~nsAutoRollup();
};

/**
 * BlitRectIter and/or ScrollRectIterBase are classes used in
 * nsIWidget::Scroll() implementations.  They provide sorting of rectangles
 * such that copying from rects[i] - aDelta to rects[i] does not alter
 * anything in rects[j] for each j > i when rect[i] and rect[j] do not
 * intersect each other nor any other rectangle.  That is, it is safe to just
 * copy non-intersecting rectangles in the order provided.
 *
 * ScrollRectIterBase is only instantiated within derived classes.  It expects
 * to be initialized through BaseInit() with a linked list of rectangles.
 *
 * BlitRectIter provides a simple constructor from an array of nsIntRects.
 */

class ScrollRectIterBase {
public:
  PRBool IsDone() { return mHead == nsnull; }
  void operator++() { mHead = mHead->mNext; }
  const nsIntRect& Rect() const { return *mHead; }

protected:
  ScrollRectIterBase() {}

  struct ScrollRect : public nsIntRect {
    ScrollRect(const nsIntRect& aIntRect) : nsIntRect(aIntRect) {}

    // Flip the coordinate system so that we can assume that the rectangles
    // are moving in the direction of decreasing x and y (left and up).
    // This function is its own inverse.
    void Flip(const nsIntPoint& aDelta)
    {
      if (aDelta.x > 0) x = -XMost();
      if (aDelta.y > 0) y = -YMost();
    }

    ScrollRect* mNext;
  };

  void BaseInit(const nsIntPoint& aDelta, ScrollRect* aHead);

private:
  void Flip(const nsIntPoint& aDelta)
  {
    for (ScrollRect* r = mHead; r; r = r->mNext) {
      r->Flip(aDelta);
    }
  }

  /**
   * Comparator for an initial sort of the rectangles.  The rectangles are
   * primarily sorted in increasing y, which is required for the algorithm.
   * The secondary sort is in decreasing x, chosen to make Move() more
   * efficient for rows of rectangles with equal y.
   */
  class InitialSortComparator {
  public:
    PRBool Equals(const ScrollRect* a, const ScrollRect* b) const
    {
      return a->y == b->y && a->x == b->x;
    }
    PRBool LessThan(const ScrollRect* a, const ScrollRect* b) const
    {
      return a->y < b->y || (a->y == b->y && a->x > b->x);
    }
  };

  void Move(ScrollRect** aUnmovedLink);

  // Linked list of rectangles; these are assumed owned by the derived class
  ScrollRect* mHead;
  // Used in sorting to point to the last mNext link in the moved chain.
  ScrollRect** mTailLink;
};

class BlitRectIter : public ScrollRectIterBase {
public:
  BlitRectIter(const nsIntPoint& aDelta, const nsTArray<nsIntRect>& aRects);
private:
  // Copying is not supported.
  BlitRectIter(const BlitRectIter&);
  void operator=(const BlitRectIter&);

  nsTArray<ScrollRect> mRects;
};

#endif // nsBaseWidget_h__

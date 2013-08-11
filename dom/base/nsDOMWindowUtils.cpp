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

#include "nsIDocShell.h"
#include "nsPresContext.h"
#include "nsDOMClassInfo.h"
#include "nsDOMError.h"
#include "nsIDOMNSEvent.h"
#include "nsIPrivateDOMEvent.h"
#include "nsDOMWindowUtils.h"
#include "nsQueryContentEventResult.h"
#include "nsGlobalWindow.h"
#include "nsIDocument.h"
#include "nsFocusManager.h"
#include "nsIEventStateManager.h"

#include "nsIScrollableView.h"

#include "nsContentUtils.h"

#include "nsIFrame.h"
#include "nsIWidget.h"
#include "nsGUIEvent.h"
#include "nsIParser.h"
#include "nsJSEnvironment.h"

#include "nsIViewManager.h"

#include "nsIDOMHTMLCanvasElement.h"
#include "nsICanvasElement.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"

#include "nsICSSLoader.h"
#include "nsICSSParser.h"
#include "nsICSSStyleSheet.h"
#include "nsUnicharInputStream.h"
#include "nsNetUtil.h"

#if defined(MOZ_X11) && defined(MOZ_WIDGET_GTK2)
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#endif

NS_INTERFACE_MAP_BEGIN(nsDOMWindowUtils)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMWindowUtils)
  NS_INTERFACE_MAP_ENTRY(nsIDOMWindowUtils)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIDOMWindowUtils_1_9_2)
  NS_INTERFACE_MAP_ENTRY(nsIDOMWindowUtils_1_9_2_5)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(WindowUtils)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(nsDOMWindowUtils)
NS_IMPL_RELEASE(nsDOMWindowUtils)

nsDOMWindowUtils::nsDOMWindowUtils(nsGlobalWindow *aWindow)
  : mWindow(aWindow)
{
}

nsDOMWindowUtils::~nsDOMWindowUtils()
{
}

nsPresContext*
nsDOMWindowUtils::GetPresContext()
{
  if (!mWindow)
    return nsnull;
  nsIDocShell *docShell = mWindow->GetDocShell();
  if (!docShell)
    return nsnull;
  nsCOMPtr<nsPresContext> presContext;
  docShell->GetPresContext(getter_AddRefs(presContext));
  return presContext;
}

NS_IMETHODIMP
nsDOMWindowUtils::GetImageAnimationMode(PRUint16 *aMode)
{
  NS_ENSURE_ARG_POINTER(aMode);
  *aMode = 0;
  nsPresContext* presContext = GetPresContext();
  if (presContext) {
    *aMode = presContext->ImageAnimationMode();
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsDOMWindowUtils::SetImageAnimationMode(PRUint16 aMode)
{
  nsPresContext* presContext = GetPresContext();
  if (presContext) {
    presContext->SetImageAnimationMode(aMode);
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsDOMWindowUtils::GetDocCharsetIsForced(PRBool *aIsForced)
{
  *aIsForced = PR_FALSE;

  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap)) || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  if (mWindow) {
    nsCOMPtr<nsIDocument> doc(do_QueryInterface(mWindow->GetExtantDocument()));
    *aIsForced = doc &&
      doc->GetDocumentCharacterSetSource() >= kCharsetFromParentForced;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::GetDocumentMetadata(const nsAString& aName,
                                      nsAString& aValue)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  if (mWindow) {
    nsCOMPtr<nsIDocument> doc(do_QueryInterface(mWindow->GetExtantDocument()));
    if (doc) {
      nsCOMPtr<nsIAtom> name = do_GetAtom(aName);
      doc->GetHeaderData(name, aValue);
      return NS_OK;
    }
  }
  
  aValue.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::Redraw(PRUint32 aCount, PRUint32 *aDurationOut)
{
  nsresult rv;

  if (aCount == 0)
    aCount = 1;

  nsCOMPtr<nsIDocShell> docShell = mWindow->GetDocShell();
  if (docShell) {
    nsCOMPtr<nsIPresShell> presShell;

    rv = docShell->GetPresShell(getter_AddRefs(presShell));
    if (NS_SUCCEEDED(rv) && presShell) {
      nsIFrame *rootFrame = presShell->GetRootFrame();

      if (rootFrame) {
        nsRect r(nsPoint(0, 0), rootFrame->GetSize());

        PRIntervalTime iStart = PR_IntervalNow();

        for (PRUint32 i = 0; i < aCount; i++)
          rootFrame->InvalidateWithFlags(r, nsIFrame::INVALIDATE_IMMEDIATE);

#if defined(MOZ_X11) && defined(MOZ_WIDGET_GTK2)
        XSync(GDK_DISPLAY(), False);
#endif

        *aDurationOut = PR_IntervalToMilliseconds(PR_IntervalNow() - iStart);

        return NS_OK;
      }
    }
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsDOMWindowUtils::SendMouseEvent(const nsAString& aType,
                                 float aX,
                                 float aY,
                                 PRInt32 aButton,
                                 PRInt32 aClickCount,
                                 PRInt32 aModifiers,
                                 PRBool aIgnoreRootScrollFrame)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  // get the widget to send the event to
  nsPoint offset;
  nsCOMPtr<nsIWidget> widget = GetWidget(&offset);
  if (!widget)
    return NS_ERROR_FAILURE;

  PRInt32 msg;
  PRBool contextMenuKey = PR_FALSE;
  if (aType.EqualsLiteral("mousedown"))
    msg = NS_MOUSE_BUTTON_DOWN;
  else if (aType.EqualsLiteral("mouseup"))
    msg = NS_MOUSE_BUTTON_UP;
  else if (aType.EqualsLiteral("mousemove"))
    msg = NS_MOUSE_MOVE;
  else if (aType.EqualsLiteral("mouseover"))
    msg = NS_MOUSE_ENTER;
  else if (aType.EqualsLiteral("mouseout"))
    msg = NS_MOUSE_EXIT;
  else if (aType.EqualsLiteral("contextmenu")) {
    msg = NS_CONTEXTMENU;
    contextMenuKey = (aButton == 0);
  } else
    return NS_ERROR_FAILURE;

  nsMouseEvent event(PR_TRUE, msg, widget, nsMouseEvent::eReal,
                     contextMenuKey ?
                       nsMouseEvent::eContextMenuKey : nsMouseEvent::eNormal);
  event.isShift = (aModifiers & nsIDOMNSEvent::SHIFT_MASK) ? PR_TRUE : PR_FALSE;
  event.isControl = (aModifiers & nsIDOMNSEvent::CONTROL_MASK) ? PR_TRUE : PR_FALSE;
  event.isAlt = (aModifiers & nsIDOMNSEvent::ALT_MASK) ? PR_TRUE : PR_FALSE;
  event.isMeta = (aModifiers & nsIDOMNSEvent::META_MASK) ? PR_TRUE : PR_FALSE;
  event.button = aButton;
  event.widget = widget;

  event.clickCount = aClickCount;
  event.time = PR_IntervalNow();
  event.flags |= NS_EVENT_FLAG_SYNTETIC_TEST_EVENT;

  float appPerDev = float(widget->GetDeviceContext()->AppUnitsPerDevPixel());
  event.refPoint.x =
    NSAppUnitsToIntPixels(nsPresContext::CSSPixelsToAppUnits(aX) + offset.x,
                          appPerDev);
  event.refPoint.y =
    NSAppUnitsToIntPixels(nsPresContext::CSSPixelsToAppUnits(aY) + offset.y,
                          appPerDev);
  event.ignoreRootScrollFrame = aIgnoreRootScrollFrame;

  nsEventStatus status;
  return widget->DispatchEvent(&event, status);
}

NS_IMETHODIMP
nsDOMWindowUtils::SendMouseScrollEvent(const nsAString& aType,
                                       float aX,
                                       float aY,
                                       PRInt32 aButton,
                                       PRInt32 aScrollFlags,
                                       PRInt32 aDelta,
                                       PRInt32 aModifiers)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  // get the widget to send the event to
  nsPoint offset;
  nsCOMPtr<nsIWidget> widget = GetWidget(&offset);
  if (!widget)
    return NS_ERROR_NULL_POINTER;

  PRInt32 msg;
  if (aType.EqualsLiteral("DOMMouseScroll"))
    msg = NS_MOUSE_SCROLL;
  else if (aType.EqualsLiteral("MozMousePixelScroll"))
    msg = NS_MOUSE_PIXEL_SCROLL;
  else
    return NS_ERROR_UNEXPECTED;

  nsMouseScrollEvent event(PR_TRUE, msg, widget);
  event.isShift = (aModifiers & nsIDOMNSEvent::SHIFT_MASK) ? PR_TRUE : PR_FALSE;
  event.isControl = (aModifiers & nsIDOMNSEvent::CONTROL_MASK) ? PR_TRUE : PR_FALSE;
  event.isAlt = (aModifiers & nsIDOMNSEvent::ALT_MASK) ? PR_TRUE : PR_FALSE;
  event.isMeta = (aModifiers & nsIDOMNSEvent::META_MASK) ? PR_TRUE : PR_FALSE;
  event.button = aButton;
  event.widget = widget;
  event.delta = aDelta;
  event.scrollFlags = aScrollFlags;

  event.time = PR_IntervalNow();

  float appPerDev = float(widget->GetDeviceContext()->AppUnitsPerDevPixel());
  event.refPoint.x =
    NSAppUnitsToIntPixels(nsPresContext::CSSPixelsToAppUnits(aX) + offset.x,
                          appPerDev);
  event.refPoint.y =
    NSAppUnitsToIntPixels(nsPresContext::CSSPixelsToAppUnits(aY) + offset.y,
                          appPerDev);

  nsEventStatus status;
  return widget->DispatchEvent(&event, status);
}

NS_IMETHODIMP
nsDOMWindowUtils::SendKeyEvent(const nsAString& aType,
                               PRInt32 aKeyCode,
                               PRInt32 aCharCode,
                               PRInt32 aModifiers,
                               PRBool aPreventDefault,
                               PRBool* aDefaultActionTaken)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  // get the widget to send the event to
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget)
    return NS_ERROR_FAILURE;

  PRInt32 msg;
  if (aType.EqualsLiteral("keydown"))
    msg = NS_KEY_DOWN;
  else if (aType.EqualsLiteral("keyup"))
    msg = NS_KEY_UP;
  else if (aType.EqualsLiteral("keypress"))
    msg = NS_KEY_PRESS;
  else
    return NS_ERROR_FAILURE;

  nsKeyEvent event(PR_TRUE, msg, widget);
  event.isShift = (aModifiers & nsIDOMNSEvent::SHIFT_MASK) ? PR_TRUE : PR_FALSE;
  event.isControl = (aModifiers & nsIDOMNSEvent::CONTROL_MASK) ? PR_TRUE : PR_FALSE;
  event.isAlt = (aModifiers & nsIDOMNSEvent::ALT_MASK) ? PR_TRUE : PR_FALSE;
  event.isMeta = (aModifiers & nsIDOMNSEvent::META_MASK) ? PR_TRUE : PR_FALSE;

  event.keyCode = aKeyCode;
  event.charCode = aCharCode;
  event.refPoint.x = event.refPoint.y = 0;
  event.time = PR_IntervalNow();

  if (aPreventDefault) {
    event.flags |= NS_EVENT_FLAG_NO_DEFAULT;
  }

  nsEventStatus status;
  nsresult rv = widget->DispatchEvent(&event, status);
  NS_ENSURE_SUCCESS(rv, rv);

  *aDefaultActionTaken = (status != nsEventStatus_eConsumeNoDefault);
  
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::SendNativeKeyEvent(PRInt32 aNativeKeyboardLayout,
                                     PRInt32 aNativeKeyCode,
                                     PRInt32 aModifiers,
                                     const nsAString& aCharacters,
                                     const nsAString& aUnmodifiedCharacters)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  // get the widget to send the event to
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget)
    return NS_ERROR_FAILURE;

  return widget->SynthesizeNativeKeyEvent(aNativeKeyboardLayout, aNativeKeyCode,
                                          aModifiers, aCharacters, aUnmodifiedCharacters);
}

NS_IMETHODIMP
nsDOMWindowUtils::ActivateNativeMenuItemAt(const nsAString& indexString)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  // get the widget to send the event to
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget)
    return NS_ERROR_FAILURE;

  return widget->ActivateNativeMenuItemAt(indexString);
}

NS_IMETHODIMP
nsDOMWindowUtils::ForceUpdateNativeMenuAt(const nsAString& indexString)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  // get the widget to send the event to
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget)
    return NS_ERROR_FAILURE;

  return widget->ForceUpdateNativeMenuAt(indexString);
}

nsIWidget*
nsDOMWindowUtils::GetWidget(nsPoint* aOffset)
{
  if (mWindow) {
    nsIDocShell *docShell = mWindow->GetDocShell();
    if (docShell) {
      nsCOMPtr<nsIPresShell> presShell;
      docShell->GetPresShell(getter_AddRefs(presShell));
      if (presShell) {
        nsIFrame* frame = presShell->GetRootFrame();
        if (frame)
          return frame->GetView()->GetNearestWidget(aOffset);
      }
    }
  }

  return nsnull;
}

NS_IMETHODIMP
nsDOMWindowUtils::Focus(nsIDOMElement* aElement)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled(
    "UniversalXPConnect", &hasCap)) || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm) {
    if (aElement)
      fm->SetFocus(aElement, 0);
    else
      fm->ClearFocus(mWindow);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::GarbageCollect()
{
  // NOTE: Only do this in NON debug builds, as this function can useful
  // during debugging.
#ifndef DEBUG
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->
                  IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;
#endif

  nsJSContext::CC();
  nsJSContext::CC();

  return NS_OK;
}


NS_IMETHODIMP
nsDOMWindowUtils::ProcessUpdates()
{
  nsPresContext* presContext = GetPresContext();
  if (!presContext)
    return NS_ERROR_UNEXPECTED;

  nsIPresShell* shell = presContext->GetPresShell();
  if (!shell)
    return NS_ERROR_UNEXPECTED;

  nsIViewManager *viewManager = shell->GetViewManager();
  if (!viewManager)
    return NS_ERROR_UNEXPECTED;
  
  nsIViewManager::UpdateViewBatch batch;
  batch.BeginUpdateViewBatch(viewManager);
  batch.EndUpdateViewBatch(NS_VMREFRESH_IMMEDIATE);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::SendSimpleGestureEvent(const nsAString& aType,
                                         float aX,
                                         float aY,
                                         PRUint32 aDirection,
                                         PRFloat64 aDelta,
                                         PRInt32 aModifiers)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  // get the widget to send the event to
  nsPoint offset;
  nsCOMPtr<nsIWidget> widget = GetWidget(&offset);
  if (!widget)
    return NS_ERROR_FAILURE;

  PRInt32 msg;
  if (aType.EqualsLiteral("MozSwipeGesture"))
    msg = NS_SIMPLE_GESTURE_SWIPE;
  else if (aType.EqualsLiteral("MozMagnifyGestureStart"))
    msg = NS_SIMPLE_GESTURE_MAGNIFY_START;
  else if (aType.EqualsLiteral("MozMagnifyGestureUpdate"))
    msg = NS_SIMPLE_GESTURE_MAGNIFY_UPDATE;
  else if (aType.EqualsLiteral("MozMagnifyGesture"))
    msg = NS_SIMPLE_GESTURE_MAGNIFY;
  else if (aType.EqualsLiteral("MozRotateGestureStart"))
    msg = NS_SIMPLE_GESTURE_ROTATE_START;
  else if (aType.EqualsLiteral("MozRotateGestureUpdate"))
    msg = NS_SIMPLE_GESTURE_ROTATE_UPDATE;
  else if (aType.EqualsLiteral("MozRotateGesture"))
    msg = NS_SIMPLE_GESTURE_ROTATE;
  else if (aType.EqualsLiteral("MozTapGesture"))
    msg = NS_SIMPLE_GESTURE_TAP;
  else if (aType.EqualsLiteral("MozPressTapGesture"))
    msg = NS_SIMPLE_GESTURE_PRESSTAP;
  else
    return NS_ERROR_FAILURE;
 
  nsSimpleGestureEvent event(PR_TRUE, msg, widget, aDirection, aDelta);
  event.isShift = (aModifiers & nsIDOMNSEvent::SHIFT_MASK) ? PR_TRUE : PR_FALSE;
  event.isControl = (aModifiers & nsIDOMNSEvent::CONTROL_MASK) ? PR_TRUE : PR_FALSE;
  event.isAlt = (aModifiers & nsIDOMNSEvent::ALT_MASK) ? PR_TRUE : PR_FALSE;
  event.isMeta = (aModifiers & nsIDOMNSEvent::META_MASK) ? PR_TRUE : PR_FALSE;
  event.time = PR_IntervalNow();

  float appPerDev = float(widget->GetDeviceContext()->AppUnitsPerDevPixel());
  event.refPoint.x =
    NSAppUnitsToIntPixels(nsPresContext::CSSPixelsToAppUnits(aX) + offset.x,
                          appPerDev);
  event.refPoint.y =
    NSAppUnitsToIntPixels(nsPresContext::CSSPixelsToAppUnits(aY) + offset.y,
                          appPerDev);

  nsEventStatus status;
  return widget->DispatchEvent(&event, status);
}

NS_IMETHODIMP
nsDOMWindowUtils::ElementFromPoint(PRInt32 aX, PRInt32 aY,
                                   PRBool aIgnoreRootScrollFrame,
                                   PRBool aFlushLayout,
                                   nsIDOMElement** aReturn)
{
  nsCOMPtr<nsIDocument> doc(do_QueryInterface(mWindow->GetExtantDocument()));
  NS_ENSURE_STATE(doc);

  return doc->ElementFromPointHelper(aX, aY, aIgnoreRootScrollFrame, aFlushLayout,
                                     aReturn);
}

NS_IMETHODIMP
nsDOMWindowUtils::NodesFromRect(float aX, float aY,
                                float aTopSize, float aRightSize,
                                float aBottomSize, float aLeftSize,
                                PRBool aIgnoreRootScrollFrame,
                                PRBool aFlushLayout,
                                nsIDOMNodeList** aReturn)
{
  nsCOMPtr<nsIDocument_MOZILLA_1_9_2_5_BRANCH> doc(do_QueryInterface(mWindow->GetExtantDocument()));
  NS_ENSURE_STATE(doc);

  return doc->NodesFromRectHelper(aX, aY, aTopSize, aRightSize, aBottomSize, aLeftSize, 
                                  aIgnoreRootScrollFrame, aFlushLayout, aReturn);
}

static already_AddRefed<gfxImageSurface>
CanvasToImageSurface(nsIDOMHTMLCanvasElement *canvas)
{
  PRUint32 w, h;
  nsresult rv;

  nsCOMPtr<nsICanvasElement> elt = do_QueryInterface(canvas);
  rv = elt->GetSize(&w, &h);
  if (NS_FAILED(rv))
    return nsnull;

  nsRefPtr<gfxImageSurface> img = new gfxImageSurface(gfxIntSize(w, h), gfxASurface::ImageFormatARGB32);
  if (img == nsnull)
    return nsnull;

  nsRefPtr<gfxContext> ctx = new gfxContext(img);
  if (ctx == nsnull)
    return nsnull;

  ctx->SetOperator(gfxContext::OPERATOR_CLEAR);
  ctx->Paint();

  ctx->SetOperator(gfxContext::OPERATOR_OVER);
  rv = elt->RenderContexts(ctx, gfxPattern::FILTER_NEAREST);
  if (NS_FAILED(rv))
    return nsnull;

  ctx = nsnull;

  return img.forget();
}

NS_IMETHODIMP
nsDOMWindowUtils::CompareCanvases(nsIDOMHTMLCanvasElement *aCanvas1,
                                  nsIDOMHTMLCanvasElement *aCanvas2,
                                  PRUint32* aMaxDifference,
                                  PRUint32* retVal)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap)) || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  if (aCanvas1 == nsnull ||
      aCanvas2 == nsnull ||
      retVal == nsnull)
    return NS_ERROR_FAILURE;

  nsRefPtr<gfxImageSurface> img1 = CanvasToImageSurface(aCanvas1);
  nsRefPtr<gfxImageSurface> img2 = CanvasToImageSurface(aCanvas2);

  if (img1 == nsnull || img2 == nsnull ||
      img1->GetSize() != img2->GetSize() ||
      img1->Stride() != img2->Stride())
    return NS_ERROR_FAILURE;

  int v;
  gfxIntSize size = img1->GetSize();
  PRUint32 stride = img1->Stride();

  // we can optimize for the common all-pass case
  if (stride == (PRUint32) size.width * 4) {
    v = memcmp(img1->Data(), img2->Data(), size.width * size.height * 4);
    if (v == 0) {
      if (aMaxDifference)
        *aMaxDifference = 0;
      *retVal = 0;
      return NS_OK;
    }
  }

  PRUint32 dc = 0;
  PRUint32 different = 0;

  for (int j = 0; j < size.height; j++) {
    unsigned char *p1 = img1->Data() + j*stride;
    unsigned char *p2 = img2->Data() + j*stride;
    v = memcmp(p1, p2, stride);

    if (v) {
      for (int i = 0; i < size.width; i++) {
        if (*(PRUint32*) p1 != *(PRUint32*) p2) {

          different++;

          dc = PR_MAX(abs(p1[0] - p2[0]), dc);
          dc = PR_MAX(abs(p1[1] - p2[1]), dc);
          dc = PR_MAX(abs(p1[2] - p2[2]), dc);
          dc = PR_MAX(abs(p1[3] - p2[3]), dc);
        }

        p1 += 4;
        p2 += 4;
      }
    }
  }

  if (aMaxDifference)
    *aMaxDifference = dc;

  *retVal = different;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::GetIsMozAfterPaintPending(PRBool *aResult)
{
  *aResult = PR_FALSE;
  nsPresContext* presContext = GetPresContext();
  if (!presContext)
    return NS_OK;
  *aResult = presContext->IsDOMPaintEventPending();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::ClearMozAfterPaintEvents()
{
  nsPresContext* presContext = GetPresContext();
  if (!presContext)
    return NS_OK;
  presContext->ClearMozAfterPaintEvents();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::DisableNonTestMouseEvents(PRBool aDisable)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->
                  IsCapabilityEnabled("UniversalXPConnect", &hasCap)) ||
      !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  NS_ENSURE_TRUE(mWindow, NS_ERROR_FAILURE);
  nsIDocShell *docShell = mWindow->GetDocShell();
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);
  nsCOMPtr<nsIPresShell> presShell;
  docShell->GetPresShell(getter_AddRefs(presShell));
  NS_ENSURE_TRUE(presShell, NS_ERROR_FAILURE);
  return presShell->DisableNonTestMouseEvents(aDisable);
}

NS_IMETHODIMP
nsDOMWindowUtils::SuppressEventHandling(PRBool aSuppress)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap)) || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  nsCOMPtr<nsIDocument> doc(do_QueryInterface(mWindow->GetExtantDocument()));
  NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);

  if (aSuppress) {
    doc->SuppressEventHandling();
  } else {
    doc->UnsuppressEventHandlingAndFireEvents(PR_TRUE);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::GetScrollXY(PRBool aFlushLayout, PRInt32* aScrollX, PRInt32* aScrollY)
{
  nsCOMPtr<nsIDocument> doc(do_QueryInterface(mWindow->GetExtantDocument()));
  NS_ENSURE_STATE(doc);

  if (aFlushLayout) {
    doc->FlushPendingNotifications(Flush_Layout);
  }

  nscoord xPos = 0, yPos = 0;

  nsIPresShell *presShell = doc->GetPrimaryShell();
  if (presShell) {
    nsIViewManager *viewManager = presShell->GetViewManager();
    if (viewManager) {
      nsIScrollableView *view = nsnull;
      viewManager->GetRootScrollableView(&view);
      if (view) {
        nsresult rv = view->GetScrollPosition(xPos, yPos);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  }

  *aScrollX = nsPresContext::AppUnitsToIntCSSPixels(xPos);
  *aScrollY = nsPresContext::AppUnitsToIntCSSPixels(yPos);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::GetIMEIsOpen(PRBool *aState)
{
  NS_ENSURE_ARG_POINTER(aState);

  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget)
    return NS_ERROR_FAILURE;

  // Open state should not be available when IME is not enabled.
  PRUint32 enabled;
  nsresult rv = widget->GetIMEEnabled(&enabled);
  NS_ENSURE_SUCCESS(rv, rv);
  if (enabled != nsIWidget::IME_STATUS_ENABLED)
    return NS_ERROR_NOT_AVAILABLE;

  return widget->GetIMEOpenState(aState);
}

NS_IMETHODIMP
nsDOMWindowUtils::GetIMEStatus(PRUint32 *aState)
{
  NS_ENSURE_ARG_POINTER(aState);

  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget)
    return NS_ERROR_FAILURE;

  return widget->GetIMEEnabled(aState);
}

NS_IMETHODIMP
nsDOMWindowUtils::GetScreenPixelsPerCSSPixel(float* aScreenPixels)
{
  *aScreenPixels = 1;

  if (!nsContentUtils::IsCallerTrustedForRead())
    return NS_ERROR_DOM_SECURITY_ERR;
  nsPresContext* presContext = GetPresContext();
  if (!presContext)
    return NS_OK;

  *aScreenPixels = float(nsPresContext::AppUnitsPerCSSPixel())/
      presContext->AppUnitsPerDevPixel();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::GetCOWForObject()
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->
                IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  nsresult rv = NS_OK;
  nsCOMPtr<nsIXPConnect> xpc = do_GetService("@mozilla.org/js/xpc/XPConnect;1",
                                             &rv);
  if (NS_FAILED(rv))
    return NS_ERROR_FAILURE;

  // get the xpconnect native call context
  nsAXPCNativeCallContext *cc = nsnull;
  xpc->GetCurrentNativeCallContext(&cc);
  if(!cc)
    return NS_ERROR_FAILURE;

  // Get JSContext of current call
  JSContext* cx;
  rv = cc->GetJSContext(&cx);
  if(NS_FAILED(rv) || !cx)
    return NS_ERROR_FAILURE;

  // get place for return value
  jsval *rval = nsnull;
  rv = cc->GetRetValPtr(&rval);
  if(NS_FAILED(rv) || !rval)
    return NS_ERROR_FAILURE;

  // get argc and argv and verify arg count
  PRUint32 argc;
  rv = cc->GetArgc(&argc);
  if(NS_FAILED(rv))
    return NS_ERROR_FAILURE;

  if(argc < 2)
    return NS_ERROR_XPC_NOT_ENOUGH_ARGS;

  jsval* argv;
  rv = cc->GetArgvPtr(&argv);
  if(NS_FAILED(rv) || !argv)
    return NS_ERROR_FAILURE;

  // first and second params must be JSObjects
  if(JSVAL_IS_PRIMITIVE(argv[0]) ||
     JSVAL_IS_PRIMITIVE(argv[1]))
    return NS_ERROR_XPC_BAD_CONVERT_JS;

  JSObject *scope = JSVAL_TO_OBJECT(argv[0]);
  JSObject *object = JSVAL_TO_OBJECT(argv[1]);
  rv = xpc->GetCOWForObject(cx, JS_GetGlobalForObject(cx, scope),
                            object, rval);

  if (NS_FAILED(rv))
    return rv;

  cc->SetReturnValueWasSet(PR_TRUE);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::DispatchDOMEventViaPresShell(nsIDOMNode* aTarget,
                                               nsIDOMEvent* aEvent,
                                               PRBool aTrusted,
                                               PRBool* aRetVal)
{
  if (!nsContentUtils::IsCallerTrustedForRead()) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  nsPresContext* presContext = GetPresContext();
  NS_ENSURE_STATE(presContext);
  nsCOMPtr<nsIPresShell> shell = presContext->GetPresShell();
  NS_ENSURE_STATE(shell);
  nsCOMPtr<nsIPrivateDOMEvent> event = do_QueryInterface(aEvent);
  NS_ENSURE_STATE(event);
  event->SetTrusted(aTrusted);
  nsEvent* internalEvent = event->GetInternalNSEvent();
  NS_ENSURE_STATE(internalEvent);
  nsCOMPtr<nsIContent> content = do_QueryInterface(aTarget);
  NS_ENSURE_STATE(content);

  nsEventStatus status = nsEventStatus_eIgnore;
  shell->HandleEventWithTarget(internalEvent, nsnull, content,
                               &status);
  *aRetVal = (status != nsEventStatus_eConsumeNoDefault);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWindowUtils::CssInitialSyntaxIsValid(const nsAString& aSheet,
                                          PRBool *aRetVal)
{
  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->
                IsCapabilityEnabled("UniversalXPConnect", &hasCap)) || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  nsCOMPtr<nsIUnicharInputStream> stream;
  nsresult rv = nsSimpleUnicharStreamFactory::GetInstance()->
    CreateInstanceFromString(aSheet, getter_AddRefs(stream));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> uri;
  nsCAutoString uriContents("data:text/css,");
  AppendUTF16toUTF8(aSheet, uriContents);
  rv = NS_NewURI(getter_AddRefs(uri), uriContents.get());
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> principal;
  rv = nsContentUtils::GetSecurityManager()->
    GetCodebasePrincipal(uri, getter_AddRefs(principal));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsICSSStyleSheet> parsedSheet;
  rv = NS_NewCSSStyleSheet(getter_AddRefs(parsedSheet));
  NS_ENSURE_SUCCESS(rv, rv);
  parsedSheet->SetURIs(uri, uri, uri);
  parsedSheet->SetPrincipal(principal);

  nsCOMPtr<nsICSSLoader> loader;
  rv = NS_NewCSSLoader(getter_AddRefs(loader));
  NS_ENSURE_SUCCESS(rv, rv);
  loader->SetCompatibilityMode(eCompatibility_NavQuirks);

  nsCOMPtr<nsICSSParser> parser;
  rv = loader->GetParserFor(parsedSheet, getter_AddRefs(parser));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsICSSParser_1_9_2> pext(do_QueryInterface(parser));
  NS_ABORT_IF_FALSE(pext, "nsICSSParser_1_9_2 missing from parser impl");

  rv = pext->ParseWithInitialSyntaxCheck(stream, uri, uri, principal, 0,
                                         PR_FALSE);
  loader->RecycleParser(parser);

  // In CheckInitialSyntax mode, the parser will return
  // NS_ERROR_DOM_SYNTAX_ERR when the sheet has been rejected.
  if (rv == NS_OK) {
    *aRetVal = PR_TRUE;
    return NS_OK;
  } else if (rv == NS_ERROR_DOM_SYNTAX_ERR) {
    *aRetVal = PR_FALSE;
    return NS_OK;
  } else {
    NS_ABORT_IF_FALSE(NS_FAILED(rv),
                      "CSS parser produced a success code other than NS_OK?!");
    return rv;
  }
}

static void
InitEvent(nsGUIEvent &aEvent, nsIntPoint *aPt = nsnull)
{
  if (aPt) {
    aEvent.refPoint = *aPt;
  }
  aEvent.time = PR_IntervalNow();
}

NS_IMETHODIMP
nsDOMWindowUtils::SendQueryContentEvent(PRUint32 aType,
                                        PRUint32 aOffset, PRUint32 aLength,
                                        PRInt32 aX, PRInt32 aY,
                                        nsIQueryContentEventResult **aResult)
{
  *aResult = nsnull;

  PRBool hasCap = PR_FALSE;
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->IsCapabilityEnabled("UniversalXPConnect", &hasCap))
      || !hasCap)
    return NS_ERROR_DOM_SECURITY_ERR;

  // get the widget to send the event to
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return NS_ERROR_FAILURE;
  }

  if (aType != NS_QUERY_SELECTED_TEXT &&
      aType != NS_QUERY_TEXT_CONTENT &&
      aType != NS_QUERY_CARET_RECT &&
      aType != NS_QUERY_TEXT_RECT &&
      aType != NS_QUERY_EDITOR_RECT) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aType != NS_QUERY_CARET_RECT)
    return NS_ERROR_NOT_IMPLEMENTED;

  nsCOMPtr<nsIWidget> targetWidget = widget;
  nsIntPoint pt(aX, aY);

  pt += widget->WidgetToScreenOffset() - targetWidget->WidgetToScreenOffset();

  nsQueryContentEvent queryEvent(PR_TRUE, aType, targetWidget);
  InitEvent(queryEvent, &pt);
  queryEvent.InitForQueryCaretRect(aOffset);

  nsEventStatus status;
  nsresult rv = targetWidget->DispatchEvent(&queryEvent, status);
  NS_ENSURE_SUCCESS(rv, rv);

  nsQueryContentEventResult* result = new nsQueryContentEventResult();
  NS_ENSURE_TRUE(result, NS_ERROR_OUT_OF_MEMORY);
  result->SetEventResult(widget, queryEvent);
  NS_ADDREF(*aResult = result);
  return NS_OK;
}


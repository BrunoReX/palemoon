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
 *   Author: Aaron Leventhal (aaronl@netscape.com)
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

#include "nsHTMLAreaAccessible.h"
#include "nsIServiceManager.h"
#include "nsIDOMElement.h"
#include "nsIDOMHTMLAreaElement.h"
#include "nsIFrame.h"
#include "nsIImageFrame.h"
#include "nsIImageMap.h"


////////////////////////////////////////////////////////////////////////////////
// nsHTMLAreaAccessible

nsHTMLAreaAccessible::
  nsHTMLAreaAccessible(nsIDOMNode *aDomNode, nsIAccessible *aParent,
                       nsIWeakReference* aShell):
  nsHTMLLinkAccessible(aDomNode, aShell)
{ 
}

////////////////////////////////////////////////////////////////////////////////
// nsIAccessible

nsresult
nsHTMLAreaAccessible::GetNameInternal(nsAString & aName)
{
  nsresult rv = nsAccessible::GetNameInternal(aName);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!aName.IsEmpty())
    return NS_OK;

  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  if (!content->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::alt,
                        aName)) {
    return GetValue(aName);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLAreaAccessible::GetDescription(nsAString& aDescription)
{
  aDescription.Truncate();

  // Still to do - follow IE's standard here
  nsCOMPtr<nsIDOMHTMLAreaElement> area(do_QueryInterface(mDOMNode));
  if (area) 
    area->GetShape(aDescription);

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLAreaAccessible::GetFirstChild(nsIAccessible **aChild)
{
  NS_ENSURE_ARG_POINTER(aChild);

  *aChild = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLAreaAccessible::GetLastChild(nsIAccessible **aChild)
{
  NS_ENSURE_ARG_POINTER(aChild);

  *aChild = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLAreaAccessible::GetChildCount(PRInt32 *aCount)
{
  NS_ENSURE_ARG_POINTER(aCount);

  *aCount = 0;
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLAreaAccessible::GetBounds(PRInt32 *x, PRInt32 *y,
                                PRInt32 *width, PRInt32 *height)
{
  nsresult rv;

  // Essentially this uses GetRect on mAreas of nsImageMap from nsImageFrame

  *x = *y = *width = *height = 0;

  nsPresContext *presContext = GetPresContext();
  NS_ENSURE_TRUE(presContext, NS_ERROR_FAILURE);

  nsCOMPtr<nsIContent> ourContent(do_QueryInterface(mDOMNode));
  NS_ENSURE_TRUE(ourContent, NS_ERROR_FAILURE);

  nsIFrame *frame = GetFrame();
  NS_ENSURE_TRUE(frame, NS_ERROR_FAILURE);
  nsIImageFrame *imageFrame = do_QueryFrame(frame);

  nsCOMPtr<nsIImageMap> map;
  imageFrame->GetImageMap(presContext, getter_AddRefs(map));
  NS_ENSURE_TRUE(map, NS_ERROR_FAILURE);

  nsRect rect;
  nsIntRect orgRectPixels;
  rv = map->GetBoundsForAreaContent(ourContent, presContext, rect);
  NS_ENSURE_SUCCESS(rv, rv);

  *x      = presContext->AppUnitsToDevPixels(rect.x); 
  *y      = presContext->AppUnitsToDevPixels(rect.y); 

  // XXX Areas are screwy; they return their rects as a pair of points, one pair
  // stored into the width and height.
  *width  = presContext->AppUnitsToDevPixels(rect.width - rect.x);
  *height = presContext->AppUnitsToDevPixels(rect.height - rect.y);

  // Put coords in absolute screen coords
  orgRectPixels = frame->GetScreenRectExternal();
  *x += orgRectPixels.x;
  *y += orgRectPixels.y;

  return NS_OK;
}

// nsAccessible::GetChildAtPoint()
nsresult
nsHTMLAreaAccessible::GetChildAtPoint(PRInt32 aX, PRInt32 aY,
                                      PRBool aDeepestChild,
                                      nsIAccessible **aChild)
{
  // Don't walk into area accessibles.
  NS_ADDREF(*aChild = this);
  return NS_OK;
}

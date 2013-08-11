/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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

// NOTE: alphabetically ordered
#include "nsAccessibilityService.h"
#include "nsCaretAccessible.h"
#include "nsIAccessibleEvent.h"
#include "nsCaret.h"
#include "nsIDOMDocument.h"
#include "nsIDOMHTMLAnchorElement.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsIDOMHTMLTextAreaElement.h"
#include "nsIFrame.h"
#include "nsIPresShell.h"
#include "nsRootAccessible.h"
#include "nsISelectionPrivate.h"
#include "nsISelection2.h"
#include "nsServiceManagerUtils.h"
#include "nsIViewManager.h"

class nsIWidget;

NS_IMPL_ISUPPORTS1(nsCaretAccessible, nsISelectionListener)
  
nsCaretAccessible::nsCaretAccessible( nsRootAccessible *aRootAccessible):
mLastCaretOffset(-1), mRootAccessible(aRootAccessible)
{
}

nsCaretAccessible::~nsCaretAccessible()
{
}

void nsCaretAccessible::Shutdown()
{
  // The caret accessible isn't shut down until the nsRootAccessible owning it is shut down
  // Each nsDocAccessible, including the nsRootAccessible, is responsible for clearing the
  // doc selection listeners they registered in this nsCaretAccessible

  ClearControlSelectionListener(); // Clear the selection listener for the currently focused control
  mLastTextAccessible = nsnull;
  mLastUsedSelection = nsnull;
  mRootAccessible = nsnull;
}

nsresult nsCaretAccessible::ClearControlSelectionListener()
{
  nsCOMPtr<nsISelectionController> controller =
    GetSelectionControllerForNode(mCurrentControl);

  mCurrentControl = nsnull;

  if (!controller)
    return NS_OK;

  // Remove 'this' registered as selection listener for the normal selection.
  nsCOMPtr<nsISelection> normalSel;
  controller->GetSelection(nsISelectionController::SELECTION_NORMAL,
                           getter_AddRefs(normalSel));
  nsCOMPtr<nsISelectionPrivate> selPrivate(do_QueryInterface(normalSel));
  NS_ENSURE_TRUE(selPrivate, NS_ERROR_FAILURE);

  nsresult rv = selPrivate->RemoveSelectionListener(this);
  NS_ENSURE_SUCCESS(rv, rv);

  // Remove 'this' registered as selection listener for the spellcheck
  // selection.
  nsCOMPtr<nsISelection> spellcheckSel;
  controller->GetSelection(nsISelectionController::SELECTION_SPELLCHECK,
                           getter_AddRefs(spellcheckSel));
  selPrivate = do_QueryInterface(spellcheckSel);
  NS_ENSURE_TRUE(selPrivate, NS_ERROR_FAILURE);

  return selPrivate->RemoveSelectionListener(this);
}

nsresult nsCaretAccessible::SetControlSelectionListener(nsIDOMNode *aCurrentNode)
{
  NS_ENSURE_TRUE(mRootAccessible, NS_ERROR_FAILURE);

  ClearControlSelectionListener();

  mCurrentControl = aCurrentNode;
  mLastTextAccessible = nsnull;

  // When focus moves such that the caret is part of a new frame selection
  // this removes the old selection listener and attaches a new one for
  // the current focus.

  nsCOMPtr<nsISelectionController> controller =
    GetSelectionControllerForNode(mCurrentControl);
#ifdef DEBUG
  PRUint16 nodeType;
  nsresult result = aCurrentNode->GetNodeType(&nodeType);
  NS_ASSERTION(NS_SUCCEEDED(result) &&
               (controller || nodeType == nsIDOMNode::DOCUMENT_NODE),
               "No selection controller for non document node!");
#endif
  if (!controller)
    return NS_OK;

  // Register 'this' as selection listener for the normal selection.
  nsCOMPtr<nsISelection> normalSel;
  controller->GetSelection(nsISelectionController::SELECTION_NORMAL,
                           getter_AddRefs(normalSel));
  nsCOMPtr<nsISelectionPrivate> selPrivate(do_QueryInterface(normalSel));
  NS_ENSURE_TRUE(selPrivate, NS_ERROR_FAILURE);

  nsresult rv = selPrivate->AddSelectionListener(this);
  NS_ENSURE_SUCCESS(rv, rv);

  // Register 'this' as selection listener for the spellcheck selection.
  nsCOMPtr<nsISelection> spellcheckSel;
  controller->GetSelection(nsISelectionController::SELECTION_SPELLCHECK,
                           getter_AddRefs(spellcheckSel));
  selPrivate = do_QueryInterface(spellcheckSel);
  NS_ENSURE_TRUE(selPrivate, NS_ERROR_FAILURE);
  
  return selPrivate->AddSelectionListener(this);
}

nsresult
nsCaretAccessible::AddDocSelectionListener(nsIPresShell *aShell)
{
  NS_ENSURE_TRUE(mRootAccessible, NS_ERROR_FAILURE);

  nsCOMPtr<nsISelectionController> selCon = do_QueryInterface(aShell);
  NS_ENSURE_TRUE(selCon, NS_ERROR_FAILURE);

  nsCOMPtr<nsISelection> domSel;
  selCon->GetSelection(nsISelectionController::SELECTION_NORMAL, getter_AddRefs(domSel));
  nsCOMPtr<nsISelectionPrivate> selPrivate = do_QueryInterface(domSel);
  NS_ENSURE_TRUE(selPrivate, NS_ERROR_FAILURE);

  nsresult rv = selPrivate->AddSelectionListener(this);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISelection> spellcheckSel;
  selCon->GetSelection(nsISelectionController::SELECTION_SPELLCHECK,
                       getter_AddRefs(spellcheckSel));
  selPrivate = do_QueryInterface(spellcheckSel);
  NS_ENSURE_TRUE(selPrivate, NS_ERROR_FAILURE);
  
  return selPrivate->AddSelectionListener(this);
}

nsresult
nsCaretAccessible::RemoveDocSelectionListener(nsIPresShell *aShell)
{
  nsCOMPtr<nsISelectionController> selCon = do_QueryInterface(aShell);
  NS_ENSURE_TRUE(selCon, NS_ERROR_FAILURE);

  nsCOMPtr<nsISelection> domSel;
  selCon->GetSelection(nsISelectionController::SELECTION_NORMAL, getter_AddRefs(domSel));
  nsCOMPtr<nsISelectionPrivate> selPrivate = do_QueryInterface(domSel);
  NS_ENSURE_TRUE(selPrivate, NS_ERROR_FAILURE);

  selPrivate->RemoveSelectionListener(this);

  nsCOMPtr<nsISelection> spellcheckSel;
  selCon->GetSelection(nsISelectionController::SELECTION_SPELLCHECK,
                       getter_AddRefs(spellcheckSel));
  selPrivate = do_QueryInterface(spellcheckSel);
  NS_ENSURE_TRUE(selPrivate, NS_ERROR_FAILURE);

  return selPrivate->RemoveSelectionListener(this);
}

NS_IMETHODIMP
nsCaretAccessible::NotifySelectionChanged(nsIDOMDocument *aDoc,
                                          nsISelection *aSel,
                                          PRInt16 aReason)
{
  NS_ENSURE_ARG(aDoc);

  nsCOMPtr<nsIDOMNode> docNode(do_QueryInterface(aDoc));
  nsCOMPtr<nsIAccessibleDocument> accDoc =
    nsAccessNode::GetDocAccessibleFor(docNode);

  // Don't fire events until document is loaded.
  if (!accDoc)
    return NS_OK;

  nsCOMPtr<nsIAccessible> accForDoc(do_QueryInterface(accDoc));
  if (nsAccUtils::State(accForDoc) & nsIAccessibleStates::STATE_BUSY)
    return NS_OK;

  nsCOMPtr<nsISelection2> sel2(do_QueryInterface(aSel));

  PRInt16 type = 0;
  sel2->GetType(&type);

  if (type == nsISelectionController::SELECTION_NORMAL)
    return NormalSelectionChanged(aDoc, aSel);

  if (type == nsISelectionController::SELECTION_SPELLCHECK)
    return SpellcheckSelectionChanged(aDoc, aSel);

  return NS_OK;
}

nsresult
nsCaretAccessible::NormalSelectionChanged(nsIDOMDocument *aDoc,
                                          nsISelection *aSel)
{
  NS_ENSURE_TRUE(mRootAccessible, NS_ERROR_FAILURE);

  mLastUsedSelection = do_GetWeakReference(aSel);

  PRInt32 rangeCount = 0;
  nsresult rv = aSel->GetRangeCount(&rangeCount);
  NS_ENSURE_SUCCESS(rv, rv);

  if (rangeCount == 0) {
    mLastTextAccessible = nsnull;
    return NS_OK; // No selection
  }

  nsCOMPtr<nsIDOMNode> textNode;
  nsCOMPtr<nsIAccessibleText> textAcc =
    nsAccUtils::GetTextAccessibleFromSelection(aSel, getter_AddRefs(textNode));
  NS_ENSURE_STATE(textAcc);

  PRInt32 caretOffset;
  rv = textAcc->GetCaretOffset(&caretOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  if (textAcc == mLastTextAccessible && caretOffset == mLastCaretOffset) {
    PRInt32 selectionCount;
    textAcc->GetSelectionCount(&selectionCount);   // Don't swallow similar events when selecting text
    if (!selectionCount) {
      return NS_OK;  // Swallow duplicate caret event
    }
  }
  mLastCaretOffset = caretOffset;
  mLastTextAccessible = textAcc;

  nsCOMPtr<nsIAccessibleCaretMoveEvent> event =
    new nsAccCaretMoveEvent(textNode);
  NS_ENSURE_TRUE(event, NS_ERROR_OUT_OF_MEMORY);

  return mRootAccessible->FireDelayedAccessibleEvent(event);
}

nsresult
nsCaretAccessible::SpellcheckSelectionChanged(nsIDOMDocument *aDoc,
                                              nsISelection *aSel)
{
  // XXX: fire an event for accessible of focus node of the selection. If
  // spellchecking is enabled then we will fire the number of events for
  // the same accessible for newly appended range of the selection (for every
  // misspelled word). If spellchecking is disabled (for example,
  // @spellcheck="false" on html:body) then we won't fire any event.

  nsCOMPtr<nsIAccessibleText> textAcc =
    nsAccUtils::GetTextAccessibleFromSelection(aSel);
  NS_ENSURE_STATE(textAcc);

  nsCOMPtr<nsIAccessible> acc(do_QueryInterface(textAcc));

  nsCOMPtr<nsIAccessibleEvent> event =
    new nsAccEvent(nsIAccessibleEvent::EVENT_TEXT_ATTRIBUTE_CHANGED,
                   acc, nsnull);
  NS_ENSURE_TRUE(event, NS_ERROR_OUT_OF_MEMORY);

  return mRootAccessible->FireAccessibleEvent(event);
}

nsIntRect
nsCaretAccessible::GetCaretRect(nsIWidget **aOutWidget)
{
  nsIntRect caretRect;
  NS_ENSURE_TRUE(aOutWidget, caretRect);
  *aOutWidget = nsnull;
  NS_ENSURE_TRUE(mRootAccessible, caretRect);

  if (!mLastTextAccessible) {
    return caretRect;    // Return empty rect
  }

  nsCOMPtr<nsIAccessNode> lastAccessNode(do_QueryInterface(mLastTextAccessible));
  NS_ENSURE_TRUE(lastAccessNode, caretRect);

  nsCOMPtr<nsIDOMNode> lastNodeWithCaret;
  lastAccessNode->GetDOMNode(getter_AddRefs(lastNodeWithCaret));
  NS_ENSURE_TRUE(lastNodeWithCaret, caretRect);

  nsCOMPtr<nsIPresShell> presShell =
    nsCoreUtils::GetPresShellFor(lastNodeWithCaret);
  NS_ENSURE_TRUE(presShell, caretRect);

  nsRefPtr<nsCaret> caret;
  presShell->GetCaret(getter_AddRefs(caret));
  NS_ENSURE_TRUE(caret, caretRect);

  PRBool isCollapsed;
  nsIView *view;
  nsCOMPtr<nsISelection> caretSelection(do_QueryReferent(mLastUsedSelection));
  NS_ENSURE_TRUE(caretSelection, caretRect);
  
  nsRect rect;
  caret->GetCaretCoordinates(nsCaret::eRenderingViewCoordinates, caretSelection,
                             &rect, &isCollapsed, &view);
  if (!view || rect.IsEmpty()) {
    return nsIntRect(); // Return empty rect
  }

  PRBool isVisible;
  caret->GetCaretVisible(&isVisible);
  if (!isVisible) {
    return nsIntRect();  // Return empty rect
  }
  nsPoint offsetFromWidget;
  *aOutWidget = view->GetNearestWidget(&offsetFromWidget);
  NS_ENSURE_TRUE(*aOutWidget, nsIntRect());

  nsPresContext *presContext = presShell->GetPresContext();
  NS_ENSURE_TRUE(presContext, nsIntRect());

  rect += offsetFromWidget;
  caretRect = rect.ToOutsidePixels(presContext->AppUnitsPerDevPixel());

  caretRect.MoveBy((*aOutWidget)->WidgetToScreenOffset());

  // Correct for character size, so that caret always matches the size of the character
  // This is important for font size transitions, and is necessary because the Gecko caret uses the
  // previous character's size as the user moves forward in the text by character.
  PRInt32 charX, charY, charWidth, charHeight;
  if (NS_SUCCEEDED(mLastTextAccessible->GetCharacterExtents(mLastCaretOffset, &charX, &charY,
                                                            &charWidth, &charHeight,
                                                            nsIAccessibleCoordinateType::COORDTYPE_SCREEN_RELATIVE))) {
    caretRect.height -= charY - caretRect.y;
    caretRect.y = charY;
  }

  return caretRect;
}

already_AddRefed<nsISelectionController>
nsCaretAccessible::GetSelectionControllerForNode(nsIDOMNode *aNode)
{
  if (!aNode)
    return nsnull;

  nsCOMPtr<nsIPresShell> presShell = nsCoreUtils::GetPresShellFor(aNode);
  if (!presShell)
    return nsnull;

  nsCOMPtr<nsIDocument> doc = presShell->GetDocument();
  if (!doc)
    return nsnull;

  // Get selection controller only for form controls, not for the document.
  nsCOMPtr<nsIContent> content(do_QueryInterface(aNode));
  if (!content)
    return nsnull;

  nsIFrame *frame = presShell->GetPrimaryFrameFor(content);
  if (!frame)
    return nsnull;

  nsPresContext *presContext = presShell->GetPresContext();
  if (!presContext)
    return nsnull;

  nsISelectionController *controller = nsnull;
  frame->GetSelectionController(presContext, &controller);
  return controller;
}


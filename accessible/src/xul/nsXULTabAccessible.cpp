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
 *   Author: John Gaunt (jgaunt@netscape.com)
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

#include "nsXULTabAccessible.h"

#include "nsAccUtils.h"
#include "Relation.h"
#include "States.h"

// NOTE: alphabetically ordered
#include "nsIAccessibleRelation.h"
#include "nsIDocument.h"
#include "nsIFrame.h"
#include "nsIDOMDocument.h"
#include "nsIDOMXULSelectCntrlEl.h"
#include "nsIDOMXULSelectCntrlItemEl.h"
#include "nsIDOMXULRelatedElement.h"

using namespace mozilla::a11y;

////////////////////////////////////////////////////////////////////////////////
// nsXULTabAccessible
////////////////////////////////////////////////////////////////////////////////

nsXULTabAccessible::
  nsXULTabAccessible(nsIContent *aContent, nsIWeakReference *aShell) :
  nsAccessibleWrap(aContent, aShell)
{
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTabAccessible: nsIAccessible

PRUint8
nsXULTabAccessible::ActionCount()
{
  return 1;
}

/** Return the name of our only action  */
NS_IMETHODIMP nsXULTabAccessible::GetActionName(PRUint8 aIndex, nsAString& aName)
{
  if (aIndex == eAction_Switch) {
    aName.AssignLiteral("switch"); 
    return NS_OK;
  }
  return NS_ERROR_INVALID_ARG;
}

/** Tell the tab to do its action */
NS_IMETHODIMP nsXULTabAccessible::DoAction(PRUint8 index)
{
  if (index == eAction_Switch) {
    nsCOMPtr<nsIDOMXULElement> tab(do_QueryInterface(mContent));
    if ( tab )
    {
      tab->Click();
      return NS_OK;
    }
    return NS_ERROR_FAILURE;
  }
  return NS_ERROR_INVALID_ARG;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTabAccessible: nsAccessible

PRUint32
nsXULTabAccessible::NativeRole()
{
  return nsIAccessibleRole::ROLE_PAGETAB;
}

PRUint64
nsXULTabAccessible::NativeState()
{
  // Possible states: focused, focusable, unavailable(disabled), offscreen.

  // get focus and disable status from base class
  PRUint64 state = nsAccessibleWrap::NativeState();

  // In the past, tabs have been focusable in classic theme
  // They may be again in the future
  // Check style for -moz-user-focus: normal to see if it's focusable
  state &= ~states::FOCUSABLE;

  nsIFrame *frame = mContent->GetPrimaryFrame();
  if (frame) {
    const nsStyleUserInterface* ui = frame->GetStyleUserInterface();
    if (ui->mUserFocus == NS_STYLE_USER_FOCUS_NORMAL)
      state |= states::FOCUSABLE;
  }

  // Check whether the tab is selected
  state |= states::SELECTABLE;
  state &= ~states::SELECTED;
  nsCOMPtr<nsIDOMXULSelectControlItemElement> tab(do_QueryInterface(mContent));
  if (tab) {
    bool selected = false;
    if (NS_SUCCEEDED(tab->GetSelected(&selected)) && selected)
      state |= states::SELECTED;
  }
  return state;
}

// nsIAccessible
Relation
nsXULTabAccessible::RelationByType(PRUint32 aType)
{
  Relation rel = nsAccessibleWrap::RelationByType(aType);
  if (aType != nsIAccessibleRelation::RELATION_LABEL_FOR)
    return rel;

  // Expose 'LABEL_FOR' relation on tab accessible for tabpanel accessible.
  nsCOMPtr<nsIDOMXULRelatedElement> tabsElm =
    do_QueryInterface(mContent->GetParent());
  if (!tabsElm)
    return rel;

  nsCOMPtr<nsIDOMNode> DOMNode(GetDOMNode());
  nsCOMPtr<nsIDOMNode> tabpanelNode;
  tabsElm->GetRelatedElement(DOMNode, getter_AddRefs(tabpanelNode));
  if (!tabpanelNode)
    return rel;

  nsCOMPtr<nsIContent> tabpanelContent(do_QueryInterface(tabpanelNode));
  rel.AppendTarget(tabpanelContent);
  return rel;
}

void
nsXULTabAccessible::GetPositionAndSizeInternal(PRInt32 *aPosInSet,
                                               PRInt32 *aSetSize)
{
  nsAccUtils::GetPositionAndSizeForXULSelectControlItem(mContent, aPosInSet,
                                                        aSetSize);
}


////////////////////////////////////////////////////////////////////////////////
// nsXULTabsAccessible
////////////////////////////////////////////////////////////////////////////////

nsXULTabsAccessible::
  nsXULTabsAccessible(nsIContent *aContent, nsIWeakReference *aShell) :
  nsXULSelectableAccessible(aContent, aShell)
{
}

PRUint32
nsXULTabsAccessible::NativeRole()
{
  return nsIAccessibleRole::ROLE_PAGETABLIST;
}

PRUint8
nsXULTabsAccessible::ActionCount()
{
  return 0;
}

/** no value */
NS_IMETHODIMP nsXULTabsAccessible::GetValue(nsAString& _retval)
{
  return NS_OK;
}

nsresult
nsXULTabsAccessible::GetNameInternal(nsAString& aName)
{
  // no name
  return NS_OK;
}


////////////////////////////////////////////////////////////////////////////////
// nsXULTabpanelsAccessible
////////////////////////////////////////////////////////////////////////////////

nsXULTabpanelsAccessible::
  nsXULTabpanelsAccessible(nsIContent *aContent, nsIWeakReference *aShell) :
  nsAccessibleWrap(aContent, aShell)
{
}

PRUint32
nsXULTabpanelsAccessible::NativeRole()
{
  return nsIAccessibleRole::ROLE_PANE;
}


////////////////////////////////////////////////////////////////////////////////
// nsXULTabpanelAccessible
////////////////////////////////////////////////////////////////////////////////

nsXULTabpanelAccessible::
  nsXULTabpanelAccessible(nsIContent *aContent, nsIWeakReference *aShell) :
  nsAccessibleWrap(aContent, aShell)
{
}

PRUint32
nsXULTabpanelAccessible::NativeRole()
{
  return nsIAccessibleRole::ROLE_PROPERTYPAGE;
}

Relation
nsXULTabpanelAccessible::RelationByType(PRUint32 aType)
{
  Relation rel = nsAccessibleWrap::RelationByType(aType);
  if (aType != nsIAccessibleRelation::RELATION_LABELLED_BY)
    return rel;

  // Expose 'LABELLED_BY' relation on tabpanel accessible for tab accessible.
  nsCOMPtr<nsIDOMXULRelatedElement> tabpanelsElm =
    do_QueryInterface(mContent->GetParent());
  if (!tabpanelsElm)
    return rel;

  nsCOMPtr<nsIDOMNode> DOMNode(GetDOMNode());
  nsCOMPtr<nsIDOMNode> tabNode;
  tabpanelsElm->GetRelatedElement(DOMNode, getter_AddRefs(tabNode));
  if (!tabNode)
    return rel;

  nsCOMPtr<nsIContent> tabContent(do_QueryInterface(tabNode));
  rel.AppendTarget(tabContent);
  return rel;
}

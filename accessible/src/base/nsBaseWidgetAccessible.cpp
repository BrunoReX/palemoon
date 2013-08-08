/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsBaseWidgetAccessible.h"

#include "Accessible-inl.h"
#include "HyperTextAccessibleWrap.h"
#include "nsAccessibilityService.h"
#include "nsAccUtils.h"
#include "nsCoreUtils.h"
#include "Role.h"
#include "States.h"

#include "nsGUIEvent.h"
#include "nsILink.h"
#include "nsIFrame.h"
#include "nsINameSpaceManager.h"
#include "nsIURI.h"

using namespace mozilla::a11y;

////////////////////////////////////////////////////////////////////////////////
// nsLeafAccessible
////////////////////////////////////////////////////////////////////////////////

nsLeafAccessible::
  nsLeafAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  AccessibleWrap(aContent, aDoc)
{
}

NS_IMPL_ISUPPORTS_INHERITED0(nsLeafAccessible, Accessible)

////////////////////////////////////////////////////////////////////////////////
// nsLeafAccessible: Accessible public

Accessible*
nsLeafAccessible::ChildAtPoint(PRInt32 aX, PRInt32 aY,
                               EWhichChildAtPoint aWhichChild)
{
  // Don't walk into leaf accessibles.
  return this;
}

////////////////////////////////////////////////////////////////////////////////
// nsLeafAccessible: Accessible private

void
nsLeafAccessible::CacheChildren()
{
  // No children for leaf accessible.
}


////////////////////////////////////////////////////////////////////////////////
// nsLinkableAccessible
////////////////////////////////////////////////////////////////////////////////

nsLinkableAccessible::
  nsLinkableAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  AccessibleWrap(aContent, aDoc),
  mActionAcc(nsnull),
  mIsLink(false),
  mIsOnclick(false)
{
}

NS_IMPL_ISUPPORTS_INHERITED0(nsLinkableAccessible, AccessibleWrap)

////////////////////////////////////////////////////////////////////////////////
// nsLinkableAccessible. nsIAccessible

NS_IMETHODIMP
nsLinkableAccessible::TakeFocus()
{
  return mActionAcc ? mActionAcc->TakeFocus() : AccessibleWrap::TakeFocus();
}

PRUint64
nsLinkableAccessible::NativeLinkState() const
{
  if (mIsLink)
    return states::LINKED | (mActionAcc->LinkState() & states::TRAVERSED);

  return 0;
}

void
nsLinkableAccessible::Value(nsString& aValue)
{
  aValue.Truncate();

  Accessible::Value(aValue);
  if (!aValue.IsEmpty())
    return;

  if (aValue.IsEmpty() && mIsLink)
    mActionAcc->Value(aValue);
}


PRUint8
nsLinkableAccessible::ActionCount()
{
  return (mIsOnclick || mIsLink) ? 1 : 0;
}

NS_IMETHODIMP
nsLinkableAccessible::GetActionName(PRUint8 aIndex, nsAString& aName)
{
  aName.Truncate();

  // Action 0 (default action): Jump to link
  if (aIndex == eAction_Jump) {   
    if (mIsLink) {
      aName.AssignLiteral("jump");
      return NS_OK;
    }
    else if (mIsOnclick) {
      aName.AssignLiteral("click");
      return NS_OK;
    }
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  return NS_ERROR_INVALID_ARG;
}

NS_IMETHODIMP
nsLinkableAccessible::DoAction(PRUint8 aIndex)
{
  if (aIndex != eAction_Jump)
    return NS_ERROR_INVALID_ARG;

  return mActionAcc ? mActionAcc->DoAction(aIndex) :
    AccessibleWrap::DoAction(aIndex);
}

KeyBinding
nsLinkableAccessible::AccessKey() const
{
  return mActionAcc ?
    mActionAcc->AccessKey() : Accessible::AccessKey();
}

////////////////////////////////////////////////////////////////////////////////
// nsLinkableAccessible. nsAccessNode

void
nsLinkableAccessible::Shutdown()
{
  mIsLink = false;
  mIsOnclick = false;
  mActionAcc = nsnull;
  AccessibleWrap::Shutdown();
}

////////////////////////////////////////////////////////////////////////////////
// nsLinkableAccessible: HyperLinkAccessible

already_AddRefed<nsIURI>
nsLinkableAccessible::AnchorURIAt(PRUint32 aAnchorIndex)
{
  if (mIsLink) {
    NS_ASSERTION(mActionAcc->IsLink(),
                 "nsIAccessibleHyperLink isn't implemented.");

    if (mActionAcc->IsLink())
      return mActionAcc->AnchorURIAt(aAnchorIndex);
  }

  return nsnull;
}

////////////////////////////////////////////////////////////////////////////////
// nsLinkableAccessible: Accessible protected

void
nsLinkableAccessible::BindToParent(Accessible* aParent,
                                   PRUint32 aIndexInParent)
{
  AccessibleWrap::BindToParent(aParent, aIndexInParent);

  // Cache action content.
  mActionAcc = nsnull;
  mIsLink = false;
  mIsOnclick = false;

  if (nsCoreUtils::HasClickListener(mContent)) {
    mIsOnclick = true;
    return;
  }

  // XXX: The logic looks broken since the click listener may be registered
  // on non accessible node in parent chain but this node is skipped when tree
  // is traversed.
  Accessible* walkUpAcc = this;
  while ((walkUpAcc = walkUpAcc->Parent()) && !walkUpAcc->IsDoc()) {
    if (walkUpAcc->LinkState() & states::LINKED) {
      mIsLink = true;
      mActionAcc = walkUpAcc;
      return;
    }

    if (nsCoreUtils::HasClickListener(walkUpAcc->GetContent())) {
      mActionAcc = walkUpAcc;
      mIsOnclick = true;
      return;
    }
  }
}

void
nsLinkableAccessible::UnbindFromParent()
{
  mActionAcc = nsnull;
  mIsLink = false;
  mIsOnclick = false;

  AccessibleWrap::UnbindFromParent();
}

////////////////////////////////////////////////////////////////////////////////
// nsEnumRoleAccessible
////////////////////////////////////////////////////////////////////////////////

nsEnumRoleAccessible::
  nsEnumRoleAccessible(nsIContent* aNode, DocAccessible* aDoc,
                       roles::Role aRole) :
  AccessibleWrap(aNode, aDoc), mRole(aRole)
{
}

NS_IMPL_ISUPPORTS_INHERITED0(nsEnumRoleAccessible, Accessible)

role
nsEnumRoleAccessible::NativeRole()
{
  return mRole;
}

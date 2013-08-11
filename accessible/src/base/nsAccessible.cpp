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
 *   John Gaunt (jgaunt@netscape.com)
 *   Aaron Leventhal (aaronl@netscape.com)
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

#include "nsAccessible.h"
#include "nsAccessibleRelation.h"
#include "nsHyperTextAccessibleWrap.h"

#include "nsIAccessibleDocument.h"
#include "nsIAccessibleHyperText.h"
#include "nsIXBLAccessible.h"
#include "nsAccessibleTreeWalker.h"

#include "nsIDOMElement.h"
#include "nsIDOMDocument.h"
#include "nsIDOMDocumentXBL.h"
#include "nsIDOMDocumentTraversal.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsIDOMNodeFilter.h"
#include "nsIDOMNSHTMLElement.h"
#include "nsIDOMTreeWalker.h"
#include "nsIDOMXULButtonElement.h"
#include "nsIDOMXULDocument.h"
#include "nsIDOMXULElement.h"
#include "nsIDOMXULLabelElement.h"
#include "nsIDOMXULSelectCntrlEl.h"
#include "nsIDOMXULSelectCntrlItemEl.h"
#include "nsPIDOMWindow.h"

#include "nsIDocument.h"
#include "nsIContent.h"
#include "nsIForm.h"
#include "nsIFormControl.h"

#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "nsIFrame.h"
#include "nsIViewManager.h"
#include "nsIDocShellTreeItem.h"
#include "nsIScrollableFrame.h"
#include "nsFocusManager.h"

#include "nsXPIDLString.h"
#include "nsUnicharUtils.h"
#include "nsReadableUtils.h"
#include "prdtoa.h"
#include "nsIAtom.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsIURI.h"
#include "nsITimer.h"
#include "nsArrayUtils.h"
#include "nsIMutableArray.h"
#include "nsIObserverService.h"
#include "nsIServiceManager.h"
#include "nsWhitespaceTokenizer.h"
#include "nsAttrName.h"
#include "nsNetUtil.h"

#ifdef NS_DEBUG
#include "nsIFrameDebug.h"
#include "nsIDOMCharacterData.h"
#endif

/**
 * nsAccessibleDOMStringList implementation
 */
nsAccessibleDOMStringList::nsAccessibleDOMStringList()
{
}

nsAccessibleDOMStringList::~nsAccessibleDOMStringList()
{
}

NS_IMPL_ISUPPORTS1(nsAccessibleDOMStringList, nsIDOMDOMStringList)

NS_IMETHODIMP
nsAccessibleDOMStringList::Item(PRUint32 aIndex, nsAString& aResult)
{
  if (aIndex >= mNames.Length()) {
    SetDOMStringToNull(aResult);
  } else {
    aResult = mNames.ElementAt(aIndex);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAccessibleDOMStringList::GetLength(PRUint32 *aLength)
{
  *aLength = mNames.Length();

  return NS_OK;
}

NS_IMETHODIMP
nsAccessibleDOMStringList::Contains(const nsAString& aString, PRBool *aResult)
{
  *aResult = mNames.Contains(aString);

  return NS_OK;
}

/*
 * Class nsAccessible
 */

////////////////////////////////////////////////////////////////////////////////
// nsAccessible. nsISupports

NS_IMPL_CYCLE_COLLECTION_CLASS(nsAccessible)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsAccessible, nsAccessNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mParent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mFirstChild)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mNextSibling)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsAccessible, nsAccessNode)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mFirstChild)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mNextSibling)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ADDREF_INHERITED(nsAccessible, nsAccessNode)
NS_IMPL_RELEASE_INHERITED(nsAccessible, nsAccessNode)

nsresult nsAccessible::QueryInterface(REFNSIID aIID, void** aInstancePtr)
{
  // Custom-built QueryInterface() knows when we support nsIAccessibleSelectable
  // based on role attribute and aria-multiselectable
  *aInstancePtr = nsnull;

  if (aIID.Equals(NS_GET_IID(nsXPCOMCycleCollectionParticipant))) {
    *aInstancePtr = &NS_CYCLE_COLLECTION_NAME(nsAccessible);
    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsIAccessible))) {
    *aInstancePtr = static_cast<nsIAccessible*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsAccessible))) {
    *aInstancePtr = static_cast<nsAccessible*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsIAccessibleSelectable))) {
    nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
    if (!content) {
      return NS_ERROR_FAILURE; // This accessible has been shut down
    }
    if (content->HasAttr(kNameSpaceID_None, nsAccessibilityAtoms::role)) {
      // If we have an XHTML role attribute present and the
      // aria-multiselectable attribute is true, then we need
      // to support nsIAccessibleSelectable
      // If either attribute (role or multiselectable) change, then we'll
      // destroy this accessible so that we can follow COM identity rules.
      nsAutoString multiselectable;
      if (content->AttrValueIs(kNameSpaceID_None, nsAccessibilityAtoms::aria_multiselectable,
                               nsAccessibilityAtoms::_true, eCaseMatters)) {
        *aInstancePtr = static_cast<nsIAccessibleSelectable*>(this);
        NS_ADDREF_THIS();
        return NS_OK;
      }
    }
  }

  if (aIID.Equals(NS_GET_IID(nsIAccessibleValue))) {
    if (mRoleMapEntry && mRoleMapEntry->valueRule != eNoValue) {
      *aInstancePtr = static_cast<nsIAccessibleValue*>(this);
      NS_ADDREF_THIS();
      return NS_OK;
    }
  }                       

  if (aIID.Equals(NS_GET_IID(nsIAccessibleHyperLink))) {
    nsCOMPtr<nsIAccessible> parent(GetParent());
    nsCOMPtr<nsIAccessibleHyperText> hyperTextParent(do_QueryInterface(parent));
    if (hyperTextParent) {
      *aInstancePtr = static_cast<nsIAccessibleHyperLink*>(this);
      NS_ADDREF_THIS();
      return NS_OK;
    }
    return NS_ERROR_NO_INTERFACE;
  }

  return nsAccessNodeWrap::QueryInterface(aIID, aInstancePtr);
}

nsAccessible::nsAccessible(nsIDOMNode* aNode, nsIWeakReference* aShell): nsAccessNodeWrap(aNode, aShell), 
  mParent(nsnull), mFirstChild(nsnull), mNextSibling(nsnull), mRoleMapEntry(nsnull),
  mAccChildCount(eChildCountUninitialized)
{
#ifdef NS_DEBUG_X
   {
     nsCOMPtr<nsIPresShell> shell(do_QueryReferent(aShell));
     printf(">>> %p Created Acc - DOM: %p  PS: %p", 
            (void*)static_cast<nsIAccessible*>(this), (void*)aNode,
            (void*)shell.get());
    nsCOMPtr<nsIContent> content = do_QueryInterface(aNode);
    if (content) {
      nsAutoString buf;
      if (content->NodeInfo())
        content->NodeInfo()->GetQualifiedName(buf);
      printf(" Con: %s@%p", NS_ConvertUTF16toUTF8(buf).get(), (void *)content.get());
      if (NS_SUCCEEDED(GetName(buf))) {
        printf(" Name:[%s]", NS_ConvertUTF16toUTF8(buf).get());
       }
     }
     printf("\n");
   }
#endif
}

//-----------------------------------------------------
// destruction
//-----------------------------------------------------
nsAccessible::~nsAccessible()
{
}

void
nsAccessible::SetRoleMapEntry(nsRoleMapEntry* aRoleMapEntry)
{
  mRoleMapEntry = aRoleMapEntry;
}

NS_IMETHODIMP
nsAccessible::GetName(nsAString& aName)
{
  aName.Truncate();

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  GetARIAName(aName);
  if (!aName.IsEmpty())
    return NS_OK;

  nsCOMPtr<nsIXBLAccessible> xblAccessible(do_QueryInterface(mDOMNode));
  if (xblAccessible) {
    xblAccessible->GetAccessibleName(aName);
    if (!aName.IsEmpty())
      return NS_OK;
  }

  nsresult rv = GetNameInternal(aName);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!aName.IsEmpty())
    return NS_OK;

  // In the end get the name from tooltip.
  nsCOMPtr<nsIContent> content = nsCoreUtils::GetRoleContent(mDOMNode);
  if (!content)
    return NS_OK;

  nsIAtom *tooltipAttr = nsnull;

  if (content->IsNodeOfType(nsINode::eHTML))
    tooltipAttr = nsAccessibilityAtoms::title;
  else if (content->IsNodeOfType(nsINode::eXUL))
    tooltipAttr = nsAccessibilityAtoms::tooltiptext;
  else
    return NS_OK;

  // XXX: if CompressWhiteSpace worked on nsAString we could avoid a copy.
  nsAutoString name;
  if (content->GetAttr(kNameSpaceID_None, tooltipAttr, name)) {
    name.CompressWhitespace();
    aName = name;
    return NS_OK_NAME_FROM_TOOLTIP;
  }

  if (rv != NS_OK_EMPTY_NAME)
    aName.SetIsVoid(PR_TRUE);

  return NS_OK;
}

NS_IMETHODIMP nsAccessible::GetDescription(nsAString& aDescription)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // There are 4 conditions that make an accessible have no accDescription:
  // 1. it's a text node; or
  // 2. It has no DHTML describedby property
  // 3. it doesn't have an accName; or
  // 4. its title attribute already equals to its accName nsAutoString name; 
  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  NS_ASSERTION(content, "No content of valid accessible!");
  if (!content)
    return NS_ERROR_FAILURE;

  if (!content->IsNodeOfType(nsINode::eTEXT)) {
    nsAutoString description;
    nsresult rv = nsTextEquivUtils::
      GetTextEquivFromIDRefs(this, nsAccessibilityAtoms::aria_describedby,
                             description);
    NS_ENSURE_SUCCESS(rv, rv);

    if (description.IsEmpty()) {
      PRBool isXUL = content->IsNodeOfType(nsINode::eXUL);
      if (isXUL) {
        // Try XUL <description control="[id]">description text</description>
        nsIContent *descriptionContent =
          nsCoreUtils::FindNeighbourPointingToNode(content,
                                                   nsAccessibilityAtoms::control,
                                                   nsAccessibilityAtoms::description);

        if (descriptionContent) {
          // We have a description content node
          nsTextEquivUtils::
            AppendTextEquivFromContent(this, descriptionContent, &description);
        }
      }
      if (description.IsEmpty()) {
        nsIAtom *descAtom = isXUL ? nsAccessibilityAtoms::tooltiptext :
                                    nsAccessibilityAtoms::title;
        if (content->GetAttr(kNameSpaceID_None, descAtom, description)) {
          nsAutoString name;
          GetName(name);
          if (name.IsEmpty() || description == name) {
            // Don't use tooltip for a description if this object
            // has no name or the tooltip is the same as the name
            description.Truncate();
          }
        }
      }
    }
    description.CompressWhitespace();
    aDescription = description;
  }

  return NS_OK;
}

// mask values for ui.key.chromeAccess and ui.key.contentAccess
#define NS_MODIFIER_SHIFT    1
#define NS_MODIFIER_CONTROL  2
#define NS_MODIFIER_ALT      4
#define NS_MODIFIER_META     8

// returns the accesskey modifier mask used in the given node's context
// (i.e. chrome or content), or 0 if an error occurs
static PRInt32
GetAccessModifierMask(nsIContent* aContent)
{
  nsCOMPtr<nsIPrefBranch> prefBranch =
    do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (!prefBranch)
    return 0;

  // use ui.key.generalAccessKey (unless it is -1)
  PRInt32 accessKey;
  nsresult rv = prefBranch->GetIntPref("ui.key.generalAccessKey", &accessKey);
  if (NS_SUCCEEDED(rv) && accessKey != -1) {
    switch (accessKey) {
      case nsIDOMKeyEvent::DOM_VK_SHIFT:   return NS_MODIFIER_SHIFT;
      case nsIDOMKeyEvent::DOM_VK_CONTROL: return NS_MODIFIER_CONTROL;
      case nsIDOMKeyEvent::DOM_VK_ALT:     return NS_MODIFIER_ALT;
      case nsIDOMKeyEvent::DOM_VK_META:    return NS_MODIFIER_META;
      default:                             return 0;
    }
  }

  // get the docShell to this DOMNode, return 0 on failure
  nsCOMPtr<nsIDocument> document = aContent->GetCurrentDoc();
  if (!document)
    return 0;
  nsCOMPtr<nsISupports> container = document->GetContainer();
  if (!container)
    return 0;
  nsCOMPtr<nsIDocShellTreeItem> treeItem(do_QueryInterface(container));
  if (!treeItem)
    return 0;

  // determine the access modifier used in this context
  PRInt32 itemType, accessModifierMask = 0;
  treeItem->GetItemType(&itemType);
  switch (itemType) {

  case nsIDocShellTreeItem::typeChrome:
    rv = prefBranch->GetIntPref("ui.key.chromeAccess", &accessModifierMask);
    break;

  case nsIDocShellTreeItem::typeContent:
    rv = prefBranch->GetIntPref("ui.key.contentAccess", &accessModifierMask);
    break;
  }

  return NS_SUCCEEDED(rv) ? accessModifierMask : 0;
}

NS_IMETHODIMP
nsAccessible::GetKeyboardShortcut(nsAString& aAccessKey)
{
  aAccessKey.Truncate();

  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  if (!content)
    return NS_ERROR_FAILURE;

  PRUint32 key = nsCoreUtils::GetAccessKeyFor(content);
  if (!key && content->IsNodeOfType(nsIContent::eELEMENT)) {
    // Copy access key from label node unless it is labeled
    // via an ancestor <label>, in which case that would be redundant
    nsCOMPtr<nsIContent> labelContent(nsCoreUtils::GetLabelContent(content));
    nsCOMPtr<nsIDOMNode> labelNode = do_QueryInterface(labelContent);
    if (labelNode && !nsCoreUtils::IsAncestorOf(labelNode, mDOMNode))
      key = nsCoreUtils::GetAccessKeyFor(labelContent);
  }

  if (!key)
    return NS_OK;

  nsAutoString accesskey(key);

  // Append the modifiers in reverse order, result: Control+Alt+Shift+Meta+<key>
  nsAutoString propertyKey;
  PRInt32 modifierMask = GetAccessModifierMask(content);
  if (modifierMask & NS_MODIFIER_META) {
    propertyKey.AssignLiteral("VK_META");
    nsAccessible::GetFullKeyName(propertyKey, accesskey, accesskey);
  }
  if (modifierMask & NS_MODIFIER_SHIFT) {
    propertyKey.AssignLiteral("VK_SHIFT");
    nsAccessible::GetFullKeyName(propertyKey, accesskey, accesskey);
  }
  if (modifierMask & NS_MODIFIER_ALT) {
    propertyKey.AssignLiteral("VK_ALT");
    nsAccessible::GetFullKeyName(propertyKey, accesskey, accesskey);
  }
  if (modifierMask & NS_MODIFIER_CONTROL) {
    propertyKey.AssignLiteral("VK_CONTROL");
    nsAccessible::GetFullKeyName(propertyKey, accesskey, accesskey);
  }

  aAccessKey = accesskey;
  return NS_OK;
}

void
nsAccessible::SetParent(nsIAccessible *aParent)
{
  if (mParent != aParent) {
    // Adopt a child -- we allow this now. the new parent
    // may be a dom node which wasn't previously accessible but now is.
    // The old parent's children now need to be invalidated, since 
    // it no longer owns the child, the new parent does
    nsRefPtr<nsAccessible> oldParent = nsAccUtils::QueryAccessible(mParent);
    if (oldParent)
      oldParent->InvalidateChildren();
  }

  mParent = aParent;
}

void
nsAccessible::SetFirstChild(nsIAccessible *aFirstChild)
{
  mFirstChild = aFirstChild;
}

void
nsAccessible::SetNextSibling(nsIAccessible *aNextSibling)
{
  mNextSibling = aNextSibling;
}

nsresult
nsAccessible::Shutdown()
{
  mNextSibling = nsnull;

  // Invalidate the child count and pointers to other accessibles, also make
  // sure none of its children point to this parent
  InvalidateChildren();
  if (mParent) {
    nsRefPtr<nsAccessible> parent(nsAccUtils::QueryAccessible(mParent));
    parent->InvalidateChildren();
    mParent = nsnull;
  }

  return nsAccessNodeWrap::Shutdown();
}

void
nsAccessible::InvalidateChildren()
{
  // Document has transformed, reset our invalid children and child count

  // Reset the sibling pointers, they will be set up again the next time
  // CacheChildren() is called.
  // Note: we don't want to start creating accessibles at this point,
  // so don't use GetNextSibling() here. (bug 387252)
  nsRefPtr<nsAccessible> child = nsAccUtils::QueryAccessible(mFirstChild);
  while (child) {
    child->mParent = nsnull;

    nsCOMPtr<nsIAccessible> next = child->mNextSibling;
    child->mNextSibling = nsnull;
    child = nsAccUtils::QueryAccessible(next);
  }

  mAccChildCount = eChildCountUninitialized;
  mFirstChild = nsnull;
}

NS_IMETHODIMP
nsAccessible::GetParent(nsIAccessible **aParent)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAccessible> cachedParent = GetCachedParent();
  if (cachedParent) {
    cachedParent.swap(*aParent);
    return NS_OK;
  }

  nsCOMPtr<nsIAccessibleDocument> docAccessible(GetDocAccessible());
  NS_ENSURE_TRUE(docAccessible, NS_ERROR_FAILURE);

  return docAccessible->GetAccessibleInParentChain(mDOMNode, PR_TRUE, aParent);
}

already_AddRefed<nsIAccessible>
nsAccessible::GetCachedParent()
{
  if (IsDefunct())
    return nsnull;

  nsCOMPtr<nsIAccessible> cachedParent = mParent;
  return cachedParent.forget();
}

already_AddRefed<nsIAccessible>
nsAccessible::GetCachedFirstChild()
{
  if (IsDefunct())
    return nsnull;

  nsCOMPtr<nsIAccessible> cachedFirstChild = mFirstChild;
  return cachedFirstChild.forget();
}

  /* readonly attribute nsIAccessible nextSibling; */
NS_IMETHODIMP nsAccessible::GetNextSibling(nsIAccessible * *aNextSibling) 
{ 
  *aNextSibling = nsnull; 
  if (!mWeakShell) {
    // This node has been shut down
    return NS_ERROR_FAILURE;
  }
  if (!mParent) {
    nsCOMPtr<nsIAccessible> parent(GetParent());
    if (parent) {
      PRInt32 numChildren;
      parent->GetChildCount(&numChildren);  // Make sure we cache all of the children
    }
  }

  if (mNextSibling || !mParent) {
    // If no parent, don't try to calculate a new sibling
    // It either means we're at the root or shutting down the parent
    NS_IF_ADDREF(*aNextSibling = mNextSibling);

    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

  /* readonly attribute nsIAccessible previousSibling; */
NS_IMETHODIMP nsAccessible::GetPreviousSibling(nsIAccessible * *aPreviousSibling) 
{
  *aPreviousSibling = nsnull;

  if (!mWeakShell) {
    // This node has been shut down
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIAccessible> parent;
  if (NS_FAILED(GetParent(getter_AddRefs(parent))) || !parent) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIAccessible> testAccessible, prevSibling;
  parent->GetFirstChild(getter_AddRefs(testAccessible));
  while (testAccessible && this != testAccessible) {
    prevSibling = testAccessible;
    prevSibling->GetNextSibling(getter_AddRefs(testAccessible));
  }

  if (!prevSibling) {
    return NS_ERROR_FAILURE;
  }

  NS_ADDREF(*aPreviousSibling = prevSibling);
  return NS_OK;
}

  /* readonly attribute nsIAccessible firstChild; */
NS_IMETHODIMP nsAccessible::GetFirstChild(nsIAccessible * *aFirstChild) 
{  
  if (gIsCacheDisabled) {
    InvalidateChildren();
  }
  PRInt32 numChildren;
  GetChildCount(&numChildren);  // Make sure we cache all of the children

#ifdef DEBUG
  nsRefPtr<nsAccessible> firstChild(nsAccUtils::QueryAccessible(mFirstChild));
  if (firstChild) {
    nsCOMPtr<nsIAccessible> realParent = firstChild->GetCachedParent();
    NS_ASSERTION(!realParent || realParent == this,
                 "Two accessibles have the same first child accessible.");
  }
#endif

  NS_IF_ADDREF(*aFirstChild = mFirstChild);

  return NS_OK;  
}

  /* readonly attribute nsIAccessible lastChild; */
NS_IMETHODIMP nsAccessible::GetLastChild(nsIAccessible * *aLastChild)
{  
  GetChildAt(-1, aLastChild);
  return NS_OK;
}

NS_IMETHODIMP nsAccessible::GetChildAt(PRInt32 aChildNum, nsIAccessible **aChild)
{
  // aChildNum is a zero-based index

  PRInt32 numChildren;
  GetChildCount(&numChildren);

  // If no children or aChildNum is larger than numChildren, return null
  if (aChildNum >= numChildren || numChildren == 0 || !mWeakShell) {
    *aChild = nsnull;
    return NS_ERROR_FAILURE;
  // If aChildNum is less than zero, set aChild to last index
  } else if (aChildNum < 0) {
    aChildNum = numChildren - 1;
  }

  nsCOMPtr<nsIAccessible> current(mFirstChild), nextSibling;
  PRInt32 index = 0;

  while (current) {
    nextSibling = current;
    if (++index > aChildNum) {
      break;
    }
    nextSibling->GetNextSibling(getter_AddRefs(current));
  }

  NS_IF_ADDREF(*aChild = nextSibling);

  return NS_OK;
}

// readonly attribute nsIArray children;
NS_IMETHODIMP nsAccessible::GetChildren(nsIArray **aOutChildren)
{
  *aOutChildren = nsnull;
  nsCOMPtr<nsIMutableArray> children = do_CreateInstance(NS_ARRAY_CONTRACTID);
  if (!children)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAccessible> curChild;
  while (NextChild(curChild)) {
    children->AppendElement(curChild, PR_FALSE);
  }
  
  NS_ADDREF(*aOutChildren = children);
  return NS_OK;
}

nsIAccessible *nsAccessible::NextChild(nsCOMPtr<nsIAccessible>& aAccessible)
{
  nsCOMPtr<nsIAccessible> nextChild;
  if (!aAccessible) {
    GetFirstChild(getter_AddRefs(nextChild));
  }
  else {
    aAccessible->GetNextSibling(getter_AddRefs(nextChild));
  }
  return (aAccessible = nextChild);
}

void nsAccessible::CacheChildren()
{
  if (!mWeakShell) {
    // This node has been shut down
    mAccChildCount = eChildCountUninitialized;
    return;
  }

  if (mAccChildCount == eChildCountUninitialized) {
    mAccChildCount = 0;// Prevent reentry
    PRBool allowsAnonChildren = GetAllowsAnonChildAccessibles();
    nsAccessibleTreeWalker walker(mWeakShell, mDOMNode, allowsAnonChildren);
    // Seed the frame hint early while we're still on a container node.
    // This is better than doing the GetPrimaryFrameFor() later on
    // a text node, because text nodes aren't in the frame map.
    walker.mState.frame = GetFrame();

    nsRefPtr<nsAccessible> prevAcc;
    PRInt32 childCount = 0;
    walker.GetFirstChild();
    SetFirstChild(walker.mState.accessible);

    while (walker.mState.accessible) {
      ++ childCount;
      prevAcc = nsAccUtils::QueryAccessible(walker.mState.accessible);
      prevAcc->SetParent(this);
      walker.GetNextSibling();
      prevAcc->SetNextSibling(walker.mState.accessible);
    }
    mAccChildCount = childCount;
  }
}

PRBool
nsAccessible::GetAllowsAnonChildAccessibles()
{
  return PR_TRUE;
}

/* readonly attribute long childCount; */
NS_IMETHODIMP nsAccessible::GetChildCount(PRInt32 *aAccChildCount) 
{
  CacheChildren();
  *aAccChildCount = mAccChildCount;
  return NS_OK;  
}

/* readonly attribute long indexInParent; */
NS_IMETHODIMP nsAccessible::GetIndexInParent(PRInt32 *aIndexInParent)
{
  *aIndexInParent = -1;
  if (!mWeakShell) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIAccessible> parent;
  GetParent(getter_AddRefs(parent));
  if (!parent) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIAccessible> sibling;
  parent->GetFirstChild(getter_AddRefs(sibling));
  if (!sibling) {
    return NS_ERROR_FAILURE;
  }

  *aIndexInParent = 0;
  while (sibling != this) {
    NS_ASSERTION(sibling, "Never ran into the same child that we started from");

    if (!sibling)
      return NS_ERROR_FAILURE;

    ++*aIndexInParent;
    nsCOMPtr<nsIAccessible> tempAccessible;
    sibling->GetNextSibling(getter_AddRefs(tempAccessible));
    sibling = tempAccessible;
  }

  return NS_OK;
}

void
nsAccessible::TestChildCache(nsIAccessible *aCachedChild)
{
#ifdef DEBUG_A11Y
  // All cached accessible nodes should be in the parent
  // It will assert if not all the children were created
  // when they were first cached, and no invalidation
  // ever corrected parent accessible's child cache.
  if (mAccChildCount <= 0)
    return;

  nsCOMPtr<nsIAccessible> sibling = mFirstChild;

  while (sibling != aCachedChild) {
    NS_ASSERTION(sibling, "[TestChildCache] Never ran into the same child that we started from");
    if (!sibling)
      return;

    nsCOMPtr<nsIAccessible> tempAccessible;
    sibling->GetNextSibling(getter_AddRefs(tempAccessible));
    sibling = tempAccessible;
  }

#endif
}

nsresult nsAccessible::GetTranslatedString(const nsAString& aKey, nsAString& aStringOut)
{
  nsXPIDLString xsValue;

  if (!gStringBundle || 
    NS_FAILED(gStringBundle->GetStringFromName(PromiseFlatString(aKey).get(), getter_Copies(xsValue)))) 
    return NS_ERROR_FAILURE;

  aStringOut.Assign(xsValue);
  return NS_OK;
}

nsresult nsAccessible::GetFullKeyName(const nsAString& aModifierName, const nsAString& aKeyName, nsAString& aStringOut)
{
  nsXPIDLString modifierName, separator;

  if (!gKeyStringBundle ||
      NS_FAILED(gKeyStringBundle->GetStringFromName(PromiseFlatString(aModifierName).get(), 
                                                    getter_Copies(modifierName))) ||
      NS_FAILED(gKeyStringBundle->GetStringFromName(PromiseFlatString(NS_LITERAL_STRING("MODIFIER_SEPARATOR")).get(), 
                                                    getter_Copies(separator)))) {
    return NS_ERROR_FAILURE;
  }

  aStringOut = modifierName + separator + aKeyName; 
  return NS_OK;
}

PRBool nsAccessible::IsVisible(PRBool *aIsOffscreen) 
{
  // We need to know if at least a kMinPixels around the object is visible
  // Otherwise it will be marked nsIAccessibleStates::STATE_OFFSCREEN
  // The STATE_INVISIBLE flag is for elements which are programmatically hidden
  
  *aIsOffscreen = PR_TRUE;
  if (!mDOMNode) {
    return PR_FALSE; // Defunct object
  }

  const PRUint16 kMinPixels  = 12;
   // Set up the variables we need, return false if we can't get at them all
  nsCOMPtr<nsIPresShell> shell(GetPresShell());
  if (!shell) 
    return PR_FALSE;

  nsIViewManager* viewManager = shell->GetViewManager();
  if (!viewManager)
    return PR_FALSE;

  nsIFrame *frame = GetFrame();
  if (!frame) {
    return PR_FALSE;
  }

  // If visibility:hidden or visibility:collapsed then mark with STATE_INVISIBLE
  if (!frame->GetStyleVisibility()->IsVisible())
  {
      return PR_FALSE;
  }

  nsPresContext *presContext = shell->GetPresContext();
  if (!presContext)
    return PR_FALSE;

  // Get the bounds of the current frame, relative to the current view.
  // We don't use the more accurate GetBoundsRect, because that is more expensive
  // and the STATE_OFFSCREEN flag that this is used for only needs to be a rough
  // indicator

  nsRect relFrameRect = frame->GetRect();
  nsIView *containingView = frame->GetViewExternal();
  if (containingView) {
    // When frame itself has a view, it has the same bounds as the view
    relFrameRect.x = relFrameRect.y = 0;
  }
  else {
    nsPoint frameOffset;
    frame->GetOffsetFromView(frameOffset, &containingView);
    if (!containingView)
      return PR_FALSE;  // no view -- not visible
    relFrameRect.x = frameOffset.x;
    relFrameRect.y = frameOffset.y;
  }

  nsRectVisibility rectVisibility;
  viewManager->GetRectVisibility(containingView, relFrameRect,
                                 nsPresContext::CSSPixelsToAppUnits(kMinPixels),
                                 &rectVisibility);

  if (rectVisibility == nsRectVisibility_kZeroAreaRect) {
    nsIAtom *frameType = frame->GetType();
    if (frameType == nsAccessibilityAtoms::textFrame) {
      // Zero area rects can occur in the first frame of a multi-frame text flow,
      // in which case the rendered text is not empty and the frame should not be marked invisible
      nsAutoString renderedText;
      frame->GetRenderedText (&renderedText, nsnull, nsnull, 0, 1);
      if (!renderedText.IsEmpty()) {
        rectVisibility = nsRectVisibility_kVisible;
      }
    }
    else if (frameType == nsAccessibilityAtoms::inlineFrame) {
      // Yuck. Unfortunately inline frames can contain larger frames inside of them,
      // so we can't really believe this is a zero area rect without checking more deeply.
      // GetBounds() will do that for us.
      PRInt32 x, y, width, height;
      GetBounds(&x, &y, &width, &height);
      if (width > 0 && height > 0) {
        rectVisibility = nsRectVisibility_kVisible;    
      }
    }
  }

  if (rectVisibility == nsRectVisibility_kZeroAreaRect && frame && 
      0 == (frame->GetStateBits() & NS_FRAME_OUT_OF_FLOW)) {
    // Consider zero area objects hidden unless they are absoultely positioned
    // or floating and may have descendants that have a non-zero size
    return PR_FALSE;
  }
  
  // Currently one of:
  // nsRectVisibility_kVisible, 
  // nsRectVisibility_kAboveViewport, 
  // nsRectVisibility_kBelowViewport, 
  // nsRectVisibility_kLeftOfViewport, 
  // nsRectVisibility_kRightOfViewport
  // This view says it is visible, but we need to check the parent view chain :(
  nsCOMPtr<nsIDOMDocument> domDoc;
  mDOMNode->GetOwnerDocument(getter_AddRefs(domDoc));
  nsCOMPtr<nsIDocument> doc(do_QueryInterface(domDoc));
  if (!doc)  {
    return PR_FALSE;
  }

  PRBool isVisible = CheckVisibilityInParentChain(doc, containingView);
  if (isVisible && rectVisibility == nsRectVisibility_kVisible) {
    *aIsOffscreen = PR_FALSE;
  }
  return isVisible;
}

nsresult
nsAccessible::GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState)
{
  *aState = 0;

  if (IsDefunct()) {
    if (aExtraState)
      *aExtraState = nsIAccessibleStates::EXT_STATE_DEFUNCT;

    return NS_OK_DEFUNCT_OBJECT;
  }

  if (aExtraState)
    *aExtraState = 0;

  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  if (!content) {
    return NS_OK;  // On document, this is not an error
  }

  // Set STATE_UNAVAILABLE state based on disabled attribute
  // The disabled attribute is mostly used in XUL elements and HTML forms, but
  // if someone sets it on another attribute, 
  // it seems reasonable to consider it unavailable
  PRBool isDisabled;
  if (content->IsNodeOfType(nsINode::eHTML)) {
    // In HTML, just the presence of the disabled attribute means it is disabled,
    // therefore disabled="false" indicates disabled!
    isDisabled = content->HasAttr(kNameSpaceID_None, nsAccessibilityAtoms::disabled);
  }
  else {
    isDisabled = content->AttrValueIs(kNameSpaceID_None,
                                      nsAccessibilityAtoms::disabled,
                                      nsAccessibilityAtoms::_true,
                                      eCaseMatters);
  }
  if (isDisabled) {
    *aState |= nsIAccessibleStates::STATE_UNAVAILABLE;
  }
  else if (content->IsNodeOfType(nsINode::eELEMENT)) {
    nsIFrame *frame = GetFrame();
    if (frame && frame->IsFocusable()) {
      *aState |= nsIAccessibleStates::STATE_FOCUSABLE;
    }

    if (gLastFocusedNode == mDOMNode) {
      *aState |= nsIAccessibleStates::STATE_FOCUSED;
    }
  }

  // Check if nsIAccessibleStates::STATE_INVISIBLE and
  // STATE_OFFSCREEN flags should be turned on for this object.
  PRBool isOffscreen;
  if (!IsVisible(&isOffscreen)) {
    *aState |= nsIAccessibleStates::STATE_INVISIBLE;
  }
  if (isOffscreen) {
    *aState |= nsIAccessibleStates::STATE_OFFSCREEN;
  }

  nsIFrame *frame = GetFrame();
  if (frame && (frame->GetStateBits() & NS_FRAME_OUT_OF_FLOW))
    *aState |= nsIAccessibleStates::STATE_FLOATING;

  // Add 'linked' state for simple xlink.
  if (nsCoreUtils::IsXLink(content))
    *aState |= nsIAccessibleStates::STATE_LINKED;

  return NS_OK;
}

  /* readonly attribute boolean focusedChild; */
NS_IMETHODIMP nsAccessible::GetFocusedChild(nsIAccessible **aFocusedChild) 
{ 
  nsCOMPtr<nsIAccessible> focusedChild;
  if (gLastFocusedNode == mDOMNode) {
    focusedChild = this;
  }
  else if (gLastFocusedNode) {
    nsCOMPtr<nsIAccessibilityService> accService =
      do_GetService("@mozilla.org/accessibilityService;1");
    NS_ENSURE_TRUE(accService, NS_ERROR_FAILURE);

    accService->GetAccessibleFor(gLastFocusedNode,
                                 getter_AddRefs(focusedChild));
    if (focusedChild) {
      nsCOMPtr<nsIAccessible> focusedParentAccessible;
      focusedChild->GetParent(getter_AddRefs(focusedParentAccessible));
      if (focusedParentAccessible != this) {
        focusedChild = nsnull;
      }
    }
  }

  NS_IF_ADDREF(*aFocusedChild = focusedChild);
  return NS_OK;
}

// nsAccessible::GetChildAtPoint()
nsresult
nsAccessible::GetChildAtPoint(PRInt32 aX, PRInt32 aY, PRBool aDeepestChild,
                              nsIAccessible **aChild)
{
  // If we can't find the point in a child, we will return the fallback answer:
  // we return |this| if the point is within it, otherwise nsnull.
  PRInt32 x = 0, y = 0, width = 0, height = 0;
  nsresult rv = GetBounds(&x, &y, &width, &height);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIAccessible> fallbackAnswer;
  if (aX >= x && aX < x + width && aY >= y && aY < y + height)
    fallbackAnswer = this;

  if (nsAccUtils::MustPrune(this)) {  // Do not dig any further
    NS_IF_ADDREF(*aChild = fallbackAnswer);
    return NS_OK;
  }

  // Search an accessible at the given point starting from accessible document
  // because containing block (see CSS2) for out of flow element (for example,
  // absolutely positioned element) may be different from its DOM parent and
  // therefore accessible for containing block may be different from accessible
  // for DOM parent but GetFrameForPoint() should be called for containing block
  // to get an out of flow element.
  nsCOMPtr<nsIAccessibleDocument> accDocument;
  rv = GetAccessibleDocument(getter_AddRefs(accDocument));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(accDocument, NS_ERROR_FAILURE);

  nsRefPtr<nsAccessNode> docAccessNode =
    nsAccUtils::QueryAccessNode(accDocument);

  nsIFrame *frame = docAccessNode->GetFrame();
  NS_ENSURE_STATE(frame);

  nsPresContext *presContext = frame->PresContext();

  nsIntRect screenRect = frame->GetScreenRectExternal();
  nsPoint offset(presContext->DevPixelsToAppUnits(aX - screenRect.x),
                 presContext->DevPixelsToAppUnits(aY - screenRect.y));

  nsCOMPtr<nsIPresShell> presShell = presContext->PresShell();
  nsIFrame *foundFrame = presShell->GetFrameForPoint(frame, offset);

  nsIContent* content = nsnull;
  if (!foundFrame || !(content = foundFrame->GetContent())) {
    NS_IF_ADDREF(*aChild = fallbackAnswer);
    return NS_OK;
  }

  nsCOMPtr<nsIDOMNode> node(do_QueryInterface(content));
  nsCOMPtr<nsIAccessibilityService> accService = GetAccService();

  nsCOMPtr<nsIDOMNode> relevantNode;
  accService->GetRelevantContentNodeFor(node, getter_AddRefs(relevantNode));
  if (!relevantNode) {
    NS_IF_ADDREF(*aChild = fallbackAnswer);
    return NS_OK;
  }

  nsCOMPtr<nsIAccessible> accessible;
  accService->GetAccessibleFor(relevantNode, getter_AddRefs(accessible));
  if (!accessible) {
    // No accessible for the node with the point, so find the first
    // accessible in the DOM parent chain
    accDocument->GetAccessibleInParentChain(relevantNode, PR_TRUE,
                                            getter_AddRefs(accessible));
    if (!accessible) {
      NS_IF_ADDREF(*aChild = fallbackAnswer);
      return NS_OK;
    }
  }

  if (accessible == this) {
    // Manually walk through accessible children and see if the are within this
    // point. Skip offscreen or invisible accessibles. This takes care of cases
    // where layout won't walk into things for us, such as image map areas and
    // sub documents (XXX: subdocuments should be handled by methods of
    // nsOuterDocAccessibles).
    nsCOMPtr<nsIAccessible> child;
    while (NextChild(child)) {
      PRInt32 childX, childY, childWidth, childHeight;
      child->GetBounds(&childX, &childY, &childWidth, &childHeight);
      if (aX >= childX && aX < childX + childWidth &&
          aY >= childY && aY < childY + childHeight &&
          (nsAccUtils::State(child) & nsIAccessibleStates::STATE_INVISIBLE) == 0) {

        if (aDeepestChild)
          return child->GetDeepestChildAtPoint(aX, aY, aChild);

        NS_IF_ADDREF(*aChild = child);
        return NS_OK;
      }
    }

    // The point is in this accessible but not in a child. We are allowed to
    // return |this| as the answer.
    NS_IF_ADDREF(*aChild = accessible);
    return NS_OK;
  }

  // Since DOM node of obtained accessible may be out of flow then we should
  // ensure obtained accessible is a child of this accessible.
  nsCOMPtr<nsIAccessible> parent, child(accessible);
  while (PR_TRUE) {
    child->GetParent(getter_AddRefs(parent));
    if (!parent) {
      // Reached the top of the hierarchy. These bounds were inside an
      // accessible that is not a descendant of this one.
      NS_IF_ADDREF(*aChild = fallbackAnswer);      
      return NS_OK;
    }

    if (parent == this) {
      NS_ADDREF(*aChild = (aDeepestChild ? accessible : child));
      return NS_OK;
    }
    child.swap(parent);
  }

  return NS_OK;
}

// nsIAccessible getChildAtPoint(in long x, in long y)
NS_IMETHODIMP
nsAccessible::GetChildAtPoint(PRInt32 aX, PRInt32 aY,
                              nsIAccessible **aAccessible)
{
  NS_ENSURE_ARG_POINTER(aAccessible);
  *aAccessible = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  return GetChildAtPoint(aX, aY, PR_FALSE, aAccessible);
}

// nsIAccessible getDeepestChildAtPoint(in long x, in long y)
NS_IMETHODIMP
nsAccessible::GetDeepestChildAtPoint(PRInt32 aX, PRInt32 aY,
                                     nsIAccessible **aAccessible)
{
  NS_ENSURE_ARG_POINTER(aAccessible);
  *aAccessible = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  return GetChildAtPoint(aX, aY, PR_TRUE, aAccessible);
}

void nsAccessible::GetBoundsRect(nsRect& aTotalBounds, nsIFrame** aBoundingFrame)
{
/*
 * This method is used to determine the bounds of a content node.
 * Because HTML wraps and links are not always rectangular, this
 * method uses the following algorithm:
 *
 * 1) Start with an empty rectangle
 * 2) Add the rect for the primary frame from for the DOM node.
 * 3) For each next frame at the same depth with the same DOM node, add that rect to total
 * 4) If that frame is an inline frame, search deeper at that point in the tree, adding all rects
 */

  // Initialization area
  *aBoundingFrame = nsnull;
  nsIFrame *firstFrame = GetBoundsFrame();
  if (!firstFrame)
    return;

  // Find common relative parent
  // This is an ancestor frame that will incompass all frames for this content node.
  // We need the relative parent so we can get absolute screen coordinates
  nsIFrame *ancestorFrame = firstFrame;

  while (ancestorFrame) {  
    *aBoundingFrame = ancestorFrame;
    // If any other frame type, we only need to deal with the primary frame
    // Otherwise, there may be more frames attached to the same content node
    if (!nsCoreUtils::IsCorrectFrameType(ancestorFrame,
                                         nsAccessibilityAtoms::inlineFrame) &&
        !nsCoreUtils::IsCorrectFrameType(ancestorFrame,
                                         nsAccessibilityAtoms::textFrame))
      break;
    ancestorFrame = ancestorFrame->GetParent();
  }

  nsIFrame *iterFrame = firstFrame;
  nsCOMPtr<nsIContent> firstContent(do_QueryInterface(mDOMNode));
  nsIContent* iterContent = firstContent;
  PRInt32 depth = 0;

  // Look only at frames below this depth, or at this depth (if we're still on the content node we started with)
  while (iterContent == firstContent || depth > 0) {
    // Coordinates will come back relative to parent frame
    nsRect currFrameBounds = iterFrame->GetRect();
    
    // Make this frame's bounds relative to common parent frame
    currFrameBounds +=
      iterFrame->GetParent()->GetOffsetToExternal(*aBoundingFrame);

    // Add this frame's bounds to total
    aTotalBounds.UnionRect(aTotalBounds, currFrameBounds);

    nsIFrame *iterNextFrame = nsnull;

    if (nsCoreUtils::IsCorrectFrameType(iterFrame,
                                        nsAccessibilityAtoms::inlineFrame)) {
      // Only do deeper bounds search if we're on an inline frame
      // Inline frames can contain larger frames inside of them
      iterNextFrame = iterFrame->GetFirstChild(nsnull);
    }

    if (iterNextFrame) 
      ++depth;  // Child was found in code above this: We are going deeper in this iteration of the loop
    else {  
      // Use next sibling if it exists, or go back up the tree to get the first next-in-flow or next-sibling 
      // within our search
      while (iterFrame) {
        iterNextFrame = iterFrame->GetNextContinuation();
        if (!iterNextFrame)
          iterNextFrame = iterFrame->GetNextSibling();
        if (iterNextFrame || --depth < 0) 
          break;
        iterFrame = iterFrame->GetParent();
      }
    }

    // Get ready for the next round of our loop
    iterFrame = iterNextFrame;
    if (iterFrame == nsnull)
      break;
    iterContent = nsnull;
    if (depth == 0)
      iterContent = iterFrame->GetContent();
  }
}


/* void getBounds (out long x, out long y, out long width, out long height); */
NS_IMETHODIMP nsAccessible::GetBounds(PRInt32 *x, PRInt32 *y, PRInt32 *width, PRInt32 *height)
{
  // This routine will get the entire rectange for all the frames in this node
  // -------------------------------------------------------------------------
  //      Primary Frame for node
  //  Another frame, same node                <- Example
  //  Another frame, same node

  nsPresContext *presContext = GetPresContext();
  if (!presContext)
  {
    *x = *y = *width = *height = 0;
    return NS_ERROR_FAILURE;
  }

  nsRect unionRectTwips;
  nsIFrame* aBoundingFrame = nsnull;
  GetBoundsRect(unionRectTwips, &aBoundingFrame);   // Unions up all primary frames for this node and all siblings after it
  if (!aBoundingFrame) {
    *x = *y = *width = *height = 0;
    return NS_ERROR_FAILURE;
  }

  *x      = presContext->AppUnitsToDevPixels(unionRectTwips.x); 
  *y      = presContext->AppUnitsToDevPixels(unionRectTwips.y);
  *width  = presContext->AppUnitsToDevPixels(unionRectTwips.width);
  *height = presContext->AppUnitsToDevPixels(unionRectTwips.height);

  // We have the union of the rectangle, now we need to put it in absolute screen coords

  nsIntRect orgRectPixels = aBoundingFrame->GetScreenRectExternal();
  *x += orgRectPixels.x;
  *y += orgRectPixels.y;

  return NS_OK;
}

// helpers

nsIFrame* nsAccessible::GetBoundsFrame()
{
  return GetFrame();
}

/* void removeSelection (); */
NS_IMETHODIMP nsAccessible::SetSelected(PRBool aSelect)
{
  // Add or remove selection
  if (!mDOMNode) {
    return NS_ERROR_FAILURE;
  }

  PRUint32 state = nsAccUtils::State(this);
  if (state & nsIAccessibleStates::STATE_SELECTABLE) {
    nsCOMPtr<nsIAccessible> multiSelect =
      nsAccUtils::GetMultiSelectFor(mDOMNode);
    if (!multiSelect) {
      return aSelect ? TakeFocus() : NS_ERROR_FAILURE;
    }
    nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
    NS_ASSERTION(content, "Called for dead accessible");

    if (mRoleMapEntry) {
      if (aSelect) {
        return content->SetAttr(kNameSpaceID_None, nsAccessibilityAtoms::aria_selected,
                                NS_LITERAL_STRING("true"), PR_TRUE);
      }
      return content->UnsetAttr(kNameSpaceID_None, nsAccessibilityAtoms::aria_selected, PR_TRUE);
    }
  }

  return NS_ERROR_FAILURE;
}

/* void takeSelection (); */
NS_IMETHODIMP nsAccessible::TakeSelection()
{
  // Select only this item
  if (!mDOMNode) {
    return NS_ERROR_FAILURE;
  }

  PRUint32 state = nsAccUtils::State(this);
  if (state & nsIAccessibleStates::STATE_SELECTABLE) {
    nsCOMPtr<nsIAccessible> multiSelect =
      nsAccUtils::GetMultiSelectFor(mDOMNode);
    if (multiSelect) {
      nsCOMPtr<nsIAccessibleSelectable> selectable = do_QueryInterface(multiSelect);
      selectable->ClearSelection();
    }
    return SetSelected(PR_TRUE);
  }

  return NS_ERROR_FAILURE;
}

/* void takeFocus (); */
NS_IMETHODIMP
nsAccessible::TakeFocus()
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));

  nsIFrame *frame = GetFrame();
  NS_ENSURE_STATE(frame);

  // If the current element can't take real DOM focus and if it has an ID and
  // ancestor with a the aria-activedescendant attribute present, then set DOM
  // focus to that ancestor and set aria-activedescendant on the ancestor to
  // the ID of the desired element.
  if (!frame->IsFocusable()) {
    nsAutoString id;
    if (content && nsCoreUtils::GetID(content, id)) {

      nsCOMPtr<nsIContent> ancestorContent = content;
      while ((ancestorContent = ancestorContent->GetParent()) &&
             !ancestorContent->HasAttr(kNameSpaceID_None,
                                       nsAccessibilityAtoms::aria_activedescendant));

      if (ancestorContent) {
        nsCOMPtr<nsIPresShell> presShell(do_QueryReferent(mWeakShell));
        if (presShell) {
          nsIFrame *frame = presShell->GetPrimaryFrameFor(ancestorContent);
          if (frame && frame->IsFocusable()) {

            content = ancestorContent;            
            content->SetAttr(kNameSpaceID_None,
                             nsAccessibilityAtoms::aria_activedescendant,
                             id, PR_TRUE);
          }
        }
      }
    }
  }

  nsCOMPtr<nsIDOMElement> element(do_QueryInterface(content));
  nsCOMPtr<nsIFocusManager> fm = do_GetService(FOCUSMANAGER_CONTRACTID);
  if (fm)
    fm->SetFocus(element, 0);

  return NS_OK;
}

nsresult
nsAccessible::GetHTMLName(nsAString& aLabel)
{
  nsCOMPtr<nsIContent> content = nsCoreUtils::GetRoleContent(mDOMNode);
  if (!content) {
    aLabel.SetIsVoid(PR_TRUE);
    return NS_OK;
  }

  nsIContent *labelContent = nsCoreUtils::GetHTMLLabelContent(content);
  if (labelContent) {
    nsAutoString label;
    nsresult rv =
      nsTextEquivUtils::AppendTextEquivFromContent(this, labelContent, &label);
    NS_ENSURE_SUCCESS(rv, rv);

    label.CompressWhitespace();
    if (!label.IsEmpty()) {
      aLabel = label;
      return NS_OK;
    }
  }

  return nsTextEquivUtils::GetNameFromSubtree(this, aLabel);
}

/**
  * 3 main cases for XUL Controls to be labeled
  *   1 - control contains label="foo"
  *   2 - control has, as a child, a label element
  *        - label has either value="foo" or children
  *   3 - non-child label contains control="controlID"
  *        - label has either value="foo" or children
  * Once a label is found, the search is discontinued, so a control
  *  that has a label child as well as having a label external to
  *  the control that uses the control="controlID" syntax will use
  *  the child label for its Name.
  */
nsresult
nsAccessible::GetXULName(nsAString& aLabel)
{
  // CASE #1 (via label attribute) -- great majority of the cases
  nsresult rv = NS_OK;

  nsAutoString label;
  nsCOMPtr<nsIDOMXULLabeledControlElement> labeledEl(do_QueryInterface(mDOMNode));
  if (labeledEl) {
    rv = labeledEl->GetLabel(label);
  }
  else {
    nsCOMPtr<nsIDOMXULSelectControlItemElement> itemEl(do_QueryInterface(mDOMNode));
    if (itemEl) {
      rv = itemEl->GetLabel(label);
    }
    else {
      nsCOMPtr<nsIDOMXULSelectControlElement> select(do_QueryInterface(mDOMNode));
      // Use label if this is not a select control element which 
      // uses label attribute to indicate which option is selected
      if (!select) {
        nsCOMPtr<nsIDOMXULElement> xulEl(do_QueryInterface(mDOMNode));
        if (xulEl) {
          rv = xulEl->GetAttribute(NS_LITERAL_STRING("label"), label);
        }
      }
    }
  }

  // CASES #2 and #3 ------ label as a child or <label control="id" ... > </label>
  nsCOMPtr<nsIContent> content = nsCoreUtils::GetRoleContent(mDOMNode);
  if (!content)
    return NS_OK;

  if (NS_FAILED(rv) || label.IsEmpty()) {
    label.Truncate();
    nsIContent *labelContent =
      nsCoreUtils::FindNeighbourPointingToNode(content, nsAccessibilityAtoms::control,
                                               nsAccessibilityAtoms::label);

    nsCOMPtr<nsIDOMXULLabelElement> xulLabel(do_QueryInterface(labelContent));
    // Check if label's value attribute is used
    if (xulLabel && NS_SUCCEEDED(xulLabel->GetValue(label)) && label.IsEmpty()) {
      // If no value attribute, a non-empty label must contain
      // children that define its text -- possibly using HTML
      nsTextEquivUtils::AppendTextEquivFromContent(this, labelContent, &label);
    }
  }

  // XXX If CompressWhiteSpace worked on nsAString we could avoid a copy
  label.CompressWhitespace();
  if (!label.IsEmpty()) {
    aLabel = label;
    return NS_OK;
  }

  // Can get text from title of <toolbaritem> if we're a child of a <toolbaritem>
  nsIContent *bindingParent = content->GetBindingParent();
  nsIContent *parent = bindingParent? bindingParent->GetParent() :
                                      content->GetParent();
  while (parent) {
    if (parent->Tag() == nsAccessibilityAtoms::toolbaritem &&
        parent->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::title, label)) {
      label.CompressWhitespace();
      aLabel = label;
      return NS_OK;
    }
    parent = parent->GetParent();
  }

  return nsTextEquivUtils::GetNameFromSubtree(this, aLabel);
}

nsresult
nsAccessible::FireAccessibleEvent(nsIAccessibleEvent *aEvent)
{
  NS_ENSURE_ARG_POINTER(aEvent);
  nsCOMPtr<nsIDOMNode> eventNode;
  aEvent->GetDOMNode(getter_AddRefs(eventNode));
  NS_ENSURE_TRUE(nsAccUtils::IsNodeRelevant(eventNode), NS_ERROR_FAILURE);

  nsCOMPtr<nsIObserverService> obsService =
    do_GetService("@mozilla.org/observer-service;1");
  NS_ENSURE_TRUE(obsService, NS_ERROR_FAILURE);

  return obsService->NotifyObservers(aEvent, NS_ACCESSIBLE_EVENT_TOPIC, nsnull);
}

NS_IMETHODIMP
nsAccessible::GetRole(PRUint32 *aRole)
{
  NS_ENSURE_ARG_POINTER(aRole);

  *aRole = nsIAccessibleRole::ROLE_NOTHING;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (mRoleMapEntry) {
    *aRole = mRoleMapEntry->role;

    // These unfortunate exceptions don't fit into the ARIA table
    // This is where the nsIAccessible role depends on both the role and ARIA state
    if (*aRole == nsIAccessibleRole::ROLE_PUSHBUTTON) {
      nsCOMPtr<nsIContent> content = do_QueryInterface(mDOMNode);
      if (content) {
        if (nsAccUtils::HasDefinedARIAToken(content, nsAccessibilityAtoms::aria_pressed)) {
          // For simplicity, any existing pressed attribute except "", or "undefined"
          // indicates a toggle
          *aRole = nsIAccessibleRole::ROLE_TOGGLE_BUTTON;
        }
        else if (content->AttrValueIs(kNameSpaceID_None, nsAccessibilityAtoms::aria_haspopup,
                                      nsAccessibilityAtoms::_true, eCaseMatters)) {
          // For button with aria-haspopup="true"
          *aRole = nsIAccessibleRole::ROLE_BUTTONMENU;
        }
      }
    }
    else if (*aRole == nsIAccessibleRole::ROLE_LISTBOX) {
      // A listbox inside of a combo box needs a special role because of ATK mapping to menu
      nsCOMPtr<nsIAccessible> possibleCombo;
      GetParent(getter_AddRefs(possibleCombo));
      if (nsAccUtils::Role(possibleCombo) == nsIAccessibleRole::ROLE_COMBOBOX) {
        *aRole = nsIAccessibleRole::ROLE_COMBOBOX_LIST;
      }
      else {   // Check to see if combo owns the listbox instead
        possibleCombo = nsRelUtils::
          GetRelatedAccessible(this, nsIAccessibleRelation::RELATION_NODE_CHILD_OF);
        if (nsAccUtils::Role(possibleCombo) == nsIAccessibleRole::ROLE_COMBOBOX)
          *aRole = nsIAccessibleRole::ROLE_COMBOBOX_LIST;
      }
    }
    else if (*aRole == nsIAccessibleRole::ROLE_OPTION) {
      nsCOMPtr<nsIAccessible> parent;
      GetParent(getter_AddRefs(parent));
      if (nsAccUtils::Role(parent) == nsIAccessibleRole::ROLE_COMBOBOX_LIST)
        *aRole = nsIAccessibleRole::ROLE_COMBOBOX_OPTION;
    }

    // We are done if the mapped role trumps native semantics
    if (mRoleMapEntry->roleRule == kUseMapRole)
      return NS_OK;
  }

  return GetRoleInternal(aRole);
}

NS_IMETHODIMP
nsAccessible::GetAttributes(nsIPersistentProperties **aAttributes)
{
  NS_ENSURE_ARG_POINTER(aAttributes);  // In/out param. Created if necessary.
  
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIContent> content = nsCoreUtils::GetRoleContent(mDOMNode);
  if (!content) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPersistentProperties> attributes = *aAttributes;
  if (!attributes) {
    // Create only if an array wasn't already passed in
    attributes = do_CreateInstance(NS_PERSISTENTPROPERTIES_CONTRACTID);
    NS_ENSURE_TRUE(attributes, NS_ERROR_OUT_OF_MEMORY);
    NS_ADDREF(*aAttributes = attributes);
  }
 
  nsresult rv = GetAttributesInternal(attributes);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString id;
  nsAutoString oldValueUnused;
  if (nsCoreUtils::GetID(content, id)) {
    // Expose ID. If an <iframe id> exists override the one on the <body> of the source doc,
    // because the specific instance is what makes the ID useful for scripts
    attributes->SetStringProperty(NS_LITERAL_CSTRING("id"), id, oldValueUnused);
  }
  
  nsAutoString xmlRoles;
  if (content->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::role, xmlRoles)) {
    attributes->SetStringProperty(NS_LITERAL_CSTRING("xml-roles"),  xmlRoles, oldValueUnused);          
  }

  nsCOMPtr<nsIAccessibleValue> supportsValue = do_QueryInterface(static_cast<nsIAccessible*>(this));
  if (supportsValue) {
    // We support values, so expose the string value as well, via the valuetext object attribute
    // We test for the value interface because we don't want to expose traditional get_accValue()
    // information such as URL's on links and documents, or text in an input
    nsAutoString valuetext;
    GetValue(valuetext);
    attributes->SetStringProperty(NS_LITERAL_CSTRING("valuetext"), valuetext, oldValueUnused);
  }

  // Expose checkable object attribute if the accessible has checkable state
  if (nsAccUtils::State(this) & nsIAccessibleStates::STATE_CHECKABLE)
    nsAccUtils::SetAccAttr(attributes, nsAccessibilityAtoms::checkable, NS_LITERAL_STRING("true"));

  // Group attributes (level/setsize/posinset)
  if (!nsAccUtils::HasAccGroupAttrs(attributes)) {
    // Calculate group attributes based on accessible hierarhy if they weren't
    // provided by ARIA or by accessible class implementation.
    PRUint32 role = nsAccUtils::Role(this);
    rv = ComputeGroupAttributes(role, attributes);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Expose object attributes from ARIA attributes.
  PRUint32 numAttrs = content->GetAttrCount();
  for (PRUint32 count = 0; count < numAttrs; count ++) {
    const nsAttrName *attr = content->GetAttrNameAt(count);
    if (attr && attr->NamespaceEquals(kNameSpaceID_None)) {
      nsIAtom *attrAtom = attr->Atom();
      const char *attrStr;
      attrAtom->GetUTF8String(&attrStr);
      if (PL_strncmp(attrStr, "aria-", 5)) 
        continue; // Not ARIA
      PRUint8 attrFlags = nsAccUtils::GetAttributeCharacteristics(attrAtom);
      if (attrFlags & ATTR_BYPASSOBJ)
        continue; // No need to handle exposing as obj attribute here
      if ((attrFlags & ATTR_VALTOKEN) &&
          !nsAccUtils::HasDefinedARIAToken(content, attrAtom))
        continue; // only expose token based attributes if they are defined
      nsAutoString value;
      if (content->GetAttr(kNameSpaceID_None, attrAtom, value)) {
        attributes->SetStringProperty(nsDependentCString(attrStr + 5), value, oldValueUnused);
      }
    }
  }

  // If there is no aria-live attribute then expose default value of 'live'
  // object attribute used for ARIA role of this accessible.
  if (mRoleMapEntry) {
    nsAutoString live;
    nsAccUtils::GetAccAttr(attributes, nsAccessibilityAtoms::live, live);
    if (live.IsEmpty()) {
      if (nsAccUtils::GetLiveAttrValue(mRoleMapEntry->liveAttRule, live))
        nsAccUtils::SetAccAttr(attributes, nsAccessibilityAtoms::live, live);
    }
  }

  return NS_OK;
}

nsresult
nsAccessible::GetAttributesInternal(nsIPersistentProperties *aAttributes)
{
  // Attributes set by this method will not be used to override attributes on a sub-document accessible
  // when there is a <frame>/<iframe> element that spawned the sub-document
  nsIContent *content = nsCoreUtils::GetRoleContent(mDOMNode);
  nsCOMPtr<nsIDOMElement> element(do_QueryInterface(content));
  NS_ENSURE_TRUE(element, NS_ERROR_UNEXPECTED);

  nsAutoString tagName;
  element->GetTagName(tagName);
  if (!tagName.IsEmpty()) {
    nsAutoString oldValueUnused;
    aAttributes->SetStringProperty(NS_LITERAL_CSTRING("tag"), tagName,
                                   oldValueUnused);
  }

  nsAccEvent::GetLastEventAttributes(mDOMNode, aAttributes);
 
  // Expose class because it may have useful microformat information
  // Let the class from an iframe's document be exposed, don't override from <iframe class>
  nsAutoString _class;
  if (content->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::_class, _class))
    nsAccUtils::SetAccAttr(aAttributes, nsAccessibilityAtoms::_class, _class);

  // Get container-foo computed live region properties based on the closest container with
  // the live region attribute. 
  // Inner nodes override outer nodes within the same document --
  //   The inner nodes can be used to override live region behavior on more general outer nodes
  // However, nodes in outer documents override nodes in inner documents:
  //   Outer doc author may want to override properties on a widget they used in an iframe
  nsCOMPtr<nsIDOMNode> startNode = mDOMNode;
  nsIContent *startContent = content;
  while (PR_TRUE) {
    NS_ENSURE_STATE(startContent);
    nsIDocument *doc = startContent->GetDocument();
    nsCOMPtr<nsIDOMNode> docNode = do_QueryInterface(doc);
    NS_ENSURE_STATE(docNode);
    nsIContent *topContent = nsCoreUtils::GetRoleContent(docNode);
    NS_ENSURE_STATE(topContent);
    nsAccUtils::SetLiveContainerAttributes(aAttributes, startContent,
                                           topContent);

    // Allow ARIA live region markup from outer documents to override
    nsCOMPtr<nsISupports> container = doc->GetContainer(); 
    nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem =
      do_QueryInterface(container);
    if (!docShellTreeItem)
      break;

    nsCOMPtr<nsIDocShellTreeItem> sameTypeParent;
    docShellTreeItem->GetSameTypeParent(getter_AddRefs(sameTypeParent));
    if (!sameTypeParent || sameTypeParent == docShellTreeItem)
      break;

    nsIDocument *parentDoc = doc->GetParentDocument();
    if (!parentDoc)
      break;

    startContent = parentDoc->FindContentForSubDocument(doc);      
  }

  // Expose 'display' attribute.
  nsAutoString value;
  nsresult rv = GetComputedStyleValue(EmptyString(),
                                      NS_LITERAL_STRING("display"),
                                      value);
  if (NS_SUCCEEDED(rv))
    nsAccUtils::SetAccAttr(aAttributes, nsAccessibilityAtoms::display,
                           value);

  // Expose 'text-align' attribute.
  rv = GetComputedStyleValue(EmptyString(), NS_LITERAL_STRING("text-align"),
                             value);
  if (NS_SUCCEEDED(rv))
    nsAccUtils::SetAccAttr(aAttributes, nsAccessibilityAtoms::textAlign,
                           value);

  // Expose 'text-indent' attribute.
  rv = GetComputedStyleValue(EmptyString(), NS_LITERAL_STRING("text-indent"),
                             value);
  if (NS_SUCCEEDED(rv))
    nsAccUtils::SetAccAttr(aAttributes, nsAccessibilityAtoms::textIndent,
                           value);

  // Expose draggable object attribute?
  nsCOMPtr<nsIDOMNSHTMLElement> htmlElement = do_QueryInterface(content);
  if (htmlElement) {
    PRBool draggable = PR_FALSE;
    htmlElement->GetDraggable(&draggable);
    if (draggable) {
      nsAccUtils::SetAccAttr(aAttributes, nsAccessibilityAtoms::draggable,
                             NS_LITERAL_STRING("true"));
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAccessible::GroupPosition(PRInt32 *aGroupLevel,
                            PRInt32 *aSimilarItemsInGroup,
                            PRInt32 *aPositionInGroup)
{
  // Every element exposes level/posinset/sizeset for IAccessdible::attributes
  // if they make sense for it. These attributes are mapped into groupPosition.
  // If 'level' attribute doesn't make sense element then it isn't represented
  // via IAccessible::attributes and groupLevel of groupPosition method is 0.
  // Elements that expose 'level' attribute only (like html headings elements)
  // don't support this method and all arguments  are equalled 0.

  NS_ENSURE_ARG_POINTER(aGroupLevel);
  NS_ENSURE_ARG_POINTER(aSimilarItemsInGroup);
  NS_ENSURE_ARG_POINTER(aPositionInGroup);

  *aGroupLevel = 0;
  *aSimilarItemsInGroup = 0;
  *aPositionInGroup = 0;

  nsCOMPtr<nsIPersistentProperties> attributes;
  nsresult rv = GetAttributes(getter_AddRefs(attributes));
  NS_ENSURE_SUCCESS(rv, rv);
  if (!attributes) {
    return NS_ERROR_FAILURE;
  }
  PRInt32 level, posInSet, setSize;
  nsAccUtils::GetAccGroupAttrs(attributes, &level, &posInSet, &setSize);

  if (!posInSet && !setSize)
    return NS_OK;

  *aGroupLevel = level;

  *aPositionInGroup = posInSet;
  *aSimilarItemsInGroup = setSize;

  return NS_OK;
}

NS_IMETHODIMP
nsAccessible::GetState(PRUint32 *aState, PRUint32 *aExtraState)
{
  NS_ENSURE_ARG_POINTER(aState);

/* Commenting out as a stop gap fix for bug 525579
 * We should not flush layout and potentially cause frames deallocation
 * while tree walking (nsAccessibleTreeWalker)
  if (!IsDefunct()) {
    // Flush layout so that all the frame construction, reflow, and styles are
    // up-to-date since we rely on frames, and styles when calculating state.
    // We don't flush the display because we don't care about painting.
    nsCOMPtr<nsIPresShell> presShell = GetPresShell();
    presShell->FlushPendingNotifications(Flush_Layout);
  }
*/

  nsresult rv = GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  // Apply ARIA states to be sure accessible states will be overriden.
  GetARIAState(aState, aExtraState);

  if (mRoleMapEntry && mRoleMapEntry->role == nsIAccessibleRole::ROLE_PAGETAB) {
    if (*aState & nsIAccessibleStates::STATE_FOCUSED) {
      *aState |= nsIAccessibleStates::STATE_SELECTED;
    } else {
      // Expose 'selected' state on ARIA tab if the focus is on internal element
      // of related tabpanel.
      nsCOMPtr<nsIAccessible> tabPanel = nsRelUtils::
        GetRelatedAccessible(this, nsIAccessibleRelation::RELATION_LABEL_FOR);

      if (nsAccUtils::Role(tabPanel) == nsIAccessibleRole::ROLE_PROPERTYPAGE) {
        nsCOMPtr<nsIAccessNode> tabPanelAccessNode(do_QueryInterface(tabPanel));
        nsCOMPtr<nsIDOMNode> tabPanelNode;
        tabPanelAccessNode->GetDOMNode(getter_AddRefs(tabPanelNode));
        NS_ENSURE_STATE(tabPanelNode);

        if (nsCoreUtils::IsAncestorOf(tabPanelNode, gLastFocusedNode))
          *aState |= nsIAccessibleStates::STATE_SELECTED;
      }
    }
  }

  const PRUint32 kExpandCollapseStates =
    nsIAccessibleStates::STATE_COLLAPSED | nsIAccessibleStates::STATE_EXPANDED;
  if ((*aState & kExpandCollapseStates) == kExpandCollapseStates) {
    // Cannot be both expanded and collapsed -- this happens in ARIA expanded
    // combobox because of limitation of nsARIAMap.
    // XXX: Perhaps we will be able to make this less hacky if we support
    // extended states in nsARIAMap, e.g. derive COLLAPSED from
    // EXPANDABLE && !EXPANDED.
    *aState &= ~nsIAccessibleStates::STATE_COLLAPSED;
  }

  // Set additional states which presence depends on another states.
  if (!aExtraState)
    return NS_OK;

  if (!(*aState & nsIAccessibleStates::STATE_UNAVAILABLE)) {
    *aExtraState |= nsIAccessibleStates::EXT_STATE_ENABLED |
                    nsIAccessibleStates::EXT_STATE_SENSITIVE;
  }

  if ((*aState & nsIAccessibleStates::STATE_COLLAPSED) ||
      (*aState & nsIAccessibleStates::STATE_EXPANDED))
    *aExtraState |= nsIAccessibleStates::EXT_STATE_EXPANDABLE;

  if (mRoleMapEntry) {
    // If an object has an ancestor with the activedescendant property
    // pointing at it, we mark it as ACTIVE even if it's not currently focused.
    // This allows screen reader virtual buffer modes to know which descendant
    // is the current one that would get focus if the user navigates to the container widget.
    nsCOMPtr<nsIContent> content = do_QueryInterface(mDOMNode);
    nsAutoString id;
    if (content && nsCoreUtils::GetID(content, id)) {
      nsIContent *ancestorContent = content;
      nsAutoString activeID;
      while ((ancestorContent = ancestorContent->GetParent()) != nsnull) {
        if (ancestorContent->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::aria_activedescendant, activeID)) {
          if (id == activeID) {
            *aExtraState |= nsIAccessibleStates::EXT_STATE_ACTIVE;
          }
          break;
        }
      }
    }
  }

  PRUint32 role;
  rv = GetRole(&role);
  NS_ENSURE_SUCCESS(rv, rv);

  // For some reasons DOM node may have not a frame. We tract such accessibles
  // as invisible.
  nsIFrame *frame = GetFrame();
  if (!frame)
    return NS_OK;

  const nsStyleDisplay* display = frame->GetStyleDisplay();
  if (display && display->mOpacity == 1.0f &&
      !(*aState & nsIAccessibleStates::STATE_INVISIBLE)) {
    *aExtraState |= nsIAccessibleStates::EXT_STATE_OPAQUE;
  }

  const nsStyleXUL *xulStyle = frame->GetStyleXUL();
  if (xulStyle) {
    // In XUL all boxes are either vertical or horizontal
    if (xulStyle->mBoxOrient == NS_STYLE_BOX_ORIENT_VERTICAL) {
      *aExtraState |= nsIAccessibleStates::EXT_STATE_VERTICAL;
    }
    else {
      *aExtraState |= nsIAccessibleStates::EXT_STATE_HORIZONTAL;
    }
  }
  
  // If we are editable, force readonly bit off
  if (*aExtraState & nsIAccessibleStates::EXT_STATE_EDITABLE)
    *aState &= ~nsIAccessibleStates::STATE_READONLY;
 
  return NS_OK;
}

nsresult
nsAccessible::GetARIAState(PRUint32 *aState, PRUint32 *aExtraState)
{
  // Test for universal states first
  nsIContent *content = nsCoreUtils::GetRoleContent(mDOMNode);
  if (!content) {
    return NS_OK;
  }

  PRUint32 index = 0;
  while (nsStateMapEntry::MapToStates(content, aState, aExtraState,
                                      nsARIAMap::gWAIUnivStateMap[index])) {
    ++ index;
  }

  if (mRoleMapEntry) {
    // Once an ARIA role is used, default to not-readonly. This can be overridden
    // by aria-readonly, or if the ARIA role is mapped to readonly by default
    *aState &= ~nsIAccessibleStates::STATE_READONLY;

    if (content->HasAttr(kNameSpaceID_None, content->GetIDAttributeName())) {
      // If has a role & ID and aria-activedescendant on the container, assume focusable
      nsIContent *ancestorContent = content;
      while ((ancestorContent = ancestorContent->GetParent()) != nsnull) {
        if (ancestorContent->HasAttr(kNameSpaceID_None, nsAccessibilityAtoms::aria_activedescendant)) {
            // ancestor has activedescendant property, this content could be active
          *aState |= nsIAccessibleStates::STATE_FOCUSABLE;
          break;
        }
      }
    }
  }

  if (*aState & nsIAccessibleStates::STATE_FOCUSABLE) {
    // Special case: aria-disabled propagates from ancestors down to any focusable descendant
    nsIContent *ancestorContent = content;
    while ((ancestorContent = ancestorContent->GetParent()) != nsnull) {
      if (ancestorContent->AttrValueIs(kNameSpaceID_None, nsAccessibilityAtoms::aria_disabled,
                                       nsAccessibilityAtoms::_true, eCaseMatters)) {
          // ancestor has aria-disabled property, this is disabled
        *aState |= nsIAccessibleStates::STATE_UNAVAILABLE;
        break;
      }
    }    
  }

  if (!mRoleMapEntry)
    return NS_OK;

  // Note: the readonly bitflag will be overridden later if content is editable
  *aState |= mRoleMapEntry->state;
  if (nsStateMapEntry::MapToStates(content, aState, aExtraState,
                                   mRoleMapEntry->attributeMap1) &&
      nsStateMapEntry::MapToStates(content, aState, aExtraState,
                                   mRoleMapEntry->attributeMap2)) {
    nsStateMapEntry::MapToStates(content, aState, aExtraState,
                                 mRoleMapEntry->attributeMap3);
  }

  return NS_OK;
}

// Not implemented by this class

/* DOMString getValue (); */
NS_IMETHODIMP
nsAccessible::GetValue(nsAString& aValue)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  if (!content)
    return NS_OK;

  if (mRoleMapEntry) {
    if (mRoleMapEntry->valueRule == eNoValue) {
      return NS_OK;
    }

    // aria-valuenow is a number, and aria-valuetext is the optional text equivalent
    // For the string value, we will try the optional text equivalent first
    if (!content->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::aria_valuetext, aValue)) {
      content->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::aria_valuenow, aValue);
    }
  }

  if (!aValue.IsEmpty())
    return NS_OK;

  // Check if it's a simple xlink.
  if (nsCoreUtils::IsXLink(content)) {
    nsCOMPtr<nsIPresShell> presShell(do_QueryReferent(mWeakShell));
    if (presShell)
      return presShell->GetLinkLocation(mDOMNode, aValue);
  }

  return NS_OK;
}

// nsIAccessibleValue
NS_IMETHODIMP
nsAccessible::GetMaximumValue(double *aMaximumValue)
{
  return GetAttrValue(nsAccessibilityAtoms::aria_valuemax, aMaximumValue);
}

NS_IMETHODIMP
nsAccessible::GetMinimumValue(double *aMinimumValue)
{
  return GetAttrValue(nsAccessibilityAtoms::aria_valuemin, aMinimumValue);
}

NS_IMETHODIMP
nsAccessible::GetMinimumIncrement(double *aMinIncrement)
{
  NS_ENSURE_ARG_POINTER(aMinIncrement);
  *aMinIncrement = 0;

  // No mimimum increment in dynamic content spec right now
  return NS_OK_NO_ARIA_VALUE;
}

NS_IMETHODIMP
nsAccessible::GetCurrentValue(double *aValue)
{
  return GetAttrValue(nsAccessibilityAtoms::aria_valuenow, aValue);
}

NS_IMETHODIMP
nsAccessible::SetCurrentValue(double aValue)
{
  if (!mDOMNode)
    return NS_ERROR_FAILURE;  // Node already shut down

  if (!mRoleMapEntry || mRoleMapEntry->valueRule == eNoValue)
    return NS_OK_NO_ARIA_VALUE;

  const PRUint32 kValueCannotChange = nsIAccessibleStates::STATE_READONLY |
                                      nsIAccessibleStates::STATE_UNAVAILABLE;

  if (nsAccUtils::State(this) & kValueCannotChange)
    return NS_ERROR_FAILURE;

  double minValue = 0;
  if (NS_SUCCEEDED(GetMinimumValue(&minValue)) && aValue < minValue)
    return NS_ERROR_INVALID_ARG;

  double maxValue = 0;
  if (NS_SUCCEEDED(GetMaximumValue(&maxValue)) && aValue > maxValue)
    return NS_ERROR_INVALID_ARG;

  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  NS_ENSURE_STATE(content);

  nsAutoString newValue;
  newValue.AppendFloat(aValue);
  return content->SetAttr(kNameSpaceID_None,
                          nsAccessibilityAtoms::aria_valuenow, newValue, PR_TRUE);
}

/* void setName (in DOMString name); */
NS_IMETHODIMP nsAccessible::SetName(const nsAString& name)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsAccessible::GetDefaultKeyBinding(nsAString& aKeyBinding)
{
  aKeyBinding.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
nsAccessible::GetKeyBindings(PRUint8 aActionIndex,
                             nsIDOMDOMStringList **aKeyBindings)
{
  // Currently we support only unique key binding on element for default action.
  NS_ENSURE_TRUE(aActionIndex == 0, NS_ERROR_INVALID_ARG);

  nsAccessibleDOMStringList *keyBindings = new nsAccessibleDOMStringList();
  NS_ENSURE_TRUE(keyBindings, NS_ERROR_OUT_OF_MEMORY);

  nsAutoString defaultKey;
  nsresult rv = GetDefaultKeyBinding(defaultKey);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!defaultKey.IsEmpty())
    keyBindings->Add(defaultKey);

  NS_ADDREF(*aKeyBindings = keyBindings);
  return NS_OK;
}

/* unsigned long getRole (); */
nsresult
nsAccessible::GetRoleInternal(PRUint32 *aRole)
{
  *aRole = nsIAccessibleRole::ROLE_NOTHING;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  if (nsCoreUtils::IsXLink(content))
    *aRole = nsIAccessibleRole::ROLE_LINK;

  return NS_OK;
}

// readonly attribute PRUint8 numActions
NS_IMETHODIMP
nsAccessible::GetNumActions(PRUint8 *aNumActions)
{
  NS_ENSURE_ARG_POINTER(aNumActions);
  *aNumActions = 0;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  PRUint32 actionRule = GetActionRule(nsAccUtils::State(this));
  if (actionRule == eNoAction)
    return NS_OK;

  *aNumActions = 1;
  return NS_OK;
}

/* DOMString getAccActionName (in PRUint8 index); */
NS_IMETHODIMP
nsAccessible::GetActionName(PRUint8 aIndex, nsAString& aName)
{
  aName.Truncate();

  if (aIndex != 0)
    return NS_ERROR_INVALID_ARG;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  PRUint32 states = nsAccUtils::State(this);
  PRUint32 actionRule = GetActionRule(states);

 switch (actionRule) {
   case eActivateAction:
     aName.AssignLiteral("activate");
     return NS_OK;

   case eClickAction:
     aName.AssignLiteral("click");
     return NS_OK;

   case eCheckUncheckAction:
     if (states & nsIAccessibleStates::STATE_CHECKED)
       aName.AssignLiteral("uncheck");
     else if (states & nsIAccessibleStates::STATE_MIXED)
       aName.AssignLiteral("cycle");
     else
       aName.AssignLiteral("check");
     return NS_OK;

   case eJumpAction:
     aName.AssignLiteral("jump");
     return NS_OK;

   case eOpenCloseAction:
     if (states & nsIAccessibleStates::STATE_COLLAPSED)
       aName.AssignLiteral("open");
     else
       aName.AssignLiteral("close");
     return NS_OK;

   case eSelectAction:
     aName.AssignLiteral("select");
     return NS_OK;

   case eSwitchAction:
     aName.AssignLiteral("switch");
     return NS_OK;
     
   case eSortAction:
     aName.AssignLiteral("sort");
     return NS_OK;
   
   case eExpandAction:
     if (states & nsIAccessibleStates::STATE_COLLAPSED)
       aName.AssignLiteral("expand");
     else
       aName.AssignLiteral("collapse");
     return NS_OK;
  }

  return NS_ERROR_INVALID_ARG;
}

// AString getActionDescription(in PRUint8 index)
NS_IMETHODIMP
nsAccessible::GetActionDescription(PRUint8 aIndex, nsAString& aDescription)
{
  // default to localized action name.
  nsAutoString name;
  nsresult rv = GetActionName(aIndex, name);
  NS_ENSURE_SUCCESS(rv, rv);

  return GetTranslatedString(name, aDescription);
}

// void doAction(in PRUint8 index)
NS_IMETHODIMP
nsAccessible::DoAction(PRUint8 aIndex)
{
  if (aIndex != 0)
    return NS_ERROR_INVALID_ARG;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (GetActionRule(nsAccUtils::State(this)) != eNoAction) {
    nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
    return DoCommand(content);
  }

  return NS_ERROR_INVALID_ARG;
}

/* DOMString getHelp (); */
NS_IMETHODIMP nsAccessible::GetHelp(nsAString& _retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* nsIAccessible getAccessibleToRight(); */
NS_IMETHODIMP nsAccessible::GetAccessibleToRight(nsIAccessible **_retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* nsIAccessible getAccessibleToLeft(); */
NS_IMETHODIMP nsAccessible::GetAccessibleToLeft(nsIAccessible **_retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* nsIAccessible getAccessibleAbove(); */
NS_IMETHODIMP nsAccessible::GetAccessibleAbove(nsIAccessible **_retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* nsIAccessible getAccessibleBelow(); */
NS_IMETHODIMP nsAccessible::GetAccessibleBelow(nsIAccessible **_retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsIDOMNode* nsAccessible::GetAtomicRegion()
{
  nsCOMPtr<nsIContent> content = nsCoreUtils::GetRoleContent(mDOMNode);
  nsIContent *loopContent = content;
  nsAutoString atomic;
  while (loopContent && !loopContent->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::aria_atomic, atomic)) {
    loopContent = loopContent->GetParent();
  }

  nsCOMPtr<nsIDOMNode> atomicRegion;
  if (atomic.EqualsLiteral("true")) {
    atomicRegion = do_QueryInterface(loopContent);
  }
  return atomicRegion;
}

// nsIAccessible getRelationByType()
NS_IMETHODIMP
nsAccessible::GetRelationByType(PRUint32 aRelationType,
                                nsIAccessibleRelation **aRelation)
{
  NS_ENSURE_ARG_POINTER(aRelation);
  *aRelation = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // Relationships are defined on the same content node that the role would be
  // defined on.
  nsIContent *content = nsCoreUtils::GetRoleContent(mDOMNode);
  if (!content)
    return NS_OK;

  nsresult rv;

  switch (aRelationType)
  {
  case nsIAccessibleRelation::RELATION_LABEL_FOR:
    {
      if (content->Tag() == nsAccessibilityAtoms::label) {
        nsIAtom *IDAttr = content->IsNodeOfType(nsINode::eHTML) ?
          nsAccessibilityAtoms::_for : nsAccessibilityAtoms::control;
        rv = nsRelUtils::
          AddTargetFromIDRefAttr(aRelationType, aRelation, content, IDAttr);
        NS_ENSURE_SUCCESS(rv, rv);

        if (rv != NS_OK_NO_RELATION_TARGET)
          return NS_OK; // XXX bug 381599, avoid performance problems
      }

      return nsRelUtils::
        AddTargetFromNeighbour(aRelationType, aRelation, content,
                               nsAccessibilityAtoms::aria_labelledby);
    }

  case nsIAccessibleRelation::RELATION_LABELLED_BY:
    {
      rv = nsRelUtils::
        AddTargetFromIDRefsAttr(aRelationType, aRelation, content,
                                nsAccessibilityAtoms::aria_labelledby);
      NS_ENSURE_SUCCESS(rv, rv);

      if (rv != NS_OK_NO_RELATION_TARGET)
        return NS_OK; // XXX bug 381599, avoid performance problems

      return nsRelUtils::
        AddTargetFromContent(aRelationType, aRelation,
                             nsCoreUtils::GetLabelContent(content));
    }

  case nsIAccessibleRelation::RELATION_DESCRIBED_BY:
    {
      rv = nsRelUtils::
        AddTargetFromIDRefsAttr(aRelationType, aRelation, content,
                                nsAccessibilityAtoms::aria_describedby);
      NS_ENSURE_SUCCESS(rv, rv);

      if (rv != NS_OK_NO_RELATION_TARGET)
        return NS_OK; // XXX bug 381599, avoid performance problems

      return nsRelUtils::
        AddTargetFromNeighbour(aRelationType, aRelation, content,
                               nsAccessibilityAtoms::control,
                               nsAccessibilityAtoms::description);
    }

  case nsIAccessibleRelation::RELATION_DESCRIPTION_FOR:
    {
      rv = nsRelUtils::
        AddTargetFromNeighbour(aRelationType, aRelation, content,
                               nsAccessibilityAtoms::aria_describedby);
      NS_ENSURE_SUCCESS(rv, rv);

      if (rv != NS_OK_NO_RELATION_TARGET)
        return NS_OK; // XXX bug 381599, avoid performance problems

      if (content->Tag() == nsAccessibilityAtoms::description &&
          content->IsNodeOfType(nsINode::eXUL)) {
        // This affectively adds an optional control attribute to xul:description,
        // which only affects accessibility, by allowing the description to be
        // tied to a control.
        return nsRelUtils::
          AddTargetFromIDRefAttr(aRelationType, aRelation, content,
                                 nsAccessibilityAtoms::control);
      }

      return NS_OK;
    }

  case nsIAccessibleRelation::RELATION_NODE_CHILD_OF:
    {
      rv = nsRelUtils::
        AddTargetFromNeighbour(aRelationType, aRelation, content,
                               nsAccessibilityAtoms::aria_owns);
      NS_ENSURE_SUCCESS(rv, rv);

      if (rv != NS_OK_NO_RELATION_TARGET)
        return NS_OK; // XXX bug 381599, avoid performance problems

      // This is an ARIA tree or treegrid that doesn't use owns, so we need to
      // get the parent the hard way.
      if (mRoleMapEntry &&
          (mRoleMapEntry->role == nsIAccessibleRole::ROLE_OUTLINEITEM ||
           mRoleMapEntry->role == nsIAccessibleRole::ROLE_ROW)) {

        nsCOMPtr<nsIAccessible> accTarget;
        nsAccUtils::GetARIATreeItemParent(this, content,
                                          getter_AddRefs(accTarget));

        return nsRelUtils::AddTarget(aRelationType, aRelation, accTarget);
      }

      // If accessible is in its own Window, or is the root of a document,
      // then we should provide NODE_CHILD_OF relation so that MSAA clients
      // can easily get to true parent instead of getting to oleacc's
      // ROLE_WINDOW accessible which will prevent us from going up further
      // (because it is system generated and has no idea about the hierarchy
      // above it).
      nsIFrame *frame = GetFrame();
      if (frame) {
        nsIView *view = frame->GetViewExternal();
        if (view) {
          nsIScrollableFrame *scrollFrame = do_QueryFrame(frame);
          if (scrollFrame || view->GetWidget() || !frame->GetParent()) {
            nsCOMPtr<nsIAccessible> accTarget;
            GetParent(getter_AddRefs(accTarget));
            return nsRelUtils::AddTarget(aRelationType, aRelation, accTarget);
          }
        }
      }

      return NS_OK;
    }

  case nsIAccessibleRelation::RELATION_CONTROLLED_BY:
    {
      return nsRelUtils::
        AddTargetFromNeighbour(aRelationType, aRelation, content,
                               nsAccessibilityAtoms::aria_controls);
    }

  case nsIAccessibleRelation::RELATION_CONTROLLER_FOR:
    {
      return nsRelUtils::
        AddTargetFromIDRefsAttr(aRelationType, aRelation, content,
                                nsAccessibilityAtoms::aria_controls);
    }

  case nsIAccessibleRelation::RELATION_FLOWS_TO:
    {
      return nsRelUtils::
        AddTargetFromIDRefsAttr(aRelationType, aRelation, content,
                                nsAccessibilityAtoms::aria_flowto);
    }

  case nsIAccessibleRelation::RELATION_FLOWS_FROM:
    {
      return nsRelUtils::
        AddTargetFromNeighbour(aRelationType, aRelation, content,
                               nsAccessibilityAtoms::aria_flowto);
    }

  case nsIAccessibleRelation::RELATION_DEFAULT_BUTTON:
    {
      if (content->IsNodeOfType(nsINode::eHTML)) {
        // HTML form controls implements nsIFormControl interface.
        nsCOMPtr<nsIFormControl> control(do_QueryInterface(content));
        if (control) {
          nsCOMPtr<nsIDOMHTMLFormElement> htmlform;
          control->GetForm(getter_AddRefs(htmlform));
          nsCOMPtr<nsIForm> form(do_QueryInterface(htmlform));
          if (form) {
            nsCOMPtr<nsIContent> formContent =
              do_QueryInterface(form->GetDefaultSubmitElement());
            return nsRelUtils::AddTargetFromContent(aRelationType, aRelation,
                                                    formContent);
          }
        }
      }
      else {
        // In XUL, use first <button default="true" .../> in the document
        nsCOMPtr<nsIDOMXULDocument> xulDoc = do_QueryInterface(content->GetDocument());
        nsCOMPtr<nsIDOMXULButtonElement> buttonEl;
        if (xulDoc) {
          nsCOMPtr<nsIDOMNodeList> possibleDefaultButtons;
          xulDoc->GetElementsByAttribute(NS_LITERAL_STRING("default"),
                                         NS_LITERAL_STRING("true"),
                                         getter_AddRefs(possibleDefaultButtons));
          if (possibleDefaultButtons) {
            PRUint32 length;
            possibleDefaultButtons->GetLength(&length);
            nsCOMPtr<nsIDOMNode> possibleButton;
            // Check for button in list of default="true" elements
            for (PRUint32 count = 0; count < length && !buttonEl; count ++) {
              possibleDefaultButtons->Item(count, getter_AddRefs(possibleButton));
              buttonEl = do_QueryInterface(possibleButton);
            }
          }
          if (!buttonEl) { // Check for anonymous accept button in <dialog>
            nsCOMPtr<nsIDOMDocumentXBL> xblDoc(do_QueryInterface(xulDoc));
            if (xblDoc) {
              nsCOMPtr<nsIDOMDocument> domDoc = do_QueryInterface(xulDoc);
              NS_ASSERTION(domDoc, "No DOM document");
              nsCOMPtr<nsIDOMElement> rootEl;
              domDoc->GetDocumentElement(getter_AddRefs(rootEl));
              if (rootEl) {
                nsCOMPtr<nsIDOMElement> possibleButtonEl;
                xblDoc->GetAnonymousElementByAttribute(rootEl,
                                                      NS_LITERAL_STRING("default"),
                                                      NS_LITERAL_STRING("true"),
                                                      getter_AddRefs(possibleButtonEl));
                buttonEl = do_QueryInterface(possibleButtonEl);
              }
            }
          }
          nsCOMPtr<nsIContent> relatedContent(do_QueryInterface(buttonEl));
          return nsRelUtils::AddTargetFromContent(aRelationType, aRelation,
                                                  relatedContent);
        }
      }
      return NS_OK;
    }

  case nsIAccessibleRelation::RELATION_MEMBER_OF:
    {
      nsCOMPtr<nsIContent> regionContent = do_QueryInterface(GetAtomicRegion());
      return nsRelUtils::
        AddTargetFromContent(aRelationType, aRelation, regionContent);
    }

  case nsIAccessibleRelation::RELATION_SUBWINDOW_OF:
  case nsIAccessibleRelation::RELATION_EMBEDS:
  case nsIAccessibleRelation::RELATION_EMBEDDED_BY:
  case nsIAccessibleRelation::RELATION_POPUP_FOR:
  case nsIAccessibleRelation::RELATION_PARENT_WINDOW_OF:
    {
      return NS_OK_NO_RELATION_TARGET;
    }

  default:
    return NS_ERROR_INVALID_ARG;
  }
}

NS_IMETHODIMP
nsAccessible::GetRelationsCount(PRUint32 *aCount)
{
  NS_ENSURE_ARG_POINTER(aCount);
  *aCount = 0;

  nsCOMPtr<nsIArray> relations;
  nsresult rv = GetRelations(getter_AddRefs(relations));
  NS_ENSURE_SUCCESS(rv, rv);

  return relations->GetLength(aCount);
}

NS_IMETHODIMP
nsAccessible::GetRelation(PRUint32 aIndex, nsIAccessibleRelation **aRelation)
{
  NS_ENSURE_ARG_POINTER(aRelation);
  *aRelation = nsnull;

  nsCOMPtr<nsIArray> relations;
  nsresult rv = GetRelations(getter_AddRefs(relations));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIAccessibleRelation> relation;
  rv = relations->QueryElementAt(aIndex, NS_GET_IID(nsIAccessibleRelation),
                                 getter_AddRefs(relation));

  // nsIArray::QueryElementAt() returns NS_ERROR_ILLEGAL_VALUE on invalid index.
  if (rv == NS_ERROR_ILLEGAL_VALUE)
    return NS_ERROR_INVALID_ARG;

  NS_ENSURE_SUCCESS(rv, rv);

  NS_IF_ADDREF(*aRelation = relation);
  return NS_OK;
}

NS_IMETHODIMP
nsAccessible::GetRelations(nsIArray **aRelations)
{
  NS_ENSURE_ARG_POINTER(aRelations);

  nsCOMPtr<nsIMutableArray> relations = do_CreateInstance(NS_ARRAY_CONTRACTID);
  NS_ENSURE_TRUE(relations, NS_ERROR_OUT_OF_MEMORY);

  for (PRUint32 relType = nsIAccessibleRelation::RELATION_FIRST;
       relType < nsIAccessibleRelation::RELATION_LAST;
       ++relType) {

    nsCOMPtr<nsIAccessibleRelation> relation;
    nsresult rv = GetRelationByType(relType, getter_AddRefs(relation));

    if (NS_SUCCEEDED(rv) && relation)
      relations->AppendElement(relation, PR_FALSE);
  }

  NS_ADDREF(*aRelations = relations);
  return NS_OK;
}

/* void extendSelection (); */
NS_IMETHODIMP nsAccessible::ExtendSelection()
{
  // XXX Should be implemented, but not high priority
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* [noscript] void getNativeInterface(out voidPtr aOutAccessible); */
NS_IMETHODIMP nsAccessible::GetNativeInterface(void **aOutAccessible)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
nsAccessible::DoCommand(nsIContent *aContent, PRUint32 aActionIndex)
{
  if (gDoCommandTimer) {
    // Already have timer going for another command
    NS_WARNING("Doubling up on do command timers doesn't work. This wasn't expected.");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsITimer> timer = do_CreateInstance("@mozilla.org/timer;1");
  NS_ENSURE_TRUE(timer, NS_ERROR_OUT_OF_MEMORY);

  nsCOMPtr<nsIContent> content = aContent;
  if (!content)
    content = do_QueryInterface(mDOMNode);

  // Command closure object memory will be free in DoCommandCallback().
  nsCommandClosure *closure =
    new nsCommandClosure(this, content, aActionIndex);
  NS_ENSURE_TRUE(closure, NS_ERROR_OUT_OF_MEMORY);

  NS_ADDREF(gDoCommandTimer = timer);
  return gDoCommandTimer->InitWithFuncCallback(DoCommandCallback,
                                               static_cast<void*>(closure),
                                               0, nsITimer::TYPE_ONE_SHOT);
}

void
nsAccessible::DoCommandCallback(nsITimer *aTimer, void *aClosure)
{
  NS_ASSERTION(gDoCommandTimer,
               "How did we get here if there was no gDoCommandTimer?");
  NS_RELEASE(gDoCommandTimer);

  nsCommandClosure *closure = static_cast<nsCommandClosure*>(aClosure);
  closure->accessible->DispatchClickEvent(closure->content,
                                          closure->actionIndex);
  delete closure;
}

void
nsAccessible::DispatchClickEvent(nsIContent *aContent, PRUint32 aActionIndex)
{
  if (IsDefunct())
    return;

  nsCOMPtr<nsIPresShell> presShell = GetPresShell();

  // Scroll into view.
  presShell->ScrollContentIntoView(aContent, NS_PRESSHELL_SCROLL_ANYWHERE,
                                   NS_PRESSHELL_SCROLL_ANYWHERE);

  // Fire mouse down and mouse up events.
  PRBool res = nsCoreUtils::DispatchMouseEvent(NS_MOUSE_BUTTON_DOWN, presShell,
                                               aContent);
  if (!res)
    return;

  nsCoreUtils::DispatchMouseEvent(NS_MOUSE_BUTTON_UP, presShell, aContent);
}

already_AddRefed<nsIAccessible>
nsAccessible::GetNextWithState(nsIAccessible *aStart, PRUint32 matchState)
{
  // Return the next descendant that matches one of the states in matchState
  // Uses depth first search
  NS_ASSERTION(matchState, "GetNextWithState() not called with a state to match");
  NS_ASSERTION(aStart, "GetNextWithState() not called with an accessible to start with");
  nsCOMPtr<nsIAccessible> look, current = aStart;
  PRUint32 state = 0;
  while (0 == (state & matchState)) {
    current->GetFirstChild(getter_AddRefs(look));
    while (!look) {
      if (current == this) {
        return nsnull; // At top of subtree
      }
      current->GetNextSibling(getter_AddRefs(look));
      if (!look) {
        current->GetParent(getter_AddRefs(look));
        current = look;
        look = nsnull;
        continue;
      }
    }
    current.swap(look);
    state = nsAccUtils::State(current);
  }

  nsIAccessible *returnAccessible = nsnull;
  current.swap(returnAccessible);

  return returnAccessible;
}

// nsIAccessibleSelectable
NS_IMETHODIMP nsAccessible::GetSelectedChildren(nsIArray **aSelectedAccessibles)
{
  *aSelectedAccessibles = nsnull;

  nsCOMPtr<nsIMutableArray> selectedAccessibles =
    do_CreateInstance(NS_ARRAY_CONTRACTID);
  NS_ENSURE_STATE(selectedAccessibles);

  nsCOMPtr<nsIAccessible> selected = this;
  while ((selected = GetNextWithState(selected, nsIAccessibleStates::STATE_SELECTED)) != nsnull) {
    selectedAccessibles->AppendElement(selected, PR_FALSE);
  }

  PRUint32 length = 0;
  selectedAccessibles->GetLength(&length); 
  if (length) { // length of nsIArray containing selected options
    *aSelectedAccessibles = selectedAccessibles;
    NS_ADDREF(*aSelectedAccessibles);
  }

  return NS_OK;
}

// return the nth selected descendant nsIAccessible object
NS_IMETHODIMP nsAccessible::RefSelection(PRInt32 aIndex, nsIAccessible **aSelected)
{
  *aSelected = nsnull;
  if (aIndex < 0) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIAccessible> selected = this;
  PRInt32 count = 0;
  while (count ++ <= aIndex) {
    selected = GetNextWithState(selected, nsIAccessibleStates::STATE_SELECTED);
    if (!selected) {
      return NS_ERROR_FAILURE; // aIndex out of range
    }
  }
  NS_IF_ADDREF(*aSelected = selected);
  return NS_OK;
}

NS_IMETHODIMP nsAccessible::GetSelectionCount(PRInt32 *aSelectionCount)
{
  *aSelectionCount = 0;
  nsCOMPtr<nsIAccessible> selected = this;
  while ((selected = GetNextWithState(selected, nsIAccessibleStates::STATE_SELECTED)) != nsnull) {
    ++ *aSelectionCount;
  }

  return NS_OK;
}

NS_IMETHODIMP nsAccessible::AddChildToSelection(PRInt32 aIndex)
{
  // Tree views and other container widgets which may have grandchildren should
  // implement a selection methods for their specific interfaces, because being
  // able to deal with selection on a per-child basis would not be enough.

  NS_ENSURE_TRUE(aIndex >= 0, NS_ERROR_FAILURE);

  nsCOMPtr<nsIAccessible> child;
  GetChildAt(aIndex, getter_AddRefs(child));

  PRUint32 state = nsAccUtils::State(child);
  if (!(state & nsIAccessibleStates::STATE_SELECTABLE)) {
    return NS_OK;
  }

  return child->SetSelected(PR_TRUE);
}

NS_IMETHODIMP nsAccessible::RemoveChildFromSelection(PRInt32 aIndex)
{
  // Tree views and other container widgets which may have grandchildren should
  // implement a selection methods for their specific interfaces, because being
  // able to deal with selection on a per-child basis would not be enough.

  NS_ENSURE_TRUE(aIndex >= 0, NS_ERROR_FAILURE);

  nsCOMPtr<nsIAccessible> child;
  GetChildAt(aIndex, getter_AddRefs(child));

  PRUint32 state = nsAccUtils::State(child);
  if (!(state & nsIAccessibleStates::STATE_SELECTED)) {
    return NS_OK;
  }

  return child->SetSelected(PR_FALSE);
}

NS_IMETHODIMP nsAccessible::IsChildSelected(PRInt32 aIndex, PRBool *aIsSelected)
{
  // Tree views and other container widgets which may have grandchildren should
  // implement a selection methods for their specific interfaces, because being
  // able to deal with selection on a per-child basis would not be enough.

  *aIsSelected = PR_FALSE;
  NS_ENSURE_TRUE(aIndex >= 0, NS_ERROR_FAILURE);

  nsCOMPtr<nsIAccessible> child;
  GetChildAt(aIndex, getter_AddRefs(child));

  PRUint32 state = nsAccUtils::State(child);
  if (state & nsIAccessibleStates::STATE_SELECTED) {
    *aIsSelected = PR_TRUE;
  }
  return NS_OK;
}

NS_IMETHODIMP nsAccessible::ClearSelection()
{
  nsCOMPtr<nsIAccessible> selected = this;
  while ((selected = GetNextWithState(selected, nsIAccessibleStates::STATE_SELECTED)) != nsnull) {
    selected->SetSelected(PR_FALSE);
  }
  return NS_OK;
}

NS_IMETHODIMP nsAccessible::SelectAllSelection(PRBool *_retval)
{
  nsCOMPtr<nsIAccessible> selectable = this;
  while ((selectable = GetNextWithState(selectable, nsIAccessibleStates::STATE_SELECTED)) != nsnull) {
    selectable->SetSelected(PR_TRUE);
  }
  return NS_OK;
}

// nsIAccessibleHyperLink
// Because of new-atk design, any embedded object in text can implement
// nsIAccessibleHyperLink, which helps determine where it is located
// within containing text

// readonly attribute long nsIAccessibleHyperLink::anchorCount
NS_IMETHODIMP
nsAccessible::GetAnchorCount(PRInt32 *aAnchorCount)
{
  NS_ENSURE_ARG_POINTER(aAnchorCount);
  *aAnchorCount = 1;
  return NS_OK;
}

// readonly attribute long nsIAccessibleHyperLink::startIndex
NS_IMETHODIMP
nsAccessible::GetStartIndex(PRInt32 *aStartIndex)
{
  NS_ENSURE_ARG_POINTER(aStartIndex);
  *aStartIndex = 0;
  PRInt32 endIndex;
  return GetLinkOffset(aStartIndex, &endIndex);
}

// readonly attribute long nsIAccessibleHyperLink::endIndex
NS_IMETHODIMP
nsAccessible::GetEndIndex(PRInt32 *aEndIndex)
{
  NS_ENSURE_ARG_POINTER(aEndIndex);
  *aEndIndex = 0;
  PRInt32 startIndex;
  return GetLinkOffset(&startIndex, aEndIndex);
}

NS_IMETHODIMP
nsAccessible::GetURI(PRInt32 aIndex, nsIURI **aURI)
{
  NS_ENSURE_ARG_POINTER(aURI);
  *aURI = nsnull;

  if (aIndex != 0)
    return NS_ERROR_INVALID_ARG;

  // Check if it's a simple xlink.
  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  if (nsCoreUtils::IsXLink(content)) {
    nsAutoString href;
    content->GetAttr(kNameSpaceID_XLink, nsAccessibilityAtoms::href, href);

    nsCOMPtr<nsIURI> baseURI = content->GetBaseURI();
    nsCOMPtr<nsIDocument> document = content->GetOwnerDoc();
    return NS_NewURI(aURI, href,
                     document ? document->GetDocumentCharacterSet().get() : nsnull,
                     baseURI);
  }

  return NS_OK;
}


NS_IMETHODIMP
nsAccessible::GetAnchor(PRInt32 aIndex,
                        nsIAccessible **aAccessible)
{
  NS_ENSURE_ARG_POINTER(aAccessible);
  *aAccessible = nsnull;

  if (aIndex != 0)
    return NS_ERROR_INVALID_ARG;

  *aAccessible = this;
  NS_ADDREF_THIS();
  return NS_OK;
}

// readonly attribute boolean nsIAccessibleHyperLink::valid
NS_IMETHODIMP
nsAccessible::GetValid(PRBool *aValid)
{
  NS_ENSURE_ARG_POINTER(aValid);
  PRUint32 state = nsAccUtils::State(this);
  *aValid = (0 == (state & nsIAccessibleStates::STATE_INVALID));
  // XXX In order to implement this we would need to follow every link
  // Perhaps we can get information about invalid links from the cache
  // In the mean time authors can use role="link" aria-invalid="true"
  // to force it for links they internally know to be invalid
  return NS_OK;
}

// readonly attribute boolean nsIAccessibleHyperLink::selected
NS_IMETHODIMP
nsAccessible::GetSelected(PRBool *aSelected)
{
  NS_ENSURE_ARG_POINTER(aSelected);
  *aSelected = (gLastFocusedNode == mDOMNode);
  return NS_OK;
}

nsresult nsAccessible::GetLinkOffset(PRInt32* aStartOffset, PRInt32* aEndOffset)
{
  *aStartOffset = *aEndOffset = 0;
  nsCOMPtr<nsIAccessible> parent(GetParent());
  if (!parent) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIAccessible> accessible, nextSibling;
  PRInt32 characterCount = 0;
  parent->GetFirstChild(getter_AddRefs(accessible));

  while (accessible) {
    if (nsAccUtils::IsText(accessible))
      characterCount += nsAccUtils::TextLength(accessible);

    else if (accessible == this) {
      *aStartOffset = characterCount;
      *aEndOffset = characterCount + 1;
      return NS_OK;
    }
    else {
      ++ characterCount;
    }
    accessible->GetNextSibling(getter_AddRefs(nextSibling));
    accessible.swap(nextSibling);
  }

  return NS_ERROR_FAILURE;
}

nsresult
nsAccessible::AppendTextTo(nsAString& aText, PRUint32 aStartOffset, PRUint32 aLength)
{
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsAccessible public methods

nsresult
nsAccessible::GetARIAName(nsAString& aName)
{
  nsCOMPtr<nsIContent> content = nsCoreUtils::GetRoleContent(mDOMNode);
  if (!content)
    return NS_OK;

  // First check for label override via aria-label property
  nsAutoString label;
  if (content->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::aria_label, label)) {
    label.CompressWhitespace();
    aName = label;
    return NS_OK;
  }
  
  // Second check for label override via aria-labelledby relationship
  nsresult rv = nsTextEquivUtils::
    GetTextEquivFromIDRefs(this, nsAccessibilityAtoms::aria_labelledby, label);
  if (NS_SUCCEEDED(rv)) {
    label.CompressWhitespace();
    aName = label;
  }

  return rv;
}

nsresult
nsAccessible::GetNameInternal(nsAString& aName)
{
  nsCOMPtr<nsIContent> content = nsCoreUtils::GetRoleContent(mDOMNode);
  if (!content)
    return NS_OK;

  if (content->IsNodeOfType(nsINode::eHTML))
    return GetHTMLName(aName);

  if (content->IsNodeOfType(nsINode::eXUL))
    return GetXULName(aName);

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsAccessible private methods

already_AddRefed<nsIAccessible>
nsAccessible::GetFirstAvailableAccessible(nsIDOMNode *aStartNode, PRBool aRequireLeaf)
{
  nsIAccessibilityService *accService = GetAccService();
  nsCOMPtr<nsIAccessible> accessible;
  nsCOMPtr<nsIDOMTreeWalker> walker; 
  nsCOMPtr<nsIDOMNode> currentNode(aStartNode);

  while (currentNode) {
    accService->GetAccessibleInWeakShell(currentNode, mWeakShell, getter_AddRefs(accessible)); // AddRef'd
    if (accessible && (!aRequireLeaf || nsAccUtils::IsLeaf(accessible))) {
      nsIAccessible *retAccessible = accessible;
      NS_ADDREF(retAccessible);
      return retAccessible;
    }
    if (!walker) {
      // Instantiate walker lazily since we won't need it in 90% of the cases
      // where the first DOM node we're given provides an accessible
      nsCOMPtr<nsIDOMDocument> document;
      currentNode->GetOwnerDocument(getter_AddRefs(document));
      nsCOMPtr<nsIDOMDocumentTraversal> trav = do_QueryInterface(document);
      NS_ASSERTION(trav, "No DOM document traversal for document");
      NS_ENSURE_TRUE(trav, nsnull);
      trav->CreateTreeWalker(mDOMNode, nsIDOMNodeFilter::SHOW_ELEMENT | nsIDOMNodeFilter::SHOW_TEXT,
                            nsnull, PR_FALSE, getter_AddRefs(walker));
      NS_ENSURE_TRUE(walker, nsnull);
      walker->SetCurrentNode(currentNode);
    }

    walker->NextNode(getter_AddRefs(currentNode));
  }

  return nsnull;
}

PRBool nsAccessible::CheckVisibilityInParentChain(nsIDocument* aDocument, nsIView* aView)
{
  nsIDocument* document = aDocument;
  nsIView* view = aView;
  // both view chain and widget chain are broken between chrome and content
  while (document != nsnull) {
    while (view != nsnull) {
      if (view->GetVisibility() == nsViewVisibility_kHide) {
        return PR_FALSE;
      }
      view = view->GetParent();
    }

    nsIDocument* parentDoc = document->GetParentDocument();
    if (parentDoc != nsnull) {
      nsIContent* content = parentDoc->FindContentForSubDocument(document);
      if (content != nsnull) {
        nsIPresShell* shell = parentDoc->GetPrimaryShell();
        if (!shell) {
          return PR_FALSE;
        }
        nsIFrame* frame = shell->GetPrimaryFrameFor(content);
        while (frame != nsnull && !frame->HasView()) {
          frame = frame->GetParent();
        }

        if (frame != nsnull) {
          view = frame->GetViewExternal();
        }
      }
    }

    document = parentDoc;
  }

  return PR_TRUE;
}

nsresult
nsAccessible::GetAttrValue(nsIAtom *aProperty, double *aValue)
{
  NS_ENSURE_ARG_POINTER(aValue);
  *aValue = 0;

  if (IsDefunct())
    return NS_ERROR_FAILURE;  // Node already shut down

 if (!mRoleMapEntry || mRoleMapEntry->valueRule == eNoValue)
    return NS_OK_NO_ARIA_VALUE;

  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  NS_ENSURE_STATE(content);

  nsAutoString attrValue;
  content->GetAttr(kNameSpaceID_None, aProperty, attrValue);

  // Return zero value if there is no attribute or its value is empty.
  if (attrValue.IsEmpty())
    return NS_OK;

  PRInt32 error = NS_OK;
  double value = attrValue.ToFloat(&error);
  if (NS_SUCCEEDED(error))
    *aValue = value;

  return NS_OK;
}

PRUint32
nsAccessible::GetActionRule(PRUint32 aStates)
{
  if (aStates & nsIAccessibleStates::STATE_UNAVAILABLE)
    return eNoAction;

  nsIContent* content = nsCoreUtils::GetRoleContent(mDOMNode);
  if (!content)
    return eNoAction;
  
  // Check if it's simple xlink.
  if (nsCoreUtils::IsXLink(content))
    return eJumpAction;

  // Has registered 'click' event handler.
  PRBool isOnclick = nsCoreUtils::HasListener(content,
                                              NS_LITERAL_STRING("click"));

  if (isOnclick)
    return eClickAction;
  
  // Get an action based on ARIA role.
  if (mRoleMapEntry &&
      mRoleMapEntry->actionRule != eNoAction)
    return mRoleMapEntry->actionRule;

  // Get an action based on ARIA attribute.
  if (nsAccUtils::HasDefinedARIAToken(content,
                                   nsAccessibilityAtoms::aria_expanded))
    return eExpandAction;

  return eNoAction;
}

nsresult
nsAccessible::ComputeGroupAttributes(PRUint32 aRole,
                                     nsIPersistentProperties *aAttributes)
{
  // The role of an accessible can be specified by ARIA attribute but ARIA
  // posinset, level, setsize may be skipped. As well this method is used
  // for non ARIA accessibles to avoid GetAccessibleInternal() method
  // implementation in subclasses. For example, it's being used to calculate
  // group attributes for HTML li elements.

  // If accessible is invisible we don't want to calculate group attributes for
  // it.
  if (nsAccUtils::State(this) & nsIAccessibleStates::STATE_INVISIBLE)
    return NS_OK;

  if (aRole != nsIAccessibleRole::ROLE_LISTITEM &&
      aRole != nsIAccessibleRole::ROLE_MENUITEM &&
      aRole != nsIAccessibleRole::ROLE_CHECK_MENU_ITEM &&
      aRole != nsIAccessibleRole::ROLE_RADIO_MENU_ITEM &&
      aRole != nsIAccessibleRole::ROLE_RADIOBUTTON &&
      aRole != nsIAccessibleRole::ROLE_PAGETAB &&
      aRole != nsIAccessibleRole::ROLE_OPTION &&
      aRole != nsIAccessibleRole::ROLE_OUTLINEITEM)
    return NS_OK;

  PRUint32 baseRole = aRole;
  if (aRole == nsIAccessibleRole::ROLE_CHECK_MENU_ITEM ||
      aRole == nsIAccessibleRole::ROLE_RADIO_MENU_ITEM)
    baseRole = nsIAccessibleRole::ROLE_MENUITEM;

  nsCOMPtr<nsIAccessible> parent = GetParent();
  NS_ENSURE_TRUE(parent, NS_ERROR_FAILURE);

  // Compute 'posinset' and 'setsize' attributes.
  PRInt32 positionInGroup = 0;
  PRInt32 setSize = 0;

  nsCOMPtr<nsIAccessible> sibling, nextSibling;
  parent->GetFirstChild(getter_AddRefs(sibling));
  NS_ENSURE_STATE(sibling);

  PRBool foundCurrent = PR_FALSE;
  PRUint32 siblingRole, siblingBaseRole;
  while (sibling) {
    siblingRole = nsAccUtils::Role(sibling);

    siblingBaseRole = siblingRole;
    if (siblingRole == nsIAccessibleRole::ROLE_CHECK_MENU_ITEM ||
        siblingRole == nsIAccessibleRole::ROLE_RADIO_MENU_ITEM)
      siblingBaseRole = nsIAccessibleRole::ROLE_MENUITEM;

    // If sibling is visible and has the same base role.
    if (siblingBaseRole == baseRole &&
        !(nsAccUtils::State(sibling) & nsIAccessibleStates::STATE_INVISIBLE)) {
      ++ setSize;
      if (!foundCurrent) {
        ++ positionInGroup;
        if (sibling == this)
          foundCurrent = PR_TRUE;
      }
    }

    // If the sibling is separator
    if (siblingRole == nsIAccessibleRole::ROLE_SEPARATOR) {
      if (foundCurrent) // the our group is ended
        break;

      // not our group, continue the searching
      positionInGroup = 0;
      setSize = 0;
    }

    sibling->GetNextSibling(getter_AddRefs(nextSibling));
    sibling = nextSibling;
  }

  // Compute 'level' attribute.
  PRInt32 groupLevel = 0;
  if (aRole == nsIAccessibleRole::ROLE_OUTLINEITEM) {
    // Always expose 'level' attribute for 'outlineitem' accessible. The number
    // of nested 'grouping' accessibles containing 'outlineitem' accessible is
    // its level.
    groupLevel = 1;
    nsCOMPtr<nsIAccessible> nextParent;
    while (parent) {
      PRUint32 parentRole = nsAccUtils::Role(parent);

      if (parentRole == nsIAccessibleRole::ROLE_OUTLINE)
        break;
      if (parentRole == nsIAccessibleRole::ROLE_GROUPING)
        ++ groupLevel;

      parent->GetParent(getter_AddRefs(nextParent));
      parent.swap(nextParent);
    }
  } else if (aRole == nsIAccessibleRole::ROLE_LISTITEM) {
    // Expose 'level' attribute on nested lists. We assume nested list is a last
    // child of listitem of parent list. We don't handle the case when nested
    // lists have more complex structure, for example when there are accessibles
    // between parent listitem and nested list.

    // Calculate 'level' attribute based on number of parent listitems.
    nsCOMPtr<nsIAccessible> nextParent;
    while (parent) {
      PRUint32 parentRole = nsAccUtils::Role(parent);

      if (parentRole == nsIAccessibleRole::ROLE_LISTITEM)
        ++ groupLevel;
      else if (parentRole != nsIAccessibleRole::ROLE_LIST)
        break;

      parent->GetParent(getter_AddRefs(nextParent));
      parent.swap(nextParent);
    }

    if (groupLevel == 0) {
      // If this listitem is on top of nested lists then expose 'level'
      // attribute.
      nsCOMPtr<nsIAccessible> parent = GetParent();
      parent->GetFirstChild(getter_AddRefs(sibling));

      while (sibling) {
        nsCOMPtr<nsIAccessible> siblingChild;
        sibling->GetLastChild(getter_AddRefs(siblingChild));
        if (nsAccUtils::Role(siblingChild) == nsIAccessibleRole::ROLE_LIST) {
          groupLevel = 1;
          break;
        }

        sibling->GetNextSibling(getter_AddRefs(nextSibling));
        sibling.swap(nextSibling);
      }
    } else
      groupLevel++; // level is 1-index based
  }

  nsAccUtils::SetAccGroupAttrs(aAttributes, groupLevel, positionInGroup,
                               setSize);

  return NS_OK;
}

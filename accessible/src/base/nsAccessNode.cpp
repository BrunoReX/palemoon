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
 * Portions created by the Initial Developer are Copyright (C) 2003
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Original Author: Aaron Leventhal (aaronl@netscape.com)
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

#include "nsAccessNode.h"
#include "nsIAccessible.h"
#include "nsAccessibilityAtoms.h"
#include "nsHashtable.h"
#include "nsAccessibilityService.h"
#include "nsIAccessibleDocument.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocument.h"
#include "nsIDOMCSSStyleDeclaration.h"
#include "nsIDOMCSSPrimitiveValue.h"
#include "nsIDOMDocument.h"
#include "nsIDOMElement.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMHTMLElement.h"
#include "nsIDOMNSDocument.h"
#include "nsIDOMNSHTMLElement.h"
#include "nsIDOMWindow.h"
#include "nsPIDOMWindow.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIFrame.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsPresContext.h"
#include "nsIPresShell.h"
#include "nsIServiceManager.h"
#include "nsIStringBundle.h"
#include "nsITimer.h"
#include "nsRootAccessible.h"
#include "nsFocusManager.h"
#include "nsIObserverService.h"

#ifdef MOZ_ACCESSIBILITY_ATK
#include "nsAppRootAccessible.h"
#else
#include "nsApplicationAccessibleWrap.h"
#endif

/* For documentation of the accessibility architecture, 
 * see http://lxr.mozilla.org/seamonkey/source/accessible/accessible-docs.html
 */

nsIStringBundle *nsAccessNode::gStringBundle = 0;
nsIStringBundle *nsAccessNode::gKeyStringBundle = 0;
nsITimer *nsAccessNode::gDoCommandTimer = 0;
nsIDOMNode *nsAccessNode::gLastFocusedNode = 0;
#ifdef DEBUG
PRBool nsAccessNode::gIsAccessibilityActive = PR_FALSE;
#endif
PRBool nsAccessNode::gIsCacheDisabled = PR_FALSE;
PRBool nsAccessNode::gIsFormFillEnabled = PR_FALSE;
nsAccessNodeHashtable nsAccessNode::gGlobalDocAccessibleCache;

nsApplicationAccessibleWrap *nsAccessNode::gApplicationAccessible = nsnull;

nsIAccessibilityService*
nsAccessNode::GetAccService()
{
  return nsAccessibilityService::GetAccessibilityService();
}

/*
 * Class nsAccessNode
 */
 
////////////////////////////////////////////////////////////////////////////////
// nsAccessible. nsISupports

NS_IMPL_CYCLE_COLLECTION_0(nsAccessNode)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsAccessNode)
  NS_INTERFACE_MAP_ENTRY(nsIAccessNode)
  NS_INTERFACE_MAP_ENTRY(nsAccessNode)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIAccessNode)
NS_INTERFACE_MAP_END
 
NS_IMPL_CYCLE_COLLECTING_ADDREF_AMBIGUOUS(nsAccessNode, nsIAccessNode)
NS_IMPL_CYCLE_COLLECTING_RELEASE_FULL(nsAccessNode, nsIAccessNode,
                                      LastRelease())

////////////////////////////////////////////////////////////////////////////////
// nsAccessible. Constructor

nsAccessNode::nsAccessNode(nsIDOMNode *aNode, nsIWeakReference* aShell): 
  mDOMNode(aNode), mWeakShell(aShell)
{
#ifdef DEBUG_A11Y
  mIsInitialized = PR_FALSE;
#endif
}

//-----------------------------------------------------
// destruction
//-----------------------------------------------------
nsAccessNode::~nsAccessNode()
{
  NS_ASSERTION(!mWeakShell, "LastRelease was never called!?!");
}

void nsAccessNode::LastRelease()
{
  // First cleanup if needed...
  if (mWeakShell) {
    Shutdown();
    NS_ASSERTION(!mWeakShell, "A Shutdown() impl forgot to call its parent's Shutdown?");
  }
  // ... then die.
  NS_DELETEXPCOM(this);
}

nsresult
nsAccessNode::Init()
{
  // We have to put this here, instead of constructor, otherwise
  // we don't have the virtual GetUniqueID() method for the hash key.
  // We need that for accessibles that don't have DOM nodes

#ifdef DEBUG_A11Y
  NS_ASSERTION(!mIsInitialized, "Initialized twice!");
#endif
  nsCOMPtr<nsIAccessibleDocument> docAccessible(GetDocAccessible());
  if (!docAccessible) {
    // No doc accessible yet for this node's document. 
    // There was probably an accessible event fired before the 
    // current document was ever asked for by the assistive technology.
    // Create a doc accessible so we can cache this node
    nsCOMPtr<nsIPresShell> presShell(do_QueryReferent(mWeakShell));
    if (presShell) {
      nsCOMPtr<nsIDOMNode> docNode(do_QueryInterface(presShell->GetDocument()));
      if (docNode) {
        nsIAccessibilityService *accService = GetAccService();
        if (accService) {
          nsCOMPtr<nsIAccessible> accessible;
          accService->GetAccessibleInShell(docNode, presShell,
                                           getter_AddRefs(accessible));
          docAccessible = do_QueryInterface(accessible);
        }
      }
    }
    NS_ASSERTION(docAccessible, "Cannot cache new nsAccessNode");
    if (!docAccessible) {
      return NS_ERROR_FAILURE;
    }
  }

  void* uniqueID;
  GetUniqueID(&uniqueID);
  nsRefPtr<nsDocAccessible> docAcc =
    nsAccUtils::QueryAccessibleDocument(docAccessible);
  NS_ASSERTION(docAcc, "No nsDocAccessible for document accessible!");
  docAcc->CacheAccessNode(uniqueID, this);

  // Make sure an ancestor in real content is cached
  // so that nsDocAccessible::RefreshNodes() can find the anonymous subtree to release when
  // the root node goes away
  nsCOMPtr<nsIContent> content = do_QueryInterface(mDOMNode);
  if (content && content->IsInAnonymousSubtree()) {
    // Specific examples of where this is used: <input type="file"> and <xul:findbar>
    nsCOMPtr<nsIAccessible> parentAccessible;
    docAccessible->GetAccessibleInParentChain(mDOMNode, PR_TRUE, getter_AddRefs(parentAccessible));
    if (parentAccessible) {
      PRInt32 childCountUnused;
      parentAccessible->GetChildCount(&childCountUnused);
    }
  }

#ifdef DEBUG_A11Y
  mIsInitialized = PR_TRUE;
#endif

  return NS_OK;
}


nsresult
nsAccessNode::Shutdown()
{
  mDOMNode = nsnull;
  mWeakShell = nsnull;

  return NS_OK;
}

NS_IMETHODIMP nsAccessNode::GetUniqueID(void **aUniqueID)
{
  *aUniqueID = static_cast<void*>(mDOMNode);
  return NS_OK;
}

NS_IMETHODIMP nsAccessNode::GetOwnerWindow(void **aWindow)
{
  *aWindow = nsnull;
  nsCOMPtr<nsIAccessibleDocument> docAccessible(GetDocAccessible());
  if (!docAccessible)
    return NS_ERROR_FAILURE; // This node or doc accessible is shut down
  return docAccessible->GetWindowHandle(aWindow);
}

already_AddRefed<nsApplicationAccessibleWrap>
nsAccessNode::GetApplicationAccessible()
{
  NS_ASSERTION(gIsAccessibilityActive, "Accessibility wasn't initialized!");

  if (!gApplicationAccessible) {
    nsApplicationAccessibleWrap::PreCreate();

    gApplicationAccessible = new nsApplicationAccessibleWrap();
    if (!gApplicationAccessible)
      return nsnull;

    // Addref on create. Will Release in ShutdownXPAccessibility()
    NS_ADDREF(gApplicationAccessible);

    nsresult rv = gApplicationAccessible->Init();
    if (NS_FAILED(rv)) {
      NS_RELEASE(gApplicationAccessible);
      gApplicationAccessible = nsnull;
      return nsnull;
    }
  }

  NS_ADDREF(gApplicationAccessible);   // Addref because we're a getter
  return gApplicationAccessible;
}

void nsAccessNode::InitXPAccessibility()
{
  NS_ASSERTION(!gIsAccessibilityActive,
               "Accessibility was initialized already!");

  nsCOMPtr<nsIStringBundleService> stringBundleService =
    do_GetService(NS_STRINGBUNDLE_CONTRACTID);
  if (stringBundleService) {
    // Static variables are released in ShutdownAllXPAccessibility();
    stringBundleService->CreateBundle(ACCESSIBLE_BUNDLE_URL, 
                                      &gStringBundle);
    stringBundleService->CreateBundle(PLATFORM_KEYS_BUNDLE_URL, 
                                      &gKeyStringBundle);
  }

  nsAccessibilityAtoms::AddRefAtoms();

  gGlobalDocAccessibleCache.Init(4);

  nsCOMPtr<nsIPrefBranch> prefBranch(do_GetService(NS_PREFSERVICE_CONTRACTID));
  if (prefBranch) {
    prefBranch->GetBoolPref("accessibility.disablecache", &gIsCacheDisabled);
    prefBranch->GetBoolPref("browser.formfill.enable", &gIsFormFillEnabled);
  }

#ifdef DEBUG
  gIsAccessibilityActive = PR_TRUE;
#endif
  NotifyA11yInitOrShutdown(PR_TRUE);
}

void nsAccessNode::NotifyA11yInitOrShutdown(PRBool aIsInit)
{
  nsCOMPtr<nsIObserverService> obsService =
    do_GetService("@mozilla.org/observer-service;1");
  NS_ASSERTION(obsService, "No observer service to notify of a11y init/shutdown");
  if (obsService) {
    static const PRUnichar kInitIndicator[] = { '1', 0 };
    static const PRUnichar kShutdownIndicator[] = { '0', 0 }; 
    obsService->NotifyObservers(nsnull, "a11y-init-or-shutdown",
                                aIsInit ? kInitIndicator  : kShutdownIndicator);
  }
}

void nsAccessNode::ShutdownXPAccessibility()
{
  // Called by nsAccessibilityService::Shutdown()
  // which happens when xpcom is shutting down
  // at exit of program

  NS_ASSERTION(gIsAccessibilityActive, "Accessibility was shutdown already!");

  NS_IF_RELEASE(gStringBundle);
  NS_IF_RELEASE(gKeyStringBundle);
  NS_IF_RELEASE(gDoCommandTimer);
  NS_IF_RELEASE(gLastFocusedNode);

  nsApplicationAccessibleWrap::Unload();
  ClearCache(gGlobalDocAccessibleCache);

  // Release gApplicationAccessible after everything else is shutdown
  // so we don't accidently create it again while tearing down root accessibles
  NS_IF_RELEASE(gApplicationAccessible);
  gApplicationAccessible = nsnull;  

#ifdef DEBUG
  gIsAccessibilityActive = PR_FALSE;
#endif
  NotifyA11yInitOrShutdown(PR_FALSE);
}

PRBool
nsAccessNode::IsDefunct()
{
  if (!mDOMNode)
    return PR_TRUE;

  // Call GetPresShell() since the accessible may be shut down in it.
  nsCOMPtr<nsIPresShell> presShell(GetPresShell());
  return !presShell;
}

already_AddRefed<nsIPresShell> nsAccessNode::GetPresShell()
{
  nsIPresShell *presShell = nsnull;
  if (mWeakShell)
    CallQueryReferent(mWeakShell.get(), &presShell);
  if (!presShell) {
    if (mWeakShell) {
      // If our pres shell has died, but we're still holding onto
      // a weak reference, our accessibles are no longer relevant
      // and should be shut down
      Shutdown();
    }
    return nsnull;
  }
  return presShell;
}

nsPresContext* nsAccessNode::GetPresContext()
{
  nsCOMPtr<nsIPresShell> presShell(GetPresShell());
  if (!presShell) {
    return nsnull;
  }
  return presShell->GetPresContext();
}

already_AddRefed<nsIAccessibleDocument> nsAccessNode::GetDocAccessible()
{
  return GetDocAccessibleFor(mWeakShell); // Addref'd
}

already_AddRefed<nsRootAccessible> nsAccessNode::GetRootAccessible()
{
  nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem =
    nsCoreUtils::GetDocShellTreeItemFor(mDOMNode);
  NS_ASSERTION(docShellTreeItem, "No docshell tree item for mDOMNode");
  if (!docShellTreeItem) {
    return nsnull;
  }
  nsCOMPtr<nsIDocShellTreeItem> root;
  docShellTreeItem->GetRootTreeItem(getter_AddRefs(root));
  NS_ASSERTION(root, "No root content tree item");
  if (!root) {
    return nsnull;
  }

  nsCOMPtr<nsIAccessibleDocument> accDoc = GetDocAccessibleFor(root);
  if (!accDoc) {
    return nsnull;
  }

  // nsRootAccessible has a special QI
  // that let us get that concrete type directly.
  nsRootAccessible* rootAccessible;
  accDoc->QueryInterface(NS_GET_IID(nsRootAccessible), (void**)&rootAccessible); // addrefs
  return rootAccessible;
}

nsIFrame*
nsAccessNode::GetFrame()
{
  nsCOMPtr<nsIPresShell> shell(do_QueryReferent(mWeakShell));
  if (!shell) 
    return nsnull;  

  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  return content ? shell->GetPrimaryFrameFor(content) : nsnull;
}

NS_IMETHODIMP
nsAccessNode::GetDOMNode(nsIDOMNode **aNode)
{
  NS_IF_ADDREF(*aNode = mDOMNode);
  return NS_OK;
}

NS_IMETHODIMP
nsAccessNode::GetNumChildren(PRInt32 *aNumChildren)
{
  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));

  if (!content) {
    *aNumChildren = 0;

    return NS_ERROR_NULL_POINTER;
  }

  *aNumChildren = content->GetChildCount();

  return NS_OK;
}

NS_IMETHODIMP
nsAccessNode::GetAccessibleDocument(nsIAccessibleDocument **aDocAccessible)
{
  *aDocAccessible = GetDocAccessibleFor(mWeakShell).get();
  return NS_OK;
}

NS_IMETHODIMP
nsAccessNode::GetInnerHTML(nsAString& aInnerHTML)
{
  aInnerHTML.Truncate();

  nsCOMPtr<nsIDOMNSHTMLElement> domNSElement(do_QueryInterface(mDOMNode));
  NS_ENSURE_TRUE(domNSElement, NS_ERROR_NULL_POINTER);

  return domNSElement->GetInnerHTML(aInnerHTML);
}

NS_IMETHODIMP
nsAccessNode::ScrollTo(PRUint32 aScrollType)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIPresShell> shell(GetPresShell());
  NS_ENSURE_TRUE(shell, NS_ERROR_FAILURE);

  nsIFrame *frame = GetFrame();
  NS_ENSURE_TRUE(frame, NS_ERROR_FAILURE);

  nsCOMPtr<nsIContent> content = frame->GetContent();
  NS_ENSURE_TRUE(content, NS_ERROR_FAILURE);

  PRInt16 vPercent, hPercent;
  nsCoreUtils::ConvertScrollTypeToPercents(aScrollType, &vPercent, &hPercent);
  return shell->ScrollContentIntoView(content, vPercent, hPercent);
}

NS_IMETHODIMP
nsAccessNode::ScrollToPoint(PRUint32 aCoordinateType, PRInt32 aX, PRInt32 aY)
{
  nsIFrame *frame = GetFrame();
  if (!frame)
    return NS_ERROR_FAILURE;

  nsIntPoint coords;
  nsresult rv = nsAccUtils::ConvertToScreenCoords(aX, aY, aCoordinateType,
                                                  this, &coords);
  NS_ENSURE_SUCCESS(rv, rv);

  nsIFrame *parentFrame = frame;
  while ((parentFrame = parentFrame->GetParent()))
    nsCoreUtils::ScrollFrameToPoint(parentFrame, frame, coords);

  return NS_OK;
}

nsresult
nsAccessNode::MakeAccessNode(nsIDOMNode *aNode, nsIAccessNode **aAccessNode)
{
  *aAccessNode = nsnull;
  
  nsIAccessibilityService *accService = GetAccService();
  NS_ENSURE_TRUE(accService, NS_ERROR_FAILURE);

  nsCOMPtr<nsIAccessNode> accessNode;
  accService->GetCachedAccessNode(aNode, mWeakShell, getter_AddRefs(accessNode));

  if (!accessNode) {
    nsCOMPtr<nsIAccessible> accessible;
    accService->GetAccessibleInWeakShell(aNode, mWeakShell,
                                         getter_AddRefs(accessible));

    accessNode = do_QueryInterface(accessible);
  }

  if (accessNode) {
    NS_ADDREF(*aAccessNode = accessNode);
    return NS_OK;
  }

  nsAccessNode *newAccessNode = new nsAccessNode(aNode, mWeakShell);
  if (!newAccessNode) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  NS_ADDREF(*aAccessNode = newAccessNode);
  newAccessNode->Init();

  return NS_OK;
}

NS_IMETHODIMP
nsAccessNode::GetFirstChildNode(nsIAccessNode **aAccessNode)
{
  NS_ENSURE_ARG_POINTER(aAccessNode);
  *aAccessNode = nsnull;
  NS_ENSURE_TRUE(mDOMNode, NS_ERROR_NULL_POINTER);

  nsCOMPtr<nsIDOMNode> domNode;
  mDOMNode->GetFirstChild(getter_AddRefs(domNode));

  return domNode ? MakeAccessNode(domNode, aAccessNode) : NS_OK;
}

NS_IMETHODIMP
nsAccessNode::GetLastChildNode(nsIAccessNode **aAccessNode)
{
  NS_ENSURE_ARG_POINTER(aAccessNode);
  *aAccessNode = nsnull;
  NS_ENSURE_TRUE(mDOMNode, NS_ERROR_NULL_POINTER);

  nsCOMPtr<nsIDOMNode> domNode;
  mDOMNode->GetLastChild(getter_AddRefs(domNode));

  return domNode ? MakeAccessNode(domNode, aAccessNode) : NS_OK;
}

NS_IMETHODIMP
nsAccessNode::GetParentNode(nsIAccessNode **aAccessNode)
{
  NS_ENSURE_ARG_POINTER(aAccessNode);
  *aAccessNode = nsnull;
  NS_ENSURE_TRUE(mDOMNode, NS_ERROR_NULL_POINTER);

  nsCOMPtr<nsIDOMNode> domNode;
  mDOMNode->GetParentNode(getter_AddRefs(domNode));

  return domNode ? MakeAccessNode(domNode, aAccessNode) : NS_OK;
}

NS_IMETHODIMP
nsAccessNode::GetPreviousSiblingNode(nsIAccessNode **aAccessNode)
{
  NS_ENSURE_ARG_POINTER(aAccessNode);
  *aAccessNode = nsnull;
  NS_ENSURE_TRUE(mDOMNode, NS_ERROR_NULL_POINTER);

  nsCOMPtr<nsIDOMNode> domNode;
  mDOMNode->GetPreviousSibling(getter_AddRefs(domNode));

  return domNode ? MakeAccessNode(domNode, aAccessNode) : NS_OK;
}

NS_IMETHODIMP
nsAccessNode::GetNextSiblingNode(nsIAccessNode **aAccessNode)
{
  NS_ENSURE_ARG_POINTER(aAccessNode);
  *aAccessNode = nsnull;
  NS_ENSURE_TRUE(mDOMNode, NS_ERROR_NULL_POINTER);

  nsCOMPtr<nsIDOMNode> domNode;
  mDOMNode->GetNextSibling(getter_AddRefs(domNode));

  return domNode ? MakeAccessNode(domNode, aAccessNode) : NS_OK;
}

NS_IMETHODIMP
nsAccessNode::GetChildNodeAt(PRInt32 aChildNum, nsIAccessNode **aAccessNode)
{
  NS_ENSURE_ARG_POINTER(aAccessNode);
  *aAccessNode = nsnull;

  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  NS_ENSURE_TRUE(content, NS_ERROR_NULL_POINTER);

  nsCOMPtr<nsIDOMNode> domNode =
    do_QueryInterface(content->GetChildAt(aChildNum));

  return domNode ? MakeAccessNode(domNode, aAccessNode) : NS_OK;
}

NS_IMETHODIMP
nsAccessNode::GetComputedStyleValue(const nsAString& aPseudoElt,
                                    const nsAString& aPropertyName,
                                    nsAString& aValue)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIDOMCSSStyleDeclaration> styleDecl;
  nsCoreUtils::GetComputedStyleDeclaration(aPseudoElt, mDOMNode,
                                           getter_AddRefs(styleDecl));
  NS_ENSURE_TRUE(styleDecl, NS_ERROR_FAILURE);

  return styleDecl->GetPropertyValue(aPropertyName, aValue);
}

NS_IMETHODIMP
nsAccessNode::GetComputedStyleCSSValue(const nsAString& aPseudoElt,
                                       const nsAString& aPropertyName,
                                       nsIDOMCSSPrimitiveValue **aCSSValue)
{
  NS_ENSURE_ARG_POINTER(aCSSValue);
  *aCSSValue = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIDOMCSSStyleDeclaration> styleDecl;
  nsCoreUtils::GetComputedStyleDeclaration(aPseudoElt, mDOMNode,
                                           getter_AddRefs(styleDecl));
  NS_ENSURE_STATE(styleDecl);

  nsCOMPtr<nsIDOMCSSValue> cssValue;
  styleDecl->GetPropertyCSSValue(aPropertyName, getter_AddRefs(cssValue));
  NS_ENSURE_TRUE(cssValue, NS_ERROR_FAILURE);

  return CallQueryInterface(cssValue, aCSSValue);
}

/***************** Hashtable of nsIAccessNode's *****************/

already_AddRefed<nsIAccessibleDocument>
nsAccessNode::GetDocAccessibleFor(nsIDocument *aDocument)
{
  if (!aDocument) {
    return nsnull;
  }

  nsIAccessibleDocument *docAccessible = nsnull;
  nsCOMPtr<nsIAccessNode> accessNode;
  gGlobalDocAccessibleCache.Get(static_cast<void*>(aDocument),
                                getter_AddRefs(accessNode));
  if (accessNode) {
    CallQueryInterface(accessNode, &docAccessible);
  }
  return docAccessible;
}
 
already_AddRefed<nsIAccessibleDocument>
nsAccessNode::GetDocAccessibleFor(nsIWeakReference *aWeakShell)
{
  nsCOMPtr<nsIPresShell> presShell(do_QueryReferent(aWeakShell));
  if (!presShell) {
    return nsnull;
  }

  return nsAccessNode::GetDocAccessibleFor(presShell->GetDocument());
}

already_AddRefed<nsIAccessibleDocument>
nsAccessNode::GetDocAccessibleFor(nsIDocShellTreeItem *aContainer,
                                  PRBool aCanCreate)
{
  if (!aCanCreate) {
    nsCOMPtr<nsIDocShell> docShell(do_QueryInterface(aContainer));
    NS_ASSERTION(docShell, "This method currently only supports docshells");
    nsCOMPtr<nsIPresShell> presShell;
    docShell->GetPresShell(getter_AddRefs(presShell));
    return presShell ? GetDocAccessibleFor(presShell->GetDocument()) : nsnull;
  }

  nsCOMPtr<nsIDOMNode> node = nsCoreUtils::GetDOMNodeForContainer(aContainer);
  if (!node) {
    return nsnull;
  }

  nsCOMPtr<nsIAccessible> accessible;
  GetAccService()->GetAccessibleFor(node, getter_AddRefs(accessible));
  nsIAccessibleDocument *docAccessible = nsnull;
  if (accessible) {
    CallQueryInterface(accessible, &docAccessible);
  }
  return docAccessible;
}
 
already_AddRefed<nsIAccessibleDocument>
nsAccessNode::GetDocAccessibleFor(nsIDOMNode *aNode)
{
  nsCOMPtr<nsIPresShell> eventShell = nsCoreUtils::GetPresShellFor(aNode);
  if (eventShell) {
    return GetDocAccessibleFor(eventShell->GetDocument());
  }

  nsCOMPtr<nsIDocument> doc(do_QueryInterface(aNode));
  if (doc) {
    return GetDocAccessibleFor(doc);
  }

  return nsnull;
}

void
nsAccessNode::PutCacheEntry(nsAccessNodeHashtable& aCache,
                            void* aUniqueID,
                            nsIAccessNode *aAccessNode)
{
#ifdef DEBUG_A11Y
  nsCOMPtr<nsIAccessNode> oldAccessNode;
  GetCacheEntry(aCache, aUniqueID, getter_AddRefs(oldAccessNode));
  NS_ASSERTION(!oldAccessNode, "This cache entry shouldn't exist already");
#endif
  aCache.Put(aUniqueID, aAccessNode);
}

void
nsAccessNode::GetCacheEntry(nsAccessNodeHashtable& aCache,
                            void* aUniqueID,
                            nsIAccessNode **aAccessNode)
{
  aCache.Get(aUniqueID, aAccessNode);  // AddRefs for us
}

PLDHashOperator nsAccessNode::ClearCacheEntry(const void* aKey, nsCOMPtr<nsIAccessNode>& aAccessNode, void* aUserArg)
{
  NS_ASSERTION(aAccessNode, "Calling ClearCacheEntry with a NULL pointer!");
  if (aAccessNode) {
    nsRefPtr<nsAccessNode> accessNode =
      nsAccUtils::QueryAccessNode(aAccessNode);
    accessNode->Shutdown();
  }

  return PL_DHASH_REMOVE;
}

void
nsAccessNode::ClearCache(nsAccessNodeHashtable& aCache)
{
  aCache.Enumerate(ClearCacheEntry, nsnull);
}

already_AddRefed<nsIDOMNode> nsAccessNode::GetCurrentFocus()
{
  nsCOMPtr<nsIPresShell> shell = nsCoreUtils::GetPresShellFor(mDOMNode);
  NS_ENSURE_TRUE(shell, nsnull);
  nsCOMPtr<nsIDocument> doc = shell->GetDocument();
  NS_ENSURE_TRUE(doc, nsnull);

  nsIDOMWindow* win = doc->GetWindow();

  nsCOMPtr<nsIDOMWindow> focusedWindow;
  nsCOMPtr<nsIDOMElement> focusedElement;
  nsCOMPtr<nsIFocusManager> fm = do_GetService(FOCUSMANAGER_CONTRACTID);
  if (fm)
    fm->GetFocusedElementForWindow(win, PR_TRUE, getter_AddRefs(focusedWindow),
                                   getter_AddRefs(focusedElement));

  nsIDOMNode *focusedNode = nsnull;
  if (focusedElement) {
    CallQueryInterface(focusedElement, &focusedNode);
  }
  else if (focusedWindow) {
    nsCOMPtr<nsIDOMDocument> doc;
    focusedWindow->GetDocument(getter_AddRefs(doc));
    if (doc)
      CallQueryInterface(doc, &focusedNode);
  }

  return focusedNode;
}

NS_IMETHODIMP
nsAccessNode::GetLanguage(nsAString& aLanguage)
{
  aLanguage.Truncate();
  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  if (!content) {
    // For documents make sure we look for lang attribute on
    // document element
    nsCOMPtr<nsIDOMDocument> domDoc(do_QueryInterface(mDOMNode));
    if (domDoc) {
      nsCOMPtr<nsIDOMHTMLDocument> htmlDoc(do_QueryInterface(mDOMNode));
      if (htmlDoc) {
        // Make sure we look for lang attribute on HTML <body>
        nsCOMPtr<nsIDOMHTMLElement> bodyElement;
        htmlDoc->GetBody(getter_AddRefs(bodyElement));
        content = do_QueryInterface(bodyElement);
      }
      if (!content) {
        nsCOMPtr<nsIDOMElement> docElement;
        domDoc->GetDocumentElement(getter_AddRefs(docElement));
        content = do_QueryInterface(docElement);
      }
    }
    if (!content) {
      return NS_ERROR_FAILURE;
    }
  }

  nsCoreUtils::GetLanguageFor(content, nsnull, aLanguage);

  if (aLanguage.IsEmpty()) { // Nothing found, so use document's language
    nsIDocument *doc = content->GetOwnerDoc();
    if (doc) {
      doc->GetHeaderData(nsAccessibilityAtoms::headerContentLanguage, aLanguage);
    }
  }
 
  return NS_OK;
}


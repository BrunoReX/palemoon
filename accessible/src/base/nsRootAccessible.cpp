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

#include "nsAccessibilityService.h"
#include "nsApplicationAccessibleWrap.h"
#include "nsAccUtils.h"
#include "nsCoreUtils.h"
#include "nsRelUtils.h"

#include "mozilla/dom/Element.h"
#include "nsHTMLSelectAccessible.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShellTreeNode.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIDOMDocument.h"
#include "nsIDOMElement.h"
#include "nsIDOMEventListener.h"
#include "nsIDOMEventTarget.h"
#include "nsIDOMHTMLAnchorElement.h"
#include "nsIDOMHTMLImageElement.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsIDOMHTMLSelectElement.h"
#include "nsIDOMDataContainerEvent.h"
#include "nsIDOMNSDocument.h"
#include "nsIDOMNSEvent.h"
#include "nsIDOMXULMenuListElement.h"
#include "nsIDOMXULMultSelectCntrlEl.h"
#include "nsIDOMXULSelectCntrlItemEl.h"
#include "nsIDOMXULPopupElement.h"
#include "nsIDocument.h"
#include "nsIEventListenerManager.h"
#include "nsIFrame.h"
#include "nsIMenuFrame.h"
#include "nsIHTMLDocument.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsISelectionPrivate.h"
#include "nsIServiceManager.h"
#include "nsPIDOMWindow.h"
#include "nsIWebBrowserChrome.h"
#include "nsReadableUtils.h"
#include "nsRootAccessible.h"
#include "nsIDOMNSEventTarget.h"
#include "nsIDOMDocumentEvent.h"
#include "nsIPrivateDOMEvent.h"
#include "nsFocusManager.h"
#include "mozilla/dom/Element.h"


#ifdef MOZ_XUL
#include "nsXULTreeAccessible.h"
#include "nsIXULDocument.h"
#include "nsIXULWindow.h"
#endif

using namespace mozilla;

////////////////////////////////////////////////////////////////////////////////
// nsISupports

// Expanded version of NS_IMPL_ISUPPORTS_INHERITED2 
// so we can QI directly to concrete nsRootAccessible
NS_IMPL_QUERY_HEAD(nsRootAccessible)
NS_IMPL_QUERY_BODY(nsIDOMEventListener)
if (aIID.Equals(NS_GET_IID(nsRootAccessible)))
  foundInterface = reinterpret_cast<nsISupports*>(this);
else
NS_IMPL_QUERY_TAIL_INHERITING(nsDocAccessible)

NS_IMPL_ADDREF_INHERITED(nsRootAccessible, nsDocAccessible) 
NS_IMPL_RELEASE_INHERITED(nsRootAccessible, nsDocAccessible)

////////////////////////////////////////////////////////////////////////////////
// Constructor/desctructor

nsRootAccessible::
  nsRootAccessible(nsIDocument *aDocument, nsIContent *aRootContent,
                   nsIWeakReference *aShell) :
  nsDocAccessibleWrap(aDocument, aRootContent, aShell)
{
  mFlags |= eRootAccessible;
}

nsRootAccessible::~nsRootAccessible()
{
}

////////////////////////////////////////////////////////////////////////////////
// nsIAccessible

/* readonly attribute AString name; */
NS_IMETHODIMP
nsRootAccessible::GetName(nsAString& aName)
{
  aName.Truncate();

  if (!mDocument) {
    return NS_ERROR_FAILURE;
  }

  if (mRoleMapEntry) {
    nsAccessible::GetName(aName);
    if (!aName.IsEmpty()) {
      return NS_OK;
    }
  }

  nsCOMPtr<nsIDOMNSDocument> document(do_QueryInterface(mDocument));
  return document->GetTitle(aName);
}

PRUint32
nsRootAccessible::NativeRole()
{
  // If it's a <dialog> or <wizard>, use nsIAccessibleRole::ROLE_DIALOG instead
  dom::Element *root = mDocument->GetRootElement();
  if (root) {
    nsCOMPtr<nsIDOMElement> rootElement(do_QueryInterface(root));
    if (rootElement) {
      nsAutoString name;
      rootElement->GetLocalName(name);
      if (name.EqualsLiteral("dialog") || name.EqualsLiteral("wizard")) {
        return nsIAccessibleRole::ROLE_DIALOG; // Always at the root
      }
    }
  }

  return nsDocAccessibleWrap::NativeRole();
}

// nsRootAccessible protected member
#ifdef MOZ_XUL
PRUint32 nsRootAccessible::GetChromeFlags()
{
  // Return the flag set for the top level window as defined 
  // by nsIWebBrowserChrome::CHROME_WINDOW_[FLAGNAME]
  // Not simple: nsIXULWindow is not just a QI from nsIDOMWindow
  nsCOMPtr<nsIDocShellTreeItem> treeItem =
    nsCoreUtils::GetDocShellTreeItemFor(mDocument);
  NS_ENSURE_TRUE(treeItem, 0);
  nsCOMPtr<nsIDocShellTreeOwner> treeOwner;
  treeItem->GetTreeOwner(getter_AddRefs(treeOwner));
  NS_ENSURE_TRUE(treeOwner, 0);
  nsCOMPtr<nsIXULWindow> xulWin(do_GetInterface(treeOwner));
  if (!xulWin) {
    return 0;
  }
  PRUint32 chromeFlags;
  xulWin->GetChromeFlags(&chromeFlags);
  return chromeFlags;
}
#endif

nsresult
nsRootAccessible::GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState)
{
  nsresult rv = nsDocAccessibleWrap::GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

#ifdef MOZ_XUL
  PRUint32 chromeFlags = GetChromeFlags();
  if (chromeFlags & nsIWebBrowserChrome::CHROME_WINDOW_RESIZE) {
    *aState |= nsIAccessibleStates::STATE_SIZEABLE;
  }
  if (chromeFlags & nsIWebBrowserChrome::CHROME_TITLEBAR) {
    // If it has a titlebar it's movable
    // XXX unless it's minimized or maximized, but not sure
    //     how to detect that
    *aState |= nsIAccessibleStates::STATE_MOVEABLE;
  }
#endif

  if (!aExtraState)
    return NS_OK;

  nsCOMPtr<nsIFocusManager> fm = do_GetService(FOCUSMANAGER_CONTRACTID);
  if (fm) {
    nsCOMPtr<nsIDOMWindow> rootWindow;
    GetWindow(getter_AddRefs(rootWindow));

    nsCOMPtr<nsIDOMWindow> activeWindow;
    fm->GetActiveWindow(getter_AddRefs(activeWindow));
    if (activeWindow == rootWindow)
      *aExtraState |= nsIAccessibleStates::EXT_STATE_ACTIVE;
  }

#ifdef MOZ_XUL
  if (GetChromeFlags() & nsIWebBrowserChrome::CHROME_MODAL) {
    *aExtraState |= nsIAccessibleStates::EXT_STATE_MODAL;
  }
#endif

  return NS_OK;
}

const char* const docEvents[] = {
#ifdef DEBUG_DRAGDROPSTART
  // Capture mouse over events and fire fake DRAGDROPSTART event to simplify
  // debugging a11y objects with event viewers
  "mouseover",
#endif
  // capture DOM focus and DOM blur events 
  "focus",
  "blur",
  // capture Form change events 
  "select",
  // capture ValueChange events (fired whenever value changes, immediately after, whether focus moves or not)
  "ValueChange",
  // capture AlertActive events (fired whenever alert pops up)
  "AlertActive",
  // add ourself as a TreeViewChanged listener (custom event fired in nsTreeBodyFrame.cpp)
  "TreeViewChanged",
  "TreeRowCountChanged",
  "TreeInvalidated",
  // add ourself as a OpenStateChange listener (custom event fired in tree.xml)
  "OpenStateChange",
  // add ourself as a CheckboxStateChange listener (custom event fired in nsHTMLInputElement.cpp)
  "CheckboxStateChange",
  // add ourself as a RadioStateChange Listener ( custom event fired in in nsHTMLInputElement.cpp  & radio.xml)
  "RadioStateChange",
  "popupshown",
  "popuphiding",
  "DOMMenuInactive",
  "DOMMenuItemActive",
  "DOMMenuBarActive",
  "DOMMenuBarInactive"
};

nsresult nsRootAccessible::AddEventListeners()
{
  // nsIDOMNSEventTarget interface allows to register event listeners to
  // receive untrusted events (synthetic events generated by untrusted code).
  // For example, XBL bindings implementations for elements that are hosted in
  // non chrome document fire untrusted events.
  nsCOMPtr<nsIDOMNSEventTarget> nstarget(do_QueryInterface(mDocument));

  if (nstarget) {
    for (const char* const* e = docEvents,
                   * const* e_end = docEvents + NS_ARRAY_LENGTH(docEvents);
         e < e_end; ++e) {
      nsresult rv = nstarget->AddEventListener(NS_ConvertASCIItoUTF16(*e),
                                               this, PR_TRUE, PR_TRUE, 1);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  if (!mCaretAccessible) {
    mCaretAccessible = new nsCaretAccessible(this);
  }

  return nsDocAccessible::AddEventListeners();
}

nsresult nsRootAccessible::RemoveEventListeners()
{
  nsCOMPtr<nsIDOMEventTarget> target(do_QueryInterface(mDocument));
  if (target) { 
    for (const char* const* e = docEvents,
                   * const* e_end = docEvents + NS_ARRAY_LENGTH(docEvents);
         e < e_end; ++e) {
      nsresult rv = target->RemoveEventListener(NS_ConvertASCIItoUTF16(*e), this, PR_TRUE);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // Do this before removing clearing caret accessible, so that it can use
  // shutdown the caret accessible's selection listener
  nsDocAccessible::RemoveEventListeners();

  if (mCaretAccessible) {
    mCaretAccessible->Shutdown();
    mCaretAccessible = nsnull;
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// public

nsCaretAccessible*
nsRootAccessible::GetCaretAccessible()
{
  return mCaretAccessible;
}

void
nsRootAccessible::FireAccessibleFocusEvent(nsAccessible* aFocusAccessible,
                                           nsIContent* aRealFocusContent,
                                           PRBool aForceEvent,
                                           EIsFromUserInput aIsFromUserInput)
{
  // Implementors: only fire delayed/async events from this method.

  // Set selection listener for focused element.
  if (mCaretAccessible && aRealFocusContent)
    mCaretAccessible->SetControlSelectionListener(aRealFocusContent);

  nsAccessible* focusAccessible = aFocusAccessible;

  // Check for aria-activedescendant, which changes which element has focus.
  // For activedescendant, the ARIA spec does not require that the user agent
  // checks whether pointed node is actually a DOM descendant of the element
  // with the aria-activedescendant attribute.
  nsIContent* content = focusAccessible->GetContent();
  if (content) {
    nsAutoString id;
    if (content->GetAttr(kNameSpaceID_None,
                         nsAccessibilityAtoms::aria_activedescendant, id)) {
      nsIDocument* DOMDoc = content->GetOwnerDoc();
      nsIContent* activeDescendantContent = DOMDoc->GetElementById(id);

      // If aria-activedescendant is set to nonexistant ID, then treat as focus
      // on the activedescendant container (which has real DOM focus).
      if (activeDescendantContent) {
        nsAccessible* activeDescendant = 
          GetAccService()->GetAccessible(activeDescendantContent);
        if (activeDescendant) {
          focusAccessible = activeDescendant;
        }
      }
    }
  }

  // Fire focus only if it changes, but always fire focus events when
  // aForceEvent == PR_TRUE
  nsINode* focusNode = focusAccessible->GetNode();
  if (gLastFocusedNode == focusNode && !aForceEvent)
    return;

  nsDocAccessible* focusDocument = focusAccessible->GetDocAccessible();
  NS_ASSERTION(focusDocument, "No document while accessible is in document?!");

  gLastFocusedAccessiblesState = nsAccUtils::State(focusAccessible);

  // Fire menu start/end events for ARIA menus.
  if (focusAccessible->ARIARole() == nsIAccessibleRole::ROLE_MENUITEM) {
    // The focus is inside a menu.
    if (!mCurrentARIAMenubar) {
      // Entering ARIA menu. Fire menu start event.
      nsAccessible* menuBarAccessible =
        nsAccUtils::GetAncestorWithRole(focusAccessible,
                                        nsIAccessibleRole::ROLE_MENUBAR);
      if (menuBarAccessible) {
        mCurrentARIAMenubar = menuBarAccessible->GetNode();
        if (mCurrentARIAMenubar) {
          nsRefPtr<AccEvent> menuStartEvent =
            new AccEvent(nsIAccessibleEvent::EVENT_MENU_START,
                         menuBarAccessible, aIsFromUserInput,
                         AccEvent::eAllowDupes);
          if (menuStartEvent)
            focusDocument->FireDelayedAccessibleEvent(menuStartEvent);
        }
      }
    }
  }
  else if (mCurrentARIAMenubar) {
    // Focus left a menu. Fire menu end event.
    nsRefPtr<AccEvent> menuEndEvent =
      new AccEvent(nsIAccessibleEvent::EVENT_MENU_END, mCurrentARIAMenubar,
                   aIsFromUserInput, AccEvent::eAllowDupes);
    if (menuEndEvent) {
      focusDocument->FireDelayedAccessibleEvent(menuEndEvent);
    }
    mCurrentARIAMenubar = nsnull;
  }

  NS_IF_RELEASE(gLastFocusedNode);
  gLastFocusedNode = focusNode;
  NS_IF_ADDREF(gLastFocusedNode);

  // Coalesce focus events from the same document, because DOM focus event might
  // be fired for the document node and then for the focused DOM element.
  focusDocument->FireDelayedAccessibleEvent(nsIAccessibleEvent::EVENT_FOCUS,
                                            focusNode,
                                            AccEvent::eCoalesceFromSameDocument,
                                            aIsFromUserInput);
}

void
nsRootAccessible::FireCurrentFocusEvent()
{
  if (IsDefunct())
    return;

  // Simulate a focus event so that we can reuse code that fires focus for
  // container children like treeitems.
  nsCOMPtr<nsINode> focusedNode = GetCurrentFocus();
  if (!focusedNode) {
    return; // No current focus
  }

  nsCOMPtr<nsIDOMDocumentEvent> docEvent = do_QueryInterface(mDocument);
  if (docEvent) {
    nsCOMPtr<nsIDOMEvent> event;
    if (NS_SUCCEEDED(docEvent->CreateEvent(NS_LITERAL_STRING("Events"),
                                           getter_AddRefs(event))) &&
        NS_SUCCEEDED(event->InitEvent(NS_LITERAL_STRING("focus"), PR_TRUE, PR_TRUE))) {

      nsCOMPtr<nsIPrivateDOMEvent> privateEvent(do_QueryInterface(event));
      nsCOMPtr<nsIDOMEventTarget> target(do_QueryInterface(focusedNode));
      privateEvent->SetTarget(target);
      HandleEvent(event);
    }
  }
}

void
nsRootAccessible::DocumentActivated(nsDocAccessible* aDocument)
{
}

////////////////////////////////////////////////////////////////////////////////
// nsIDOMEventListener

NS_IMETHODIMP
nsRootAccessible::HandleEvent(nsIDOMEvent* aDOMEvent)
{
  nsCOMPtr<nsIDOMNSEvent> DOMNSEvent(do_QueryInterface(aDOMEvent));
  nsCOMPtr<nsIDOMEventTarget> DOMEventTarget;
  DOMNSEvent->GetOriginalTarget(getter_AddRefs(DOMEventTarget));
  nsCOMPtr<nsINode> origTargetNode(do_QueryInterface(DOMEventTarget));
  if (!origTargetNode)
    return NS_OK;

  nsDocAccessible* document =
    GetAccService()->GetDocAccessible(origTargetNode->GetOwnerDoc());

  if (document) {
#ifdef DEBUG_NOTIFICATIONS
    if (origTargetNode->IsElement()) {
      nsIContent* elm = origTargetNode->AsElement();

      nsAutoString tag;
      elm->Tag()->ToString(tag);

      nsIAtom* atomid = elm->GetID();
      nsCAutoString id;
      if (atomid)
        atomid->ToUTF8String(id);

      nsAutoString eventType;
      aDOMEvent->GetType(eventType);

      printf("\nPend DOM event processing for %s@id='%s', type: %s\n\n",
             NS_ConvertUTF16toUTF8(tag).get(), id.get(),
             NS_ConvertUTF16toUTF8(eventType).get());
    }
#endif

    // Root accessible exists longer than any of its descendant documents so
    // that we are guaranteed notification is processed before root accessible
    // is destroyed.
    document->HandleNotification<nsRootAccessible, nsIDOMEvent>
      (this, &nsRootAccessible::ProcessDOMEvent, aDOMEvent);
  }

  return NS_OK;
}

// nsRootAccessible protected
void
nsRootAccessible::ProcessDOMEvent(nsIDOMEvent* aDOMEvent)
{
  nsCOMPtr<nsIDOMNSEvent> DOMNSEvent(do_QueryInterface(aDOMEvent));
  nsCOMPtr<nsIDOMEventTarget> DOMEventTarget;
  DOMNSEvent->GetOriginalTarget(getter_AddRefs(DOMEventTarget));
  nsCOMPtr<nsINode> origTargetNode(do_QueryInterface(DOMEventTarget));

  nsAutoString eventType;
  aDOMEvent->GetType(eventType);

  nsCOMPtr<nsIWeakReference> weakShell =
    nsCoreUtils::GetWeakShellFor(origTargetNode);
  if (!weakShell)
    return;

  nsAccessible* accessible =
    GetAccService()->GetAccessibleOrContainer(origTargetNode, weakShell);

  if (eventType.EqualsLiteral("popuphiding")) {
    HandlePopupHidingEvent(origTargetNode, accessible);
    return;
  }

  if (!accessible)
    return;

  nsDocAccessible* targetDocument = accessible->GetDocAccessible();
  NS_ASSERTION(targetDocument, "No document while accessible is in document?!");

  nsINode* targetNode = accessible->GetNode();
  nsIContent* targetContent = targetNode->IsElement() ?
    targetNode->AsElement() : nsnull;
  nsIContent* origTargetContent = origTargetNode->IsElement() ?
    origTargetNode->AsElement() : nsnull;

#ifdef MOZ_XUL
  PRBool isTree = targetContent ?
    targetContent->NodeInfo()->Equals(nsAccessibilityAtoms::tree,
                                      kNameSpaceID_XUL) : PR_FALSE;

  if (isTree) {
    nsRefPtr<nsXULTreeAccessible> treeAcc = do_QueryObject(accessible);
    NS_ASSERTION(treeAcc,
                 "Accessible for xul:tree isn't nsXULTreeAccessible.");

    if (treeAcc) {
      if (eventType.EqualsLiteral("TreeViewChanged")) {
        treeAcc->TreeViewChanged();
        return;
      }

      if (eventType.EqualsLiteral("TreeRowCountChanged")) {
        HandleTreeRowCountChangedEvent(aDOMEvent, treeAcc);
        return;
      }
      
      if (eventType.EqualsLiteral("TreeInvalidated")) {
        HandleTreeInvalidatedEvent(aDOMEvent, treeAcc);
        return;
      }
    }
  }
#endif

  if (eventType.EqualsLiteral("RadioStateChange")) {
    PRUint32 state = nsAccUtils::State(accessible);

    // radiogroup in prefWindow is exposed as a list,
    // and panebutton is exposed as XULListitem in A11y.
    // nsXULListitemAccessible::GetStateInternal uses STATE_SELECTED in this case,
    // so we need to check nsIAccessibleStates::STATE_SELECTED also.
    PRBool isEnabled = (state & (nsIAccessibleStates::STATE_CHECKED |
                        nsIAccessibleStates::STATE_SELECTED)) != 0;

    nsRefPtr<AccEvent> accEvent =
      new AccStateChangeEvent(accessible, nsIAccessibleStates::STATE_CHECKED,
                              PR_FALSE, isEnabled);
    nsEventShell::FireEvent(accEvent);

    if (isEnabled)
      FireAccessibleFocusEvent(accessible, origTargetContent);

    return;
  }

  if (eventType.EqualsLiteral("CheckboxStateChange")) {
    PRUint32 state = nsAccUtils::State(accessible);

    PRBool isEnabled = !!(state & nsIAccessibleStates::STATE_CHECKED);

    nsRefPtr<AccEvent> accEvent =
      new AccStateChangeEvent(accessible, nsIAccessibleStates::STATE_CHECKED,
                              PR_FALSE, isEnabled);

    nsEventShell::FireEvent(accEvent);
    return;
  }

  nsAccessible *treeItemAccessible = nsnull;
#ifdef MOZ_XUL
  // If it's a tree element, need the currently selected item
  if (isTree) {
    nsCOMPtr<nsIDOMXULMultiSelectControlElement> multiSelect =
      do_QueryInterface(targetNode);
    if (multiSelect) {
      PRInt32 treeIndex = -1;
      multiSelect->GetCurrentIndex(&treeIndex);
      if (treeIndex >= 0) {
        nsRefPtr<nsXULTreeAccessible> treeAcc = do_QueryObject(accessible);
        if (treeAcc) {
          treeItemAccessible = treeAcc->GetTreeItemAccessible(treeIndex);
          if (treeItemAccessible)
            accessible = treeItemAccessible;
        }
      }
    }
  }
#endif

#ifdef MOZ_XUL
  if (treeItemAccessible && eventType.EqualsLiteral("OpenStateChange")) {
    PRUint32 state = nsAccUtils::State(accessible); // collapsed/expanded changed
    PRBool isEnabled = (state & nsIAccessibleStates::STATE_EXPANDED) != 0;

    nsRefPtr<AccEvent> accEvent =
      new AccStateChangeEvent(accessible, nsIAccessibleStates::STATE_EXPANDED,
                              PR_FALSE, isEnabled);
    nsEventShell::FireEvent(accEvent);
    return;
  }

  if (treeItemAccessible && eventType.EqualsLiteral("select")) {
    // If multiselect tree, we should fire selectionadd or selection removed
    if (gLastFocusedNode == targetNode) {
      nsCOMPtr<nsIDOMXULMultiSelectControlElement> multiSel =
        do_QueryInterface(targetNode);
      nsAutoString selType;
      multiSel->GetSelType(selType);
      if (selType.IsEmpty() || !selType.EqualsLiteral("single")) {
        // XXX: We need to fire EVENT_SELECTION_ADD and EVENT_SELECTION_REMOVE
        // for each tree item. Perhaps each tree item will need to cache its
        // selection state and fire an event after a DOM "select" event when
        // that state changes. nsXULTreeAccessible::UpdateTreeSelection();
        nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_SELECTION_WITHIN,
                                accessible);
        return;
      }

      nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_SELECTION,
                              treeItemAccessible);
      return;
    }
  }
  else
#endif
  if (eventType.EqualsLiteral("focus")) {
    // Keep a reference to the target node. We might want to change
    // it to the individual radio button or selected item, and send
    // the focus event to that.
    nsCOMPtr<nsINode> focusedItem = targetNode;
    if (!treeItemAccessible) {
      nsCOMPtr<nsIDOMXULSelectControlElement> selectControl =
        do_QueryInterface(targetNode);
      if (selectControl) {
        nsCOMPtr<nsIDOMXULMenuListElement> menuList =
          do_QueryInterface(targetNode);
        if (!menuList) {
          // Don't do this for menu lists, the items only get focused
          // when the list is open, based on DOMMenuitemActive events
          nsCOMPtr<nsIDOMXULSelectControlItemElement> selectedItem;
          selectControl->GetSelectedItem(getter_AddRefs(selectedItem));
          if (selectedItem)
            focusedItem = do_QueryInterface(selectedItem);

          if (!focusedItem)
            return;

          accessible = GetAccService()->GetAccessibleInWeakShell(focusedItem,
                                                                 weakShell);
          if (!accessible)
            return;
        }
      }
    }
    FireAccessibleFocusEvent(accessible, origTargetContent);
  }
  else if (eventType.EqualsLiteral("blur")) {
    NS_IF_RELEASE(gLastFocusedNode);
    gLastFocusedAccessiblesState = 0;
  }
  else if (eventType.EqualsLiteral("AlertActive")) { 
    nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_ALERT, accessible);
  }
  else if (eventType.EqualsLiteral("popupshown")) {
    HandlePopupShownEvent(accessible);
  }
  else if (eventType.EqualsLiteral("DOMMenuInactive")) {
    if (accessible->Role() == nsIAccessibleRole::ROLE_MENUPOPUP) {
      nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_MENUPOPUP_END,
                              accessible);
    }
  }
  else if (eventType.EqualsLiteral("DOMMenuItemActive")) {
    PRBool fireFocus = PR_FALSE;
    if (!treeItemAccessible) {
#ifdef MOZ_XUL
      if (isTree) {
        return; // Tree with nothing selected
      }
#endif
      nsIFrame* menuFrame = accessible->GetFrame();
      if (!menuFrame)
        return;

      nsIMenuFrame* imenuFrame = do_QueryFrame(menuFrame);
      if (imenuFrame)
        fireFocus = PR_TRUE;
      // QI failed for nsIMenuFrame means it's not on menu bar
      if (imenuFrame && imenuFrame->IsOnMenuBar() &&
                       !imenuFrame->IsOnActiveMenuBar()) {
        // It is a top level menuitem. Only fire a focus event when the menu bar
        // is active.
        return;
      } else {
        nsAccessible *containerAccessible = accessible->GetParent();
        if (!containerAccessible)
          return;
        // It is not top level menuitem
        // Only fire focus event if it is not inside collapsed popup
        // and not a listitem of a combo box
        if (nsAccUtils::State(containerAccessible) & nsIAccessibleStates::STATE_COLLAPSED) {
          nsAccessible *containerParent = containerAccessible->GetParent();
          if (!containerParent)
            return;
          if (containerParent->Role() != nsIAccessibleRole::ROLE_COMBOBOX) {
            return;
          }
        }
      }
    }
    if (!fireFocus) {
      nsCOMPtr<nsINode> realFocusedNode = GetCurrentFocus();
      nsIContent* realFocusedContent =
        realFocusedNode->IsElement() ? realFocusedNode->AsElement() : nsnull;
      nsIContent* containerContent = targetContent;
      while (containerContent) {
        nsCOMPtr<nsIDOMXULPopupElement> popup = do_QueryInterface(containerContent);
        if (popup || containerContent == realFocusedContent) { 
          // If we're inside the focus or a popup we can fire focus events
          // for the changed active item
          fireFocus = PR_TRUE;
          break;
        }
        containerContent = containerContent->GetParent();
      }
    }
    if (fireFocus) {
      // Always asynch, always from user input.
      FireAccessibleFocusEvent(accessible, origTargetContent, PR_TRUE,
                               eFromUserInput);
    }
  }
  else if (eventType.EqualsLiteral("DOMMenuBarActive")) {  // Always from user input
    nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_MENU_START,
                            accessible, eFromUserInput);
  }
  else if (eventType.EqualsLiteral("DOMMenuBarInactive")) {  // Always from user input
    nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_MENU_END,
                            accessible, eFromUserInput);
    FireCurrentFocusEvent();
  }
  else if (eventType.EqualsLiteral("ValueChange")) {
    targetDocument->
      FireDelayedAccessibleEvent(nsIAccessibleEvent::EVENT_VALUE_CHANGE,
                                 targetNode, AccEvent::eRemoveDupes);
  }
#ifdef DEBUG_DRAGDROPSTART
  else if (eventType.EqualsLiteral("mouseover")) {
    nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_DRAGDROP_START,
                            accessible);
  }
#endif
}


////////////////////////////////////////////////////////////////////////////////
// nsAccessNode

void
nsRootAccessible::Shutdown()
{
  // Called manually or by nsAccessNode::LastRelease()
  if (!mWeakShell)
    return;  // Already shutdown

  mCurrentARIAMenubar = nsnull;

  nsDocAccessibleWrap::Shutdown();
}

// nsRootAccessible protected member
already_AddRefed<nsIDocShellTreeItem>
nsRootAccessible::GetContentDocShell(nsIDocShellTreeItem *aStart)
{
  if (!aStart) {
    return nsnull;
  }

  PRInt32 itemType;
  aStart->GetItemType(&itemType);
  if (itemType == nsIDocShellTreeItem::typeContent) {
    nsDocAccessible *accDoc = nsAccUtils::GetDocAccessibleFor(aStart);

    // Hidden documents don't have accessibles (like SeaMonkey's sidebar),
    // they are of no interest for a11y.
    if (!accDoc)
      return nsnull;

    // If ancestor chain of accessibles is not completely visible,
    // don't use this one. This happens for example if it's inside
    // a background tab (tabbed browsing)
    nsAccessible *parent = accDoc->GetParent();
    while (parent) {
      if (nsAccUtils::State(parent) & nsIAccessibleStates::STATE_INVISIBLE)
        return nsnull;

      if (parent == this)
        break; // Don't check past original root accessible we started with

      parent = parent->GetParent();
    }

    NS_ADDREF(aStart);
    return aStart;
  }
  nsCOMPtr<nsIDocShellTreeNode> treeNode(do_QueryInterface(aStart));
  if (treeNode) {
    PRInt32 subDocuments;
    treeNode->GetChildCount(&subDocuments);
    for (PRInt32 count = 0; count < subDocuments; count ++) {
      nsCOMPtr<nsIDocShellTreeItem> treeItemChild, contentTreeItem;
      treeNode->GetChildAt(count, getter_AddRefs(treeItemChild));
      NS_ENSURE_TRUE(treeItemChild, nsnull);
      contentTreeItem = GetContentDocShell(treeItemChild);
      if (contentTreeItem) {
        NS_ADDREF(aStart = contentTreeItem);
        return aStart;
      }
    }
  }
  return nsnull;
}

// nsIAccessible method
NS_IMETHODIMP
nsRootAccessible::GetRelationByType(PRUint32 aRelationType,
                                    nsIAccessibleRelation **aRelation)
{
  NS_ENSURE_ARG_POINTER(aRelation);
  *aRelation = nsnull;

  if (!mDocument || aRelationType != nsIAccessibleRelation::RELATION_EMBEDS) {
    return nsDocAccessibleWrap::GetRelationByType(aRelationType, aRelation);
  }

  nsCOMPtr<nsIDocShellTreeItem> treeItem =
    nsCoreUtils::GetDocShellTreeItemFor(mDocument);
  nsCOMPtr<nsIDocShellTreeItem> contentTreeItem = GetContentDocShell(treeItem);
  // there may be no content area, so we need a null check
  if (contentTreeItem) {
    nsDocAccessible *accDoc = nsAccUtils::GetDocAccessibleFor(contentTreeItem);
    return nsRelUtils::AddTarget(aRelationType, aRelation, accDoc);
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// Protected members

void
nsRootAccessible::HandlePopupShownEvent(nsAccessible* aAccessible)
{
  PRUint32 role = aAccessible->Role();

  if (role == nsIAccessibleRole::ROLE_MENUPOPUP) {
    // Don't fire menupopup events for combobox and autocomplete lists.
    nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_MENUPOPUP_START,
                            aAccessible);
    return;
  }

  if (role == nsIAccessibleRole::ROLE_TOOLTIP) {
    // There is a single <xul:tooltip> node which Mozilla moves around.
    // The accessible for it stays the same no matter where it moves. 
    // AT's expect to get an EVENT_SHOW for the tooltip. 
    // In event callback the tooltip's accessible will be ready.
    nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_SHOW, aAccessible);
    return;
  }

  if (role == nsIAccessibleRole::ROLE_COMBOBOX_LIST) {
    // Fire expanded state change event for comboboxes and autocompeletes.
    nsAccessible* combobox = aAccessible->GetParent();
    if (!combobox)
      return;

    PRUint32 comboboxRole = combobox->Role();
    if (comboboxRole == nsIAccessibleRole::ROLE_COMBOBOX ||
        comboboxRole == nsIAccessibleRole::ROLE_AUTOCOMPLETE) {
      nsRefPtr<AccEvent> event =
        new AccStateChangeEvent(combobox,
                                nsIAccessibleStates::STATE_EXPANDED,
                                PR_FALSE, PR_TRUE);
      if (event)
        nsEventShell::FireEvent(event);
    }
  }
}

void
nsRootAccessible::HandlePopupHidingEvent(nsINode* aNode,
                                         nsAccessible* aAccessible)
{
  // If accessible focus was on or inside popup that closes, then restore it
  // to true current focus. This is the case when we've been getting
  // DOMMenuItemActive events inside of a combo box that closes. The real focus
  // is on the combo box. It's also the case when a popup gets focus in ATK --
  // when it closes we need to fire an event to restore focus to where it was.

  if (gLastFocusedNode &&
      nsCoreUtils::IsAncestorOf(aNode, gLastFocusedNode)) {
    // Focus was on or inside of a popup that's being hidden
    FireCurrentFocusEvent();
  }

  // Fire expanded state change event for comboboxes and autocompletes.
  if (!aAccessible ||
      aAccessible->Role() != nsIAccessibleRole::ROLE_COMBOBOX_LIST)
    return;

  nsAccessible* combobox = aAccessible->GetParent();
  if (!combobox)
    return;

  PRUint32 comboboxRole = combobox->Role();
  if (comboboxRole == nsIAccessibleRole::ROLE_COMBOBOX ||
      comboboxRole == nsIAccessibleRole::ROLE_AUTOCOMPLETE) {
    nsRefPtr<AccEvent> event =
      new AccStateChangeEvent(combobox,
                              nsIAccessibleStates::STATE_EXPANDED,
                              PR_FALSE, PR_FALSE);
    if (event)
      nsEventShell::FireEvent(event);
  }
}

#ifdef MOZ_XUL
void
nsRootAccessible::HandleTreeRowCountChangedEvent(nsIDOMEvent* aEvent,
                                                 nsXULTreeAccessible* aAccessible)
{
  nsCOMPtr<nsIDOMDataContainerEvent> dataEvent(do_QueryInterface(aEvent));
  if (!dataEvent)
    return;

  nsCOMPtr<nsIVariant> indexVariant;
  dataEvent->GetData(NS_LITERAL_STRING("index"),
                     getter_AddRefs(indexVariant));
  if (!indexVariant)
    return;

  nsCOMPtr<nsIVariant> countVariant;
  dataEvent->GetData(NS_LITERAL_STRING("count"),
                     getter_AddRefs(countVariant));
  if (!countVariant)
    return;

  PRInt32 index, count;
  indexVariant->GetAsInt32(&index);
  countVariant->GetAsInt32(&count);

  aAccessible->InvalidateCache(index, count);
}

void
nsRootAccessible::HandleTreeInvalidatedEvent(nsIDOMEvent* aEvent,
                                             nsXULTreeAccessible* aAccessible)
{
  nsCOMPtr<nsIDOMDataContainerEvent> dataEvent(do_QueryInterface(aEvent));
  if (!dataEvent)
    return;

  PRInt32 startRow = 0, endRow = -1, startCol = 0, endCol = -1;

  nsCOMPtr<nsIVariant> startRowVariant;
  dataEvent->GetData(NS_LITERAL_STRING("startrow"),
                     getter_AddRefs(startRowVariant));
  if (startRowVariant)
    startRowVariant->GetAsInt32(&startRow);

  nsCOMPtr<nsIVariant> endRowVariant;
  dataEvent->GetData(NS_LITERAL_STRING("endrow"),
                     getter_AddRefs(endRowVariant));
  if (endRowVariant)
    endRowVariant->GetAsInt32(&endRow);

  nsCOMPtr<nsIVariant> startColVariant;
  dataEvent->GetData(NS_LITERAL_STRING("startcolumn"),
                     getter_AddRefs(startColVariant));
  if (startColVariant)
    startColVariant->GetAsInt32(&startCol);

  nsCOMPtr<nsIVariant> endColVariant;
  dataEvent->GetData(NS_LITERAL_STRING("endcolumn"),
                     getter_AddRefs(endColVariant));
  if (endColVariant)
    endColVariant->GetAsInt32(&endCol);

  aAccessible->TreeViewInvalidated(startRow, endRow, startCol, endCol);
}
#endif

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
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Jonas Sicking <jonas@sicking.cc> (Original Author)
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

#include "nsCCUncollectableMarker.h"
#include "nsIObserverService.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsServiceManagerUtils.h"
#include "nsIDOMDocument.h"
#include "nsIContentViewer.h"
#include "nsIDocument.h"
#include "nsIWindowMediator.h"
#include "nsPIDOMWindow.h"
#include "nsIWebNavigation.h"
#include "nsISHistory.h"
#include "nsISHEntry.h"
#include "nsISHContainer.h"
#include "nsIWindowWatcher.h"
#include "mozilla/Services.h"
#include "nsIXULWindow.h"
#include "nsIAppShellService.h"
#include "nsAppShellCID.h"
#include "nsEventListenerManager.h"
#include "nsContentUtils.h"
#include "nsGlobalWindow.h"
#include "nsJSEnvironment.h"
#include "nsInProcessTabChildGlobal.h"
#include "nsFrameLoader.h"
#include "nsGenericElement.h"
#include "xpcpublic.h"

static bool sInited = 0;
PRUint32 nsCCUncollectableMarker::sGeneration = 0;
#ifdef MOZ_XUL
#include "nsXULPrototypeCache.h"
#endif

NS_IMPL_ISUPPORTS1(nsCCUncollectableMarker, nsIObserver)

/* static */
nsresult
nsCCUncollectableMarker::Init()
{
  if (sInited) {
    return NS_OK;
  }
  
  nsCOMPtr<nsIObserver> marker = new nsCCUncollectableMarker;
  NS_ENSURE_TRUE(marker, NS_ERROR_OUT_OF_MEMORY);

  nsCOMPtr<nsIObserverService> obs =
    mozilla::services::GetObserverService();
  if (!obs)
    return NS_ERROR_FAILURE;

  nsresult rv;

  // This makes the observer service hold an owning reference to the marker
  rv = obs->AddObserver(marker, "xpcom-shutdown", false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = obs->AddObserver(marker, "cycle-collector-begin", false);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = obs->AddObserver(marker, "cycle-collector-forget-skippable", false);
  NS_ENSURE_SUCCESS(rv, rv);

  sInited = true;

  return NS_OK;
}

static void
MarkUserData(void* aNode, nsIAtom* aKey, void* aValue, void* aData)
{
  nsIDocument* d = static_cast<nsINode*>(aNode)->GetCurrentDoc();
  if (d && nsCCUncollectableMarker::InGeneration(d->GetMarkedCCGeneration())) {
    nsGenericElement::MarkUserData(aNode, aKey, aValue, aData);
  }
}

static void
MarkUserDataHandler(void* aNode, nsIAtom* aKey, void* aValue, void* aData)
{
  nsIDocument* d = static_cast<nsINode*>(aNode)->GetCurrentDoc();
  if (d && nsCCUncollectableMarker::InGeneration(d->GetMarkedCCGeneration())) {
    nsGenericElement::MarkUserDataHandler(aNode, aKey, aValue, aData);
  }
}

static void
MarkMessageManagers()
{
  nsCOMPtr<nsIChromeFrameMessageManager> globalMM =
    do_GetService("@mozilla.org/globalmessagemanager;1");
  if (!globalMM) {
    return;
  }

  globalMM->MarkForCC();
  PRUint32 childCount = 0;
  globalMM->GetChildCount(&childCount);
  for (PRUint32 i = 0; i < childCount; ++i) {
    nsCOMPtr<nsITreeItemFrameMessageManager> windowMM;
    globalMM->GetChildAt(i, getter_AddRefs(windowMM));
    if (!windowMM) {
      continue;
    }
    windowMM->MarkForCC();
    PRUint32 tabChildCount = 0;
    windowMM->GetChildCount(&tabChildCount);
    for (PRUint32 j = 0; j < tabChildCount; ++j) {
      nsCOMPtr<nsITreeItemFrameMessageManager> tabMM;
      windowMM->GetChildAt(j, getter_AddRefs(tabMM));
      if (!tabMM) {
        continue;
      }
      tabMM->MarkForCC();
      //XXX hack warning, but works, since we know that
      //    callback data is frameloader.
      void* cb = static_cast<nsFrameMessageManager*>(tabMM.get())->
        GetCallbackData();
      nsFrameLoader* fl = static_cast<nsFrameLoader*>(cb);
      if (fl) {
        nsIDOMEventTarget* et = fl->GetTabChildGlobalAsEventTarget();
        if (!et) {
          continue;
        }
        static_cast<nsInProcessTabChildGlobal*>(et)->MarkForCC();
        nsEventListenerManager* elm = et->GetListenerManager(false);
        if (elm) {
          elm->UnmarkGrayJSListeners();
        }
      }
    }
  }
}

void
MarkContentViewer(nsIContentViewer* aViewer, bool aCleanupJS,
                  bool aPrepareForCC)
{
  if (!aViewer) {
    return;
  }

  nsIDocument *doc = aViewer->GetDocument();
  if (doc &&
      doc->GetMarkedCCGeneration() != nsCCUncollectableMarker::sGeneration) {
    doc->MarkUncollectableForCCGeneration(nsCCUncollectableMarker::sGeneration);
    if (aCleanupJS) {
      nsEventListenerManager* elm = doc->GetListenerManager(false);
      if (elm) {
        elm->UnmarkGrayJSListeners();
      }
      nsCOMPtr<nsIDOMEventTarget> win = do_QueryInterface(doc->GetInnerWindow());
      if (win) {
        elm = win->GetListenerManager(false);
        if (elm) {
          elm->UnmarkGrayJSListeners();
        }
        static_cast<nsGlobalWindow*>(win.get())->UnmarkGrayTimers();
      }

      doc->PropertyTable(DOM_USER_DATA_HANDLER)->
        EnumerateAll(MarkUserDataHandler, &nsCCUncollectableMarker::sGeneration);
    } else if (aPrepareForCC) {
      // Unfortunately we need to still mark user data just before running CC so
      // that it has the right generation. 
      doc->PropertyTable(DOM_USER_DATA)->
        EnumerateAll(MarkUserData, &nsCCUncollectableMarker::sGeneration);
    }
  }
}

void MarkDocShell(nsIDocShellTreeNode* aNode, bool aCleanupJS,
                  bool aPrepareForCC);

void
MarkSHEntry(nsISHEntry* aSHEntry, bool aCleanupJS, bool aPrepareForCC)
{
  if (!aSHEntry) {
    return;
  }

  nsCOMPtr<nsIContentViewer> cview;
  aSHEntry->GetContentViewer(getter_AddRefs(cview));
  MarkContentViewer(cview, aCleanupJS, aPrepareForCC);

  nsCOMPtr<nsIDocShellTreeItem> child;
  PRInt32 i = 0;
  while (NS_SUCCEEDED(aSHEntry->ChildShellAt(i++, getter_AddRefs(child))) &&
         child) {
    MarkDocShell(child, aCleanupJS, aPrepareForCC);
  }

  nsCOMPtr<nsISHContainer> shCont = do_QueryInterface(aSHEntry);
  PRInt32 count;
  shCont->GetChildCount(&count);
  for (i = 0; i < count; ++i) {
    nsCOMPtr<nsISHEntry> childEntry;
    shCont->GetChildAt(i, getter_AddRefs(childEntry));
    MarkSHEntry(childEntry, aCleanupJS, aPrepareForCC);
  }
  
}

void
MarkDocShell(nsIDocShellTreeNode* aNode, bool aCleanupJS, bool aPrepareForCC)
{
  nsCOMPtr<nsIDocShell> shell = do_QueryInterface(aNode);
  if (!shell) {
    return;
  }

  nsCOMPtr<nsIContentViewer> cview;
  shell->GetContentViewer(getter_AddRefs(cview));
  MarkContentViewer(cview, aCleanupJS, aPrepareForCC);

  nsCOMPtr<nsIWebNavigation> webNav = do_QueryInterface(shell);
  nsCOMPtr<nsISHistory> history;
  webNav->GetSessionHistory(getter_AddRefs(history));
  if (history) {
    PRInt32 i, historyCount;
    history->GetCount(&historyCount);
    for (i = 0; i < historyCount; ++i) {
      nsCOMPtr<nsIHistoryEntry> historyEntry;
      history->GetEntryAtIndex(i, false, getter_AddRefs(historyEntry));
      nsCOMPtr<nsISHEntry> shEntry = do_QueryInterface(historyEntry);

      MarkSHEntry(shEntry, aCleanupJS, aPrepareForCC);
    }
  }

  PRInt32 i, childCount;
  aNode->GetChildCount(&childCount);
  for (i = 0; i < childCount; ++i) {
    nsCOMPtr<nsIDocShellTreeItem> child;
    aNode->GetChildAt(i, getter_AddRefs(child));
    MarkDocShell(child, aCleanupJS, aPrepareForCC);
  }
}

void
MarkWindowList(nsISimpleEnumerator* aWindowList, bool aCleanupJS,
               bool aPrepareForCC)
{
  nsCOMPtr<nsISupports> iter;
  while (NS_SUCCEEDED(aWindowList->GetNext(getter_AddRefs(iter))) &&
         iter) {
    nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(iter);
    if (window) {
      nsCOMPtr<nsIDocShellTreeNode> rootDocShell =
        do_QueryInterface(window->GetDocShell());

      MarkDocShell(rootDocShell, aCleanupJS, aPrepareForCC);
    }
  }
}

nsresult
nsCCUncollectableMarker::Observe(nsISupports* aSubject, const char* aTopic,
                                 const PRUnichar* aData)
{
  if (!strcmp(aTopic, "xpcom-shutdown")) {
    nsCOMPtr<nsIObserverService> obs =
      mozilla::services::GetObserverService();
    if (!obs)
      return NS_ERROR_FAILURE;

    // No need for kungFuDeathGrip here, yay observerservice!
    obs->RemoveObserver(this, "xpcom-shutdown");
    obs->RemoveObserver(this, "cycle-collector-begin");
    obs->RemoveObserver(this, "cycle-collector-forget-skippable");
    
    sGeneration = 0;
    
    return NS_OK;
  }

  NS_ASSERTION(!strcmp(aTopic, "cycle-collector-begin") ||
               !strcmp(aTopic, "cycle-collector-forget-skippable"), "wrong topic");

  // JS cleanup can be slow. Do it only if there has been a GC.
  bool cleanupJS =
    !nsJSContext::CleanupSinceLastGC() &&
    !strcmp(aTopic, "cycle-collector-forget-skippable");

  bool prepareForCC = !strcmp(aTopic, "cycle-collector-begin");
    

  // Increase generation to effectivly unmark all current objects
  if (!++sGeneration) {
    ++sGeneration;
  }

  nsresult rv;

  // Iterate all toplevel windows
  nsCOMPtr<nsISimpleEnumerator> windowList;
  nsCOMPtr<nsIWindowMediator> med =
    do_GetService(NS_WINDOWMEDIATOR_CONTRACTID);
  if (med) {
    rv = med->GetEnumerator(nsnull, getter_AddRefs(windowList));
    NS_ENSURE_SUCCESS(rv, rv);

    MarkWindowList(windowList, cleanupJS, prepareForCC);
  }

  nsCOMPtr<nsIWindowWatcher> ww =
    do_GetService(NS_WINDOWWATCHER_CONTRACTID);
  if (ww) {
    rv = ww->GetWindowEnumerator(getter_AddRefs(windowList));
    NS_ENSURE_SUCCESS(rv, rv);

    MarkWindowList(windowList, cleanupJS, prepareForCC);
  }

  nsCOMPtr<nsIAppShellService> appShell = 
    do_GetService(NS_APPSHELLSERVICE_CONTRACTID);
  if (appShell) {
    nsCOMPtr<nsIXULWindow> hw;
    appShell->GetHiddenWindow(getter_AddRefs(hw));
    if (hw) {
      nsCOMPtr<nsIDocShell> shell;
      hw->GetDocShell(getter_AddRefs(shell));
      nsCOMPtr<nsIDocShellTreeNode> shellTreeNode = do_QueryInterface(shell);
      MarkDocShell(shellTreeNode, cleanupJS, prepareForCC);
    }
  }

  if (cleanupJS) {
    nsContentUtils::UnmarkGrayJSListenersInCCGenerationDocuments(sGeneration);
    MarkMessageManagers();
    xpc_UnmarkSkippableJSHolders();
  }

#ifdef MOZ_XUL
  nsXULPrototypeCache* xulCache = nsXULPrototypeCache::GetInstance();
  if (xulCache) {
    xulCache->MarkInCCGeneration(sGeneration);
  }
#endif

  return NS_OK;
}


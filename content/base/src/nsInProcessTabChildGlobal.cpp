/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8; -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
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
 * The Original Code is Mozilla Content App.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
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

#include "nsInProcessTabChildGlobal.h"
#include "nsContentUtils.h"
#include "nsIScriptSecurityManager.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsEventDispatcher.h"
#include "nsIComponentManager.h"
#include "nsIServiceManager.h"
#include "nsIJSRuntimeService.h"
#include "nsComponentManagerUtils.h"
#include "nsNetUtil.h"
#include "nsScriptLoader.h"
#include "nsIJSContextStack.h"
#include "nsFrameLoader.h"
#include "nsIPrivateDOMEvent.h"
#include "xpcpublic.h"

bool SendSyncMessageToParent(void* aCallbackData,
                             const nsAString& aMessage,
                             const nsAString& aJSON,
                             InfallibleTArray<nsString>* aJSONRetVal)
{
  nsInProcessTabChildGlobal* tabChild =
    static_cast<nsInProcessTabChildGlobal*>(aCallbackData);
  nsCOMPtr<nsIContent> owner = tabChild->mOwner;
  nsTArray<nsCOMPtr<nsIRunnable> > asyncMessages;
  asyncMessages.SwapElements(tabChild->mASyncMessages);
  PRUint32 len = asyncMessages.Length();
  for (PRUint32 i = 0; i < len; ++i) {
    nsCOMPtr<nsIRunnable> async = asyncMessages[i];
    async->Run();
  }
  if (tabChild->mChromeMessageManager) {
    nsRefPtr<nsFrameMessageManager> mm = tabChild->mChromeMessageManager;
    mm->ReceiveMessage(owner, aMessage, true, aJSON, nsnull, aJSONRetVal);
  }
  return true;
}

class nsAsyncMessageToParent : public nsRunnable
{
public:
  nsAsyncMessageToParent(nsInProcessTabChildGlobal* aTabChild,
                         const nsAString& aMessage, const nsAString& aJSON)
    : mTabChild(aTabChild), mMessage(aMessage), mJSON(aJSON) {}

  NS_IMETHOD Run()
  {
    mTabChild->mASyncMessages.RemoveElement(this);
    if (mTabChild->mChromeMessageManager) {
      nsRefPtr<nsFrameMessageManager> mm = mTabChild->mChromeMessageManager;
      mm->ReceiveMessage(mTabChild->mOwner, mMessage, false,
                         mJSON, nsnull, nsnull);
    }
    return NS_OK;
  }
  nsRefPtr<nsInProcessTabChildGlobal> mTabChild;
  nsString mMessage;
  nsString mJSON;
};

bool SendAsyncMessageToParent(void* aCallbackData,
                              const nsAString& aMessage,
                              const nsAString& aJSON)
{
  nsInProcessTabChildGlobal* tabChild =
    static_cast<nsInProcessTabChildGlobal*>(aCallbackData);
  nsCOMPtr<nsIRunnable> ev =
    new nsAsyncMessageToParent(tabChild, aMessage, aJSON);
  tabChild->mASyncMessages.AppendElement(ev);
  NS_DispatchToCurrentThread(ev);
  return true;
}

nsInProcessTabChildGlobal::nsInProcessTabChildGlobal(nsIDocShell* aShell,
                                                     nsIContent* aOwner,
                                                     nsFrameMessageManager* aChrome)
: mDocShell(aShell), mInitialized(false), mLoadingScript(false),
  mDelayedDisconnect(false), mOwner(aOwner), mChromeMessageManager(aChrome)
{
}

nsInProcessTabChildGlobal::~nsInProcessTabChildGlobal()
{
  NS_ASSERTION(!mCx, "Couldn't release JSContext?!?");
}

nsresult
nsInProcessTabChildGlobal::Init()
{
#ifdef DEBUG
  nsresult rv =
#endif
  InitTabChildGlobal();
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
                   "Couldn't initialize nsInProcessTabChildGlobal");
  mMessageManager = new nsFrameMessageManager(false,
                                              SendSyncMessageToParent,
                                              SendAsyncMessageToParent,
                                              nsnull,
                                              this,
                                              nsnull,
                                              mCx);
  return NS_OK;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsInProcessTabChildGlobal)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsInProcessTabChildGlobal,
                                                nsDOMEventTargetWrapperCache)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mMessageManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mGlobal)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsInProcessTabChildGlobal,
                                                  nsDOMEventTargetWrapperCache)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mMessageManager)
  nsFrameScriptExecutor::Traverse(tmp, cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(nsInProcessTabChildGlobal)
  NS_INTERFACE_MAP_ENTRY(nsIFrameMessageManager)
  NS_INTERFACE_MAP_ENTRY(nsISyncMessageSender)
  NS_INTERFACE_MAP_ENTRY(nsIContentFrameMessageManager)
  NS_INTERFACE_MAP_ENTRY(nsIInProcessContentFrameMessageManager)
  NS_INTERFACE_MAP_ENTRY(nsIScriptContextPrincipal)
  NS_INTERFACE_MAP_ENTRY(nsIScriptObjectPrincipal)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(ContentFrameMessageManager)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetWrapperCache)

NS_IMPL_ADDREF_INHERITED(nsInProcessTabChildGlobal, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(nsInProcessTabChildGlobal, nsDOMEventTargetHelper)

NS_IMETHODIMP
nsInProcessTabChildGlobal::GetContent(nsIDOMWindow** aContent)
{
  *aContent = nsnull;
  nsCOMPtr<nsIDOMWindow> window = do_GetInterface(mDocShell);
  window.swap(*aContent);
  return NS_OK;
}

NS_IMETHODIMP
nsInProcessTabChildGlobal::GetDocShell(nsIDocShell** aDocShell)
{
  NS_IF_ADDREF(*aDocShell = mDocShell);
  return NS_OK;
}

NS_IMETHODIMP
nsInProcessTabChildGlobal::Btoa(const nsAString& aBinaryData,
                            nsAString& aAsciiBase64String)
{
  return nsContentUtils::Btoa(aBinaryData, aAsciiBase64String);
}

NS_IMETHODIMP
nsInProcessTabChildGlobal::Atob(const nsAString& aAsciiString,
                            nsAString& aBinaryData)
{
  return nsContentUtils::Atob(aAsciiString, aBinaryData);
}


NS_IMETHODIMP
nsInProcessTabChildGlobal::PrivateNoteIntentionalCrash()
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

void
nsInProcessTabChildGlobal::Disconnect()
{
  // Let the frame scripts know the child is being closed. We do any other
  // cleanup after the event has been fired. See DelayedDisconnect
  nsContentUtils::AddScriptRunner(
     NS_NewRunnableMethod(this, &nsInProcessTabChildGlobal::DelayedDisconnect)
  );
}

void
nsInProcessTabChildGlobal::DelayedDisconnect()
{
  // Don't let the event escape
  mOwner = nsnull;

  // Fire the "unload" event
  nsCOMPtr<nsIDOMEvent> event;
  NS_NewDOMEvent(getter_AddRefs(event), nsnull, nsnull);
  if (event) {
    event->InitEvent(NS_LITERAL_STRING("unload"), false, false);
    nsCOMPtr<nsIPrivateDOMEvent> privateEvent(do_QueryInterface(event));
    privateEvent->SetTrusted(true);

    bool dummy;
    nsDOMEventTargetHelper::DispatchEvent(event, &dummy);
  }

  // Continue with the Disconnect cleanup
  nsCOMPtr<nsIDOMWindow> win = do_GetInterface(mDocShell);
  nsCOMPtr<nsPIDOMWindow> pwin = do_QueryInterface(win);
  if (pwin) {
    pwin->SetChromeEventHandler(pwin->GetChromeEventHandler());
  }
  mDocShell = nsnull;
  mChromeMessageManager = nsnull;
  if (mMessageManager) {
    static_cast<nsFrameMessageManager*>(mMessageManager.get())->Disconnect();
    mMessageManager = nsnull;
  }
  if (mListenerManager) {
    mListenerManager->Disconnect();
  }
  
  if (!mLoadingScript) {
    if (mCx) {
      DestroyCx();
    }
  } else {
    mDelayedDisconnect = true;
  }
}

NS_IMETHODIMP_(nsIContent *)
nsInProcessTabChildGlobal::GetOwnerContent()
{
  return mOwner;
}

nsresult
nsInProcessTabChildGlobal::PreHandleEvent(nsEventChainPreVisitor& aVisitor)
{
  aVisitor.mCanHandle = true;
  aVisitor.mParentTarget = mOwner;

#ifdef DEBUG
  if (mOwner) {
    nsCOMPtr<nsIFrameLoaderOwner> owner = do_QueryInterface(mOwner);
    nsRefPtr<nsFrameLoader> fl = owner->GetFrameLoader();
    if (fl) {
      NS_ASSERTION(this == fl->GetTabChildGlobalAsEventTarget(),
                   "Wrong event target!");
      NS_ASSERTION(fl->mMessageManager == mChromeMessageManager,
                   "Wrong message manager!");
    }
  }
#endif

  return NS_OK;
}

nsresult
nsInProcessTabChildGlobal::InitTabChildGlobal()
{

  nsISupports* scopeSupports =
    NS_ISUPPORTS_CAST(nsIDOMEventTarget*, this);
  NS_ENSURE_STATE(InitTabChildGlobalInternal(scopeSupports));
  return NS_OK;
}

class nsAsyncScriptLoad : public nsRunnable
{
public:
  nsAsyncScriptLoad(nsInProcessTabChildGlobal* aTabChild, const nsAString& aURL)
  : mTabChild(aTabChild), mURL(aURL) {}

  NS_IMETHOD Run()
  {
    mTabChild->LoadFrameScript(mURL);
    return NS_OK;
  }
  nsRefPtr<nsInProcessTabChildGlobal> mTabChild;
  nsString mURL;
};

void
nsInProcessTabChildGlobal::LoadFrameScript(const nsAString& aURL)
{
  if (!nsContentUtils::IsSafeToRunScript()) {
    nsContentUtils::AddScriptRunner(new nsAsyncScriptLoad(this, aURL));
    return;
  }
  if (!mInitialized) {
    mInitialized = true;
    Init();
  }
  bool tmp = mLoadingScript;
  mLoadingScript = true;
  LoadFrameScriptInternal(aURL);
  mLoadingScript = tmp;
  if (!mLoadingScript && mDelayedDisconnect) {
    mDelayedDisconnect = false;
    Disconnect();
  }
}

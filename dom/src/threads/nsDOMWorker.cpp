/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
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
 * The Original Code is Web Workers.
 *
 * The Initial Developer of the Original Code is
 *   Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ben Turner <bent.mozilla@gmail.com> (Original Author)
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

#include "nsDOMWorker.h"

#include "nsIDOMEvent.h"
#include "nsIEventTarget.h"
#include "nsIJSRuntimeService.h"
#include "nsIXPConnect.h"

#include "jscntxt.h"
#ifdef MOZ_SHARK
#include "jsdbgapi.h"
#endif
#include "nsAtomicRefcnt.h"
#include "nsAXPCNativeCallContext.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsDOMClassInfoID.h"
#include "nsGlobalWindow.h"
#include "nsJSON.h"
#include "nsJSUtils.h"
#include "nsProxyRelease.h"
#include "nsThreadUtils.h"
#include "nsNativeCharsetUtils.h"
#include "xpcprivate.h"

#include "nsDOMThreadService.h"
#include "nsDOMWorkerEvents.h"
#include "nsDOMWorkerLocation.h"
#include "nsDOMWorkerNavigator.h"
#include "nsDOMWorkerPool.h"
#include "nsDOMWorkerScriptLoader.h"
#include "nsDOMWorkerTimeout.h"
#include "nsDOMWorkerXHR.h"

using namespace mozilla;

class TestComponentThreadsafetyRunnable : public nsIRunnable
{
public:
  NS_DECL_ISUPPORTS

  TestComponentThreadsafetyRunnable(const nsACString& aContractId,
                                    PRBool aService)
  : mContractId(aContractId),
    mService(aService),
    mIsThreadsafe(PR_FALSE)
  { }

  NS_IMETHOD Run()
  {
    NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

    nsresult rv;
    nsCOMPtr<nsISupports> instance;
    if (mService) {
      instance = do_GetService(mContractId.get(), &rv);
    }
    else {
      instance = do_CreateInstance(mContractId.get(), &rv);
    }
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIClassInfo> classInfo = do_QueryInterface(instance, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    PRUint32 flags;
    rv = classInfo->GetFlags(&flags);
    NS_ENSURE_SUCCESS(rv, rv);

    mIsThreadsafe = !!(flags & nsIClassInfo::THREADSAFE);
    return NS_OK;
  }

  PRBool IsThreadsafe()
  {
    NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");
    return mIsThreadsafe;
  }

private:
  nsCString mContractId;
  PRBool mService;
  PRBool mIsThreadsafe;
};

NS_IMPL_THREADSAFE_ISUPPORTS1(TestComponentThreadsafetyRunnable, nsIRunnable)

class nsDOMWorkerFunctions
{
public:
  typedef nsDOMWorker::WorkerPrivilegeModel WorkerPrivilegeModel;

  // Same as window.dump().
  static JSBool
  Dump(JSContext* aCx, uintN aArgc, jsval* aVp);

  // Same as window.setTimeout().
  static JSBool
  SetTimeout(JSContext* aCx, uintN aArgc, jsval* aVp) {
    return MakeTimeout(aCx, aArgc, aVp, PR_FALSE);
  }

  // Same as window.setInterval().
  static JSBool
  SetInterval(JSContext* aCx, uintN aArgc, jsval* aVp) {
    return MakeTimeout(aCx, aArgc, aVp, PR_TRUE);
  }

  // Used for both clearTimeout() and clearInterval().
  static JSBool
  KillTimeout(JSContext* aCx, uintN aArgc, jsval* aVp);

  static JSBool
  LoadScripts(JSContext* aCx, uintN aArgc, jsval* aVp);

  static JSBool
  NewXMLHttpRequest(JSContext* aCx, uintN aArgc, jsval* aVp);

  static JSBool
  NewWorker(JSContext* aCx, uintN aArgc, jsval* aVp) {
    return MakeNewWorker(aCx, aArgc, aVp, nsDOMWorker::CONTENT);
  }

  static JSBool
  AtoB(JSContext* aCx, uintN aArgc, jsval* aVp);

  static JSBool
  BtoA(JSContext* aCx, uintN aArgc, jsval* aVp);

  // Chrome-only functions
  static JSBool
  NewChromeWorker(JSContext* aCx, uintN aArgc, jsval* aVp);

  static JSBool
  XPCOMLazyGetter(JSContext* aCx, JSObject* aObj, jsid aId, jsval* aVp);

  static JSBool
  CreateInstance(JSContext* aCx, uintN aArgc, jsval* aVp) {
    return GetInstanceCommon(aCx, aArgc, aVp, PR_FALSE);
  }

  static JSBool
  GetService(JSContext* aCx, uintN aArgc, jsval* aVp) {
    return GetInstanceCommon(aCx, aArgc, aVp, PR_TRUE);
  }

#ifdef BUILD_CTYPES
  static JSBool
  CTypesLazyGetter(JSContext* aCx, JSObject* aObj, jsid aId, jsval* aVp);
#endif

private:
  // Internal helper for SetTimeout and SetInterval.
  static JSBool
  MakeTimeout(JSContext* aCx, uintN aArgc, jsval* aVp, PRBool aIsInterval);

  static JSBool
  MakeNewWorker(JSContext* aCx, uintN aArgc, jsval* aVp,
                WorkerPrivilegeModel aPrivilegeModel);

  static JSBool
  GetInstanceCommon(JSContext* aCx, uintN aArgc, jsval* aVp, PRBool aService);
};

JSFunctionSpec gDOMWorkerXPCOMFunctions[] = {
  {"createInstance", nsDOMWorkerFunctions::CreateInstance, 1, JSPROP_ENUMERATE},
  {"getService",     nsDOMWorkerFunctions::GetService,     1, JSPROP_ENUMERATE},
  { nsnull,          nsnull,                               0, 0 }
};

JSBool
nsDOMWorkerFunctions::Dump(JSContext* aCx,
                           uintN aArgc,
                           jsval* aVp)
{
  JS_SET_RVAL(cx, aVp, JSVAL_VOID);
  if (!nsGlobalWindow::DOMWindowDumpEnabled()) {
    return JS_TRUE;
  }

  JSString* str;
  if (aArgc && (str = JS_ValueToString(aCx, JS_ARGV(aCx, aVp)[0])) && str) {
    nsDependentJSString depStr;
    if (depStr.init(aCx, str)) {
      fputs(NS_ConvertUTF16toUTF8(depStr).get(), stderr);
      fflush(stderr);
    }
  }
  return JS_TRUE;
}

JSBool
nsDOMWorkerFunctions::MakeTimeout(JSContext* aCx,
                                  uintN aArgc,
                                  jsval* aVp,
                                  PRBool aIsInterval)
{
  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");

  if (worker->IsCanceled()) {
    return JS_FALSE;
  }

  PRUint32 id = worker->NextTimeoutId();

  if (worker->IsClosing()) {
    // Timeouts won't run in the close handler, fake success and bail.
    JS_SET_RVAL(aCx, aVp, INT_TO_JSVAL(id));
    return JS_TRUE;
  }

  nsRefPtr<nsDOMWorkerTimeout> timeout = new nsDOMWorkerTimeout(worker, id);
  if (!timeout) {
    JS_ReportOutOfMemory(aCx);
    return JS_FALSE;
  }

  nsresult rv = timeout->Init(aCx, aArgc, JS_ARGV(aCx, aVp), aIsInterval);
  if (NS_FAILED(rv)) {
    JS_ReportError(aCx, "Failed to initialize timeout!");
    return JS_FALSE;
  }

  rv = worker->AddFeature(timeout, aCx);
  if (NS_FAILED(rv)) {
    JS_ReportOutOfMemory(aCx);
    return JS_FALSE;
  }

  rv = timeout->Start();
  if (NS_FAILED(rv)) {
    JS_ReportError(aCx, "Failed to start timeout!");
    return JS_FALSE;
  }

  JS_SET_RVAL(aCx, aVp, INT_TO_JSVAL(id));
  return JS_TRUE;
}

JSBool
nsDOMWorkerFunctions::KillTimeout(JSContext* aCx,
                                  uintN aArgc,
                                  jsval* aVp)
{
  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");

  if (worker->IsCanceled()) {
    return JS_FALSE;
  }

  if (!aArgc) {
    JS_ReportError(aCx, "Function requires at least 1 parameter");
    return JS_FALSE;
  }

  uint32 id;
  if (!JS_ValueToECMAUint32(aCx, JS_ARGV(aCx, aVp)[0], &id)) {
    JS_ReportError(aCx, "First argument must be a timeout id");
    return JS_FALSE;
  }

  worker->CancelTimeoutWithId(PRUint32(id));
  JS_SET_RVAL(aCx, aVp, JSVAL_VOID);
  return JS_TRUE;
}

JSBool
nsDOMWorkerFunctions::LoadScripts(JSContext* aCx,
                                  uintN aArgc,
                                  jsval* aVp)
{
  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");

  if (worker->IsCanceled()) {
    return JS_FALSE;
  }

  if (!aArgc) {
    // No argument is ok according to spec.
    return JS_TRUE;
  }

  nsAutoTArray<nsString, 10> urls;

  if (!urls.SetCapacity((PRUint32)aArgc)) {
    JS_ReportOutOfMemory(aCx);
    return JS_FALSE;
  }

  jsval* argv = JS_ARGV(aCx, aVp);
  for (uintN index = 0; index < aArgc; index++) {
    jsval val = argv[index];

    if (!JSVAL_IS_STRING(val)) {
      JS_ReportError(aCx, "Argument %d must be a string", index);
      return JS_FALSE;
    }

    JSString* str = JS_ValueToString(aCx, val);
    if (!str) {
      JS_ReportError(aCx, "Couldn't convert argument %d to a string", index);
      return JS_FALSE;
    }

    nsString* newURL = urls.AppendElement();
    NS_ASSERTION(newURL, "Shouldn't fail if SetCapacity succeeded above!");

    nsDependentJSString depStr;
    if (!depStr.init(aCx, str)) {
      return JS_FALSE;
    }

    newURL->Assign(depStr);
  }

  nsRefPtr<nsDOMWorkerScriptLoader> loader =
    new nsDOMWorkerScriptLoader(worker);
  if (!loader) {
    JS_ReportOutOfMemory(aCx);
    return JS_FALSE;
  }

  nsresult rv = worker->AddFeature(loader, aCx);
  if (NS_FAILED(rv)) {
    JS_ReportOutOfMemory(aCx);
    return JS_FALSE;
  }

  rv = loader->LoadScripts(aCx, urls, PR_TRUE);
  if (NS_FAILED(rv)) {
    if (!JS_IsExceptionPending(aCx)) {
      JS_ReportError(aCx, "Failed to load scripts");
    }
    return JS_FALSE;
  }

  JS_SET_RVAL(aCx, aVp, JSVAL_VOID);
  return JS_TRUE;
}

JSBool
nsDOMWorkerFunctions::NewXMLHttpRequest(JSContext* aCx,
                                        uintN aArgc,
                                        jsval* aVp)
{
  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");

  if (worker->IsCanceled()) {
    return JS_FALSE;
  }

  if (aArgc) {
    JS_ReportError(aCx, "XMLHttpRequest constructor takes no arguments!");
    return JS_FALSE;
  }

  nsRefPtr<nsDOMWorkerXHR> xhr = new nsDOMWorkerXHR(worker);
  if (!xhr) {
    JS_ReportOutOfMemory(aCx);
    return JS_FALSE;
  }

  nsresult rv = xhr->Init();
  if (NS_FAILED(rv)) {
    JS_ReportError(aCx, "Failed to construct XMLHttpRequest!");
    return JS_FALSE;
  }

  rv = worker->AddFeature(xhr, aCx);
  if (NS_FAILED(rv)) {
    JS_ReportOutOfMemory(aCx);
    return JS_FALSE;
  }

  nsCOMPtr<nsIXPConnectJSObjectHolder> xhrWrapped;
  jsval v;
  rv = nsContentUtils::WrapNative(aCx, JSVAL_TO_OBJECT(JS_CALLEE(aCx, aVp)),
                                  static_cast<nsIXMLHttpRequest*>(xhr), &v,
                                  getter_AddRefs(xhrWrapped));
  if (NS_FAILED(rv)) {
    JS_ReportError(aCx, "Failed to wrap XMLHttpRequest!");
    return JS_FALSE;
  }

  JS_SET_RVAL(aCs, aVp, v);
  return JS_TRUE;
}

JSBool
nsDOMWorkerFunctions::AtoB(JSContext* aCx,
                           uintN aArgc,
                           jsval* aVp)
{
  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");

  if (worker->IsCanceled()) {
    return JS_FALSE;
  }

  if (!aArgc) {
    JS_ReportError(aCx, "Function requires at least 1 parameter");
    return JS_FALSE;
  }

  return nsXPConnect::Base64Decode(aCx, JS_ARGV(aCx, aVp)[0],
                                   &JS_RVAL(aCx, aVp));
}

JSBool
nsDOMWorkerFunctions::BtoA(JSContext* aCx,
                           uintN aArgc,
                           jsval* aVp)
{
  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");

  if (worker->IsCanceled()) {
    return JS_FALSE;
  }

  if (!aArgc) {
    JS_ReportError(aCx, "Function requires at least 1 parameter");
    return JS_FALSE;
  }

  return nsXPConnect::Base64Encode(aCx, JS_ARGV(aCx, aVp)[0],
                                   &JS_RVAL(aCx, aVp));
}

JSBool
nsDOMWorkerFunctions::NewChromeWorker(JSContext* aCx,
                                      uintN aArgc,
                                      jsval* aVp)
{
  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");

  if (!worker->IsPrivileged()) {
    JS_ReportError(aCx, "Cannot create a priviliged worker!");
    return JS_FALSE;
  }

  return MakeNewWorker(aCx, aArgc, aVp, nsDOMWorker::CHROME);
}

JSBool
nsDOMWorkerFunctions::XPCOMLazyGetter(JSContext* aCx,
                                      JSObject* aObj,
                                      jsid aId,
                                      jsval* aVp)
{
#ifdef DEBUG
  {
    NS_ASSERTION(JS_GetGlobalForObject(aCx, aObj) == aObj, "Bad object!");
    NS_ASSERTION(JSID_IS_STRING(aId), "Not a string!");
    NS_ASSERTION(nsDependentJSString(aId).EqualsLiteral("XPCOM"), "Bad id!");
  }
#endif
  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");

  if (worker->IsCanceled()) {
    return JS_FALSE;
  }

  PRUint16 dummy;
  nsCOMPtr<nsIXPCSecurityManager> secMan;
  nsContentUtils::XPConnect()->
    GetSecurityManagerForJSContext(aCx, getter_AddRefs(secMan), &dummy);
  if (!secMan) {
    JS_ReportError(aCx, "Could not get security manager!");
    return JS_FALSE;
  }

  nsCID dummyCID;
  if (NS_FAILED(secMan->CanGetService(aCx, dummyCID))) {
    JS_ReportError(aCx, "Access to the XPCOM object is denied!");
    return JS_FALSE;
  }

  JSObject* xpcom = JS_NewObject(aCx, nsnull, nsnull, nsnull);
  NS_ENSURE_TRUE(xpcom, JS_FALSE);

  JSBool ok = JS_DefineFunctions(aCx, xpcom, gDOMWorkerXPCOMFunctions);
  NS_ENSURE_TRUE(ok, JS_FALSE);

  ok = JS_DeletePropertyById(aCx, aObj, aId);
  NS_ENSURE_TRUE(ok, JS_FALSE);

  jsval xpcomVal = OBJECT_TO_JSVAL(xpcom);
  ok = JS_SetPropertyById(aCx, aObj, aId, &xpcomVal);
  NS_ENSURE_TRUE(ok, JS_FALSE);

  JS_SET_RVAL(aCx, aVp, xpcomVal);
  return JS_TRUE;
}

JSBool
nsDOMWorkerFunctions::GetInstanceCommon(JSContext* aCx,
                                        uintN aArgc,
                                        jsval* aVp,
                                        PRBool aService)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");

  if (worker->IsCanceled()) {
    return JS_FALSE;
  }

  if (!aArgc) {
    JS_ReportError(aCx, "Function requires at least 1 parameter");
    return JS_FALSE;
  }

  JSString* str = JS_ValueToString(aCx, JS_ARGV(aCx, aVp)[0]);
  if (!str) {
    NS_ASSERTION(JS_IsExceptionPending(aCx), "Need to set an exception!");
    return JS_FALSE;
  }

  JSAutoByteString strBytes(aCx, str);
  if (!strBytes) {
    NS_ASSERTION(JS_IsExceptionPending(aCx), "Need to set an exception!");
    return JS_FALSE;
  }

  nsDependentCString contractId(strBytes.ptr(), JS_GetStringLength(str));

  nsDOMThreadService* threadService = nsDOMThreadService::get();

  ThreadsafeStatus status =
    threadService->GetContractIdThreadsafeStatus(contractId);

  if (status == Unknown) {
    nsCOMPtr<nsIThread> mainThread;
    nsresult rv = NS_GetMainThread(getter_AddRefs(mainThread));
    if (NS_FAILED(rv)) {
      JS_ReportError(aCx, "Failed to get main thread!");
      return JS_FALSE;
    }

    nsRefPtr<TestComponentThreadsafetyRunnable> runnable =
      new TestComponentThreadsafetyRunnable(contractId, aService);

    rv = mainThread->Dispatch(runnable, NS_DISPATCH_SYNC);
    if (NS_FAILED(rv)) {
      JS_ReportError(aCx, "Failed to check threadsafety!");
      return JS_FALSE;
    }

    // The worker may have been canceled while waiting above. Check again.
    if (worker->IsCanceled()) {
      return JS_FALSE;
    }

    if (runnable->IsThreadsafe()) {
      threadService->NoteThreadsafeContractId(contractId, PR_TRUE);
      status = Threadsafe;
    }
    else {
      threadService->NoteThreadsafeContractId(contractId, PR_FALSE);
      status = NotThreadsafe;
    }
  }

  if (status == NotThreadsafe) {
    JS_ReportError(aCx, "ChromeWorker may not create an XPCOM object that is "
                   "not threadsafe!");
    return JS_FALSE;
  }

  nsCOMPtr<nsISupports> instance;
  if (aService) {
    instance = do_GetService(contractId.get());
    if (!instance) {
      JS_ReportError(aCx, "Could not get the service!");
      return JS_FALSE;
    }
  }
  else {
    instance = do_CreateInstance(contractId.get());
    if (!instance) {
      JS_ReportError(aCx, "Could not create the instance!");
      return JS_FALSE;
    }
  }

  JSObject* global = JS_GetGlobalForObject(aCx, JS_GetScopeChain(aCx));
  if (!global) {
    NS_ASSERTION(JS_IsExceptionPending(aCx), "Need to set an exception!");
    return JS_FALSE;
  }

  jsval val;
  nsCOMPtr<nsIXPConnectJSObjectHolder> wrapper;
  if (NS_FAILED(nsContentUtils::WrapNative(aCx, global, instance, &val,
                                           getter_AddRefs(wrapper)))) {
    JS_ReportError(aCx, "Failed to wrap object!");
    return JS_FALSE;
  }

  JS_SET_RVAL(aCx, aVp, val);
  return JS_TRUE;
}

JSBool
nsDOMWorkerFunctions::MakeNewWorker(JSContext* aCx,
                                    uintN aArgc,
                                    jsval* aVp,
                                    WorkerPrivilegeModel aPrivilegeModel)
{
  JSObject *obj = JSVAL_TO_OBJECT(JS_CALLEE(aCx, aVp));
    
  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");

  if (worker->IsCanceled()) {
    return JS_FALSE;
  }

  if (!aArgc) {
    JS_ReportError(aCx, "Worker constructor must have an argument!");
    return JS_FALSE;
  }

  // This pointer is protected by our pool, but it is *not* threadsafe and must
  // not be used in any way other than to pass it along to the Initialize call.
  nsIScriptGlobalObject* owner = worker->Pool()->ScriptGlobalObject();

  nsCOMPtr<nsIXPConnectWrappedNative> wrappedWorker =
    worker->GetWrappedNative();
  if (!wrappedWorker) {
    JS_ReportError(aCx, "Couldn't get wrapped native of worker!");
    return JS_FALSE;
  }

  nsRefPtr<nsDOMWorker> newWorker =
    new nsDOMWorker(worker, wrappedWorker, aPrivilegeModel);
  if (!newWorker) {
    JS_ReportOutOfMemory(aCx);
    return JS_FALSE;
  }

  nsresult rv = newWorker->InitializeInternal(owner, aCx, obj, aArgc,
                                              JS_ARGV(aCx, aVp));
  if (NS_FAILED(rv)) {
    JS_ReportError(aCx, "Couldn't initialize new worker!");
    return JS_FALSE;
  }

  nsCOMPtr<nsIXPConnectJSObjectHolder> workerWrapped;
  jsval v;
  rv = nsContentUtils::WrapNative(aCx, obj, static_cast<nsIWorker*>(newWorker), &v, 
                                  getter_AddRefs(workerWrapped));
  if (NS_FAILED(rv)) {
    JS_ReportError(aCx, "Failed to wrap new worker!");
    return JS_FALSE;
  }

  JS_SET_RVAL(aCx, aVp, v);
  return JS_TRUE;
}

#ifdef BUILD_CTYPES
static char*
UnicodeToNative(JSContext *cx, const jschar *source, size_t slen)
{
  nsCAutoString native;
  nsDependentString unicode(reinterpret_cast<const PRUnichar*>(source), slen);
  nsresult rv = NS_CopyUnicodeToNative(unicode, native);
  if (NS_FAILED(rv)) {
    JS_ReportError(cx, "could not convert string to native charset");
    return NULL;
  }

  char* result = static_cast<char*>(JS_malloc(cx, native.Length() + 1));
  if (!result)
    return NULL;

  memcpy(result, native.get(), native.Length() + 1);
  return result;
}

static JSCTypesCallbacks sCallbacks = {
  UnicodeToNative
};

JSBool
nsDOMWorkerFunctions::CTypesLazyGetter(JSContext* aCx,
                                       JSObject* aObj,
                                       jsid aId,
                                       jsval* aVp)
{
#ifdef DEBUG
  {
    NS_ASSERTION(JS_GetGlobalForObject(aCx, aObj) == aObj, "Bad object!");
    NS_ASSERTION(JSID_IS_STRING(aId), "Not a string!");
    NS_ASSERTION(nsDependentJSString(aId).EqualsLiteral("ctypes"), "Bad id!");
  }
#endif
  nsDOMWorker* worker = static_cast<nsDOMWorker*>(JS_GetContextPrivate(aCx));
  NS_ASSERTION(worker, "This should be set by the DOM thread service!");
  NS_ASSERTION(worker->IsPrivileged(), "This shouldn't be possible!");

  if (worker->IsCanceled()) {
    return JS_FALSE;
  }

  jsval ctypes;
  return JS_DeletePropertyById(aCx, aObj, aId) &&
         JS_InitCTypesClass(aCx, aObj) &&
         JS_GetProperty(aCx, aObj, "ctypes", &ctypes) &&
         JS_SetCTypesCallbacks(aCx, JSVAL_TO_OBJECT(ctypes), &sCallbacks) &&
         JS_GetPropertyById(aCx, aObj, aId, aVp);
}
#endif
JSFunctionSpec gDOMWorkerFunctions[] = {
  { "dump",                nsDOMWorkerFunctions::Dump,                1, 0 },
  { "setTimeout",          nsDOMWorkerFunctions::SetTimeout,          1, 0 },
  { "clearTimeout",        nsDOMWorkerFunctions::KillTimeout,         1, 0 },
  { "setInterval",         nsDOMWorkerFunctions::SetInterval,         1, 0 },
  { "clearInterval",       nsDOMWorkerFunctions::KillTimeout,         1, 0 },
  { "importScripts",       nsDOMWorkerFunctions::LoadScripts,         1, 0 },
  { "XMLHttpRequest",      nsDOMWorkerFunctions::NewXMLHttpRequest,   0, JSFUN_CONSTRUCTOR },
  { "Worker",              nsDOMWorkerFunctions::NewWorker,           1, JSFUN_CONSTRUCTOR },
  { "atob",                nsDOMWorkerFunctions::AtoB,                1, 0 },
  { "btoa",                nsDOMWorkerFunctions::BtoA,                1, 0 },
  { nsnull,                nsnull,                                    0, 0 }
};
JSFunctionSpec gDOMWorkerChromeFunctions[] = {
  { "ChromeWorker",        nsDOMWorkerFunctions::NewChromeWorker,     1, JSFUN_CONSTRUCTOR },
  { nsnull,                nsnull,                                    0, 0 }
};
enum DOMWorkerStructuredDataType
{
  // We have a special tag for XPCWrappedNatives that are being passed between
  // threads. This will not work across processes and cannot be persisted. Only
  // for ChromeWorker use at present.
  DOMWORKER_SCTAG_WRAPPEDNATIVE = JS_SCTAG_USER_MIN + 0x1000,

  DOMWORKER_SCTAG_END
};

PR_STATIC_ASSERT(DOMWORKER_SCTAG_END <= JS_SCTAG_USER_MAX);

// static
JSBool
WriteStructuredClone(JSContext* aCx,
                     JSStructuredCloneWriter* aWriter,
                     JSObject* aObj,
                     void* aClosure)
{
  NS_ASSERTION(aClosure, "Null pointer!");

  // We'll stash any nsISupports pointers that need to be AddRef'd here.
  nsTArray<nsCOMPtr<nsISupports> >* wrappedNatives =
    static_cast<nsTArray<nsCOMPtr<nsISupports> >*>(aClosure);

  // See if this is a wrapped native.
  nsCOMPtr<nsIXPConnectWrappedNative> wrappedNative;
  nsContentUtils::XPConnect()->
    GetWrappedNativeOfJSObject(aCx, aObj, getter_AddRefs(wrappedNative));
  if (wrappedNative) {
    // Get the raw nsISupports out of it.
    nsISupports* wrappedObject = wrappedNative->Native();
    NS_ASSERTION(wrappedObject, "Null pointer?!");

    // See if this nsISupports is threadsafe.
    nsCOMPtr<nsIClassInfo> classInfo = do_QueryInterface(wrappedObject);
    if (classInfo) {
      PRUint32 flags;
      if (NS_SUCCEEDED(classInfo->GetFlags(&flags)) &&
          (flags & nsIClassInfo::THREADSAFE)) {
        // Write the raw pointer into the stream, and add it to the list we're
        // building.
        return JS_WriteUint32Pair(aWriter, DOMWORKER_SCTAG_WRAPPEDNATIVE, 0) &&
               JS_WriteBytes(aWriter, &wrappedObject, sizeof(wrappedObject)) &&
               wrappedNatives->AppendElement(wrappedObject);
      }
    }
  }

  // Something failed above, try using the runtime callbacks instead.
  const JSStructuredCloneCallbacks* runtimeCallbacks =
    aCx->runtime->structuredCloneCallbacks;
  if (runtimeCallbacks) {
    return runtimeCallbacks->write(aCx, aWriter, aObj, nsnull);
  }

  // We can't handle this object, throw an exception if one hasn't been thrown
  // already.
  if (!JS_IsExceptionPending(aCx)) {
    nsDOMClassInfo::ThrowJSException(aCx, NS_ERROR_DOM_DATA_CLONE_ERR);
  }
  return JS_FALSE;
}

nsDOMWorkerScope::nsDOMWorkerScope(nsDOMWorker* aWorker)
: mWorker(aWorker),
  mWrappedNative(nsnull),
  mHasOnerror(PR_FALSE)
{
  NS_ASSERTION(aWorker, "Null pointer!");
}

NS_IMPL_ISUPPORTS_INHERITED3(nsDOMWorkerScope, nsDOMWorkerMessageHandler,
                                               nsIWorkerScope,
                                               nsIWorkerGlobalScope,
                                               nsIXPCScriptable)

NS_IMPL_CI_INTERFACE_GETTER4(nsDOMWorkerScope, nsIWorkerScope,
                                               nsIWorkerGlobalScope,
                                               nsIDOMEventTarget,
                                               nsIXPCScriptable)

NS_IMPL_THREADSAFE_DOM_CI_GETINTERFACES(nsDOMWorkerScope)
NS_IMPL_THREADSAFE_DOM_CI_ALL_THE_REST(nsDOMWorkerScope)

// Need to return a scriptable helper so that XPConnect can get our
// nsIXPCScriptable flags properly (to not enumerate QI, for instance).
NS_IMETHODIMP
nsDOMWorkerScope::GetHelperForLanguage(PRUint32 aLanguage,
                                       nsISupports** _retval)
{
  if (aLanguage == nsIProgrammingLanguage::JAVASCRIPT) {
    NS_ADDREF(*_retval = NS_ISUPPORTS_CAST(nsIWorkerScope*, this));
  }
  else {
    *_retval = nsnull;
  }
  return NS_OK;
}

// Use the xpc_map_end.h macros to generate the nsIXPCScriptable methods we want
// for the scope.

#define XPC_MAP_CLASSNAME nsDOMWorkerScope
#define XPC_MAP_QUOTED_CLASSNAME "DedicatedWorkerGlobalScope"
#define XPC_MAP_WANT_POSTCREATE
#define XPC_MAP_WANT_TRACE
#define XPC_MAP_WANT_FINALIZE

#define XPC_MAP_FLAGS                                      \
  nsIXPCScriptable::USE_JSSTUB_FOR_ADDPROPERTY           | \
  nsIXPCScriptable::USE_JSSTUB_FOR_DELPROPERTY           | \
  nsIXPCScriptable::USE_JSSTUB_FOR_SETPROPERTY           | \
  nsIXPCScriptable::DONT_ENUM_QUERY_INTERFACE            | \
  nsIXPCScriptable::CLASSINFO_INTERFACES_ONLY            | \
  nsIXPCScriptable::DONT_REFLECT_INTERFACE_NAMES         | \
  nsIXPCScriptable::WANT_ADDPROPERTY

#define XPC_MAP_WANT_ADDPROPERTY

#include "xpc_map_end.h"

NS_IMETHODIMP
nsDOMWorkerScope::PostCreate(nsIXPConnectWrappedNative*  aWrapper,
                             JSContext* /* aCx */,
                             JSObject* /* aObj */)
{
  NS_ASSERTION(!mWrappedNative, "Already got a wrapper?!");
  mWrappedNative = aWrapper;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorkerScope::Trace(nsIXPConnectWrappedNative* /* aWrapper */,
                        JSTracer* aTracer,
                        JSObject* /*aObj */)
{
  nsDOMWorkerMessageHandler::Trace(aTracer);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorkerScope::Finalize(nsIXPConnectWrappedNative* /* aWrapper */,
                           JSContext* /* aCx */,
                           JSObject* /* aObj */)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  ClearAllListeners();
  mWrappedNative = nsnull;
  return NS_OK;
}

already_AddRefed<nsIXPConnectWrappedNative>
nsDOMWorkerScope::GetWrappedNative()
{
  nsCOMPtr<nsIXPConnectWrappedNative> wrappedNative = mWrappedNative;
  NS_ASSERTION(wrappedNative, "Null wrapped native!");
  return wrappedNative.forget();
}

NS_IMETHODIMP
nsDOMWorkerScope::AddProperty(nsIXPConnectWrappedNative* aWrapper,
                              JSContext* aCx,
                              JSObject* aObj,
                              jsid aId,
                              jsval* aVp,
                              PRBool* _retval)
{
  // We're not going to be setting any exceptions manually so set _retval to
  // true in the beginning.
  *_retval = PR_TRUE;

  // Bail out now if any of our prerequisites are not met. We only care about
  // someone making an 'onmessage' or 'onerror' function so aId must be a
  // string and aVp must be a function.
  JSObject* funObj;
  if (!(JSID_IS_STRING(aId) &&
        JSVAL_IS_OBJECT(*aVp) &&
        (funObj = JSVAL_TO_OBJECT(*aVp)) &&
        JS_ObjectIsFunction(aCx, funObj))) {
    return NS_OK;
  }

  JSFlatString *str = JSID_TO_FLAT_STRING(aId);

  // Figure out which listener we're setting.
  SetListenerFunc func;
  if (JS_FlatStringEqualsAscii(str, "onmessage")) {
    func = &nsDOMWorkerScope::SetOnmessage;
  }
  else if (JS_FlatStringEqualsAscii(str, "onerror")) {
    func = &nsDOMWorkerScope::SetOnerror;
  }
  else {
    // Some other function, we don't need to do anything special after all.
    return NS_OK;
  }

  // Wrap the function as an nsIDOMEventListener.
  nsCOMPtr<nsIDOMEventListener> listener;
  nsresult rv =
    nsContentUtils::XPConnect()->WrapJS(aCx, funObj,
                                        NS_GET_IID(nsIDOMEventListener),
                                        getter_AddRefs(listener));
  NS_ENSURE_SUCCESS(rv, rv);

  // And pass the listener to the appropriate setter.
  rv = (this->*func)(listener);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorkerScope::GetSelf(nsIWorkerGlobalScope** aSelf)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");
  NS_ENSURE_ARG_POINTER(aSelf);

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  NS_ADDREF(*aSelf = this);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorkerScope::GetNavigator(nsIWorkerNavigator** _retval)
{
  if (!mNavigator) {
    mNavigator = new nsDOMWorkerNavigator();
    NS_ENSURE_TRUE(mNavigator, NS_ERROR_OUT_OF_MEMORY);
  }

  NS_ADDREF(*_retval = mNavigator);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorkerScope::GetLocation(nsIWorkerLocation** _retval)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  nsCOMPtr<nsIWorkerLocation> location = mWorker->GetLocation();
  NS_ASSERTION(location, "This should never be null!");

  location.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorkerScope::GetOnerror(nsIDOMEventListener** aOnerror)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");
  NS_ENSURE_ARG_POINTER(aOnerror);

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  if (!mHasOnerror) {
    // Spec says we have to return 'undefined' until something is set here.
    nsIXPConnect* xpc = nsContentUtils::XPConnect();
    NS_ENSURE_TRUE(xpc, NS_ERROR_UNEXPECTED);

    nsAXPCNativeCallContext* cc;
    nsresult rv = xpc->GetCurrentNativeCallContext(&cc);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(cc, NS_ERROR_UNEXPECTED);

    jsval* retval;
    rv = cc->GetRetValPtr(&retval);
    NS_ENSURE_SUCCESS(rv, rv);

    *retval = JSVAL_VOID;
    return cc->SetReturnValueWasSet(PR_TRUE);
  }

  nsCOMPtr<nsIDOMEventListener> listener =
    GetOnXListener(NS_LITERAL_STRING("error"));
  listener.forget(aOnerror);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorkerScope::SetOnerror(nsIDOMEventListener* aOnerror)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  mHasOnerror = PR_TRUE;

  return SetOnXListener(NS_LITERAL_STRING("error"), aOnerror);
}

NS_IMETHODIMP
nsDOMWorkerScope::PostMessage(/* JSObject aMessage */)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  return mWorker->PostMessageInternal(PR_FALSE);
}

NS_IMETHODIMP
nsDOMWorkerScope::Close()
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  return mWorker->Close();
}

NS_IMETHODIMP
nsDOMWorkerScope::GetOnmessage(nsIDOMEventListener** aOnmessage)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");
  NS_ENSURE_ARG_POINTER(aOnmessage);

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  nsCOMPtr<nsIDOMEventListener> listener =
    GetOnXListener(NS_LITERAL_STRING("message"));
  listener.forget(aOnmessage);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorkerScope::SetOnmessage(nsIDOMEventListener* aOnmessage)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  return SetOnXListener(NS_LITERAL_STRING("message"), aOnmessage);
}

NS_IMETHODIMP
nsDOMWorkerScope::GetOnclose(nsIDOMEventListener** aOnclose)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");
  NS_ENSURE_ARG_POINTER(aOnclose);

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  nsCOMPtr<nsIDOMEventListener> listener =
    GetOnXListener(NS_LITERAL_STRING("close"));
  listener.forget(aOnclose);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorkerScope::SetOnclose(nsIDOMEventListener* aOnclose)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  nsresult rv = SetOnXListener(NS_LITERAL_STRING("close"), aOnclose);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorkerScope::RemoveEventListener(const nsAString& aType,
                                      nsIDOMEventListener* aListener,
                                      PRBool aUseCapture)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  return nsDOMWorkerMessageHandler::RemoveEventListener(aType, aListener,
                                                        aUseCapture);
}

NS_IMETHODIMP
nsDOMWorkerScope::DispatchEvent(nsIDOMEvent* aEvent,
                                PRBool* _retval)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  return nsDOMWorkerMessageHandler::DispatchEvent(aEvent, _retval);
}

NS_IMETHODIMP
nsDOMWorkerScope::AddEventListener(const nsAString& aType,
                                   nsIDOMEventListener* aListener,
                                   PRBool aUseCapture,
                                   PRBool aWantsUntrusted,
                                   PRUint8 optional_argc)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (mWorker->IsCanceled()) {
    return NS_ERROR_ABORT;
  }

  return nsDOMWorkerMessageHandler::AddEventListener(aType, aListener,
                                                     aUseCapture,
                                                     aWantsUntrusted,
                                                     optional_argc);
}

class nsWorkerHoldingRunnable : public nsIRunnable
{
public:
  NS_DECL_ISUPPORTS

  nsWorkerHoldingRunnable(nsDOMWorker* aWorker)
  : mWorker(aWorker), mWorkerWN(aWorker->GetWrappedNative()) { }

  NS_IMETHOD Run() {
    return NS_OK;
  }

  void ReplaceWrappedNative(nsIXPConnectWrappedNative* aWrappedNative) {
    mWorkerWN = aWrappedNative;
  }

protected:
  virtual ~nsWorkerHoldingRunnable() { }

  nsRefPtr<nsDOMWorker> mWorker;

private:
  nsCOMPtr<nsIXPConnectWrappedNative> mWorkerWN;
};

NS_IMPL_THREADSAFE_ISUPPORTS1(nsWorkerHoldingRunnable, nsIRunnable)

class nsDOMFireEventRunnable : public nsWorkerHoldingRunnable
{
public:
  NS_DECL_ISUPPORTS_INHERITED

  nsDOMFireEventRunnable(nsDOMWorker* aWorker,
                         nsDOMWorkerEvent* aEvent,
                         PRBool aToInner)
  : nsWorkerHoldingRunnable(aWorker), mEvent(aEvent), mToInner(aToInner)
  {
    NS_ASSERTION(aWorker && aEvent, "Null pointer!");
  }

  NS_IMETHOD Run() {
#ifdef DEBUG
    if (NS_IsMainThread()) {
      NS_ASSERTION(!mToInner, "Should only run outer events on main thread!");
      NS_ASSERTION(!mWorker->mParent, "Worker shouldn't have a parent!");
    }
    else {
      JSContext* cx = nsDOMThreadService::GetCurrentContext();
      nsDOMWorker* currentWorker = (nsDOMWorker*)JS_GetContextPrivate(cx);
      NS_ASSERTION(currentWorker, "Must have a worker here!");

      nsDOMWorker* targetWorker = mToInner ? mWorker.get() : mWorker->mParent;
      NS_ASSERTION(currentWorker == targetWorker, "Wrong worker!");
    }
#endif
    if (mWorker->IsCanceled()) {
      return NS_ERROR_ABORT;
    }

    // If the worker is suspended and we're running on the main thread then we
    // can't actually dispatch the event yet. Instead we queue it for whenever
    // we resume.
    if (mWorker->IsSuspended() && NS_IsMainThread()) {
      if (!mWorker->QueueSuspendedRunnable(this)) {
        NS_ERROR("Out of memory?!");
        return NS_ERROR_ABORT;
      }
      return NS_OK;
    }

    nsCOMPtr<nsIDOMEventTarget> target = mToInner ?
      static_cast<nsDOMWorkerMessageHandler*>(mWorker->GetInnerScope()) :
      static_cast<nsDOMWorkerMessageHandler*>(mWorker);

    NS_ASSERTION(target, "Null target!");
    NS_ENSURE_TRUE(target, NS_ERROR_FAILURE);

    mEvent->SetTarget(target);
    return target->DispatchEvent(mEvent, nsnull);
  }

protected:
  nsRefPtr<nsDOMWorkerEvent> mEvent;
  PRBool mToInner;
};

NS_IMPL_ISUPPORTS_INHERITED0(nsDOMFireEventRunnable, nsWorkerHoldingRunnable)

// Standard NS_IMPL_THREADSAFE_ADDREF without the logging stuff (since this
// class is made to be inherited anyway).
NS_IMETHODIMP_(nsrefcnt)
nsDOMWorkerFeature::AddRef()
{
  NS_ASSERTION(mRefCnt >= 0, "Illegal refcnt!");
  return NS_AtomicIncrementRefcnt(mRefCnt);
}

// Custom NS_IMPL_THREADSAFE_RELEASE. Checks the mFreeToDie flag before calling
// delete. If the flag is false then the feature still lives in the worker's
// list and must be removed. We rely on the fact that the RemoveFeature method
// calls AddRef and Release after setting the mFreeToDie flag so we won't leak.
NS_IMETHODIMP_(nsrefcnt)
nsDOMWorkerFeature::Release()
{
  NS_ASSERTION(mRefCnt, "Double release!");
  nsrefcnt count = NS_AtomicDecrementRefcnt(mRefCnt);
  if (count == 0) {
    if (mFreeToDie) {
      mRefCnt = 1;
      delete this;
    }
    else {
      mWorker->RemoveFeature(this, nsnull);
    }
  }
  return count;
}

NS_IMPL_QUERY_INTERFACE0(nsDOMWorkerFeature)

nsDOMWorker::nsDOMWorker(nsDOMWorker* aParent,
                         nsIXPConnectWrappedNative* aParentWN,
                         WorkerPrivilegeModel aPrivilegeModel)
: mParent(aParent),
  mParentWN(aParentWN),
  mPrivilegeModel(aPrivilegeModel),
  mLock("nsDOMWorker.mLock"),
  mInnerScope(nsnull),
  mGlobal(NULL),
  mNextTimeoutId(0),
  mFeatureSuspendDepth(0),
  mWrappedNative(nsnull),
  mErrorHandlerRecursionCount(0),
  mStatus(eRunning),
  mExpirationTime(0),
  mSuspended(PR_FALSE),
  mCompileAttempted(PR_FALSE)
{
#ifdef DEBUG
  PRBool mainThread = NS_IsMainThread();
  NS_ASSERTION(aParent ? !mainThread : mainThread, "Wrong thread!");
#endif
}

nsDOMWorker::~nsDOMWorker()
{
  if (mPool) {
    mPool->NoteDyingWorker(this);
  }

  NS_ASSERTION(!mFeatures.Length(), "Live features!");
  NS_ASSERTION(!mQueuedRunnables.Length(), "Events that never ran!");

  nsCOMPtr<nsIThread> mainThread;
  NS_GetMainThread(getter_AddRefs(mainThread));

  nsIPrincipal* principal;
  mPrincipal.forget(&principal);
  if (principal) {
    NS_ProxyRelease(mainThread, principal, PR_FALSE);
  }

  nsIURI* uri;
  mBaseURI.forget(&uri);
  if (uri) {
    NS_ProxyRelease(mainThread, uri, PR_FALSE);
  }
}

// static
nsresult
nsDOMWorker::NewWorker(nsISupports** aNewObject)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsCOMPtr<nsISupports> newWorker =
    NS_ISUPPORTS_CAST(nsIWorker*, new nsDOMWorker(nsnull, nsnull, CONTENT));
  NS_ENSURE_TRUE(newWorker, NS_ERROR_OUT_OF_MEMORY);

  newWorker.forget(aNewObject);
  return NS_OK;
}

// static
nsresult
nsDOMWorker::NewChromeDOMWorker(nsDOMWorker** aNewObject)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  // Subsumes nsContentUtils::IsCallerChrome
  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  NS_ASSERTION(ssm, "Should never be null!");

  PRBool enabled;
  nsresult rv = ssm->IsCapabilityEnabled("UniversalXPConnect", &enabled);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(enabled, NS_ERROR_DOM_SECURITY_ERR);

  nsRefPtr<nsDOMWorker> newWorker = new nsDOMWorker(nsnull, nsnull, CHROME);
  NS_ENSURE_TRUE(newWorker, NS_ERROR_OUT_OF_MEMORY);

  newWorker.forget(aNewObject);
  return NS_OK;
}

// static
nsresult
nsDOMWorker::NewChromeWorker(nsISupports** aNewObject)
{
  nsDOMWorker* newWorker;
  nsresult rv = NewChromeDOMWorker(&newWorker);
  NS_ENSURE_SUCCESS(rv, rv);

  *aNewObject = NS_ISUPPORTS_CAST(nsIWorker*, newWorker);
  return NS_OK;
}

NS_IMPL_ADDREF_INHERITED(nsDOMWorker, nsDOMWorkerMessageHandler)
NS_IMPL_RELEASE_INHERITED(nsDOMWorker, nsDOMWorkerMessageHandler)

NS_INTERFACE_MAP_BEGIN(nsDOMWorker)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIWorker)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIWorker)
  NS_INTERFACE_MAP_ENTRY(nsIAbstractWorker)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIDOMEventTarget,
                                   nsDOMWorkerMessageHandler)
  NS_INTERFACE_MAP_ENTRY(nsIJSNativeInitializer)
  NS_INTERFACE_MAP_ENTRY(nsITimerCallback)
NS_INTERFACE_MAP_END

// Use the xpc_map_end.h macros to generate the nsIXPCScriptable methods we want
// for the worker.

#define XPC_MAP_CLASSNAME nsDOMWorker
#define XPC_MAP_QUOTED_CLASSNAME "Worker"
#define XPC_MAP_WANT_PRECREATE
#define XPC_MAP_WANT_POSTCREATE
#define XPC_MAP_WANT_TRACE
#define XPC_MAP_WANT_FINALIZE

#define XPC_MAP_FLAGS                                      \
  nsIXPCScriptable::USE_JSSTUB_FOR_ADDPROPERTY           | \
  nsIXPCScriptable::USE_JSSTUB_FOR_DELPROPERTY           | \
  nsIXPCScriptable::USE_JSSTUB_FOR_SETPROPERTY           | \
  nsIXPCScriptable::DONT_ENUM_QUERY_INTERFACE            | \
  nsIXPCScriptable::CLASSINFO_INTERFACES_ONLY            | \
  nsIXPCScriptable::DONT_REFLECT_INTERFACE_NAMES

#include "xpc_map_end.h"

NS_IMETHODIMP
nsDOMWorker::PreCreate(nsISupports* aObject,
                       JSContext* aCx,
                       JSObject* /* aPlannedParent */,
                       JSObject** aParent)
{
  nsCOMPtr<nsIWorker> iworker(do_QueryInterface(aObject));
  NS_ENSURE_TRUE(iworker, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIXPConnectWrappedNative> wrappedNative;
  {
    MutexAutoLock lock(mLock);
    wrappedNative = mWrappedNative;
  }

  // Don't allow XPConnect to create multiple WrappedNatives for this object.
  if (wrappedNative) {
    JSObject* object;
    nsresult rv = wrappedNative->GetJSObject(&object);
    NS_ENSURE_SUCCESS(rv, rv);

    *aParent = JS_GetParent(aCx, object);
  }

  return IsPrivileged() ? NS_SUCCESS_CHROME_ACCESS_ONLY : NS_OK;
}

NS_IMETHODIMP
nsDOMWorker::PostCreate(nsIXPConnectWrappedNative* aWrapper,
                        JSContext* /* aCx */,
                        JSObject* /* aObj */)
{
  MutexAutoLock lock(mLock);
  mWrappedNative = aWrapper;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorker::Trace(nsIXPConnectWrappedNative* /* aWrapper */,
                   JSTracer* aTracer,
                   JSObject* /*aObj */)
{
  PRBool canceled = PR_FALSE;
  {
    MutexAutoLock lock(mLock);
    canceled = mStatus == eKilled;
  }

  if (!canceled) {
    nsDOMWorkerMessageHandler::Trace(aTracer);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorker::Finalize(nsIXPConnectWrappedNative* /* aWrapper */,
                      JSContext* aCx,
                      JSObject* /* aObj */)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  // Don't leave dangling JSObject pointers in our handlers!
  ClearAllListeners();

  // Clear our wrapped native now that it has died.
  {
    MutexAutoLock lock(mLock);
    mWrappedNative = nsnull;
  }

  // Do this *after* we null out mWrappedNative so that we don't hand out a
  // freed pointer.
  if (TerminateInternal(PR_TRUE) == NS_ERROR_ILLEGAL_DURING_SHUTDOWN) {
    // We're shutting down, jump right to Kill.
    Kill();
  }

  return NS_OK;
}

NS_IMPL_CI_INTERFACE_GETTER3(nsDOMWorker, nsIWorker,
                                          nsIAbstractWorker,
                                          nsIDOMEventTarget)
NS_IMPL_THREADSAFE_DOM_CI_GETINTERFACES(nsDOMWorker)
NS_IMPL_THREADSAFE_DOM_CI_ALL_THE_REST(nsDOMWorker)

NS_IMETHODIMP
nsDOMWorker::GetHelperForLanguage(PRUint32 aLanguage,
                                  nsISupports** _retval)
{
  if (aLanguage == nsIProgrammingLanguage::JAVASCRIPT) {
    NS_ADDREF(*_retval = NS_ISUPPORTS_CAST(nsIWorker*, this));
  }
  else {
    *_retval = nsnull;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDOMWorker::Initialize(nsISupports* aOwner,
                        JSContext* aCx,
                        JSObject* aObj,
                        PRUint32 aArgc,
                        jsval* aArgv)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ENSURE_ARG_POINTER(aOwner);

  nsCOMPtr<nsIScriptGlobalObject> globalObj(do_QueryInterface(aOwner));
  NS_ENSURE_TRUE(globalObj, NS_NOINTERFACE);

  return InitializeInternal(globalObj, aCx, aObj, aArgc, aArgv);
}

nsresult
nsDOMWorker::InitializeInternal(nsIScriptGlobalObject* aOwner,
                                JSContext* aCx,
                                JSObject* aObj,
                                PRUint32 aArgc,
                                jsval* aArgv)
{
  NS_ASSERTION(aCx, "Null context!");
  NS_ASSERTION(aObj, "Null global object!");

  NS_ENSURE_TRUE(aArgc, NS_ERROR_XPC_NOT_ENOUGH_ARGS);
  NS_ENSURE_ARG_POINTER(aArgv);

  JSString* str = JS_ValueToString(aCx, aArgv[0]);
  NS_ENSURE_TRUE(str, NS_ERROR_XPC_BAD_CONVERT_JS);

  nsDependentJSString depStr;
  NS_ENSURE_TRUE(depStr.init(aCx, str), NS_ERROR_OUT_OF_MEMORY);

  mScriptURL.Assign(depStr);
  NS_ENSURE_FALSE(mScriptURL.IsEmpty(), NS_ERROR_INVALID_ARG);

  nsresult rv;

  // Figure out the principal and base URI to use if we're on the main thread.
  // Otherwise this is a sub-worker and it will have its principal set by the
  // script loader.
  if (NS_IsMainThread()) {
    nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
    NS_ASSERTION(ssm, "Should never be null!");

    PRBool isChrome;
    rv = ssm->IsCapabilityEnabled("UniversalXPConnect", &isChrome);
    NS_ENSURE_SUCCESS(rv, rv);

    NS_ASSERTION(isChrome || aOwner, "How can we have a non-chrome, non-window "
                 "worker?!");

    // Chrome callers (whether ChromeWorker of Worker) always get the system
    // principal here as they're allowed to load anything. The script loader may
    // change the principal later depending on the script uri.
    if (isChrome) {
      rv = ssm->GetSystemPrincipal(getter_AddRefs(mPrincipal));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (aOwner) {
      // We're being created inside a window. Get the document's base URI and
      // use it as our base URI.
      nsCOMPtr<nsPIDOMWindow> domWindow = do_QueryInterface(aOwner, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      nsIDOMDocument* domDocument = domWindow->GetExtantDocument();
      NS_ENSURE_STATE(domDocument);

      nsCOMPtr<nsIDocument> document = do_QueryInterface(domDocument, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      mBaseURI = document->GetDocBaseURI();

      if (!mPrincipal) {
        // Use the document's NodePrincipal as our principal if we're not being
        // called from chrome.
        mPrincipal = document->NodePrincipal();
        NS_ENSURE_STATE(mPrincipal);
      }
    }
    else {
      // We're being created outside of a window. Need to figure out the script
      // that is creating us in order for us to use relative URIs later on.
      JSStackFrame* frame = JS_GetScriptedCaller(aCx, nsnull);
      if (frame) {
        JSScript* script = JS_GetFrameScript(aCx, frame);
        NS_ENSURE_STATE(script);

        const char* filename = JS_GetScriptFilename(aCx, script);

        rv = NS_NewURI(getter_AddRefs(mBaseURI), filename);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }

    NS_ASSERTION(mPrincipal, "Should have set the principal!");
  }

  NS_ASSERTION(!mGlobal, "Already got a global?!");

  nsCOMPtr<nsIXPConnectJSObjectHolder> thisWrapped;
  jsval v;
  rv = nsContentUtils::WrapNative(aCx, aObj, static_cast<nsIWorker*>(this), &v,
                                  getter_AddRefs(thisWrapped));
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(mWrappedNative, "Post-create hook should have set this!");

  mKillTimer = do_CreateInstance(NS_TIMER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIThread> mainThread;
  rv = NS_GetMainThread(getter_AddRefs(mainThread));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mKillTimer->SetTarget(mainThread);
  NS_ENSURE_SUCCESS(rv, rv);

  // This is pretty cool - all we have to do to get our script executed is to
  // pass a no-op runnable to the thread service and it will make sure we have
  // a context and global object.
  nsCOMPtr<nsIRunnable> runnable(new nsWorkerHoldingRunnable(this));
  NS_ENSURE_TRUE(runnable, NS_ERROR_OUT_OF_MEMORY);

  nsRefPtr<nsDOMThreadService> threadService =
    nsDOMThreadService::GetOrInitService();
  NS_ENSURE_STATE(threadService);

  rv = threadService->RegisterWorker(this, aOwner);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(mPool, "RegisterWorker should have set our pool!");

  rv = threadService->Dispatch(this, runnable);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void
nsDOMWorker::Cancel()
{
  // Called by the pool when the window that created us is being torn down. Must
  // always be on the main thread.
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  // We set the eCanceled status to indicate this. It behaves just like the
  // eTerminated status (canceled while close runnable is unscheduled, not
  // canceled while close runnable is running) except that it always reports
  // that it is canceled when running on the main thread. This status trumps all
  // others (except eKilled). Have to do this because the window that created
  // us has gone away and had its scope cleared so XPConnect will assert all
  // over the place if we try to run anything.

  PRBool enforceTimeout = PR_FALSE;
  {
    MutexAutoLock lock(mLock);

    NS_ASSERTION(mStatus != eCanceled, "Canceled more than once?!");

    if (mStatus == eKilled) {
      return;
    }

    DOMWorkerStatus oldStatus = mStatus;
    mStatus = eCanceled;
    if (oldStatus != eRunning) {
      enforceTimeout = PR_TRUE;
    }
  }

  PRUint32 timeoutMS = nsDOMThreadService::GetWorkerCloseHandlerTimeoutMS();
  NS_ASSERTION(timeoutMS, "This must not be 0!");

#ifdef DEBUG
  nsresult rv;
#endif
  if (enforceTimeout) {
    // Tell the thread service to enforce a timeout on the close handler that
    // is already scheduled.
    nsDOMThreadService::get()->
      SetWorkerTimeout(this, PR_MillisecondsToInterval(timeoutMS));

#ifdef DEBUG
    rv =
#endif
    mKillTimer->InitWithCallback(this, timeoutMS, nsITimer::TYPE_ONE_SHOT);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Failed to init kill timer!");

    return;
  }

#ifdef DEBUG
  rv =
#endif
  FireCloseRunnable(PR_MillisecondsToInterval(timeoutMS), PR_TRUE, PR_FALSE);
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Failed to fire close runnable!");
}

void
nsDOMWorker::Kill()
{
  // Cancel all features and set our status to eKilled. This should only be
  // called on the main thread by the thread service or our kill timer to
  // indicate that the worker's close handler has run (or timed out).
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(IsClosing(), "Close handler should have run by now!");

  // If the close handler finished before our kill timer then we don't need it
  // any longer.
  if (mKillTimer) {
    mKillTimer->Cancel();
    mKillTimer = nsnull;
  }

  PRUint32 count, index;
  nsAutoTArray<nsRefPtr<nsDOMWorkerFeature>, 20> features;
  {
    MutexAutoLock lock(mLock);

    if (mStatus == eKilled) {
      NS_ASSERTION(mFeatures.Length() == 0, "Features added after killed!");
      return;
    }
    mStatus = eKilled;

    count = mFeatures.Length();
    for (index = 0; index < count; index++) {
      nsDOMWorkerFeature*& feature = mFeatures[index];

#ifdef DEBUG
      nsRefPtr<nsDOMWorkerFeature>* newFeature =
#endif
      features.AppendElement(feature);
      NS_ASSERTION(newFeature, "Out of memory!");

      feature->FreeToDie(PR_TRUE);
    }

    mFeatures.Clear();
  }

  count = features.Length();
  for (index = 0; index < count; index++) {
    features[index]->Cancel();
  }

  // Make sure we kill any queued runnables that we never had a chance to run.
  mQueuedRunnables.Clear();

  // We no longer need to keep our inner scope.
  mInnerScope = nsnull;
  mScopeWN = nsnull;
  mGlobal = NULL;

  // And we can let our parent die now too.
  mParent = nsnull;
  mParentWN = nsnull;
}

void
nsDOMWorker::Suspend()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  PRBool shouldSuspendFeatures;
  {
    MutexAutoLock lock(mLock);
    NS_ASSERTION(!mSuspended, "Suspended more than once!");
    shouldSuspendFeatures = !mSuspended;
    mSuspended = PR_TRUE;
  }

  if (shouldSuspendFeatures) {
    SuspendFeatures();
  }
}

void
nsDOMWorker::Resume()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  PRBool shouldResumeFeatures;
  {
    MutexAutoLock lock(mLock);
#ifdef DEBUG
    // Should only have a mismatch if GC or Cancel happened while suspended.
    if (!mSuspended) {
      NS_ASSERTION(mStatus == eCanceled ||
                   (mStatus == eTerminated && !mWrappedNative),
                   "Not suspended!");
    }
#endif
    shouldResumeFeatures = mSuspended;
    mSuspended = PR_FALSE;
  }

  if (shouldResumeFeatures) {
    ResumeFeatures();
  }

  // Repost any events that were queued for the main thread while suspended.
  PRUint32 count = mQueuedRunnables.Length();
  for (PRUint32 index = 0; index < count; index++) {
    NS_DispatchToCurrentThread(mQueuedRunnables[index]);
  }
  mQueuedRunnables.Clear();
}

PRBool
nsDOMWorker::IsCanceled()
{
  MutexAutoLock lock(mLock);
  return IsCanceledNoLock();
}

PRBool
nsDOMWorker::IsCanceledNoLock()
{
  // If we haven't started the close process then we're not canceled.
  if (mStatus == eRunning) {
    return PR_FALSE;
  }

  // There are several conditions under which we want JS code to abort and all
  // other functions to bail:
  // 1. If we've already run our close handler then we are canceled forevermore.
  // 2. If we've been terminated then we want to pretend to be canceled until
  //    our close handler is scheduled and running.
  // 3. If we've been canceled then we pretend to be canceled until the close
  //    handler has been scheduled.
  // 4. If the close handler has run for longer than the allotted time then we
  //    should be canceled as well.
  // 5. If we're on the main thread then we'll pretend to be canceled if the
  //    user has navigated away from the page.
  return mStatus == eKilled ||
         (mStatus == eTerminated && !mExpirationTime) ||
         (mStatus == eCanceled && !mExpirationTime) ||
         (mExpirationTime && mExpirationTime != PR_INTERVAL_NO_TIMEOUT &&
          mExpirationTime <= PR_IntervalNow()) ||
         (mStatus == eCanceled && NS_IsMainThread());
}

PRBool
nsDOMWorker::IsClosing()
{
  MutexAutoLock lock(mLock);
  return mStatus != eRunning;
}

PRBool
nsDOMWorker::IsSuspended()
{
  MutexAutoLock lock(mLock);
  return mSuspended;
}

nsresult
nsDOMWorker::PostMessageInternal(PRBool aToInner)
{
  nsIXPConnect* xpc = nsContentUtils::XPConnect();
  NS_ENSURE_TRUE(xpc, NS_ERROR_UNEXPECTED);

  nsAXPCNativeCallContext* cc;
  nsresult rv = xpc->GetCurrentNativeCallContext(&cc);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(cc, NS_ERROR_UNEXPECTED);

  PRUint32 argc;
  rv = cc->GetArgc(&argc);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!argc) {
    return NS_ERROR_XPC_NOT_ENOUGH_ARGS;
  }

  jsval* argv;
  rv = cc->GetArgvPtr(&argv);
  NS_ENSURE_SUCCESS(rv, rv);

  JSContext* cx;
  rv = cc->GetJSContext(&cx);
  NS_ENSURE_SUCCESS(rv, rv);

  // If we're a ChromeWorker then we allow wrapped natives to be passed via
  // structured cloning by supplying a custom write callback. To do that we need
  // to make sure they stay alive while the message is being sent, so we collect
  // the wrapped natives in an array to be packaged with the message.
  JSStructuredCloneCallbacks callbacks = {
    nsnull, IsPrivileged() ? WriteStructuredClone : nsnull, nsnull
  };

  JSAutoRequest ar(cx);

  JSAutoStructuredCloneBuffer buffer;
  nsTArray<nsCOMPtr<nsISupports> > wrappedNatives;
  if (!buffer.write(cx, argv[0], &callbacks, &wrappedNatives)) {
    return NS_ERROR_DOM_DATA_CLONE_ERR;
  }

  nsRefPtr<nsDOMWorkerMessageEvent> message = new nsDOMWorkerMessageEvent();
  NS_ENSURE_TRUE(message, NS_ERROR_OUT_OF_MEMORY);

  rv = message->InitMessageEvent(NS_LITERAL_STRING("message"), PR_FALSE,
                                 PR_FALSE, EmptyString(), EmptyString(),
                                 nsnull);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = message->SetJSData(cx, buffer, wrappedNatives);
  NS_ENSURE_SUCCESS(rv, rv);

  nsRefPtr<nsDOMFireEventRunnable> runnable =
    new nsDOMFireEventRunnable(this, message, aToInner);
  NS_ENSURE_TRUE(runnable, NS_ERROR_OUT_OF_MEMORY);

  // If aToInner is true then we want to target the runnable at this worker's
  // thread. Otherwise we need to target the parent's thread.
  nsDOMWorker* target = aToInner ? this : mParent;

  // If this is a top-level worker then target the main thread. Otherwise use
  // the thread service to find the target's thread.
  if (!target) {
    nsCOMPtr<nsIThread> mainThread;
    rv = NS_GetMainThread(getter_AddRefs(mainThread));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mainThread->Dispatch(runnable, NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    rv = nsDOMThreadService::get()->Dispatch(target, runnable);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

PRBool
nsDOMWorker::SetGlobalForContext(JSContext* aCx, nsLazyAutoRequest *aRequest,
                                 JSAutoEnterCompartment *aComp)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (!CompileGlobalObject(aCx, aRequest, aComp)) {
    return PR_FALSE;
  }

  JS_SetGlobalObject(aCx, mGlobal);
  return PR_TRUE;
}

PRBool
nsDOMWorker::CompileGlobalObject(JSContext* aCx, nsLazyAutoRequest *aRequest,
                                 JSAutoEnterCompartment *aComp)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  // On success, we enter a request and a cross-compartment call that both
  // belong to the caller. But on failure, we must not remain in a request or
  // cross-compartment call. So we enter both only locally at first. On
  // failure, the local request and call will automatically get cleaned
  // up. Once success is certain, we swap them into *aRequest and *aCall.
  nsLazyAutoRequest localRequest;
  JSAutoEnterCompartment localAutoCompartment;
  localRequest.enter(aCx);

  PRBool success;
  if (mGlobal) {
    success = localAutoCompartment.enter(aCx, mGlobal);
    NS_ENSURE_TRUE(success, PR_FALSE);

    aRequest->swap(localRequest);
    aComp->swap(localAutoCompartment);
    return PR_TRUE;
  }

  if (mCompileAttempted) {
    // Don't try to recompile a bad script.
    return PR_FALSE;
  }
  mCompileAttempted = PR_TRUE;

  NS_ASSERTION(!mScriptURL.IsEmpty(), "Must have a url here!");

  NS_ASSERTION(!JS_GetGlobalObject(aCx), "Global object should be unset!");

  nsRefPtr<nsDOMWorkerScope> scope = new nsDOMWorkerScope(this);
  NS_ENSURE_TRUE(scope, PR_FALSE);

  nsISupports* scopeSupports = NS_ISUPPORTS_CAST(nsIWorkerScope*, scope);

  nsIXPConnect* xpc = nsContentUtils::XPConnect();

  const PRUint32 flags = nsIXPConnect::INIT_JS_STANDARD_CLASSES |
                         nsIXPConnect::OMIT_COMPONENTS_OBJECT;

  nsCOMPtr<nsIXPConnectJSObjectHolder> globalWrapper;
  nsresult rv =
    xpc->InitClassesWithNewWrappedGlobal(aCx, scopeSupports,
                                         NS_GET_IID(nsISupports), nsnull,
                                         NS_ISUPPORTS_CAST(nsIWorker*, this),
                                         flags, getter_AddRefs(globalWrapper));
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  JSObject* global;
  rv = globalWrapper->GetJSObject(&global);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  NS_ASSERTION(JS_GetGlobalObject(aCx) == global, "Global object mismatch!");

  success = localAutoCompartment.enter(aCx, global);
  NS_ENSURE_TRUE(success, PR_FALSE);

#ifdef DEBUG
  {
    jsval components;
    if (JS_GetProperty(aCx, global, "Components", &components)) {
      NS_ASSERTION(components == JSVAL_VOID,
                   "Components property still defined!");
    }
  }
#endif

  // Set up worker thread functions.
  success = JS_DefineFunctions(aCx, global, gDOMWorkerFunctions);
  NS_ENSURE_TRUE(success, PR_FALSE);

  success = JS_DefineProfilingFunctions(aCx, global);
  NS_ENSURE_TRUE(success, PR_FALSE);

  if (IsPrivileged()) {
    // Add chrome functions.
    success = JS_DefineFunctions(aCx, global, gDOMWorkerChromeFunctions);
    NS_ENSURE_TRUE(success, PR_FALSE);

    success = JS_DefineProperty(aCx, global, "XPCOM", JSVAL_VOID,
                                nsDOMWorkerFunctions::XPCOMLazyGetter, nsnull,
                                0);
    NS_ENSURE_TRUE(success, PR_FALSE);

#ifdef BUILD_CTYPES
    // Add the lazy getter for ctypes.
    success = JS_DefineProperty(aCx, global, "ctypes", JSVAL_VOID,
                                nsDOMWorkerFunctions::CTypesLazyGetter, nsnull,
                                0);
    NS_ENSURE_TRUE(success, PR_FALSE);
#endif
  }

  // From here on out we have to remember to null mGlobal, mInnerScope, and
  // mScopeWN if something fails! We really don't need to hang on to mGlobal
  // as long as we have mScopeWN, but it saves us a virtual call every time the
  // worker is scheduled. Meh.
  mGlobal = global;
  mInnerScope = scope;
  mScopeWN = scope->GetWrappedNative();
  NS_ASSERTION(mScopeWN, "Should have a wrapped native here!");

  nsRefPtr<nsDOMWorkerScriptLoader> loader =
    new nsDOMWorkerScriptLoader(this);

  rv = AddFeature(loader, aCx);
  if (NS_FAILED(rv)) {
    mGlobal = NULL;
    mInnerScope = nsnull;
    mScopeWN = nsnull;
    return PR_FALSE;
  }

  rv = loader->LoadWorkerScript(aCx, mScriptURL);

  JS_ReportPendingException(aCx);

  if (NS_FAILED(rv)) {
    mGlobal = NULL;
    mInnerScope = nsnull;
    mScopeWN = nsnull;
    return PR_FALSE;
  }

  NS_ASSERTION(mPrincipal, "Script loader didn't set our principal!");
  NS_ASSERTION(mBaseURI, "Script loader didn't set our base uri!");

  // Make sure we kept the system principal.
  if (IsPrivileged() && !nsContentUtils::IsSystemPrincipal(mPrincipal)) {
    static const char warning[] = "ChromeWorker attempted to load a "
                                  "non-chrome worker script!";
    NS_WARNING(warning);

    JS_ReportError(aCx, warning);

    mGlobal = NULL;
    mInnerScope = nsnull;
    mScopeWN = nsnull;
    return PR_FALSE;
  }

  rv = loader->ExecuteScripts(aCx);

  JS_ReportPendingException(aCx);

  if (NS_FAILED(rv)) {
    mGlobal = NULL;
    mInnerScope = nsnull;
    mScopeWN = nsnull;
    return PR_FALSE;
  }

  aRequest->swap(localRequest);
  aComp->swap(localAutoCompartment);
  return PR_TRUE;
}

void
nsDOMWorker::SetPool(nsDOMWorkerPool* aPool)
{
  NS_ASSERTION(!mPool, "Shouldn't ever set pool more than once!");
  mPool = aPool;
}

already_AddRefed<nsIXPConnectWrappedNative>
nsDOMWorker::GetWrappedNative()
{
  nsCOMPtr<nsIXPConnectWrappedNative> wrappedNative;
  {
    MutexAutoLock lock(mLock);
    wrappedNative = mWrappedNative;
  }
  return wrappedNative.forget();
}

nsresult
nsDOMWorker::AddFeature(nsDOMWorkerFeature* aFeature,
                        JSContext* aCx)
{
  NS_ASSERTION(aFeature, "Null pointer!");

  PRBool shouldSuspend;
  {
    // aCx may be null.
    JSAutoSuspendRequest asr(aCx);

    MutexAutoLock lock(mLock);

    if (mStatus == eKilled) {
      // No features may be added after we've been canceled. Sorry.
      return NS_ERROR_FAILURE;
    }

    nsDOMWorkerFeature** newFeature = mFeatures.AppendElement(aFeature);
    NS_ENSURE_TRUE(newFeature, NS_ERROR_OUT_OF_MEMORY);

    aFeature->FreeToDie(PR_FALSE);
    shouldSuspend = mFeatureSuspendDepth > 0;
  }

  if (shouldSuspend) {
    aFeature->Suspend();
  }

  return NS_OK;
}

void
nsDOMWorker::RemoveFeature(nsDOMWorkerFeature* aFeature,
                           JSContext* aCx)
{
  NS_ASSERTION(aFeature, "Null pointer!");

  // This *must* be a nsRefPtr so that we call Release after setting FreeToDie.
  nsRefPtr<nsDOMWorkerFeature> feature(aFeature);
  {
    // aCx may be null.
    JSAutoSuspendRequest asr(aCx);

    MutexAutoLock lock(mLock);

#ifdef DEBUG
    PRBool removed =
#endif
    mFeatures.RemoveElement(aFeature);
    NS_ASSERTION(removed, "Feature not in the list!");

    aFeature->FreeToDie(PR_TRUE);
  }
}

void
nsDOMWorker::CancelTimeoutWithId(PRUint32 aId)
{
  nsRefPtr<nsDOMWorkerFeature> foundFeature;
  {
    MutexAutoLock lock(mLock);
    PRUint32 count = mFeatures.Length();
    for (PRUint32 index = 0; index < count; index++) {
      nsDOMWorkerFeature*& feature = mFeatures[index];
      if (feature->HasId() && feature->GetId() == aId) {
        foundFeature = feature;
        feature->FreeToDie(PR_TRUE);
        mFeatures.RemoveElementAt(index);
        break;
      }
    }
  }

  if (foundFeature) {
    foundFeature->Cancel();
  }
}

void
nsDOMWorker::SuspendFeatures()
{
  nsAutoTArray<nsRefPtr<nsDOMWorkerFeature>, 20> features;
  {
    MutexAutoLock lock(mLock);

    // We don't really have to worry about overflow here because the only way
    // to do this is through recursive script loading, which uses the stack. We
    // would exceed our stack limit long before this counter.
    NS_ASSERTION(mFeatureSuspendDepth < PR_UINT32_MAX, "Shouldn't happen!");
    if (++mFeatureSuspendDepth != 1) {
      // Allow nested suspending of timeouts.
      return;
    }

#ifdef DEBUG
    nsRefPtr<nsDOMWorkerFeature>* newFeatures =
#endif
    features.AppendElements(mFeatures);
    NS_WARN_IF_FALSE(newFeatures, "Out of memory!");
  }

  PRUint32 count = features.Length();
  for (PRUint32 i = 0; i < count; i++) {
    features[i]->Suspend();
  }
}

void
nsDOMWorker::ResumeFeatures()
{
  nsAutoTArray<nsRefPtr<nsDOMWorkerFeature>, 20> features;
  {
    MutexAutoLock lock(mLock);

    NS_ASSERTION(mFeatureSuspendDepth > 0, "Shouldn't happen!");
    if (--mFeatureSuspendDepth != 0) {
      return;
    }

    features.AppendElements(mFeatures);
  }

  PRUint32 count = features.Length();
  for (PRUint32 i = 0; i < count; i++) {
    features[i]->Resume();
  }
}

void
nsDOMWorker::SetPrincipal(nsIPrincipal* aPrincipal)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(aPrincipal, "Null pointer!");

  mPrincipal = aPrincipal;
}

nsresult
nsDOMWorker::SetBaseURI(nsIURI* aURI)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(aURI, "Don't hand me a null pointer!");

  mBaseURI = aURI;

  nsCOMPtr<nsIURL> url(do_QueryInterface(aURI));
  NS_ENSURE_TRUE(url, NS_ERROR_NO_INTERFACE);

  mLocation = nsDOMWorkerLocation::NewLocation(url);
  NS_ENSURE_TRUE(mLocation, NS_ERROR_FAILURE);

  return NS_OK;
}

void
nsDOMWorker::ClearBaseURI()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  mBaseURI = nsnull;
  mLocation = nsnull;
}

nsresult
nsDOMWorker::FireCloseRunnable(PRIntervalTime aTimeoutInterval,
                               PRBool aClearQueue,
                               PRBool aFromFinalize)
{
  // Resume the worker (but not its features) if we're currently suspended. This
  // should only ever happen if we are being called from Cancel (page falling
  // out of bfcache or quitting) or Finalize, in which case all we really want
  // to do is unblock the waiting thread.
  PRBool wakeUp;
  {
    MutexAutoLock lock(mLock);
    NS_ASSERTION(mExpirationTime == 0,
                 "Close runnable should not be scheduled already!");

    if ((wakeUp = mSuspended)) {
      NS_ASSERTION(mStatus == eCanceled ||
                   (mStatus == eTerminated && aFromFinalize),
                   "How can this happen otherwise?!");
      mSuspended = PR_FALSE;
    }
  }

  if (wakeUp) {
    ReentrantMonitorAutoEnter mon(mPool->GetReentrantMonitor());
    mon.NotifyAll();
  }

  nsRefPtr<nsDOMWorkerEvent> event = new nsDOMWorkerEvent();
  NS_ENSURE_TRUE(event, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv =
    event->InitEvent(NS_LITERAL_STRING("close"), PR_FALSE, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  nsRefPtr<nsDOMFireEventRunnable> runnable =
    new nsDOMFireEventRunnable(this, event, PR_TRUE);
  NS_ENSURE_TRUE(runnable, NS_ERROR_OUT_OF_MEMORY);

  // Our worker has been collected and we want to keep the inner scope alive,
  // so pass that along in the runnable.
  if (aFromFinalize) {
    // Make sure that our scope wrapped native exists here, but if the worker
    // script failed to compile then it will be null already.
    if (mGlobal) {
      NS_ASSERTION(mScopeWN, "This shouldn't be null!");
    }
    runnable->ReplaceWrappedNative(mScopeWN);
  }

  return nsDOMThreadService::get()->Dispatch(this, runnable, aTimeoutInterval,
                                             aClearQueue);
}

nsresult
nsDOMWorker::Close()
{
  {
    MutexAutoLock lock(mLock);
    NS_ASSERTION(mStatus != eKilled, "This should be impossible!");
    if (mStatus != eRunning) {
      return NS_OK;
    }
    mStatus = eClosed;
  }

  nsresult rv = FireCloseRunnable(PR_INTERVAL_NO_TIMEOUT, PR_FALSE, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
nsDOMWorker::TerminateInternal(PRBool aFromFinalize)
{
  {
    MutexAutoLock lock(mLock);
#ifdef DEBUG
    if (!aFromFinalize) {
      NS_ASSERTION(mStatus != eCanceled, "Shouldn't be able to get here!");
    }
#endif

    if (mStatus == eRunning) {
      // This is the beginning of the close process, fire an event and prevent
      // any other close events from being generated.
      mStatus = eTerminated;
    }
    else {
      if (mStatus == eClosed) {
        // The worker was previously closed which means that an expiration time
        // might not be set. Setting the status to eTerminated will force the
        // worker to jump to its close handler.
        mStatus = eTerminated;
      }
      // No need to fire another close handler, it has already been done.
      return NS_OK;
    }
  }

  nsresult rv = FireCloseRunnable(PR_INTERVAL_NO_TIMEOUT, PR_TRUE,
                                  aFromFinalize);
  if (rv == NS_ERROR_ILLEGAL_DURING_SHUTDOWN) {
    return rv;
  }

  // Warn about other kinds of failures.
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

already_AddRefed<nsDOMWorker>
nsDOMWorker::GetParent()
{
  nsRefPtr<nsDOMWorker> parent(mParent);
  return parent.forget();
}

void
nsDOMWorker::SetExpirationTime(PRIntervalTime aExpirationTime)
{
  {
    MutexAutoLock lock(mLock);

    NS_ASSERTION(mStatus != eRunning && mStatus != eKilled, "Bad status!");
    NS_ASSERTION(!mExpirationTime || mExpirationTime == PR_INTERVAL_NO_TIMEOUT,
                 "Overwriting a timeout that was previously set!");

    mExpirationTime = aExpirationTime;
  }
}

#ifdef DEBUG
PRIntervalTime
nsDOMWorker::GetExpirationTime()
{
  MutexAutoLock lock(mLock);
  return mExpirationTime;
}
#endif

// static
JSObject*
nsDOMWorker::ReadStructuredClone(JSContext* aCx,
                                 JSStructuredCloneReader* aReader,
                                 uint32 aTag,
                                 uint32 aData,
                                 void* aClosure)
{
  NS_ASSERTION(aCx, "Null context!");
  NS_ASSERTION(aReader, "Null reader!");
  NS_ASSERTION(!aClosure, "Shouldn't have a closure here!");

  if (aTag == DOMWORKER_SCTAG_WRAPPEDNATIVE) {
    NS_ASSERTION(!aData, "Huh?");

    nsISupports* wrappedNative;
    if (JS_ReadBytes(aReader, &wrappedNative, sizeof(wrappedNative))) {
      NS_ASSERTION(wrappedNative, "Null pointer?!");

      JSObject* global = JS_GetGlobalForObject(aCx, JS_GetScopeChain(aCx));
      if (global) {
        jsval val;
        nsCOMPtr<nsIXPConnectJSObjectHolder> wrapper;
        if (NS_SUCCEEDED(nsContentUtils::WrapNative(aCx, global, wrappedNative,
                                                    &val,
                                                    getter_AddRefs(wrapper)))) {
          return JSVAL_TO_OBJECT(val);
        }
      }
    }
  }

  // Something failed above, try using the runtime callbacks instead.
  const JSStructuredCloneCallbacks* runtimeCallbacks =
    aCx->runtime->structuredCloneCallbacks;
  if (runtimeCallbacks) {
    return runtimeCallbacks->read(aCx, aReader, aTag, aData, nsnull);
  }

  // We can't handle this object, throw an exception if one hasn't been thrown
  // already.
  if (!JS_IsExceptionPending(aCx)) {
    nsDOMClassInfo::ThrowJSException(aCx, NS_ERROR_DOM_DATA_CLONE_ERR);
  }
  return nsnull;
}

PRBool
nsDOMWorker::QueueSuspendedRunnable(nsIRunnable* aRunnable)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  return mQueuedRunnables.AppendElement(aRunnable) ? PR_TRUE : PR_FALSE;
}

NS_IMETHODIMP
nsDOMWorker::RemoveEventListener(const nsAString& aType,
                                 nsIDOMEventListener* aListener,
                                 PRBool aUseCapture)
{
  if (IsCanceled()) {
    return NS_OK;
  }

  return nsDOMWorkerMessageHandler::RemoveEventListener(aType, aListener,
                                                        aUseCapture);
}

NS_IMETHODIMP
nsDOMWorker::DispatchEvent(nsIDOMEvent* aEvent,
                           PRBool* _retval)
{
  {
    MutexAutoLock lock(mLock);
    if (IsCanceledNoLock()) {
      return NS_OK;
    }
    if (mStatus == eTerminated) {
      nsCOMPtr<nsIWorkerMessageEvent> messageEvent(do_QueryInterface(aEvent));
      if (messageEvent) {
        // This is a message event targeted to a terminated worker. Ignore it.
        return NS_OK;
      }
    }
  }

  return nsDOMWorkerMessageHandler::DispatchEvent(aEvent, _retval);
}

NS_IMETHODIMP
nsDOMWorker::AddEventListener(const nsAString& aType,
                              nsIDOMEventListener* aListener,
                              PRBool aUseCapture,
                              PRBool aWantsUntrusted,
                              PRUint8 aOptionalArgc)
{
  NS_ASSERTION(mWrappedNative, "Called after Finalize!");
  if (IsCanceled()) {
    return NS_OK;
  }

  return nsDOMWorkerMessageHandler::AddEventListener(aType, aListener,
                                                     aUseCapture,
                                                     aWantsUntrusted,
                                                     aOptionalArgc);
}

/**
 * See nsIWorker
 */
NS_IMETHODIMP
nsDOMWorker::PostMessage(/* JSObject aMessage */)
{
  {
    MutexAutoLock lock(mLock);
    // There's no reason to dispatch this message after the close handler has
    // been triggered since it will never be allowed to run.
    if (mStatus != eRunning) {
      return NS_OK;
    }
  }

  return PostMessageInternal(PR_TRUE);
}

/**
 * See nsIWorker
 */
NS_IMETHODIMP
nsDOMWorker::GetOnerror(nsIDOMEventListener** aOnerror)
{
  NS_ENSURE_ARG_POINTER(aOnerror);

  if (IsCanceled()) {
    *aOnerror = nsnull;
    return NS_OK;
  }

  nsCOMPtr<nsIDOMEventListener> listener =
    GetOnXListener(NS_LITERAL_STRING("error"));

  listener.forget(aOnerror);
  return NS_OK;
}

/**
 * See nsIWorker
 */
NS_IMETHODIMP
nsDOMWorker::SetOnerror(nsIDOMEventListener* aOnerror)
{
  NS_ASSERTION(mWrappedNative, "Called after Finalize!");
  if (IsCanceled()) {
    return NS_OK;
  }

  return SetOnXListener(NS_LITERAL_STRING("error"), aOnerror);
}

/**
 * See nsIWorker
 */
NS_IMETHODIMP
nsDOMWorker::GetOnmessage(nsIDOMEventListener** aOnmessage)
{
  NS_ENSURE_ARG_POINTER(aOnmessage);

  if (IsCanceled()) {
    *aOnmessage = nsnull;
    return NS_OK;
  }

  nsCOMPtr<nsIDOMEventListener> listener =
    GetOnXListener(NS_LITERAL_STRING("message"));

  listener.forget(aOnmessage);
  return NS_OK;
}

/**
 * See nsIWorker
 */
NS_IMETHODIMP
nsDOMWorker::SetOnmessage(nsIDOMEventListener* aOnmessage)
{
  NS_ASSERTION(mWrappedNative, "Called after Finalize!");
  if (IsCanceled()) {
    return NS_OK;
  }

  return SetOnXListener(NS_LITERAL_STRING("message"), aOnmessage);
}

NS_IMETHODIMP
nsDOMWorker::Terminate()
{
  return TerminateInternal(PR_FALSE);
}

NS_IMETHODIMP
nsDOMWorker::Notify(nsITimer* aTimer)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  Kill();
  return NS_OK;
}

NS_IMETHODIMP
nsWorkerFactory::NewChromeWorker(nsIWorker** _retval)
{
  nsresult rv;

  // Get the arguments from XPConnect.
  nsCOMPtr<nsIXPConnect> xpc;
  xpc = do_GetService(nsIXPConnect::GetCID());
  NS_ASSERTION(xpc, "Could not get XPConnect");

  nsAXPCNativeCallContext* cc;
  rv = xpc->GetCurrentNativeCallContext(&cc);
  NS_ENSURE_SUCCESS(rv, rv);

  JSContext* cx;
  rv = cc->GetJSContext(&cx);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 argc;
  rv = cc->GetArgc(&argc);
  NS_ENSURE_SUCCESS(rv, rv);

  jsval* argv;
  rv = cc->GetArgvPtr(&argv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Determine the current script global. We need it to register the worker.
  // NewChromeDOMWorker will check that we are chrome, so no access check.
  JSObject* global = JS_GetGlobalForScopeChain(cx);
  NS_ENSURE_TRUE(global, NS_ERROR_UNEXPECTED);

  // May be null if we're being called from a JSM or something.
  nsCOMPtr<nsIScriptGlobalObject> scriptGlobal =
    nsJSUtils::GetStaticScriptGlobal(cx, global);

  // Create, initialize, and return the worker.
  nsRefPtr<nsDOMWorker> chromeWorker;
  rv = nsDOMWorker::NewChromeDOMWorker(getter_AddRefs(chromeWorker));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = chromeWorker->InitializeInternal(scriptGlobal, cx, global, argc, argv);
  NS_ENSURE_SUCCESS(rv, rv);

  chromeWorker.forget(_retval);
  return NS_OK;
}

NS_IMPL_ISUPPORTS1(nsWorkerFactory, nsIWorkerFactory)

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
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
 * The Original Code is Indexed Database.
 *
 * The Initial Developer of the Original Code is
 * The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ben Turner <bent.mozilla@gmail.com>
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

#ifndef mozilla_dom_indexeddb_asyncconnectionhelper_h__
#define mozilla_dom_indexeddb_asyncconnectionhelper_h__

// Only meant to be included in IndexedDB source files, not exported.
#include "DatabaseInfo.h"
#include "IndexedDatabase.h"
#include "IDBDatabase.h"
#include "IDBRequest.h"

#include "mozIStorageProgressHandler.h"
#include "nsIRunnable.h"

#include "nsDOMEvent.h"

class mozIStorageConnection;
class nsIEventTarget;

BEGIN_INDEXEDDB_NAMESPACE

class IDBTransaction;

// A common base class for AsyncConnectionHelper and OpenDatabaseHelper that
// IDBRequest can use.
class HelperBase : public nsIRunnable
{
  friend class IDBRequest;
public:
  virtual nsresult GetResultCode() = 0;

  virtual nsresult GetSuccessResult(JSContext* aCx,
                                    jsval* aVal) = 0;

protected:
  HelperBase(IDBRequest* aRequest)
    : mRequest(aRequest)
  { }

  virtual ~HelperBase();

  /**
   * Helper to wrap a native into a jsval. Uses the global object of the request
   * to parent the native.
   */
  nsresult WrapNative(JSContext* aCx,
                      nsISupports* aNative,
                      jsval* aResult);

  /**
   * Gives the subclass a chance to release any objects that must be released
   * on the main thread, regardless of success or failure. Subclasses that
   * implement this method *MUST* call the base class implementation as well.
   */
  virtual void ReleaseMainThreadObjects();

  nsRefPtr<IDBRequest> mRequest;
};

/**
 * Must be subclassed. The subclass must implement DoDatabaseWork. It may then
 * choose to implement OnSuccess and OnError depending on the needs of the
 * subclass. If the default implementation of OnSuccess is desired then the
 * subclass can implement GetSuccessResult to properly set the result of the
 * success event. Call Dispatch to start the database operation. Must be created
 * and Dispatched from the main thread only. Target thread may not be the main
 * thread.
 */
class AsyncConnectionHelper : public HelperBase,
                              public mozIStorageProgressHandler
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIRUNNABLE
  NS_DECL_MOZISTORAGEPROGRESSHANDLER

  nsresult Dispatch(nsIEventTarget* aDatabaseThread);

  // Only for transactions!
  nsresult DispatchToTransactionPool();

  void SetError(nsresult aErrorCode)
  {
    NS_ASSERTION(NS_FAILED(aErrorCode), "Not a failure code!");
    mResultCode = aErrorCode;
  }

  static IDBTransaction* GetCurrentTransaction();

  bool HasTransaction()
  {
    return mTransaction;
  }

  nsISupports* GetSource()
  {
    return mRequest ? mRequest->Source() : nsnull;
  }

  nsresult GetResultCode()
  {
    return mResultCode;
  }

protected:
  AsyncConnectionHelper(IDBDatabase* aDatabase,
                        IDBRequest* aRequest);

  AsyncConnectionHelper(IDBTransaction* aTransaction,
                        IDBRequest* aRequest);

  virtual ~AsyncConnectionHelper();

  /**
   * This is called on the main thread after Dispatch is called but before the
   * runnable is actually dispatched to the database thread. Allows the subclass
   * to initialize itself.
   */
  virtual nsresult Init();

  /**
   * This callback is run on the database thread.
   */
  virtual nsresult DoDatabaseWork(mozIStorageConnection* aConnection) = 0;

  /**
   * This function returns the event to be dispatched at the request when
   * OnSuccess is called.  A subclass can override this to fire an event other
   * than "success" at the request.
   */
  virtual already_AddRefed<nsDOMEvent> CreateSuccessEvent();

  /**
   * This callback is run on the main thread if DoDatabaseWork returned NS_OK.
   * The default implementation fires a "success" DOM event with its target set
   * to the request. Returning anything other than NS_OK from the OnSuccess
   * callback will trigger the OnError callback.
   */
  virtual nsresult OnSuccess();

  /**
   * This callback is run on the main thread if DoDatabaseWork or OnSuccess
   * returned an error code. The default implementation fires an "error" DOM
   * event with its target set to the request.
   */
  virtual void OnError();

  /**
   * This function is called by the request on the main thread when script
   * accesses the result property of the request.
   */
  virtual nsresult GetSuccessResult(JSContext* aCx,
                                    jsval* aVal);

  /**
   * Gives the subclass a chance to release any objects that must be released
   * on the main thread, regardless of success or failure. Subclasses that
   * implement this method *MUST* call the base class implementation as well.
   */
  virtual void ReleaseMainThreadObjects();

  /**
   * Helper to make a JS array object out of an array of clone buffers.
   */
  static nsresult ConvertCloneReadInfosToArray(
                                JSContext* aCx,
                                nsTArray<StructuredCloneReadInfo>& aReadInfos,
                                jsval* aResult);

protected:
  nsRefPtr<IDBDatabase> mDatabase;
  nsRefPtr<IDBTransaction> mTransaction;

private:
  nsCOMPtr<mozIStorageProgressHandler> mOldProgressHandler;
  nsresult mResultCode;
  bool mDispatched;
};

END_INDEXEDDB_NAMESPACE

#endif // mozilla_dom_indexeddb_asyncconnectionhelper_h__

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

#include "IDBCursor.h"

#include "nsIVariant.h"

#include "jscntxt.h"
#include "mozilla/storage.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsEventDispatcher.h"
#include "nsJSUtils.h"
#include "nsThreadUtils.h"

#include "AsyncConnectionHelper.h"
#include "DatabaseInfo.h"
#include "IDBEvents.h"
#include "IDBIndex.h"
#include "IDBObjectStore.h"
#include "IDBTransaction.h"
#include "TransactionThreadPool.h"

USING_INDEXEDDB_NAMESPACE

namespace {

inline
already_AddRefed<IDBRequest>
GenerateRequest(IDBCursor* aCursor)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  IDBDatabase* database = aCursor->Transaction()->Database();
  return IDBRequest::Create(aCursor, database->ScriptContext(),
                            database->Owner(), aCursor->Transaction());
}

} // anonymous namespace

BEGIN_INDEXEDDB_NAMESPACE

class ContinueHelper : public AsyncConnectionHelper
{
public:
  ContinueHelper(IDBCursor* aCursor)
  : AsyncConnectionHelper(aCursor->mTransaction, aCursor->mRequest),
    mCursor(aCursor)
  { }

  ~ContinueHelper()
  {
    IDBObjectStore::ClearStructuredCloneBuffer(mCloneBuffer);
  }

  nsresult DoDatabaseWork(mozIStorageConnection* aConnection);
  nsresult GetSuccessResult(JSContext* aCx,
                            jsval* aVal);

  void ReleaseMainThreadObjects()
  {
    mCursor = nsnull;
    AsyncConnectionHelper::ReleaseMainThreadObjects();
  }

protected:
  virtual nsresult
  BindArgumentsToStatement(mozIStorageStatement* aStatement) = 0;

  virtual nsresult
  GatherResultsFromStatement(mozIStorageStatement* aStatement) = 0;

protected:
  nsRefPtr<IDBCursor> mCursor;
  Key mKey;
  Key mObjectKey;
  JSAutoStructuredCloneBuffer mCloneBuffer;
};

class ContinueObjectStoreHelper : public ContinueHelper
{
public:
  ContinueObjectStoreHelper(IDBCursor* aCursor)
  : ContinueHelper(aCursor)
  { }

private:
  nsresult BindArgumentsToStatement(mozIStorageStatement* aStatement);
  nsresult GatherResultsFromStatement(mozIStorageStatement* aStatement);
};

class ContinueIndexHelper : public ContinueHelper
{
public:
  ContinueIndexHelper(IDBCursor* aCursor)
  : ContinueHelper(aCursor)
  { }

private:
  nsresult BindArgumentsToStatement(mozIStorageStatement* aStatement);
  nsresult GatherResultsFromStatement(mozIStorageStatement* aStatement);
};

class ContinueIndexObjectHelper : public ContinueIndexHelper
{
public:
  ContinueIndexObjectHelper(IDBCursor* aCursor)
  : ContinueIndexHelper(aCursor)
  { }

private:
  nsresult GatherResultsFromStatement(mozIStorageStatement* aStatement);
};

END_INDEXEDDB_NAMESPACE

// static
already_AddRefed<IDBCursor>
IDBCursor::Create(IDBRequest* aRequest,
                  IDBTransaction* aTransaction,
                  IDBObjectStore* aObjectStore,
                  PRUint16 aDirection,
                  const Key& aRangeKey,
                  const nsACString& aContinueQuery,
                  const nsACString& aContinueToQuery,
                  const Key& aKey,
                  JSAutoStructuredCloneBuffer& aCloneBuffer)
{
  NS_ASSERTION(aObjectStore, "Null pointer!");
  NS_ASSERTION(!aKey.IsUnset(), "Bad key!");

  nsRefPtr<IDBCursor> cursor =
    IDBCursor::CreateCommon(aRequest, aTransaction, aObjectStore, aDirection,
                            aRangeKey, aContinueQuery, aContinueToQuery);
  NS_ASSERTION(cursor, "This shouldn't fail!");

  cursor->mObjectStore = aObjectStore;
  cursor->mType = OBJECTSTORE;
  cursor->mKey = aKey;
  cursor->mCloneBuffer.swap(aCloneBuffer);

  return cursor.forget();
}

// static
already_AddRefed<IDBCursor>
IDBCursor::Create(IDBRequest* aRequest,
                  IDBTransaction* aTransaction,
                  IDBIndex* aIndex,
                  PRUint16 aDirection,
                  const Key& aRangeKey,
                  const nsACString& aContinueQuery,
                  const nsACString& aContinueToQuery,
                  const Key& aKey,
                  const Key& aObjectKey)
{
  NS_ASSERTION(aIndex, "Null pointer!");
  NS_ASSERTION(!aKey.IsUnset(), "Bad key!");
  NS_ASSERTION(!aObjectKey.IsUnset(), "Bad key!");

  nsRefPtr<IDBCursor> cursor =
    IDBCursor::CreateCommon(aRequest, aTransaction, aIndex->ObjectStore(),
                            aDirection, aRangeKey, aContinueQuery,
                            aContinueToQuery);
  NS_ASSERTION(cursor, "This shouldn't fail!");

  cursor->mIndex = aIndex;
  cursor->mType = INDEXKEY;
  cursor->mKey = aKey,
  cursor->mObjectKey = aObjectKey;

  return cursor.forget();
}

// static
already_AddRefed<IDBCursor>
IDBCursor::Create(IDBRequest* aRequest,
                  IDBTransaction* aTransaction,
                  IDBIndex* aIndex,
                  PRUint16 aDirection,
                  const Key& aRangeKey,
                  const nsACString& aContinueQuery,
                  const nsACString& aContinueToQuery,
                  const Key& aKey,
                  const Key& aObjectKey,
                  JSAutoStructuredCloneBuffer& aCloneBuffer)
{
  NS_ASSERTION(aIndex, "Null pointer!");
  NS_ASSERTION(!aKey.IsUnset(), "Bad key!");

  nsRefPtr<IDBCursor> cursor =
    IDBCursor::CreateCommon(aRequest, aTransaction, aIndex->ObjectStore(),
                            aDirection, aRangeKey, aContinueQuery,
                            aContinueToQuery);
  NS_ASSERTION(cursor, "This shouldn't fail!");

  cursor->mObjectStore = aIndex->ObjectStore();
  cursor->mIndex = aIndex;
  cursor->mType = INDEXOBJECT;
  cursor->mKey = aKey;
  cursor->mObjectKey = aObjectKey;
  cursor->mCloneBuffer.swap(aCloneBuffer);

  return cursor.forget();
}

// static
already_AddRefed<IDBCursor>
IDBCursor::CreateCommon(IDBRequest* aRequest,
                        IDBTransaction* aTransaction,
                        IDBObjectStore* aObjectStore,
                        PRUint16 aDirection,
                        const Key& aRangeKey,
                        const nsACString& aContinueQuery,
                        const nsACString& aContinueToQuery)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(aRequest, "Null pointer!");
  NS_ASSERTION(aTransaction, "Null pointer!");
  NS_ASSERTION(aObjectStore, "Null pointer!");
  NS_ASSERTION(!aContinueQuery.IsEmpty(), "Empty query!");
  NS_ASSERTION(!aContinueToQuery.IsEmpty(), "Empty query!");

  nsRefPtr<IDBCursor> cursor = new IDBCursor();

  cursor->mRequest = aRequest;
  cursor->mTransaction = aTransaction;
  cursor->mObjectStore = aObjectStore;
  cursor->mScriptContext = aTransaction->Database()->ScriptContext();
  cursor->mOwner = aTransaction->Database()->Owner();
  cursor->mDirection = aDirection;
  cursor->mContinueQuery = aContinueQuery;
  cursor->mContinueToQuery = aContinueToQuery;
  cursor->mRangeKey = aRangeKey;

  return cursor.forget();
}

IDBCursor::IDBCursor()
: mType(OBJECTSTORE),
  mDirection(nsIIDBCursor::NEXT),
  mCachedPrimaryKey(JSVAL_VOID),
  mCachedValue(JSVAL_VOID),
  mHaveCachedPrimaryKey(false),
  mHaveCachedValue(false),
  mRooted(false),
  mContinueCalled(false),
  mHaveValue(true)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
}

IDBCursor::~IDBCursor()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (mRooted) {
    NS_DROP_JS_OBJECTS(this, IDBCursor);
  }
  IDBObjectStore::ClearStructuredCloneBuffer(mCloneBuffer);
}

NS_IMPL_CYCLE_COLLECTION_CLASS(IDBCursor)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(IDBCursor)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(mRequest,
                                                       nsIDOMEventTarget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(mTransaction,
                                                       nsIDOMEventTarget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mObjectStore)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mIndex)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mOwner)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mScriptContext)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(IDBCursor)
  NS_ASSERTION(tmp->mHaveCachedPrimaryKey ||
               JSVAL_IS_VOID(tmp->mCachedPrimaryKey),
               "Should have a cached primary key");
  NS_ASSERTION(tmp->mHaveCachedValue || JSVAL_IS_VOID(tmp->mCachedValue),
               "Should have a cached value");
  if (JSVAL_IS_GCTHING(tmp->mCachedValue)) {
    void *gcThing = JSVAL_TO_GCTHING(tmp->mCachedValue);
    NS_IMPL_CYCLE_COLLECTION_TRACE_JS_CALLBACK(gcThing, "mCachedValue")
  }
  if (JSVAL_IS_GCTHING(tmp->mCachedPrimaryKey)) {
    void *gcThing = JSVAL_TO_GCTHING(tmp->mCachedPrimaryKey);
    NS_IMPL_CYCLE_COLLECTION_TRACE_JS_CALLBACK(gcThing, "mCachedPrimaryKey")
  }
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(IDBCursor)
  // Don't unlink mObjectStore, mIndex, or mTransaction!
  if (tmp->mRooted) {
    NS_DROP_JS_OBJECTS(tmp, IDBCursor);
    tmp->mCachedPrimaryKey = JSVAL_VOID;
    tmp->mCachedValue = JSVAL_VOID;
    tmp->mHaveCachedPrimaryKey = false;
    tmp->mHaveCachedValue = false;
    tmp->mRooted = false;
    tmp->mHaveValue = false;
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mRequest)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mOwner)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mScriptContext)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(IDBCursor)
  NS_INTERFACE_MAP_ENTRY(nsIIDBCursor)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIIDBCursorWithValue, mType != INDEXKEY)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO_CONDITIONAL(IDBCursorWithValue,
                                                   mType != INDEXKEY)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO_CONDITIONAL(IDBCursor,
                                                   mType == INDEXKEY)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(IDBCursor)
NS_IMPL_CYCLE_COLLECTING_RELEASE(IDBCursor)

DOMCI_DATA(IDBCursor, IDBCursor)
DOMCI_DATA(IDBCursorWithValue, IDBCursor)

NS_IMETHODIMP
IDBCursor::GetDirection(PRUint16* aDirection)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  *aDirection = mDirection;
  return NS_OK;
}

NS_IMETHODIMP
IDBCursor::GetSource(nsISupports** aSource)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  return mType == OBJECTSTORE ?
         CallQueryInterface(mObjectStore, aSource) :
         CallQueryInterface(mIndex, aSource);
}

NS_IMETHODIMP
IDBCursor::GetKey(nsIVariant** aKey)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mCachedKey) {
    nsresult rv;
    nsCOMPtr<nsIWritableVariant> variant =
      do_CreateInstance(NS_VARIANT_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    NS_ASSERTION(!mKey.IsUnset() || !mHaveValue, "Bad key!");

    if (!mHaveValue) {
      rv = variant->SetAsVoid();
    }
    else if (mKey.IsString()) {
      rv = variant->SetAsAString(mKey.StringValue());
    }
    else if (mKey.IsInt()) {
      rv = variant->SetAsInt64(mKey.IntValue());
    }
    else {
      NS_NOTREACHED("Huh?!");
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    rv = variant->SetWritable(PR_FALSE);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    nsIWritableVariant* result;
    variant.forget(&result);

    mCachedKey = dont_AddRef(static_cast<nsIVariant*>(result));
  }

  nsCOMPtr<nsIVariant> result(mCachedKey);
  result.forget(aKey);
  return NS_OK;
}

NS_IMETHODIMP
IDBCursor::GetPrimaryKey(JSContext* aCx,
                         jsval* aValue)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mHaveValue) {
    *aValue = JSVAL_VOID;
    return NS_OK;
  }

  if (!mHaveCachedPrimaryKey) {
    if (!mRooted) {
      NS_HOLD_JS_OBJECTS(this, IDBCursor);
      mRooted = true;
    }

    JSAutoRequest ar(aCx);

    NS_ASSERTION(mType == OBJECTSTORE ? !mKey.IsUnset() :
                                        !mObjectKey.IsUnset(), "Bad key!");

    nsresult rv =
      IDBObjectStore::GetJSValFromKey(mType == OBJECTSTORE ? mKey : mObjectKey,
                                      aCx, &mCachedPrimaryKey);
    NS_ENSURE_SUCCESS(rv, rv);

    mHaveCachedPrimaryKey = true;
  }

  *aValue = mCachedPrimaryKey;
  return NS_OK;
}

NS_IMETHODIMP
IDBCursor::GetValue(JSContext* aCx,
                    jsval* aValue)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(mType != INDEXKEY, "GetValue shouldn't exist on index keys");

  if (!mHaveValue) {
    *aValue = JSVAL_VOID;
    return NS_OK;
  }

  if (!mHaveCachedValue) {
    if (!mRooted) {
      NS_HOLD_JS_OBJECTS(this, IDBCursor);
      mRooted = true;
    }

    if (!IDBObjectStore::DeserializeValue(aCx, mCloneBuffer, &mCachedValue)) {
      mCachedValue = JSVAL_VOID;
      return NS_ERROR_DOM_DATA_CLONE_ERR;
    }

    mCloneBuffer.clear();
    mHaveCachedValue = true;
  }

  *aValue = mCachedValue;
  return NS_OK;
}

NS_IMETHODIMP
IDBCursor::Continue(const jsval &aKey,
                    JSContext* aCx)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!mHaveValue || mContinueCalled) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR;
  }

  Key key;
  nsresult rv = IDBObjectStore::GetKeyFromJSVal(aKey, aCx, key);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!key.IsUnset()) {
    switch (mDirection) {
      case nsIIDBCursor::NEXT:
      case nsIIDBCursor::NEXT_NO_DUPLICATE:
        if (key <= mKey) {
          return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
        }
        break;

      case nsIIDBCursor::PREV:
      case nsIIDBCursor::PREV_NO_DUPLICATE:
        if (key >= mKey) {
          return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
        }
        break;

      default:
        NS_NOTREACHED("Unknown direction type!");
    }
  }

  mContinueToKey = key;

#ifdef DEBUG
  {
    PRUint16 readyState;
    if (NS_FAILED(mRequest->GetReadyState(&readyState))) {
      NS_ERROR("This should never fail!");
    }
    NS_ASSERTION(readyState == nsIIDBRequest::DONE, "Should be DONE!");
  }
#endif

  mRequest->Reset();

  nsRefPtr<ContinueHelper> helper;
  switch (mType) {
    case OBJECTSTORE:
      helper = new ContinueObjectStoreHelper(this);
      break;

    case INDEXKEY:
      helper = new ContinueIndexHelper(this);
      break;

    case INDEXOBJECT:
      helper = new ContinueIndexObjectHelper(this);
      break;

    default:
      NS_NOTREACHED("Unknown cursor type!");
  }

  rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mContinueCalled = true;
  return NS_OK;
}

NS_IMETHODIMP
IDBCursor::Update(const jsval& aValue,
                  JSContext* aCx,
                  nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!mTransaction->IsWriteAllowed()) {
    return NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR;
  }

  if (!mHaveValue || mType == INDEXKEY) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR;
  }

  NS_ASSERTION(mObjectStore, "This cannot be null!");
  NS_ASSERTION(!mKey.IsUnset() , "Bad key!");
  NS_ASSERTION(mType != INDEXOBJECT || !mObjectKey.IsUnset(), "Bad key!");

  nsresult rv;

  JSAutoRequest ar(aCx);

  const Key& objectKey = (mType == OBJECTSTORE) ? mKey : mObjectKey;

  if (!mObjectStore->KeyPath().IsEmpty()) {
    // This has to be an object.
    if (JSVAL_IS_PRIMITIVE(aValue)) {
      return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
    }

    // Make sure the object given has the correct keyPath value set on it.
    const nsString& keyPath = mObjectStore->KeyPath();

    jsval prop;
    JSBool ok = JS_GetUCProperty(aCx, JSVAL_TO_OBJECT(aValue),
                                 reinterpret_cast<const jschar*>(keyPath.get()),
                                 keyPath.Length(), &prop);
    NS_ENSURE_TRUE(ok, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    Key key;
    rv = IDBObjectStore::GetKeyFromJSVal(prop, aCx, key);
    if (NS_FAILED(rv)) {
      return rv;
    }

    if (key != objectKey) {
      return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
    }

    return mObjectStore->Put(aValue, JSVAL_VOID, aCx, 0, _retval);
  }

  jsval keyVal;
  rv = IDBObjectStore::GetJSValFromKey(objectKey, aCx, &keyVal);
  NS_ENSURE_SUCCESS(rv, rv);

  return mObjectStore->Put(aValue, keyVal, aCx, 1, _retval);
}

NS_IMETHODIMP
IDBCursor::Delete(JSContext* aCx,
                  nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!mTransaction->IsWriteAllowed()) {
    return NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR;
  }

  if (!mHaveValue || mType == INDEXKEY) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR;
  }

  NS_ASSERTION(mObjectStore, "This cannot be null!");
  NS_ASSERTION(!mKey.IsUnset() , "Bad key!");

  const Key& objectKey = (mType == OBJECTSTORE) ? mKey : mObjectKey;

  jsval key;
  nsresult rv = IDBObjectStore::GetJSValFromKey(objectKey, aCx, &key);
  NS_ENSURE_SUCCESS(rv, rv);

  return mObjectStore->Delete(key, aCx, _retval);
}

nsresult
ContinueHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  // We need to pick a query based on whether or not the cursor's mContinueToKey
  // is set. If it is unset then othing was passed to continue so we'll grab the
  // next item in the database that is greater than (less than, if we're running
  // a PREV cursor) the current key. If it is set then a key was passed to
  // continue so we'll grab the next item in the database that is greater than
  // (less than, if we're running a PREV cursor) or equal to the key that was
  // specified.

  const nsCString& query = mCursor->mContinueToKey.IsUnset() ?
                           mCursor->mContinueQuery :
                           mCursor->mContinueToQuery;
  NS_ASSERTION(!query.IsEmpty(), "Bad query!");

  nsCOMPtr<mozIStorageStatement> stmt = mTransaction->GetCachedStatement(query);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = BindArgumentsToStatement(stmt);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  PRBool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (hasResult) {
    rv = GatherResultsFromStatement(stmt);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }
  else {
    mKey = Key::UNSETKEY;
  }

  return NS_OK;
}

nsresult
ContinueHelper::GetSuccessResult(JSContext* aCx,
                                 jsval* aVal)
{
  // Remove cached stuff from last time.
  mCursor->mCachedKey = nsnull;
  mCursor->mCachedValue = JSVAL_VOID;
  mCursor->mCachedPrimaryKey = JSVAL_VOID;
  mCursor->mHaveCachedValue = false;
  mCursor->mHaveCachedPrimaryKey = false;
  mCursor->mContinueCalled = false;

  if (mKey.IsUnset()) {
    mCursor->mHaveValue = false;
    *aVal = JSVAL_VOID;
  }
  else {
    NS_ASSERTION(mCursor->mType == IDBCursor::OBJECTSTORE ||
                 !mObjectKey.IsUnset(), "Bad key!");

    // Set new values.
    mCursor->mKey = mKey;
    mCursor->mObjectKey = mObjectKey;
    mCursor->mContinueToKey = Key::UNSETKEY;

    mCursor->mCloneBuffer.swap(mCloneBuffer);
    mCloneBuffer.clear();

    nsresult rv = WrapNative(aCx, mCursor, aVal);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
ContinueObjectStoreHelper::BindArgumentsToStatement(
                                               mozIStorageStatement* aStatement)
{
  // Bind object store id.
  nsresult rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("id"),
                                            mCursor->mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_NAMED_LITERAL_CSTRING(currentKeyName, "current_key");
  NS_NAMED_LITERAL_CSTRING(rangeKeyName, "range_key");

  // Bind current key.
  const Key& currentKey = mCursor->mContinueToKey.IsUnset() ?
                          mCursor->mKey :
                          mCursor->mContinueToKey;

  if (currentKey.IsString()) {
    rv = aStatement->BindStringByName(currentKeyName, currentKey.StringValue());
  }
  else if (currentKey.IsInt()) {
    rv = aStatement->BindInt64ByName(currentKeyName, currentKey.IntValue());
  }
  else {
    NS_NOTREACHED("Bad key!");
  }
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  // Bind range key if it is specified.
  const Key& rangeKey = mCursor->mRangeKey;

  if (!rangeKey.IsUnset()) {
    if (rangeKey.IsString()) {
      rv = aStatement->BindStringByName(rangeKeyName, rangeKey.StringValue());
    }
    else if (rangeKey.IsInt()) {
      rv = aStatement->BindInt64ByName(rangeKeyName, rangeKey.IntValue());
    }
    else {
      NS_NOTREACHED("Bad key!");
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  return NS_OK;
}

nsresult
ContinueObjectStoreHelper::GatherResultsFromStatement(
                                               mozIStorageStatement* aStatement)
{
  // Figure out what kind of key we have next.
  PRInt32 keyType;
  nsresult rv = aStatement->GetTypeOfIndex(0, &keyType);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (keyType == mozIStorageStatement::VALUE_TYPE_INTEGER) {
    mKey = aStatement->AsInt64(0);
  }
  else if (keyType == mozIStorageStatement::VALUE_TYPE_TEXT) {
    rv = aStatement->GetString(0, mKey.ToString());
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }
  else {
    NS_NOTREACHED("Bad SQLite type!");
  }

  rv = IDBObjectStore::GetStructuredCloneDataFromStatement(aStatement, 1,
                                                           mCloneBuffer);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
ContinueIndexHelper::BindArgumentsToStatement(mozIStorageStatement* aStatement)
{
  // Bind index id.
  nsresult rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("id"),
                                            mCursor->mIndex->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_NAMED_LITERAL_CSTRING(currentKeyName, "current_key");

  // Bind current key.
  const Key& currentKey = mCursor->mContinueToKey.IsUnset() ?
                          mCursor->mKey :
                          mCursor->mContinueToKey;

  if (currentKey.IsString()) {
    rv = aStatement->BindStringByName(currentKeyName, currentKey.StringValue());
  }
  else if (currentKey.IsInt()) {
    rv = aStatement->BindInt64ByName(currentKeyName, currentKey.IntValue());
  }
  else {
    NS_NOTREACHED("Bad key!");
  }
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  // Bind range key if it is specified.
  if (!mCursor->mRangeKey.IsUnset()) {
    NS_NAMED_LITERAL_CSTRING(rangeKeyName, "range_key");
    if (mCursor->mRangeKey.IsString()) {
      rv = aStatement->BindStringByName(rangeKeyName,
                                        mCursor->mRangeKey.StringValue());
    }
    else if (mCursor->mRangeKey.IsInt()) {
      rv = aStatement->BindInt64ByName(rangeKeyName,
                                       mCursor->mRangeKey.IntValue());
    }
    else {
      NS_NOTREACHED("Bad key!");
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  // Bind object key if duplicates are allowed and we're not continuing to a
  // specific key.
  if ((mCursor->mDirection == nsIIDBCursor::NEXT ||
       mCursor->mDirection == nsIIDBCursor::PREV) &&
       mCursor->mContinueToKey.IsUnset()) {
    NS_ASSERTION(!mCursor->mObjectKey.IsUnset(), "Bad key!");

    NS_NAMED_LITERAL_CSTRING(objectKeyName, "object_key");
    if (mCursor->mObjectKey.IsString()) {
      rv = aStatement->BindStringByName(objectKeyName,
                                        mCursor->mObjectKey.StringValue());
    }
    else if (mCursor->mObjectKey.IsInt()) {
      rv = aStatement->BindInt64ByName(objectKeyName,
                                       mCursor->mObjectKey.IntValue());
    }
    else {
      NS_NOTREACHED("Bad key!");
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  return NS_OK;
}

nsresult
ContinueIndexHelper::GatherResultsFromStatement(
                                               mozIStorageStatement* aStatement)
{
  PRInt32 keyType;
  nsresult rv = aStatement->GetTypeOfIndex(0, &keyType);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ASSERTION(keyType == mozIStorageStatement::VALUE_TYPE_INTEGER ||
               keyType == mozIStorageStatement::VALUE_TYPE_TEXT,
               "Bad key type!");

  if (keyType == mozIStorageStatement::VALUE_TYPE_INTEGER) {
    mKey = aStatement->AsInt64(0);
  }
  else if (keyType == mozIStorageStatement::VALUE_TYPE_TEXT) {
    rv = aStatement->GetString(0, mKey.ToString());
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }
  else {
    NS_NOTREACHED("Bad SQLite type!");
  }

  rv = aStatement->GetTypeOfIndex(1, &keyType);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ASSERTION(keyType == mozIStorageStatement::VALUE_TYPE_INTEGER ||
               keyType == mozIStorageStatement::VALUE_TYPE_TEXT,
               "Bad key type!");

  if (keyType == mozIStorageStatement::VALUE_TYPE_INTEGER) {
    mObjectKey = aStatement->AsInt64(1);
  }
  else if (keyType == mozIStorageStatement::VALUE_TYPE_TEXT) {
    rv = aStatement->GetString(1, mObjectKey.ToString());
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }
  else {
    NS_NOTREACHED("Bad SQLite type!");
  }

  return NS_OK;
}

nsresult
ContinueIndexObjectHelper::GatherResultsFromStatement(
                                               mozIStorageStatement* aStatement)
{
  PRInt32 keyType;
  nsresult rv = aStatement->GetTypeOfIndex(0, &keyType);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ASSERTION(keyType == mozIStorageStatement::VALUE_TYPE_INTEGER ||
               keyType == mozIStorageStatement::VALUE_TYPE_TEXT,
               "Bad key type!");

  if (keyType == mozIStorageStatement::VALUE_TYPE_INTEGER) {
    mKey = aStatement->AsInt64(0);
  }
  else if (keyType == mozIStorageStatement::VALUE_TYPE_TEXT) {
    rv = aStatement->GetString(0, mKey.ToString());
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }
  else {
    NS_NOTREACHED("Bad SQLite type!");
  }

  rv = aStatement->GetTypeOfIndex(1, &keyType);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ASSERTION(keyType == mozIStorageStatement::VALUE_TYPE_INTEGER ||
               keyType == mozIStorageStatement::VALUE_TYPE_TEXT,
               "Bad key type!");

  if (keyType == mozIStorageStatement::VALUE_TYPE_INTEGER) {
    mObjectKey = aStatement->AsInt64(1);
  }
  else if (keyType == mozIStorageStatement::VALUE_TYPE_TEXT) {
    rv = aStatement->GetString(1, mObjectKey.ToString());
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }
  else {
    NS_NOTREACHED("Bad SQLite type!");
  }

  rv = IDBObjectStore::GetStructuredCloneDataFromStatement(aStatement, 2,
                                                           mCloneBuffer);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

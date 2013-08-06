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

#include "IDBObjectStore.h"

#include "nsIJSContextStack.h"
#include "nsIVariant.h"

#include "jscntxt.h"
#include "mozilla/storage.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsEventDispatcher.h"
#include "nsJSUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"

#include "AsyncConnectionHelper.h"
#include "IDBCursor.h"
#include "IDBEvents.h"
#include "IDBIndex.h"
#include "IDBKeyRange.h"
#include "IDBTransaction.h"
#include "DatabaseInfo.h"

USING_INDEXEDDB_NAMESPACE

namespace {

class AddHelper : public AsyncConnectionHelper
{
public:
  AddHelper(IDBTransaction* aTransaction,
            IDBRequest* aRequest,
            IDBObjectStore* aObjectStore,
            JSAutoStructuredCloneBuffer& aCloneBuffer,
            const Key& aKey,
            bool aOverwrite,
            nsTArray<IndexUpdateInfo>& aIndexUpdateInfo)
  : AsyncConnectionHelper(aTransaction, aRequest), mObjectStore(aObjectStore),
    mKey(aKey), mOverwrite(aOverwrite)
  {
    mCloneBuffer.swap(aCloneBuffer);
    mIndexUpdateInfo.SwapElements(aIndexUpdateInfo);
  }

  ~AddHelper()
  {
    IDBObjectStore::ClearStructuredCloneBuffer(mCloneBuffer);
  }

  nsresult DoDatabaseWork(mozIStorageConnection* aConnection);
  nsresult GetSuccessResult(JSContext* aCx,
                            jsval* aVal);

  void ReleaseMainThreadObjects()
  {
    mObjectStore = nsnull;
    IDBObjectStore::ClearStructuredCloneBuffer(mCloneBuffer);
    AsyncConnectionHelper::ReleaseMainThreadObjects();
  }

  nsresult ModifyValueForNewKey();
  nsresult UpdateIndexes(mozIStorageConnection* aConnection,
                         PRInt64 aObjectDataId);

private:
  // In-params.
  nsRefPtr<IDBObjectStore> mObjectStore;

  // These may change in the autoincrement case.
  JSAutoStructuredCloneBuffer mCloneBuffer;
  Key mKey;
  const bool mOverwrite;
  nsTArray<IndexUpdateInfo> mIndexUpdateInfo;
};

class GetHelper : public AsyncConnectionHelper
{
public:
  GetHelper(IDBTransaction* aTransaction,
            IDBRequest* aRequest,
            IDBObjectStore* aObjectStore,
            const Key& aKey)
  : AsyncConnectionHelper(aTransaction, aRequest), mObjectStore(aObjectStore),
    mKey(aKey)
  { }

  ~GetHelper()
  {
    IDBObjectStore::ClearStructuredCloneBuffer(mCloneBuffer);
  }

  nsresult DoDatabaseWork(mozIStorageConnection* aConnection);
  nsresult GetSuccessResult(JSContext* aCx,
                            jsval* aVal);

  void ReleaseMainThreadObjects()
  {
    mObjectStore = nsnull;
    IDBObjectStore::ClearStructuredCloneBuffer(mCloneBuffer);
    AsyncConnectionHelper::ReleaseMainThreadObjects();
  }

protected:
  // In-params.
  nsRefPtr<IDBObjectStore> mObjectStore;
  const Key mKey;

private:
  // Out-params.
  JSAutoStructuredCloneBuffer mCloneBuffer;
};

class DeleteHelper : public GetHelper
{
public:
  DeleteHelper(IDBTransaction* aTransaction,
               IDBRequest* aRequest,
               IDBObjectStore* aObjectStore,
               const Key& aKey)
  : GetHelper(aTransaction, aRequest, aObjectStore, aKey)
  { }

  nsresult DoDatabaseWork(mozIStorageConnection* aConnection);
  nsresult GetSuccessResult(JSContext* aCx,
                            jsval* aVal);
};

class ClearHelper : public AsyncConnectionHelper
{
public:
  ClearHelper(IDBTransaction* aTransaction,
              IDBRequest* aRequest,
              IDBObjectStore* aObjectStore)
  : AsyncConnectionHelper(aTransaction, aRequest), mObjectStore(aObjectStore)
  { }

  nsresult DoDatabaseWork(mozIStorageConnection* aConnection);

  void ReleaseMainThreadObjects()
  {
    mObjectStore = nsnull;
    AsyncConnectionHelper::ReleaseMainThreadObjects();
  }

protected:
  // In-params.
  nsRefPtr<IDBObjectStore> mObjectStore;
};

class OpenCursorHelper : public AsyncConnectionHelper
{
public:
  OpenCursorHelper(IDBTransaction* aTransaction,
                   IDBRequest* aRequest,
                   IDBObjectStore* aObjectStore,
                   const Key& aLowerKey,
                   const Key& aUpperKey,
                   PRBool aLowerOpen,
                   PRBool aUpperOpen,
                   PRUint16 aDirection)
  : AsyncConnectionHelper(aTransaction, aRequest), mObjectStore(aObjectStore),
    mLowerKey(aLowerKey), mUpperKey(aUpperKey), mLowerOpen(aLowerOpen),
    mUpperOpen(aUpperOpen), mDirection(aDirection)
  { }

  ~OpenCursorHelper()
  {
    IDBObjectStore::ClearStructuredCloneBuffer(mCloneBuffer);
  }

  nsresult DoDatabaseWork(mozIStorageConnection* aConnection);
  nsresult GetSuccessResult(JSContext* aCx,
                            jsval* aVal);

  void ReleaseMainThreadObjects()
  {
    mObjectStore = nsnull;
    IDBObjectStore::ClearStructuredCloneBuffer(mCloneBuffer);
    AsyncConnectionHelper::ReleaseMainThreadObjects();
  }

private:
  // In-params.
  nsRefPtr<IDBObjectStore> mObjectStore;
  const Key mLowerKey;
  const Key mUpperKey;
  const PRPackedBool mLowerOpen;
  const PRPackedBool mUpperOpen;
  const PRUint16 mDirection;

  // Out-params.
  Key mKey;
  JSAutoStructuredCloneBuffer mCloneBuffer;
  nsCString mContinueQuery;
  nsCString mContinueToQuery;
  Key mRangeKey;
};

class CreateIndexHelper : public AsyncConnectionHelper
{
public:
  CreateIndexHelper(IDBTransaction* aTransaction,
                    IDBIndex* aIndex)
  : AsyncConnectionHelper(aTransaction, nsnull), mIndex(aIndex)
  { }

  nsresult DoDatabaseWork(mozIStorageConnection* aConnection);

  nsresult OnSuccess()
  {
    return NS_OK;
  }

  void OnError()
  {
    NS_ASSERTION(mTransaction->IsAborted(), "How else can this fail?!");
  }

  void ReleaseMainThreadObjects()
  {
    mIndex = nsnull;
    AsyncConnectionHelper::ReleaseMainThreadObjects();
  }

private:
  nsresult InsertDataFromObjectStore(mozIStorageConnection* aConnection);

  // In-params.
  nsRefPtr<IDBIndex> mIndex;
};

class DeleteIndexHelper : public AsyncConnectionHelper
{
public:
  DeleteIndexHelper(IDBTransaction* aTransaction,
                    const nsAString& aName,
                    IDBObjectStore* aObjectStore)
  : AsyncConnectionHelper(aTransaction, nsnull), mName(aName),
    mObjectStore(aObjectStore)
  { }

  nsresult DoDatabaseWork(mozIStorageConnection* aConnection);

  nsresult OnSuccess()
  {
    return NS_OK;
  }

  void OnError()
  {
    NS_ASSERTION(mTransaction->IsAborted(), "How else can this fail?!");
  }

  void ReleaseMainThreadObjects()
  {
    mObjectStore = nsnull;
    AsyncConnectionHelper::ReleaseMainThreadObjects();
  }

private:
  // In-params
  nsString mName;
  nsRefPtr<IDBObjectStore> mObjectStore;
};

class GetAllHelper : public AsyncConnectionHelper
{
public:
  GetAllHelper(IDBTransaction* aTransaction,
               IDBRequest* aRequest,
               IDBObjectStore* aObjectStore,
               const Key& aLowerKey,
               const Key& aUpperKey,
               const PRBool aLowerOpen,
               const PRBool aUpperOpen,
               const PRUint32 aLimit)
  : AsyncConnectionHelper(aTransaction, aRequest), mObjectStore(aObjectStore),
    mLowerKey(aLowerKey), mUpperKey(aUpperKey), mLowerOpen(aLowerOpen),
    mUpperOpen(aUpperOpen), mLimit(aLimit)
  { }

  ~GetAllHelper()
  {
    for (PRUint32 index = 0; index < mCloneBuffers.Length(); index++) {
      IDBObjectStore::ClearStructuredCloneBuffer(mCloneBuffers[index]);
    }
  }

  nsresult DoDatabaseWork(mozIStorageConnection* aConnection);
  nsresult GetSuccessResult(JSContext* aCx,
                            jsval* aVal);

  void ReleaseMainThreadObjects()
  {
    mObjectStore = nsnull;
    for (PRUint32 index = 0; index < mCloneBuffers.Length(); index++) {
      IDBObjectStore::ClearStructuredCloneBuffer(mCloneBuffers[index]);
    }
    AsyncConnectionHelper::ReleaseMainThreadObjects();
  }

protected:
  // In-params.
  nsRefPtr<IDBObjectStore> mObjectStore;
  const Key mLowerKey;
  const Key mUpperKey;
  const PRPackedBool mLowerOpen;
  const PRPackedBool mUpperOpen;
  const PRUint32 mLimit;

private:
  // Out-params.
  nsTArray<JSAutoStructuredCloneBuffer> mCloneBuffers;
};

NS_STACK_CLASS
class AutoRemoveIndex
{
public:
  AutoRemoveIndex(PRUint32 aDatabaseId,
                  const nsAString& aObjectStoreName,
                  const nsAString& aIndexName)
  : mDatabaseId(aDatabaseId), mObjectStoreName(aObjectStoreName),
    mIndexName(aIndexName)
  { }

  ~AutoRemoveIndex()
  {
    if (mDatabaseId) {
      ObjectStoreInfo* info;
      if (ObjectStoreInfo::Get(mDatabaseId, mObjectStoreName, &info)) {
        for (PRUint32 index = 0; index < info->indexes.Length(); index++) {
          if (info->indexes[index].name == mIndexName) {
            info->indexes.RemoveElementAt(index);
            break;
          }
        }
      }
    }
  }

  void forget()
  {
    mDatabaseId = 0;
  }

private:
  PRUint32 mDatabaseId;
  nsString mObjectStoreName;
  nsString mIndexName;
};

inline
nsresult
GetKeyFromObject(JSContext* aCx,
                 JSObject* aObj,
                 const nsString& aKeyPath,
                 Key& aKey)
{
  NS_PRECONDITION(aCx && aObj, "Null pointers!");
  NS_ASSERTION(!aKeyPath.IsVoid(), "This will explode!");

  const jschar* keyPathChars = reinterpret_cast<const jschar*>(aKeyPath.get());
  const size_t keyPathLen = aKeyPath.Length();

  jsval key;
  JSBool ok = JS_GetUCProperty(aCx, aObj, keyPathChars, keyPathLen, &key);
  NS_ENSURE_TRUE(ok, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsresult rv = IDBObjectStore::GetKeyFromJSVal(key, aCx, aKey);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

inline
already_AddRefed<IDBRequest>
GenerateRequest(IDBObjectStore* aObjectStore)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  IDBDatabase* database = aObjectStore->Transaction()->Database();
  return IDBRequest::Create(aObjectStore, database->ScriptContext(),
                            database->Owner(), aObjectStore->Transaction());
}

} // anonymous namespace

// static
already_AddRefed<IDBObjectStore>
IDBObjectStore::Create(IDBTransaction* aTransaction,
                       const ObjectStoreInfo* aStoreInfo)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsRefPtr<IDBObjectStore> objectStore = new IDBObjectStore();

  objectStore->mScriptContext = aTransaction->Database()->ScriptContext();
  objectStore->mOwner = aTransaction->Database()->Owner();

  objectStore->mTransaction = aTransaction;
  objectStore->mName = aStoreInfo->name;
  objectStore->mId = aStoreInfo->id;
  objectStore->mKeyPath = aStoreInfo->keyPath;
  objectStore->mAutoIncrement = aStoreInfo->autoIncrement;
  objectStore->mDatabaseId = aStoreInfo->databaseId;

  return objectStore.forget();
}

// static
nsresult
IDBObjectStore::GetKeyFromVariant(nsIVariant* aKeyVariant,
                                  Key& aKey)
{
  if (!aKeyVariant) {
    aKey = Key::UNSETKEY;
    return NS_OK;
  }

  PRUint16 type;
  nsresult rv = aKeyVariant->GetDataType(&type);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  // See xpcvariant.cpp, these are the only types we should expect.
  switch (type) {
    case nsIDataType::VTYPE_VOID:
      aKey = Key::UNSETKEY;
      return NS_OK;

    case nsIDataType::VTYPE_WSTRING_SIZE_IS:
      rv = aKeyVariant->GetAsAString(aKey.ToString());
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
      return NS_OK;

    case nsIDataType::VTYPE_INT32:
    case nsIDataType::VTYPE_DOUBLE:
      rv = aKeyVariant->GetAsInt64(aKey.ToIntPtr());
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
      return NS_OK;

    default:
      return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  NS_NOTREACHED("Can't get here!");
  return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
}

// static
nsresult
IDBObjectStore::GetKeyFromJSVal(jsval aKeyVal,
                                JSContext* aCx,
                                Key& aKey)
{
  if (JSVAL_IS_VOID(aKeyVal)) {
    aKey = Key::UNSETKEY;
  }
  else if (JSVAL_IS_STRING(aKeyVal)) {
    nsDependentJSString depStr;
    if (!depStr.init(aCx, JSVAL_TO_STRING(aKeyVal))) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    aKey = depStr;
  }
  else if (JSVAL_IS_INT(aKeyVal)) {
    aKey = JSVAL_TO_INT(aKeyVal);
  }
  else if (JSVAL_IS_DOUBLE(aKeyVal)) {
    aKey = JSVAL_TO_DOUBLE(aKeyVal);
  }
  else {
    return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  return NS_OK;
}

// static
nsresult
IDBObjectStore::GetJSValFromKey(const Key& aKey,
                                JSContext* aCx,
                                jsval* aKeyVal)
{
  if (aKey.IsUnset()) {
    *aKeyVal = JSVAL_VOID;
    return NS_OK;
  }

  if (aKey.IsInt()) {
    JSBool ok = JS_NewNumberValue(aCx, aKey.IntValue(), aKeyVal);
    NS_ENSURE_TRUE(ok, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
    return NS_OK;
  }

  if (aKey.IsString()) {
    const nsString& keyString = aKey.StringValue();
    JSString* str =
      JS_NewUCStringCopyN(aCx,
                          reinterpret_cast<const jschar*>(keyString.get()),
                          keyString.Length());
    NS_ENSURE_TRUE(str, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    *aKeyVal = STRING_TO_JSVAL(str);
    return NS_OK;
  }

  NS_NOTREACHED("Unknown key type!");
  return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
}

// static
nsresult
IDBObjectStore::GetKeyPathValueFromStructuredData(const PRUint8* aData,
                                                  PRUint32 aDataLength,
                                                  const nsAString& aKeyPath,
                                                  JSContext** aCx,
                                                  Key& aValue)
{
  NS_ASSERTION(aData, "Null pointer!");
  NS_ASSERTION(aDataLength, "Empty data!");
  NS_ASSERTION(!aKeyPath.IsEmpty(), "Empty keyPath!");
  NS_ASSERTION(aCx, "Null pointer!");

  nsresult rv;

  if (!*aCx) {
    rv = nsContentUtils::ThreadJSContextStack()->GetSafeJSContext(aCx);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }
  JSContext*& cx = *aCx;

  JSAutoRequest ar(cx);

  jsval clone;
  if (!JS_ReadStructuredClone(cx, reinterpret_cast<const uint64*>(aData),
                              aDataLength, JS_STRUCTURED_CLONE_VERSION,
                              &clone, NULL, NULL)) {
    return NS_ERROR_DOM_DATA_CLONE_ERR;
  }

  if (JSVAL_IS_PRIMITIVE(clone)) {
    // This isn't an object, so just leave the key unset.
    aValue = Key::UNSETKEY;
    return NS_OK;
  }

  JSObject* obj = JSVAL_TO_OBJECT(clone);

  const jschar* keyPathChars =
    reinterpret_cast<const jschar*>(aKeyPath.BeginReading());
  const size_t keyPathLen = aKeyPath.Length();

  jsval keyVal;
  JSBool ok = JS_GetUCProperty(cx, obj, keyPathChars, keyPathLen, &keyVal);
  NS_ENSURE_TRUE(ok, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = GetKeyFromJSVal(keyVal, *aCx, aValue);
  if (NS_FAILED(rv)) {
    // If the object doesn't have a value that we can use for our index then we
    // leave it unset.
    aValue = Key::UNSETKEY;
  }

  return NS_OK;
}

/* static */
nsresult
IDBObjectStore::GetIndexUpdateInfo(ObjectStoreInfo* aObjectStoreInfo,
                                   JSContext* aCx,
                                   jsval aObject,
                                   nsTArray<IndexUpdateInfo>& aUpdateInfoArray)
{
  JSObject* cloneObj = nsnull;

  PRUint32 count = aObjectStoreInfo->indexes.Length();
  if (count && !JSVAL_IS_PRIMITIVE(aObject)) {
    if (!aUpdateInfoArray.SetCapacity(count)) {
      NS_ERROR("Out of memory!");
      return NS_ERROR_OUT_OF_MEMORY;
    }

    cloneObj = JSVAL_TO_OBJECT(aObject);

    for (PRUint32 indexesIndex = 0; indexesIndex < count; indexesIndex++) {
      const IndexInfo& indexInfo = aObjectStoreInfo->indexes[indexesIndex];

      const jschar* keyPathChars =
        reinterpret_cast<const jschar*>(indexInfo.keyPath.BeginReading());
      const size_t keyPathLen = indexInfo.keyPath.Length();

      jsval keyPathValue;
      JSBool ok = JS_GetUCProperty(aCx, cloneObj, keyPathChars, keyPathLen,
                                   &keyPathValue);
      NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

      Key value;
      nsresult rv = GetKeyFromJSVal(keyPathValue, aCx, value);
      if (NS_FAILED(rv) || value.IsUnset()) {
        // Not a value we can do anything with, ignore it.
        continue;
      }

      IndexUpdateInfo* updateInfo = aUpdateInfoArray.AppendElement();
      updateInfo->info = indexInfo;
      updateInfo->value = value;
    }
  }
  else {
    aUpdateInfoArray.Clear();
  }

  return NS_OK;
}

/* static */
nsresult
IDBObjectStore::UpdateIndexes(IDBTransaction* aTransaction,
                              PRInt64 aObjectStoreId,
                              const Key& aObjectStoreKey,
                              bool aAutoIncrement,
                              bool aOverwrite,
                              PRInt64 aObjectDataId,
                              const nsTArray<IndexUpdateInfo>& aUpdateInfoArray)
{
#ifdef DEBUG
  if (aAutoIncrement) {
    NS_ASSERTION(aObjectDataId != LL_MININT, "Bad objectData id!");
  }
  else {
    NS_ASSERTION(aObjectDataId == LL_MININT, "Bad objectData id!");
  }
#endif

  PRUint32 indexCount = aUpdateInfoArray.Length();
  NS_ASSERTION(indexCount, "Don't call me!");

  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv;

  if (!aAutoIncrement) {
    stmt = aTransaction->GetCachedStatement(
      "SELECT id "
      "FROM object_data "
      "WHERE object_store_id = :osid "
      "AND key_value = :key_value"
    );
    NS_ENSURE_TRUE(stmt, NS_ERROR_FAILURE);

    mozStorageStatementScoper scoper(stmt);

    rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"), aObjectStoreId);
    NS_ENSURE_SUCCESS(rv, rv);

    NS_ASSERTION(!aObjectStoreKey.IsUnset(), "This shouldn't happen!");

    NS_NAMED_LITERAL_CSTRING(keyValue, "key_value");

    if (aObjectStoreKey.IsInt()) {
      rv = stmt->BindInt64ByName(keyValue, aObjectStoreKey.IntValue());
    }
    else if (aObjectStoreKey.IsString()) {
      rv = stmt->BindStringByName(keyValue, aObjectStoreKey.StringValue());
    }
    else {
      NS_NOTREACHED("Unknown key type!");
    }
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool hasResult;
    rv = stmt->ExecuteStep(&hasResult);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!hasResult) {
      NS_ERROR("This is bad, we just added this value! Where'd it go?!");
      return NS_ERROR_FAILURE;
    }

    aObjectDataId = stmt->AsInt64(0);
  }

  NS_ASSERTION(aObjectDataId != LL_MININT, "Bad objectData id!");

  for (PRUint32 indexIndex = 0; indexIndex < indexCount; indexIndex++) {
    const IndexUpdateInfo& updateInfo = aUpdateInfoArray[indexIndex];

    stmt = aTransaction->IndexUpdateStatement(updateInfo.info.autoIncrement,
                                              updateInfo.info.unique,
                                              aOverwrite);
    NS_ENSURE_TRUE(stmt, NS_ERROR_FAILURE);

    mozStorageStatementScoper scoper2(stmt);

    rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("index_id"),
                               updateInfo.info.id);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("object_data_id"),
                               aObjectDataId);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!updateInfo.info.autoIncrement) {
      NS_NAMED_LITERAL_CSTRING(objectDataKey, "object_data_key");

      if (aObjectStoreKey.IsInt()) {
        rv = stmt->BindInt64ByName(objectDataKey, aObjectStoreKey.IntValue());
      }
      else if (aObjectStoreKey.IsString()) {
        rv = stmt->BindStringByName(objectDataKey,
                                    aObjectStoreKey.StringValue());
      }
      else {
        NS_NOTREACHED("Unknown key type!");
      }
      NS_ENSURE_SUCCESS(rv, rv);
    }

    NS_NAMED_LITERAL_CSTRING(value, "value");

    if (updateInfo.value.IsInt()) {
      rv = stmt->BindInt64ByName(value, updateInfo.value.IntValue());
    }
    else if (updateInfo.value.IsString()) {
      rv = stmt->BindStringByName(value, updateInfo.value.StringValue());
    }
    else if (updateInfo.value.IsUnset()) {
      rv = stmt->BindStringByName(value, updateInfo.value.StringValue());
    }
    else {
      NS_NOTREACHED("Unknown key type!");
    }
    NS_ENSURE_SUCCESS(rv, rv);

    rv = stmt->Execute();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  return NS_OK;
}

// static
nsresult
IDBObjectStore::GetStructuredCloneDataFromStatement(
                                           mozIStorageStatement* aStatement,
                                           PRUint32 aIndex,
                                           JSAutoStructuredCloneBuffer& aBuffer)
{
#ifdef DEBUG
  {
    PRInt32 valueType;
    NS_ASSERTION(NS_SUCCEEDED(aStatement->GetTypeOfIndex(aIndex, &valueType)) &&
                 valueType == mozIStorageStatement::VALUE_TYPE_BLOB,
                 "Bad value type!");
  }
#endif

  const PRUint8* data;
  PRUint32 dataLength;
  nsresult rv = aStatement->GetSharedBlob(aIndex, &dataLength, &data);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  JSContext* cx;
  rv = nsContentUtils::ThreadJSContextStack()->GetSafeJSContext(&cx);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  JSAutoRequest ar(cx);

  uint64* newData = static_cast<uint64*>(JS_malloc(cx, dataLength));
  NS_ENSURE_TRUE(newData, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  memcpy(newData, data, dataLength);
  aBuffer.adopt(cx, newData, dataLength);

  return NS_OK;
}

// static
void
IDBObjectStore::ClearStructuredCloneBuffer(JSAutoStructuredCloneBuffer& aBuffer)
{
  if (aBuffer.data()) {
    JSContext* cx;
    if (NS_SUCCEEDED(nsContentUtils::ThreadJSContextStack()->
                     GetSafeJSContext(&cx))) {
      JSAutoRequest ar(cx);
      aBuffer.clear(cx);
    }
    else {
      NS_WARNING("Couldn't get safe JSContext! Leaking data!");
      uint64* data;
      size_t length;
      aBuffer.steal(&data, &length);
    }
  }
}

IDBObjectStore::IDBObjectStore()
: mId(LL_MININT),
  mAutoIncrement(PR_FALSE)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
}

IDBObjectStore::~IDBObjectStore()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
}

nsresult
IDBObjectStore::GetAddInfo(JSContext* aCx,
                           jsval aValue,
                           jsval aKeyVal,
                           JSAutoStructuredCloneBuffer& aCloneBuffer,
                           Key& aKey,
                           nsTArray<IndexUpdateInfo>& aUpdateInfoArray)
{
  nsresult rv;

  // Return DATA_ERR if a key was passed in and this objectStore uses inline
  // keys.
  if (!JSVAL_IS_VOID(aKeyVal) && !mKeyPath.IsEmpty()) {
    return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  JSAutoRequest ar(aCx);

  if (mKeyPath.IsEmpty()) {
    // Out-of-line keys must be passed in.
    rv = GetKeyFromJSVal(aKeyVal, aCx, aKey);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    // Inline keys live on the object. Make sure that the value passed in is an
    // object.
    if (JSVAL_IS_PRIMITIVE(aValue)) {
      return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
    }

    rv = GetKeyFromObject(aCx, JSVAL_TO_OBJECT(aValue), mKeyPath, aKey);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Return DATA_ERR if no key was specified this isn't an autoIncrement
  // objectStore.
  if (aKey.IsUnset() && !mAutoIncrement) {
    return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  // Figure out indexes and the index values to update here.
  ObjectStoreInfo* info;
  if (!ObjectStoreInfo::Get(mTransaction->Database()->Id(), mName, &info)) {
    NS_ERROR("This should never fail!");
  }

  rv = GetIndexUpdateInfo(info, aCx, aValue, aUpdateInfoArray);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (!aCloneBuffer.write(aCx, aValue)) {
    return NS_ERROR_DOM_DATA_CLONE_ERR;
  }

  return NS_OK;
}

nsresult
IDBObjectStore::AddOrPut(const jsval& aValue,
                         const jsval& aKey,
                         JSContext* aCx,
                         PRUint8 aOptionalArgCount,
                         nsIIDBRequest** _retval,
                         bool aOverwrite)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!IsWriteAllowed()) {
    return NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR;
  }

  jsval keyval = (aOptionalArgCount >= 1) ? aKey : JSVAL_VOID;

  JSAutoStructuredCloneBuffer cloneBuffer;
  Key key;
  nsTArray<IndexUpdateInfo> updateInfo;

  nsresult rv = GetAddInfo(aCx, aValue, keyval, cloneBuffer, key, updateInfo);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Put requires a key.
  if (aOverwrite && key.IsUnset()) {
    return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<AddHelper> helper =
    new AddHelper(mTransaction, request, this, cloneBuffer, key, aOverwrite,
                  updateInfo);

  rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(IDBObjectStore)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(IDBObjectStore)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(mTransaction,
                                                       nsPIDOMEventTarget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mOwner)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mScriptContext)

  for (PRUint32 i = 0; i < tmp->mCreatedIndexes.Length(); i++) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mCreatedIndexes[i]");
    cb.NoteXPCOMChild(static_cast<nsIIDBIndex*>(tmp->mCreatedIndexes[i].get()));
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(IDBObjectStore)
  // Don't unlink mTransaction!
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mOwner)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mScriptContext)

  tmp->mCreatedIndexes.Clear();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(IDBObjectStore)
  NS_INTERFACE_MAP_ENTRY(nsIIDBObjectStore)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(IDBObjectStore)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(IDBObjectStore)
NS_IMPL_CYCLE_COLLECTING_RELEASE(IDBObjectStore)

DOMCI_DATA(IDBObjectStore, IDBObjectStore)

NS_IMETHODIMP
IDBObjectStore::GetName(nsAString& aName)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  aName.Assign(mName);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::GetKeyPath(nsAString& aKeyPath)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  aKeyPath.Assign(mKeyPath);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::GetTransaction(nsIIDBTransaction** aTransaction)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsCOMPtr<nsIIDBTransaction> transaction(mTransaction);
  transaction.forget(aTransaction);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::GetIndexNames(nsIDOMDOMStringList** aIndexNames)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  ObjectStoreInfo* info;
  if (!ObjectStoreInfo::Get(mTransaction->Database()->Id(), mName, &info)) {
    NS_ERROR("This should never fail!");
  }

  nsRefPtr<nsDOMStringList> list(new nsDOMStringList());

  PRUint32 count = info->indexes.Length();
  for (PRUint32 index = 0; index < count; index++) {
    NS_ENSURE_TRUE(list->Add(info->indexes[index].name),
                   NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  list.forget(aIndexNames);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Get(nsIVariant* aKey,
                    nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  Key key;
  nsresult rv = GetKeyFromVariant(aKey, key);
  if (NS_FAILED(rv)) {
    // Check to see if this is a key range.
    PRUint16 type;
    rv = aKey->GetDataType(&type);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    if (type != nsIDataType::VTYPE_INTERFACE &&
        type != nsIDataType::VTYPE_INTERFACE_IS) {
      return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
    }

    // XXX I hate this API. Move to jsvals, stat.
    nsID* iid;
    nsCOMPtr<nsISupports> supports;
    rv = aKey->GetAsInterface(&iid, getter_AddRefs(supports));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    NS_Free(iid);

    nsCOMPtr<nsIIDBKeyRange> keyRange = do_QueryInterface(supports);
    if (!keyRange) {
      return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
    }

    return GetAll(keyRange, 0, 0, _retval);
  }

  if (key.IsUnset()) {
    return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<GetHelper> helper(new GetHelper(mTransaction, request, this, key));

  rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::GetAll(nsIIDBKeyRange* aKeyRange,
                       PRUint32 aLimit,
                       PRUint8 aOptionalArgCount,
                       nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (aOptionalArgCount < 2) {
    aLimit = PR_UINT32_MAX;
  }

  nsresult rv;
  Key lowerKey, upperKey;
  PRBool lowerOpen = PR_FALSE, upperOpen = PR_FALSE;

  if (aKeyRange) {
    nsCOMPtr<nsIVariant> variant;
    rv = aKeyRange->GetLower(getter_AddRefs(variant));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    rv = IDBObjectStore::GetKeyFromVariant(variant, lowerKey);
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = aKeyRange->GetUpper(getter_AddRefs(variant));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    rv = IDBObjectStore::GetKeyFromVariant(variant, upperKey);
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = aKeyRange->GetLowerOpen(&lowerOpen);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    rv = aKeyRange->GetUpperOpen(&upperOpen);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<GetAllHelper> helper =
    new GetAllHelper(mTransaction, request, this, lowerKey, upperKey, lowerOpen,
                     upperOpen, aLimit);

  rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Add(const jsval& aValue,
                    const jsval& aKey,
                    JSContext* aCx,
                    PRUint8 aOptionalArgCount,
                    nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  return AddOrPut(aValue, aKey, aCx, aOptionalArgCount, _retval, false);
}

NS_IMETHODIMP
IDBObjectStore::Put(const jsval& aValue,
                    const jsval& aKey,
                    JSContext* aCx,
                    PRUint8 aOptionalArgCount,
                    nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  return AddOrPut(aValue, aKey, aCx, aOptionalArgCount, _retval, true);
}

NS_IMETHODIMP
IDBObjectStore::Delete(const jsval& aKey,
                       JSContext* aCx,
                       nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!IsWriteAllowed()) {
    return NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR;
  }

  Key key;
  nsresult rv = GetKeyFromJSVal(aKey, aCx, key);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (key.IsUnset()) {
    return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<DeleteHelper> helper =
    new DeleteHelper(mTransaction, request, this, key);

  rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Clear(nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!IsWriteAllowed()) {
    return NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<ClearHelper> helper(new ClearHelper(mTransaction, request, this));

  nsresult rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::OpenCursor(nsIIDBKeyRange* aKeyRange,
                           PRUint16 aDirection,
                           PRUint8 aOptionalArgCount,
                           nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  nsresult rv;
  Key lowerKey, upperKey;
  PRBool lowerOpen = PR_FALSE, upperOpen = PR_FALSE;

  if (aKeyRange) {
    nsCOMPtr<nsIVariant> variant;
    rv = aKeyRange->GetLower(getter_AddRefs(variant));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    rv = IDBObjectStore::GetKeyFromVariant(variant, lowerKey);
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = aKeyRange->GetUpper(getter_AddRefs(variant));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    rv = IDBObjectStore::GetKeyFromVariant(variant, upperKey);
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = aKeyRange->GetLowerOpen(&lowerOpen);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    rv = aKeyRange->GetUpperOpen(&upperOpen);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  if (aOptionalArgCount >= 2) {
    if (aDirection != nsIIDBCursor::NEXT &&
        aDirection != nsIIDBCursor::NEXT_NO_DUPLICATE &&
        aDirection != nsIIDBCursor::PREV &&
        aDirection != nsIIDBCursor::PREV_NO_DUPLICATE) {
      return NS_ERROR_DOM_INDEXEDDB_NON_TRANSIENT_ERR;
    }
  }
  else {
    aDirection = nsIIDBCursor::NEXT;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<OpenCursorHelper> helper =
    new OpenCursorHelper(mTransaction, request, this, lowerKey, upperKey,
                         lowerOpen, upperOpen, aDirection);

  rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::CreateIndex(const nsAString& aName,
                            const nsAString& aKeyPath,
                            const jsval& aOptions,
                            JSContext* aCx,
                            nsIIDBIndex** _retval)
{
  NS_PRECONDITION(NS_IsMainThread(), "Wrong thread!");

  if (aName.IsEmpty() || aKeyPath.IsEmpty()) {
    return NS_ERROR_DOM_INDEXEDDB_NON_TRANSIENT_ERR;
  }

  IDBTransaction* transaction = AsyncConnectionHelper::GetCurrentTransaction();

  if (!transaction ||
      transaction != mTransaction ||
      mTransaction->Mode() != nsIIDBTransaction::VERSION_CHANGE) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR;
  }

  ObjectStoreInfo* info;
  if (!ObjectStoreInfo::Get(mTransaction->Database()->Id(), mName, &info)) {
    NS_ERROR("This should never fail!");
  }

  bool found = false;
  PRUint32 indexCount = info->indexes.Length();
  for (PRUint32 index = 0; index < indexCount; index++) {
    if (info->indexes[index].name == aName) {
      found = true;
      break;
    }
  }

  if (found) {
    return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
  }

  NS_ASSERTION(mTransaction->IsOpen(), "Impossible!");

  bool unique = false;

  // Get optional arguments.
  if (!JSVAL_IS_VOID(aOptions) && !JSVAL_IS_NULL(aOptions)) {
    if (JSVAL_IS_PRIMITIVE(aOptions)) {
    // XXX Update spec for a real code here
      return NS_ERROR_DOM_INDEXEDDB_NON_TRANSIENT_ERR;
    }

    NS_ASSERTION(JSVAL_IS_OBJECT(aOptions), "Huh?!");
    JSObject* options = JSVAL_TO_OBJECT(aOptions);

    js::AutoIdArray ids(aCx, JS_Enumerate(aCx, options));
    if (!ids) {
      return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
    }

    for (size_t index = 0; index < ids.length(); index++) {
      jsid id = ids[index];

      if (id != nsDOMClassInfo::sUnique_id) {
        // XXX Update spec for a real code here
        return NS_ERROR_DOM_INDEXEDDB_NON_TRANSIENT_ERR;
      }

      jsval val;
      if (!JS_GetPropertyById(aCx, options, id, &val)) {
        NS_WARNING("JS_GetPropertyById failed!");
        return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
      }

      if (id == nsDOMClassInfo::sUnique_id) {
        JSBool boolVal;
        if (!JS_ValueToBoolean(aCx, val, &boolVal)) {
          NS_WARNING("JS_ValueToBoolean failed!");
          return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
        }
        unique = !!boolVal;
      }
      else {
        NS_NOTREACHED("Shouldn't be able to get here!");
      }
    }
  }

  DatabaseInfo* databaseInfo;
  if (!DatabaseInfo::Get(mTransaction->Database()->Id(), &databaseInfo)) {
    NS_ERROR("This should never fail!");
  }

  IndexInfo* indexInfo = info->indexes.AppendElement();
  if (!indexInfo) {
    NS_WARNING("Out of memory!");
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  indexInfo->id = databaseInfo->nextIndexId++;
  indexInfo->name = aName;
  indexInfo->keyPath = aKeyPath;
  indexInfo->unique = unique;
  indexInfo->autoIncrement = mAutoIncrement;

  // Don't leave this in the list if we fail below!
  AutoRemoveIndex autoRemove(databaseInfo->id, mName, aName);

#ifdef DEBUG
  for (PRUint32 index = 0; index < mCreatedIndexes.Length(); index++) {
    if (mCreatedIndexes[index]->Name() == aName) {
      NS_ERROR("Already created this one!");
    }
  }
#endif

  nsRefPtr<IDBIndex> index(IDBIndex::Create(this, indexInfo));

  if (!mCreatedIndexes.AppendElement(index)) {
    NS_WARNING("Out of memory!");
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  nsRefPtr<CreateIndexHelper> helper =
    new CreateIndexHelper(mTransaction, index);

  nsresult rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  autoRemove.forget();

  index.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Index(const nsAString& aName,
                      nsIIDBIndex** _retval)
{
  NS_PRECONDITION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (aName.IsEmpty()) {
    return NS_ERROR_DOM_INDEXEDDB_NON_TRANSIENT_ERR;
  }

  ObjectStoreInfo* info;
  if (!ObjectStoreInfo::Get(mTransaction->Database()->Id(), mName, &info)) {
    NS_ERROR("This should never fail!");
  }

  IndexInfo* indexInfo = nsnull;
  PRUint32 indexCount = info->indexes.Length();
  for (PRUint32 index = 0; index < indexCount; index++) {
    if (info->indexes[index].name == aName) {
      indexInfo = &(info->indexes[index]);
      break;
    }
  }

  if (!indexInfo) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_FOUND_ERR;
  }

  nsRefPtr<IDBIndex> retval;
  for (PRUint32 i = 0; i < mCreatedIndexes.Length(); i++) {
    nsRefPtr<IDBIndex>& index = mCreatedIndexes[i];
    if (index->Name() == aName) {
      retval = index;
      break;
    }
  }

  if (!retval) {
    retval = IDBIndex::Create(this, indexInfo);
    NS_ENSURE_TRUE(retval, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    if (!mCreatedIndexes.AppendElement(retval)) {
      NS_WARNING("Out of memory!");
      return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
    }
  }

  retval.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::DeleteIndex(const nsAString& aName)
{
  NS_PRECONDITION(NS_IsMainThread(), "Wrong thread!");

  if (aName.IsEmpty()) {
    return NS_ERROR_DOM_INDEXEDDB_NON_TRANSIENT_ERR;
  }

  IDBTransaction* transaction = AsyncConnectionHelper::GetCurrentTransaction();

  if (!transaction ||
      transaction != mTransaction ||
      mTransaction->Mode() != nsIIDBTransaction::VERSION_CHANGE) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR;
  }

  NS_ASSERTION(mTransaction->IsOpen(), "Impossible!");

  ObjectStoreInfo* info;
  if (!ObjectStoreInfo::Get(mTransaction->Database()->Id(), mName, &info)) {
    NS_ERROR("This should never fail!");
  }

  PRUint32 index = 0;
  for (; index < info->indexes.Length(); index++) {
    if (info->indexes[index].name == aName) {
      break;
    }
  }

  if (index == info->indexes.Length()) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_FOUND_ERR;
  }

  nsRefPtr<DeleteIndexHelper> helper =
    new DeleteIndexHelper(mTransaction, aName, this);

  nsresult rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  info->indexes.RemoveElementAt(index);
  return NS_OK;
}

nsresult
AddHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_PRECONDITION(aConnection, "Passed a null connection!");

  nsresult rv;
  bool mayOverwrite = mOverwrite;
  bool unsetKey = mKey.IsUnset();

  bool autoIncrement = mObjectStore->IsAutoIncrement();
  PRInt64 osid = mObjectStore->Id();
  const nsString& keyPath = mObjectStore->KeyPath();

  if (unsetKey) {
    NS_ASSERTION(autoIncrement, "Must have a key for non-autoIncrement!");

    // Will need to add first and then set the key later.
    mayOverwrite = false;
  }

  if (autoIncrement && !unsetKey) {
    mayOverwrite = true;
  }

  nsCOMPtr<mozIStorageStatement> stmt;
  if (!mOverwrite && !unsetKey) {
    // Make sure the key doesn't exist already
    stmt = mTransaction->GetStatement(autoIncrement);
    NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    mozStorageStatementScoper scoper(stmt);

    rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"), osid);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    NS_NAMED_LITERAL_CSTRING(id, "id");

    if (mKey.IsInt()) {
      rv = stmt->BindInt64ByName(id, mKey.IntValue());
    }
    else if (mKey.IsString()) {
      rv = stmt->BindStringByName(id, mKey.StringValue());
    }
    else {
      NS_NOTREACHED("Unknown key type!");
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    PRBool hasResult;
    rv = stmt->ExecuteStep(&hasResult);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    if (hasResult) {
      return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
    }
  }

  // Now we add it to the database (or update, depending on our variables).
  stmt = mTransaction->AddStatement(true, mayOverwrite, autoIncrement);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  NS_NAMED_LITERAL_CSTRING(keyValue, "key_value");

  rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"), osid);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (!autoIncrement || mayOverwrite) {
    NS_ASSERTION(!mKey.IsUnset(), "This shouldn't happen!");

    if (mKey.IsInt()) {
      rv = stmt->BindInt64ByName(keyValue, mKey.IntValue());
    }
    else if (mKey.IsString()) {
      rv = stmt->BindStringByName(keyValue, mKey.StringValue());
    }
    else {
      NS_NOTREACHED("Unknown key type!");
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  const PRUint8* buffer = reinterpret_cast<const PRUint8*>(mCloneBuffer.data());
  size_t bufferLength = mCloneBuffer.nbytes();

  rv = stmt->BindBlobByName(NS_LITERAL_CSTRING("data"), buffer, bufferLength);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->Execute();
  if (NS_FAILED(rv)) {
    if (mayOverwrite && rv == NS_ERROR_STORAGE_CONSTRAINT) {
      scoper.Abandon();

      rv = stmt->Reset();
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      stmt = mTransaction->AddStatement(false, true, autoIncrement);
      NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      mozStorageStatementScoper scoper2(stmt);

      rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"), osid);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      NS_ASSERTION(!mKey.IsUnset(), "This shouldn't happen!");

      if (mKey.IsInt()) {
        rv = stmt->BindInt64ByName(keyValue, mKey.IntValue());
      }
      else if (mKey.IsString()) {
        rv = stmt->BindStringByName(keyValue, mKey.StringValue());
      }
      else {
        NS_NOTREACHED("Unknown key type!");
      }
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      rv = stmt->BindBlobByName(NS_LITERAL_CSTRING("data"), buffer,
                                bufferLength);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      rv = stmt->Execute();
    }

    if (NS_FAILED(rv)) {
      return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
    }
  }

  // If we are supposed to generate a key, get the new id.
  if (autoIncrement && !mOverwrite) {
#ifdef DEBUG
    PRInt64 oldKey = unsetKey ? 0 : mKey.IntValue();
#endif

    rv = aConnection->GetLastInsertRowID(mKey.ToIntPtr());
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

#ifdef DEBUG
    NS_ASSERTION(mKey.IsInt(), "Bad key value!");
    if (!unsetKey) {
      NS_ASSERTION(mKey.IntValue() == oldKey, "Something went haywire!");
    }
#endif

    if (!keyPath.IsEmpty() && unsetKey) {
      // Special case where someone put an object into an autoIncrement'ing
      // objectStore with no key in its keyPath set. We needed to figure out
      // which row id we would get above before we could set that properly.
      rv = ModifyValueForNewKey();
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      scoper.Abandon();
      rv = stmt->Reset();
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      stmt = mTransaction->AddStatement(false, true, true);
      NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      mozStorageStatementScoper scoper2(stmt);

      rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"), osid);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      rv = stmt->BindInt64ByName(keyValue, mKey.IntValue());
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      buffer = reinterpret_cast<const PRUint8*>(mCloneBuffer.data());
      bufferLength = mCloneBuffer.nbytes();

      rv = stmt->BindBlobByName(NS_LITERAL_CSTRING("data"), buffer,
                                bufferLength);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      rv = stmt->Execute();
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
    }
  }

  // Update our indexes if needed.
  if (!mIndexUpdateInfo.IsEmpty()) {
    PRInt64 objectDataId = autoIncrement ? mKey.IntValue() : LL_MININT;
    rv = IDBObjectStore::UpdateIndexes(mTransaction, osid, mKey,
                                       autoIncrement, mOverwrite,
                                       objectDataId, mIndexUpdateInfo);
    if (rv == NS_ERROR_STORAGE_CONSTRAINT) {
      return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  return NS_OK;
}

nsresult
AddHelper::GetSuccessResult(JSContext* aCx,
                            jsval* aVal)
{
  NS_ASSERTION(!mKey.IsUnset(), "Badness!");

  mCloneBuffer.clear(aCx);

  return IDBObjectStore::GetJSValFromKey(mKey, aCx, aVal);
}

nsresult
AddHelper::ModifyValueForNewKey()
{
  NS_ASSERTION(mObjectStore->IsAutoIncrement() &&
               !mObjectStore->KeyPath().IsEmpty() &&
               mKey.IsInt(),
               "Don't call me!");

  const nsString& keyPath = mObjectStore->KeyPath();

  JSContext* cx;
  nsresult rv = nsContentUtils::ThreadJSContextStack()->GetSafeJSContext(&cx);
  NS_ENSURE_SUCCESS(rv, rv);

  JSAutoRequest ar(cx);

  jsval clone;
  if (!mCloneBuffer.read(&clone, cx)) {
    return NS_ERROR_DOM_DATA_CLONE_ERR;
  }

  NS_ASSERTION(!JSVAL_IS_PRIMITIVE(clone), "We should have an object!");

  JSObject* obj = JSVAL_TO_OBJECT(clone);
  JSBool ok;

  const jschar* keyPathChars = reinterpret_cast<const jschar*>(keyPath.get());
  const size_t keyPathLen = keyPath.Length();

#ifdef DEBUG
  {
    jsval prop;
    ok = JS_GetUCProperty(cx, obj, keyPathChars, keyPathLen, &prop);
    NS_ASSERTION(ok && JSVAL_IS_VOID(prop), "Already has a key prop!");
  }
#endif

  jsval key;
  ok = JS_NewNumberValue(cx, mKey.IntValue(), &key);
  NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

  ok = JS_DefineUCProperty(cx, obj, keyPathChars, keyPathLen, key, nsnull,
                           nsnull, JSPROP_ENUMERATE);
  NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

  if (!mCloneBuffer.write(cx, OBJECT_TO_JSVAL(obj))) {
    return NS_ERROR_DOM_DATA_CLONE_ERR;
  }

  return NS_OK;
}

nsresult
GetHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_PRECONDITION(aConnection, "Passed a null connection!");

  nsCOMPtr<mozIStorageStatement> stmt =
    mTransaction->GetStatement(mObjectStore->IsAutoIncrement());
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                                      mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ASSERTION(!mKey.IsUnset(), "Must have a key here!");

  NS_NAMED_LITERAL_CSTRING(id, "id");

  if (mKey.IsInt()) {
    rv = stmt->BindInt64ByName(id, mKey.IntValue());
  }
  else if (mKey.IsString()) {
    rv = stmt->BindStringByName(id, mKey.StringValue());
  }
  else {
    NS_NOTREACHED("Unknown key type!");
  }
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  // Search for it!
  PRBool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (hasResult) {
    rv = IDBObjectStore::GetStructuredCloneDataFromStatement(stmt, 0,
                                                             mCloneBuffer);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
GetHelper::GetSuccessResult(JSContext* aCx,
                            jsval* aVal)
{
  nsresult rv = ConvertCloneBufferToJSVal(aCx, mCloneBuffer, aVal);

  mCloneBuffer.clear(aCx);

  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult
DeleteHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_PRECONDITION(aConnection, "Passed a null connection!");

  nsCOMPtr<mozIStorageStatement> stmt =
    mTransaction->DeleteStatement(mObjectStore->IsAutoIncrement());
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                                      mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ASSERTION(!mKey.IsUnset(), "Must have a key here!");

  NS_NAMED_LITERAL_CSTRING(key_value, "key_value");

  if (mKey.IsInt()) {
    rv = stmt->BindInt64ByName(key_value, mKey.IntValue());
  }
  else if (mKey.IsString()) {
    rv = stmt->BindStringByName(key_value, mKey.StringValue());
  }
  else {
    NS_NOTREACHED("Unknown key type!");
  }
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  return NS_OK;
}

nsresult
DeleteHelper::GetSuccessResult(JSContext* aCx,
                               jsval* aVal)
{
  NS_ASSERTION(!mKey.IsUnset(), "Badness!");
  return IDBObjectStore::GetJSValFromKey(mKey, aCx, aVal);
}

nsresult
ClearHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_PRECONDITION(aConnection, "Passed a null connection!");

  nsCString table;
  if (mObjectStore->IsAutoIncrement()) {
    table.AssignLiteral("ai_object_data");
  }
  else {
    table.AssignLiteral("object_data");
  }

  nsCString query = NS_LITERAL_CSTRING("DELETE FROM ") + table +
                    NS_LITERAL_CSTRING(" WHERE object_store_id = :osid");

  nsCOMPtr<mozIStorageStatement> stmt = mTransaction->GetCachedStatement(query);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                                      mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  return NS_OK;
}

nsresult
OpenCursorHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  nsCString table;
  nsCString keyColumn;

  if (mObjectStore->IsAutoIncrement()) {
    table.AssignLiteral("ai_object_data");
    keyColumn.AssignLiteral("id");
  }
  else {
    table.AssignLiteral("object_data");
    keyColumn.AssignLiteral("key_value");
  }

  NS_NAMED_LITERAL_CSTRING(id, "id");
  NS_NAMED_LITERAL_CSTRING(lowerKeyName, "lower_key");
  NS_NAMED_LITERAL_CSTRING(upperKeyName, "upper_key");

  nsCAutoString keyRangeClause;
  if (!mLowerKey.IsUnset()) {
    AppendConditionClause(keyColumn, lowerKeyName, false, !mLowerOpen,
                          keyRangeClause);
  }
  if (!mUpperKey.IsUnset()) {
    AppendConditionClause(keyColumn, upperKeyName, true, !mUpperOpen,
                          keyRangeClause);
  }

  nsCAutoString directionClause = NS_LITERAL_CSTRING(" ORDER BY ") + keyColumn;
  switch (mDirection) {
    case nsIIDBCursor::NEXT:
    case nsIIDBCursor::NEXT_NO_DUPLICATE:
      directionClause += NS_LITERAL_CSTRING(" ASC");
      break;

    case nsIIDBCursor::PREV:
    case nsIIDBCursor::PREV_NO_DUPLICATE:
      directionClause += NS_LITERAL_CSTRING(" DESC");
      break;

    default:
      NS_NOTREACHED("Unknown direction type!");
  }

  nsCString firstQuery = NS_LITERAL_CSTRING("SELECT ") + keyColumn +
                         NS_LITERAL_CSTRING(", data FROM ") + table +
                         NS_LITERAL_CSTRING(" WHERE object_store_id = :") +
                         id + keyRangeClause + directionClause;

  nsCOMPtr<mozIStorageStatement> stmt =
    mTransaction->GetCachedStatement(firstQuery);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(id, mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (!mLowerKey.IsUnset()) {
    if (mLowerKey.IsString()) {
      rv = stmt->BindStringByName(lowerKeyName, mLowerKey.StringValue());
    }
    else if (mLowerKey.IsInt()) {
      rv = stmt->BindInt64ByName(lowerKeyName, mLowerKey.IntValue());
    }
    else {
      NS_NOTREACHED("Bad key!");
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  if (!mUpperKey.IsUnset()) {
    if (mUpperKey.IsString()) {
      rv = stmt->BindStringByName(upperKeyName, mUpperKey.StringValue());
    }
    else if (mUpperKey.IsInt()) {
      rv = stmt->BindInt64ByName(upperKeyName, mUpperKey.IntValue());
    }
    else {
      NS_NOTREACHED("Bad key!");
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  PRBool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (!hasResult) {
    mKey = Key::UNSETKEY;
    return NS_OK;
  }

  PRInt32 keyType;
  rv = stmt->GetTypeOfIndex(0, &keyType);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (keyType == mozIStorageStatement::VALUE_TYPE_INTEGER) {
    mKey = stmt->AsInt64(0);
  }
  else if (keyType == mozIStorageStatement::VALUE_TYPE_TEXT) {
    rv = stmt->GetString(0, mKey.ToString());
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }
  else {
    NS_NOTREACHED("Bad SQLite type!");
  }

  rv = IDBObjectStore::GetStructuredCloneDataFromStatement(stmt, 1,
                                                           mCloneBuffer);
  NS_ENSURE_SUCCESS(rv, rv);

  // Now we need to make the query to get the next match.
  keyRangeClause.Truncate();
  nsCAutoString continueToKeyRangeClause;

  NS_NAMED_LITERAL_CSTRING(currentKey, "current_key");
  NS_NAMED_LITERAL_CSTRING(rangeKey, "range_key");

  switch (mDirection) {
    case nsIIDBCursor::NEXT:
    case nsIIDBCursor::NEXT_NO_DUPLICATE:
      AppendConditionClause(keyColumn, currentKey, false, false,
                            keyRangeClause);
      AppendConditionClause(keyColumn, currentKey, false, true,
                            continueToKeyRangeClause);
      if (!mUpperKey.IsUnset()) {
        AppendConditionClause(keyColumn, rangeKey, true, !mUpperOpen,
                              keyRangeClause);
        AppendConditionClause(keyColumn, rangeKey, true, !mUpperOpen,
                              continueToKeyRangeClause);
        mRangeKey = mUpperKey;
      }
      break;

    case nsIIDBCursor::PREV:
    case nsIIDBCursor::PREV_NO_DUPLICATE:
      AppendConditionClause(keyColumn, currentKey, true, false, keyRangeClause);
      AppendConditionClause(keyColumn, currentKey, true, true,
                           continueToKeyRangeClause);
      if (!mLowerKey.IsUnset()) {
        AppendConditionClause(keyColumn, rangeKey, false, !mLowerOpen,
                              keyRangeClause);
        AppendConditionClause(keyColumn, rangeKey, false, !mLowerOpen,
                              continueToKeyRangeClause);
        mRangeKey = mLowerKey;
      }
      break;

    default:
      NS_NOTREACHED("Unknown direction type!");
  }

  mContinueQuery = NS_LITERAL_CSTRING("SELECT ") + keyColumn +
                   NS_LITERAL_CSTRING(", data FROM ") + table +
                   NS_LITERAL_CSTRING(" WHERE object_store_id = :") + id +
                   keyRangeClause + directionClause +
                   NS_LITERAL_CSTRING(" LIMIT 1");

  mContinueToQuery = NS_LITERAL_CSTRING("SELECT ") + keyColumn +
                     NS_LITERAL_CSTRING(", data FROM ") + table +
                     NS_LITERAL_CSTRING(" WHERE object_store_id = :") + id +
                     continueToKeyRangeClause + directionClause +
                     NS_LITERAL_CSTRING(" LIMIT 1");

  return NS_OK;
}

nsresult
OpenCursorHelper::GetSuccessResult(JSContext* aCx,
                                   jsval* aVal)
{
  if (mKey.IsUnset()) {
    *aVal = JSVAL_VOID;
    return NS_OK;
  }

  nsRefPtr<IDBCursor> cursor =
    IDBCursor::Create(mRequest, mTransaction, mObjectStore, mDirection,
                      mRangeKey, mContinueQuery, mContinueToQuery, mKey,
                      mCloneBuffer);
  NS_ENSURE_TRUE(cursor, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ASSERTION(!mCloneBuffer.data(), "Should have swapped!");

  return WrapNative(aCx, cursor, aVal);
}

nsresult
CreateIndexHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  // Insert the data into the database.
  nsCOMPtr<mozIStorageStatement> stmt =
    mTransaction->GetCachedStatement(
    "INSERT INTO object_store_index (id, name, key_path, unique_index, "
      "object_store_id, object_store_autoincrement) "
    "VALUES (:id, :name, :key_path, :unique, :osid, :os_auto_increment)"
  );
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("id"),
                                      mIndex->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->BindStringByName(NS_LITERAL_CSTRING("name"), mIndex->Name());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->BindStringByName(NS_LITERAL_CSTRING("key_path"),
                              mIndex->KeyPath());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->BindInt32ByName(NS_LITERAL_CSTRING("unique"),
                             mIndex->IsUnique() ? 1 : 0);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                             mIndex->ObjectStore()->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->BindInt32ByName(NS_LITERAL_CSTRING("os_auto_increment"),
                             mIndex->IsAutoIncrement() ? 1 : 0);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (NS_FAILED(stmt->Execute())) {
    return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
  }

#ifdef DEBUG
  {
    PRInt64 id;
    aConnection->GetLastInsertRowID(&id);
    NS_ASSERTION(mIndex->Id() == id, "Bad index id!");
  }
#endif

  // Now we need to populate the index with data from the object store.
  rv = InsertDataFromObjectStore(aConnection);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

nsresult
CreateIndexHelper::InsertDataFromObjectStore(mozIStorageConnection* aConnection)
{
  nsCAutoString table;
  nsCAutoString columns;
  if (mIndex->IsAutoIncrement()) {
    table.AssignLiteral("ai_object_data");
    columns.AssignLiteral("id, data");
  }
  else {
    table.AssignLiteral("object_data");
    columns.AssignLiteral("id, data, key_value");
  }

  nsCString query = NS_LITERAL_CSTRING("SELECT ") + columns +
                    NS_LITERAL_CSTRING(" FROM ") + table +
                    NS_LITERAL_CSTRING(" WHERE object_store_id = :osid");

  nsCOMPtr<mozIStorageStatement> stmt = mTransaction->GetCachedStatement(query);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                                      mIndex->ObjectStore()->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  PRBool hasResult;
  while (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
    nsCOMPtr<mozIStorageStatement> insertStmt =
      mTransaction->IndexUpdateStatement(mIndex->IsAutoIncrement(),
                                         mIndex->IsUnique(), false);
    NS_ENSURE_TRUE(insertStmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    mozStorageStatementScoper scoper2(insertStmt);

    rv = insertStmt->BindInt64ByName(NS_LITERAL_CSTRING("index_id"),
                                     mIndex->Id());
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    rv = insertStmt->BindInt64ByName(NS_LITERAL_CSTRING("object_data_id"),
                                     stmt->AsInt64(0));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    if (!mIndex->IsAutoIncrement()) {
      NS_NAMED_LITERAL_CSTRING(objectDataKey, "object_data_key");

      PRInt32 keyType;
      rv = stmt->GetTypeOfIndex(2, &keyType);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      if (keyType == mozIStorageStatement::VALUE_TYPE_INTEGER) {
        rv = insertStmt->BindInt64ByName(objectDataKey, stmt->AsInt64(2));
      }
      else if (keyType == mozIStorageStatement::VALUE_TYPE_TEXT) {
        nsString stringKey;
        rv = stmt->GetString(2, stringKey);
        NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

        rv = insertStmt->BindStringByName(objectDataKey, stringKey);
      }
      else {
        NS_NOTREACHED("Bad SQLite type!");
      }
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
    }

    const PRUint8* data;
    PRUint32 dataLength;
    rv = stmt->GetSharedBlob(1, &dataLength, &data);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    Key key;
    JSContext* cx = nsnull;
    rv = IDBObjectStore::GetKeyPathValueFromStructuredData(data, dataLength,
                                                           mIndex->KeyPath(),
                                                           &cx, key);
    NS_ENSURE_SUCCESS(rv, rv);

    if (key.IsUnset()) {
      continue;
    }

    NS_NAMED_LITERAL_CSTRING(value, "value");
    if (key.IsInt()) {
      rv = insertStmt->BindInt64ByName(value, key.IntValue());
    }
    else if (key.IsString()) {
      rv = insertStmt->BindStringByName(value, key.StringValue());
    }
    else {
      return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    rv = insertStmt->Execute();
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  return NS_OK;
}

nsresult
DeleteIndexHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_PRECONDITION(!NS_IsMainThread(), "Wrong thread!");

  nsCOMPtr<mozIStorageStatement> stmt =
    mTransaction->GetCachedStatement(
      "DELETE FROM object_store_index "
      "WHERE name = :name "
    );
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindStringByName(NS_LITERAL_CSTRING("name"), mName);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (NS_FAILED(stmt->Execute())) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_FOUND_ERR;
  }

  return NS_OK;
}

nsresult
GetAllHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  nsCString table;
  nsCString keyColumn;

  if (mObjectStore->IsAutoIncrement()) {
    table.AssignLiteral("ai_object_data");
    keyColumn.AssignLiteral("id");
  }
  else {
    table.AssignLiteral("object_data");
    keyColumn.AssignLiteral("key_value");
  }

  NS_NAMED_LITERAL_CSTRING(osid, "osid");
  NS_NAMED_LITERAL_CSTRING(lowerKeyName, "lower_key");
  NS_NAMED_LITERAL_CSTRING(upperKeyName, "upper_key");

  nsCAutoString keyRangeClause;
  if (!mLowerKey.IsUnset()) {
    keyRangeClause = NS_LITERAL_CSTRING(" AND ") + keyColumn;
    if (mLowerOpen) {
      keyRangeClause.AppendLiteral(" > :");
    }
    else {
      keyRangeClause.AppendLiteral(" >= :");
    }
    keyRangeClause.Append(lowerKeyName);
  }

  if (!mUpperKey.IsUnset()) {
    keyRangeClause += NS_LITERAL_CSTRING(" AND ") + keyColumn;
    if (mUpperOpen) {
      keyRangeClause.AppendLiteral(" < :");
    }
    else {
      keyRangeClause.AppendLiteral(" <= :");
    }
    keyRangeClause.Append(upperKeyName);
  }

  nsCAutoString limitClause;
  if (mLimit != PR_UINT32_MAX) {
    limitClause.AssignLiteral(" LIMIT ");
    limitClause.AppendInt(mLimit);
  }

  nsCString query = NS_LITERAL_CSTRING("SELECT data FROM ") + table +
                    NS_LITERAL_CSTRING(" WHERE object_store_id = :") + osid +
                    keyRangeClause + NS_LITERAL_CSTRING(" ORDER BY ") +
                    keyColumn + NS_LITERAL_CSTRING(" ASC") + limitClause;

  if (!mCloneBuffers.SetCapacity(50)) {
    NS_ERROR("Out of memory!");
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  nsCOMPtr<mozIStorageStatement> stmt = mTransaction->GetCachedStatement(query);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(osid, mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (!mLowerKey.IsUnset()) {
    if (mLowerKey.IsString()) {
      rv = stmt->BindStringByName(lowerKeyName, mLowerKey.StringValue());
    }
    else if (mLowerKey.IsInt()) {
      rv = stmt->BindInt64ByName(lowerKeyName, mLowerKey.IntValue());
    }
    else {
      NS_NOTREACHED("Bad key!");
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  if (!mUpperKey.IsUnset()) {
    if (mUpperKey.IsString()) {
      rv = stmt->BindStringByName(upperKeyName, mUpperKey.StringValue());
    }
    else if (mUpperKey.IsInt()) {
      rv = stmt->BindInt64ByName(upperKeyName, mUpperKey.IntValue());
    }
    else {
      NS_NOTREACHED("Bad key!");
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  PRBool hasResult;
  while (NS_SUCCEEDED((rv = stmt->ExecuteStep(&hasResult))) && hasResult) {
    if (mCloneBuffers.Capacity() == mCloneBuffers.Length()) {
      if (!mCloneBuffers.SetCapacity(mCloneBuffers.Capacity() * 2)) {
        NS_ERROR("Out of memory!");
        return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
      }
    }

    JSAutoStructuredCloneBuffer* buffer = mCloneBuffers.AppendElement();
    NS_ASSERTION(buffer, "Shouldn't fail if SetCapacity succeeded!");

    rv = IDBObjectStore::GetStructuredCloneDataFromStatement(stmt, 0, *buffer);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  return NS_OK;
}

nsresult
GetAllHelper::GetSuccessResult(JSContext* aCx,
                               jsval* aVal)
{
  NS_ASSERTION(mCloneBuffers.Length() <= mLimit, "Too many results!");

  nsresult rv = ConvertCloneBuffersToArray(aCx, mCloneBuffers, aVal);

  for (PRUint32 index = 0; index < mCloneBuffers.Length(); index++) {
    mCloneBuffers[index].clear(aCx);
  }

  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

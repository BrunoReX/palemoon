/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_UndoManager_h
#define mozilla_dom_UndoManager_h

#include "mozilla/dom/UndoManagerBinding.h"

#include "nsCycleCollectionParticipant.h"
#include "nsCOMPtr.h"
#include "nsCOMArray.h"
#include "nsTArray.h"
#include "nsWrapperCache.h"
#include "mozilla/dom/Nullable.h"
#include "nsIContent.h"

class nsIUndoManagerTransaction;
class nsITransactionManager;
class nsIMutationObserver;

namespace mozilla {
class ErrorResult;
namespace dom {

class UndoManager : public nsISupports,
                    public nsWrapperCache
{
  friend class TxnScopeGuard;
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(UndoManager)

  explicit UndoManager(nsIContent* aNode);

  void Transact(JSContext* aCx, nsIUndoManagerTransaction& aTransaction,
                bool aMerge, ErrorResult& aRv);
  void Undo(JSContext* aCx, ErrorResult& aRv);
  void Redo(JSContext* acx, ErrorResult& aRv);
  void Item(uint32_t aIndex,
            Nullable<nsTArray<nsRefPtr<nsIUndoManagerTransaction> > >& aItems,
            ErrorResult& aRv);
  uint32_t GetLength(ErrorResult& aRv);
  uint32_t GetPosition(ErrorResult& aRv);
  void ClearUndo(ErrorResult& aRv);
  void ClearRedo(ErrorResult& aRv);
  static bool PrefEnabled();
  void Disconnect();

  nsISupports* GetParentObject() const
  {
    return mHostNode;
  }

  JSObject* WrapObject(JSContext* aCx, JSObject* aScope,
                       bool* aTriedToWrap)
  {
    return mozilla::dom::UndoManagerBinding::Wrap(aCx, aScope, this,
                                                  aTriedToWrap);
  }

  nsITransactionManager* GetTransactionManager();

protected:
  virtual ~UndoManager();
  nsCOMPtr<nsITransactionManager> mTxnManager;
  nsCOMPtr<nsIContent> mHostNode;

  /**
   * Executes |aTransaction| as a manual transaction.
   */
  void ManualTransact(nsIUndoManagerTransaction* aTransaction,
                      ErrorResult& aRv);

  /**
   * Executes |aTransaction| as an automatic transaction.
   */
  void AutomaticTransact(nsIUndoManagerTransaction* aTransaction,
                         ErrorResult& aRv);

  /**
   * Appends the transactions in the undo transaction history at |aIndex|
   * to the array |aItems|.
   */
  void ItemInternal(uint32_t aIndex,
                    nsTArray<nsIUndoManagerTransaction*>& aItems,
                    ErrorResult& aRv);

  /**
   * Dispatches an event to the undo scope host.
   * @param aPreviousPosition The index of the transaction that
   *                          triggered the event.
   */
  void DispatchTransactionEvent(JSContext* aCx, const nsAString& aType,
                                uint32_t aPreviousPosition,
                                ErrorResult& aRv);
  bool mInTransaction;
  bool mIsDisconnected;
};

} // namespace dom
} // namespace mozilla

#endif

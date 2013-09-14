/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>                      // for printf

#include "SplitElementTxn.h"
#include "nsAString.h"
#include "nsDebug.h"                    // for NS_ASSERTION, etc
#include "nsEditor.h"                   // for nsEditor
#include "nsError.h"                    // for NS_ERROR_NOT_INITIALIZED, etc
#include "nsIContent.h"                 // for nsIContent
#include "nsIDOMCharacterData.h"        // for nsIDOMCharacterData
#include "nsIEditor.h"                  // for nsEditor::DebugDumpContent, etc
#include "nsISelection.h"               // for nsISelection
#include "nsISupportsUtils.h"           // for NS_ADDREF

using namespace mozilla;

#ifdef DEBUG
static bool gNoisy = false;
#endif


// note that aEditor is not refcounted
SplitElementTxn::SplitElementTxn()
  : EditTxn()
{
}

NS_IMPL_CYCLE_COLLECTION_INHERITED_2(SplitElementTxn, EditTxn,
                                     mParent,
                                     mNewLeftNode)

NS_IMPL_ADDREF_INHERITED(SplitElementTxn, EditTxn)
NS_IMPL_RELEASE_INHERITED(SplitElementTxn, EditTxn)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SplitElementTxn)
NS_INTERFACE_MAP_END_INHERITING(EditTxn)

NS_IMETHODIMP SplitElementTxn::Init(nsEditor   *aEditor,
                                    nsINode    *aNode,
                                    int32_t     aOffset)
{
  NS_ASSERTION(aEditor && aNode, "bad args");
  if (!aEditor || !aNode) { return NS_ERROR_NOT_INITIALIZED; }

  mEditor = aEditor;
  mExistingRightNode = do_QueryInterface(aNode);
  mOffset = aOffset;
  return NS_OK;
}

NS_IMETHODIMP SplitElementTxn::DoTransaction(void)
{
#ifdef DEBUG
  if (gNoisy)
  {
    printf("%p Do Split of node %p offset %d\n",
           static_cast<void*>(this),
           static_cast<void*>(mExistingRightNode.get()),
           mOffset);
  }
#endif

  NS_ASSERTION(mExistingRightNode && mEditor, "bad state");
  if (!mExistingRightNode || !mEditor) { return NS_ERROR_NOT_INITIALIZED; }

  // create a new node
  ErrorResult rv;
  mNewLeftNode = mExistingRightNode->CloneNode(false, rv);
  NS_ENSURE_SUCCESS(rv.ErrorCode(), rv.ErrorCode());
  NS_ASSERTION(mNewLeftNode, "could not create element.");
  NS_ENSURE_TRUE(mNewLeftNode, NS_ERROR_NULL_POINTER);
  mEditor->MarkNodeDirty(mExistingRightNode);

#ifdef DEBUG
  if (gNoisy)
  {
    printf("  created left node = %p\n",
           static_cast<void*>(mNewLeftNode.get()));
  }
#endif

  // get the parent node
  mParent = mExistingRightNode->GetParentNode();
  NS_ENSURE_TRUE(mParent, NS_ERROR_NULL_POINTER);

  // insert the new node
  nsresult result = mEditor->SplitNodeImpl(mExistingRightNode->AsDOMNode(),
                                           mOffset,
                                           mNewLeftNode->AsDOMNode(),
                                           mParent->AsDOMNode());
  NS_ENSURE_SUCCESS(result, result);
  if (mNewLeftNode) {
    bool bAdjustSelection;
    mEditor->ShouldTxnSetSelection(&bAdjustSelection);
    if (bAdjustSelection)
    {
      nsCOMPtr<nsISelection> selection;
      result = mEditor->GetSelection(getter_AddRefs(selection));
      NS_ENSURE_SUCCESS(result, result);
      NS_ENSURE_TRUE(selection, NS_ERROR_NULL_POINTER);
      result = selection->Collapse(mNewLeftNode->AsDOMNode(), mOffset);
    }
    else
    {
      // do nothing - dom range gravity will adjust selection
    }
  }
  return result;
}

NS_IMETHODIMP SplitElementTxn::UndoTransaction(void)
{
#ifdef DEBUG
  if (gNoisy) { 
    printf("%p Undo Split of existing node %p and new node %p offset %d\n",
           static_cast<void*>(this),
           static_cast<void*>(mExistingRightNode.get()),
           static_cast<void*>(mNewLeftNode.get()),
           mOffset);
  }
#endif

  NS_ASSERTION(mEditor && mExistingRightNode && mNewLeftNode && mParent, "bad state");
  if (!mEditor || !mExistingRightNode || !mNewLeftNode || !mParent) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // this assumes Do inserted the new node in front of the prior existing node
  nsresult result = mEditor->JoinNodesImpl(mExistingRightNode->AsDOMNode(),
                                           mNewLeftNode->AsDOMNode(),
                                           mParent->AsDOMNode(),
                                           false);
#ifdef DEBUG
  if (gNoisy) 
  { 
    printf("** after join left child node %p into right node %p\n",
           static_cast<void*>(mNewLeftNode.get()),
           static_cast<void*>(mExistingRightNode.get()));
    if (gNoisy) {mEditor->DebugDumpContent(); } // DEBUG
  }
  if (NS_SUCCEEDED(result))
  {
    if (gNoisy)
    {
      printf("  left node = %p removed\n",
             static_cast<void*>(mNewLeftNode.get()));
    }
  }
#endif

  return result;
}

/* redo cannot simply resplit the right node, because subsequent transactions
 * on the redo stack may depend on the left node existing in its previous state.
 */
NS_IMETHODIMP SplitElementTxn::RedoTransaction(void)
{
  NS_ASSERTION(mEditor && mExistingRightNode && mNewLeftNode && mParent, "bad state");
  if (!mEditor || !mExistingRightNode || !mNewLeftNode || !mParent) {
    return NS_ERROR_NOT_INITIALIZED;
  }

#ifdef DEBUG
  if (gNoisy) { 
    printf("%p Redo Split of existing node %p and new node %p offset %d\n",
           static_cast<void*>(this),
           static_cast<void*>(mExistingRightNode.get()),
           static_cast<void*>(mNewLeftNode.get()),
           mOffset);
    if (gNoisy) {mEditor->DebugDumpContent(); } // DEBUG
  }
#endif

  // first, massage the existing node so it is in its post-split state
  nsCOMPtr<nsIDOMCharacterData>rightNodeAsText = do_QueryInterface(mExistingRightNode);
  if (rightNodeAsText)
  {
    nsresult result = rightNodeAsText->DeleteData(0, mOffset);
    NS_ENSURE_SUCCESS(result, result);
#ifdef DEBUG
    if (gNoisy) 
    { 
      printf("** after delete of text in right text node %p offset %d\n",
             static_cast<void*>(rightNodeAsText.get()),
             mOffset);
      mEditor->DebugDumpContent();  // DEBUG
    }
#endif
  }
  else
  {
    nsCOMPtr<nsINode> child = mExistingRightNode->GetFirstChild();
    for (int32_t i=0; i<mOffset; i++)
    {
      if (!child) {return NS_ERROR_NULL_POINTER;}
      ErrorResult rv;
      mExistingRightNode->RemoveChild(*child, rv);
      if (NS_SUCCEEDED(rv.ErrorCode()))
      {
        mNewLeftNode->AppendChild(*child, rv);
        NS_ENSURE_SUCCESS(rv.ErrorCode(), rv.ErrorCode());
#ifdef DEBUG
        if (gNoisy)
        {
          printf("** move child node %p from right node %p to left node %p\n",
                 static_cast<void*>(child),
                 static_cast<void*>(mExistingRightNode.get()),
                 static_cast<void*>(mNewLeftNode.get()));
          if (gNoisy) {mEditor->DebugDumpContent(); } // DEBUG
        }
#endif
      }
      child = child->GetNextSibling();
    }
  }
  // second, re-insert the left node into the tree
  ErrorResult rv;
  mParent->InsertBefore(*mNewLeftNode, mExistingRightNode, rv);
#ifdef DEBUG
  if (gNoisy)
  {
    printf("** reinsert left child node %p before right node %p\n",
           static_cast<void*>(mNewLeftNode.get()),
           static_cast<void*>(mExistingRightNode.get()));
    if (gNoisy) {mEditor->DebugDumpContent(); } // DEBUG
  }
#endif
  return rv.ErrorCode();
}


NS_IMETHODIMP SplitElementTxn::GetTxnDescription(nsAString& aString)
{
  aString.AssignLiteral("SplitElementTxn");
  return NS_OK;
}

NS_IMETHODIMP SplitElementTxn::GetNewNode(nsINode **aNewNode)
{
  NS_ENSURE_TRUE(aNewNode, NS_ERROR_NULL_POINTER);
  NS_ENSURE_TRUE(mNewLeftNode, NS_ERROR_NOT_INITIALIZED);
  *aNewNode = mNewLeftNode;
  NS_ADDREF(*aNewNode);
  return NS_OK;
}

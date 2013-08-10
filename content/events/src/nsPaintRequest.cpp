/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsPaintRequest.h"

#include "nsDOMClassInfoID.h"
#include "nsClientRect.h"
#include "nsIFrame.h"
#include "nsContentUtils.h"
#include "mozilla/dom/PaintRequestListBinding.h"

DOMCI_DATA(PaintRequest, nsPaintRequest)

NS_INTERFACE_TABLE_HEAD(nsPaintRequest)
  NS_INTERFACE_TABLE1(nsPaintRequest, nsIDOMPaintRequest)
  NS_INTERFACE_TABLE_TO_MAP_SEGUE
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(PaintRequest)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(nsPaintRequest)
NS_IMPL_RELEASE(nsPaintRequest)

NS_IMETHODIMP
nsPaintRequest::GetClientRect(nsIDOMClientRect** aResult)
{
  nsRefPtr<nsClientRect> clientRect = new nsClientRect();
  if (!clientRect)
    return NS_ERROR_OUT_OF_MEMORY;
  clientRect->SetLayoutRect(mRequest.mRect);
  clientRect.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsPaintRequest::GetReason(nsAString& aResult)
{
  aResult.AssignLiteral("repaint");
  return NS_OK;
}

DOMCI_DATA(PaintRequestList, nsPaintRequestList)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_1(nsPaintRequestList, mParent)

NS_INTERFACE_TABLE_HEAD(nsPaintRequestList)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_TABLE1(nsPaintRequestList, nsIDOMPaintRequestList)
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(nsPaintRequestList)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(PaintRequestList)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsPaintRequestList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsPaintRequestList)

JSObject*
nsPaintRequestList::WrapObject(JSContext *cx, JSObject *scope,
                               bool *triedToWrap)
{
  return mozilla::dom::PaintRequestListBinding::Wrap(cx, scope, this,
                                                     triedToWrap);
}

NS_IMETHODIMP    
nsPaintRequestList::GetLength(uint32_t* aLength)
{
  *aLength = Length();
  return NS_OK;
}

NS_IMETHODIMP    
nsPaintRequestList::Item(uint32_t aIndex, nsIDOMPaintRequest** aReturn)
{
  NS_IF_ADDREF(*aReturn = Item(aIndex));
  return NS_OK;
}

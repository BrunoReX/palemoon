/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/EventTarget.h"
#include "nsEventListenerManager.h"


namespace mozilla {
namespace dom {

void
EventTarget::RemoveEventListener(const nsAString& aType,
                                 nsIDOMEventListener* aListener,
                                 bool aUseCapture,
                                 ErrorResult& aRv)
{
  nsEventListenerManager* elm = GetListenerManager(false);
  if (elm) {
    elm->RemoveEventListener(aType, aListener, aUseCapture);
  }
}

EventHandlerNonNull*
EventTarget::GetEventHandler(nsIAtom* aType)
{
  nsEventListenerManager* elm = GetListenerManager(false);
  return elm ? elm->GetEventHandler(aType) : nullptr;
}

void
EventTarget::SetEventHandler(nsIAtom* aType, EventHandlerNonNull* aHandler,
                             ErrorResult& rv)
{
  rv = GetListenerManager(true)->SetEventHandler(aType, aHandler);
}

} // namespace dom
} // namespace mozilla

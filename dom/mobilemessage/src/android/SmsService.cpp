/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SmsMessage.h"
#include "SmsService.h"
#include "SmsSegmentInfo.h"
#include "AndroidBridge.h"
#include "jsapi.h"

namespace mozilla {
namespace dom {
namespace mobilemessage {

NS_IMPL_ISUPPORTS1(SmsService, nsISmsService)

NS_IMETHODIMP
SmsService::HasSupport(bool* aHasSupport)
{
  *aHasSupport = true;
  return NS_OK;
}

NS_IMETHODIMP
SmsService::GetSegmentInfoForText(const nsAString & aText,
                                  nsIDOMMozSmsSegmentInfo** aResult)
{
  if (!AndroidBridge::Bridge()) {
    return NS_ERROR_FAILURE;
  }

  SmsSegmentInfoData data;
  nsresult rv = AndroidBridge::Bridge()->GetSegmentInfoForText(aText, &data);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMMozSmsSegmentInfo> info = new SmsSegmentInfo(data);
  info.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
SmsService::Send(const nsAString& aNumber, const nsAString& aMessage,
                 nsIMobileMessageCallback* aRequest)
{
  if (!AndroidBridge::Bridge()) {
    return NS_OK;
  }

  AndroidBridge::Bridge()->SendMessage(aNumber, aMessage, aRequest);
  return NS_OK;
}

} // namespace mobilemessage
} // namespace dom
} // namespace mozilla

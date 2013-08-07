/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mounir Lamouri <mounir.lamouri@mozilla.com> (Original Author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "mozilla/dom/ContentChild.h"
#include "SmsIPCService.h"
#include "nsXULAppAPI.h"
#include "jsapi.h"
#include "mozilla/dom/sms/SmsChild.h"
#include "mozilla/dom/sms/SmsMessage.h"
#include "SmsFilter.h"

namespace mozilla {
namespace dom {
namespace sms {

PSmsChild* SmsIPCService::sSmsChild = nsnull;

NS_IMPL_ISUPPORTS2(SmsIPCService, nsISmsService, nsISmsDatabaseService)

/* static */ PSmsChild*
SmsIPCService::GetSmsChild()
{
  if (!sSmsChild) {
    sSmsChild = ContentChild::GetSingleton()->SendPSmsConstructor();
  }

  return sSmsChild;
}

/*
 * Implementation of nsISmsService.
 */
NS_IMETHODIMP
SmsIPCService::HasSupport(bool* aHasSupport)
{
  GetSmsChild()->SendHasSupport(aHasSupport);

  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::GetNumberOfMessagesForText(const nsAString& aText, PRUint16* aResult)
{
  GetSmsChild()->SendGetNumberOfMessagesForText(nsString(aText), aResult);

  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::Send(const nsAString& aNumber, const nsAString& aMessage,
                    PRInt32 aRequestId, PRUint64 aProcessId)
{
  GetSmsChild()->SendSendMessage(nsString(aNumber), nsString(aMessage),
                                 aRequestId, ContentChild::GetSingleton()->GetID());

  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::CreateSmsMessage(PRInt32 aId,
                                const nsAString& aDelivery,
                                const nsAString& aSender,
                                const nsAString& aReceiver,
                                const nsAString& aBody,
                                const jsval& aTimestamp,
                                JSContext* aCx,
                                nsIDOMMozSmsMessage** aMessage)
{
  return SmsMessage::Create(
    aId, aDelivery, aSender, aReceiver, aBody, aTimestamp, aCx, aMessage);
}

/*
 * Implementation of nsISmsDatabaseService.
 */
NS_IMETHODIMP
SmsIPCService::SaveSentMessage(const nsAString& aReceiver,
                               const nsAString& aBody,
                               PRUint64 aDate, PRInt32* aId)
{
  GetSmsChild()->SendSaveSentMessage(nsString(aReceiver), nsString(aBody),
                                     aDate, aId);

  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::GetMessageMoz(PRInt32 aMessageId, PRInt32 aRequestId,
                             PRUint64 aProcessId)
{
  GetSmsChild()->SendGetMessage(aMessageId, aRequestId,
                                ContentChild::GetSingleton()->GetID());
  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::DeleteMessage(PRInt32 aMessageId, PRInt32 aRequestId,
                             PRUint64 aProcessId)
{
  GetSmsChild()->SendDeleteMessage(aMessageId, aRequestId,
                                   ContentChild::GetSingleton()->GetID());
  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::CreateMessageList(nsIDOMMozSmsFilter* aFilter, bool aReverse,
                                 PRInt32 aRequestId, PRUint64 aProcessId)
{
  SmsFilter* filter = static_cast<SmsFilter*>(aFilter);
  GetSmsChild()->SendCreateMessageList(filter->GetData(), aReverse, aRequestId,
                                       ContentChild::GetSingleton()->GetID());

  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::GetNextMessageInList(PRInt32 aListId, PRInt32 aRequestId,
                                    PRUint64 aProcessId)
{
  GetSmsChild()->SendGetNextMessageInList(aListId, aRequestId,
                                          ContentChild::GetSingleton()->GetID());
  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::ClearMessageList(PRInt32 aListId)
{
  GetSmsChild()->SendClearMessageList(aListId);
  return NS_OK;
}

} // namespace sms
} // namespace dom
} // namespace mozilla

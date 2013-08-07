/* vim: set shiftwidth=2 tabstop=8 autoindent cindent expandtab: */
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
 * The Original Code is nsDOMTransitionEvent.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation (original author)
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

#include "nsDOMAnimationEvent.h"
#include "nsGUIEvent.h"
#include "nsDOMClassInfoID.h"
#include "nsIClassInfo.h"
#include "nsIXPCScriptable.h"

nsDOMAnimationEvent::nsDOMAnimationEvent(nsPresContext *aPresContext,
                                         nsAnimationEvent *aEvent)
  : nsDOMEvent(aPresContext, aEvent ? aEvent
                                    : new nsAnimationEvent(false, 0,
                                                           EmptyString(),
                                                           0.0))
{
  if (aEvent) {
    mEventIsInternal = false;
  }
  else {
    mEventIsInternal = true;
    mEvent->time = PR_Now();
  }
}

nsDOMAnimationEvent::~nsDOMAnimationEvent()
{
  if (mEventIsInternal) {
    delete AnimationEvent();
    mEvent = nsnull;
  }
}

DOMCI_DATA(AnimationEvent, nsDOMAnimationEvent)

NS_INTERFACE_MAP_BEGIN(nsDOMAnimationEvent)
  NS_INTERFACE_MAP_ENTRY(nsIDOMAnimationEvent)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(AnimationEvent)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEvent)

NS_IMPL_ADDREF_INHERITED(nsDOMAnimationEvent, nsDOMEvent)
NS_IMPL_RELEASE_INHERITED(nsDOMAnimationEvent, nsDOMEvent)

NS_IMETHODIMP
nsDOMAnimationEvent::GetAnimationName(nsAString & aAnimationName)
{
  aAnimationName = AnimationEvent()->animationName;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAnimationEvent::GetElapsedTime(float *aElapsedTime)
{
  *aElapsedTime = AnimationEvent()->elapsedTime;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAnimationEvent::InitAnimationEvent(const nsAString & typeArg,
                                        bool canBubbleArg,
                                        bool cancelableArg,
                                        const nsAString & animationNameArg,
                                        float elapsedTimeArg)
{
  nsresult rv = nsDOMEvent::InitEvent(typeArg, canBubbleArg, cancelableArg);
  NS_ENSURE_SUCCESS(rv, rv);

  AnimationEvent()->animationName = animationNameArg;
  AnimationEvent()->elapsedTime = elapsedTimeArg;

  return NS_OK;
}

nsresult
NS_NewDOMAnimationEvent(nsIDOMEvent **aInstancePtrResult,
                        nsPresContext *aPresContext,
                        nsAnimationEvent *aEvent)
{
  nsDOMAnimationEvent *it = new nsDOMAnimationEvent(aPresContext, aEvent);
  return CallQueryInterface(it, aInstancePtrResult);
}

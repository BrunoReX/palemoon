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
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#ifndef nsEventListenerManager_h__
#define nsEventListenerManager_h__

#include "nsEventListenerManager.h"
#include "jsapi.h"
#include "nsCOMPtr.h"
#include "nsIDOMEventTarget.h"
#include "nsIDOMEventListener.h"
#include "nsAutoPtr.h"
#include "nsCOMArray.h"
#include "nsHashtable.h"
#include "nsIScriptContext.h"
#include "nsCycleCollectionParticipant.h"
#include "nsTObserverArray.h"
#include "nsGUIEvent.h"
#include "nsIJSEventListener.h"

class nsIDOMEvent;
class nsIAtom;
class nsIWidget;
struct nsPoint;
struct EventTypeData;
class nsEventTargetChainItem;
class nsPIDOMWindow;
class nsCxPusher;
class nsIEventListenerInfo;
class nsIDocument;

typedef struct {
  nsRefPtr<nsIDOMEventListener> mListener;
  PRUint32                      mEventType;
  nsCOMPtr<nsIAtom>             mTypeAtom;
  PRUint16                      mFlags;
  PRPackedBool                  mHandlerIsString;

  nsIJSEventListener* GetJSListener() const {
    return (mFlags & NS_PRIV_EVENT_FLAG_SCRIPT) ?
      static_cast<nsIJSEventListener *>(mListener.get()) : nsnull;
  }
} nsListenerStruct;

/*
 * Event listener manager
 */

class nsEventListenerManager
{

public:
  nsEventListenerManager(nsISupports* aTarget);
  virtual ~nsEventListenerManager();

  NS_INLINE_DECL_REFCOUNTING(nsEventListenerManager)

  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(nsEventListenerManager)

  void AddEventListener(const nsAString& aType,
                        nsIDOMEventListener* aListener,
                        PRBool aUseCapture,
                        PRBool aWantsUntrusted);
  void RemoveEventListener(const nsAString& aType,
                           nsIDOMEventListener* aListener,
                           PRBool aUseCapture);

  /**
  * Sets events listeners of all types. 
  * @param an event listener
  */
  void AddEventListenerByType(nsIDOMEventListener *aListener,
                              const nsAString& type,
                              PRInt32 aFlags);
  void RemoveEventListenerByType(nsIDOMEventListener *aListener,
                                 const nsAString& type,
                                 PRInt32 aFlags);

  /**
   * Sets the current "inline" event listener for aName to be a
   * function compiled from aFunc if !aDeferCompilation.  If
   * aDeferCompilation, then we assume that we can get the string from
   * mTarget later and compile lazily.
   */
  // XXXbz does that play correctly with nodes being adopted across
  // documents?  Need to double-check the spec here.
  nsresult AddScriptEventListener(nsIAtom *aName,
                                  const nsAString& aFunc,
                                  PRUint32 aLanguage,
                                  PRBool aDeferCompilation,
                                  PRBool aPermitUntrustedEvents);
  /**
   * Remove the current "inline" event listener for aName.
   */
  void RemoveScriptEventListener(nsIAtom *aName);

  void HandleEvent(nsPresContext* aPresContext,
                   nsEvent* aEvent, 
                   nsIDOMEvent** aDOMEvent,
                   nsIDOMEventTarget* aCurrentTarget,
                   PRUint32 aFlags,
                   nsEventStatus* aEventStatus,
                   nsCxPusher* aPusher)
  {
    if (mListeners.IsEmpty() || aEvent->flags & NS_EVENT_FLAG_STOP_DISPATCH) {
      return;
    }

    if (!mMayHaveCapturingListeners &&
        !(aEvent->flags & NS_EVENT_FLAG_BUBBLE)) {
      return;
    }

    if (!mMayHaveSystemGroupListeners &&
        aFlags & NS_EVENT_FLAG_SYSTEM_EVENT) {
      return;
    }

    // Check if we already know that there is no event listener for the event.
    if (mNoListenerForEvent == aEvent->message &&
        (mNoListenerForEvent != NS_USER_DEFINED_EVENT ||
         mNoListenerForEventAtom == aEvent->userType)) {
      return;
    }
    HandleEventInternal(aPresContext, aEvent, aDOMEvent, aCurrentTarget,
                        aFlags, aEventStatus, aPusher);
  }

  void HandleEventInternal(nsPresContext* aPresContext,
                           nsEvent* aEvent, 
                           nsIDOMEvent** aDOMEvent,
                           nsIDOMEventTarget* aCurrentTarget,
                           PRUint32 aFlags,
                           nsEventStatus* aEventStatus,
                           nsCxPusher* aPusher);

  /**
   * Tells the event listener manager that its target (which owns it) is
   * no longer using it (and could go away).
   */
  void Disconnect();

  /**
   * Allows us to quickly determine if we have mutation listeners registered.
   */
  PRBool HasMutationListeners();

  /**
   * Allows us to quickly determine whether we have unload or beforeunload
   * listeners registered.
   */
  PRBool HasUnloadListeners();

  /**
   * Returns the mutation bits depending on which mutation listeners are
   * registered to this listener manager.
   * @note If a listener is an nsIDOMMutationListener, all possible mutation
   *       event bits are returned. All bits are also returned if one of the
   *       event listeners is registered to handle DOMSubtreeModified events.
   */
  PRUint32 MutationListenerBits();

  /**
   * Returns PR_TRUE if there is at least one event listener for aEventName.
   */
  PRBool HasListenersFor(const nsAString& aEventName);

  /**
   * Returns PR_TRUE if there is at least one event listener.
   */
  PRBool HasListeners();

  /**
   * Sets aList to the list of nsIEventListenerInfo objects representing the
   * listeners managed by this listener manager.
   */
  nsresult GetListenerInfo(nsCOMArray<nsIEventListenerInfo>* aList);

  PRUint32 GetIdentifierForEvent(nsIAtom* aEvent);

  static void Shutdown();

  /**
   * Returns PR_TRUE if there may be a paint event listener registered,
   * PR_FALSE if there definitely isn't.
   */
  PRBool MayHavePaintEventListener() { return mMayHavePaintEventListener; }

  /**
   * Returns PR_TRUE if there may be a MozAudioAvailable event listener registered,
   * PR_FALSE if there definitely isn't.
   */
  PRBool MayHaveAudioAvailableEventListener() { return mMayHaveAudioAvailableEventListener; }

  /**
   * Returns PR_TRUE if there may be a touch event listener registered,
   * PR_FALSE if there definitely isn't.
   */
  PRBool MayHaveTouchEventListener() { return mMayHaveTouchEventListener; }

  PRInt64 SizeOf() const;
protected:
  nsresult HandleEventSubType(nsListenerStruct* aListenerStruct,
                              nsIDOMEventListener* aListener,
                              nsIDOMEvent* aDOMEvent,
                              nsIDOMEventTarget* aCurrentTarget,
                              PRUint32 aPhaseFlags,
                              nsCxPusher* aPusher);

  /**
   * Compile the "inline" event listener for aListenerStruct.  The
   * body of the listener can be provided in aBody; if this is null we
   * will look for it on mTarget.
   */
  nsresult CompileEventHandlerInternal(nsListenerStruct *aListenerStruct,
                                       PRBool aNeedsCxPush,
                                       const nsAString* aBody);

  /**
   * Find the nsListenerStruct for the "inline" event listener for aTypeAtom.
   */
  nsListenerStruct* FindJSEventListener(PRUint32 aEventType, nsIAtom* aTypeAtom);

  /**
   * Set the "inline" event listener for aName to aHandler.  aHandler
   * may be null to indicate that we should lazily get and compile the
   * string for this listener.  The nsListenerStruct that results, if
   * any, is returned in aListenerStruct.
   */
  nsresult SetJSEventListener(nsIScriptContext *aContext,
                              void *aScopeGlobal,
                              nsIAtom* aName,
                              JSObject *aHandler,
                              PRBool aPermitUntrustedEvents,
                              nsListenerStruct **aListenerStruct);

public:
  /**
   * Set the "inline" event listener for aEventName to |v|.  This
   * might actually remove the event listener, depending on the value
   * of |v|.
   */
  nsresult SetJSEventListenerToJsval(nsIAtom *aEventName, JSContext *cx,
                                     JSObject *aScope, const jsval &v);
  /**
   * Get the value of the "inline" event listener for aEventName.
   * This may cause lazy compilation if the listener is uncompiled.
   */
  void GetJSEventListener(nsIAtom *aEventName, jsval *vp);

protected:
  void AddEventListener(nsIDOMEventListener *aListener, 
                        PRUint32 aType,
                        nsIAtom* aTypeAtom,
                        PRInt32 aFlags);
  void RemoveEventListener(nsIDOMEventListener *aListener,
                           PRUint32 aType,
                           nsIAtom* aUserType,
                           PRInt32 aFlags);
  void RemoveAllListeners();
  const EventTypeData* GetTypeDataForIID(const nsIID& aIID);
  const EventTypeData* GetTypeDataForEventName(nsIAtom* aName);
  nsPIDOMWindow* GetInnerWindowForTarget();

  PRUint32 mMayHavePaintEventListener : 1;
  PRUint32 mMayHaveMutationListeners : 1;
  PRUint32 mMayHaveCapturingListeners : 1;
  PRUint32 mMayHaveSystemGroupListeners : 1;
  PRUint32 mMayHaveAudioAvailableEventListener : 1;
  PRUint32 mMayHaveTouchEventListener : 1;
  PRUint32 mNoListenerForEvent : 26;

  nsAutoTObserverArray<nsListenerStruct, 2> mListeners;
  nsISupports*                              mTarget;  //WEAK
  nsCOMPtr<nsIAtom>                         mNoListenerForEventAtom;

  static PRUint32                           mInstanceCount;
  static jsid                               sAddListenerID;

  friend class nsEventTargetChainItem;
  static PRUint32                           sCreatedCount;
};

#endif // nsEventListenerManager_h__

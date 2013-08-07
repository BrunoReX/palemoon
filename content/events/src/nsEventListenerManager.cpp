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

#include "nsISupports.h"
#include "nsGUIEvent.h"
#include "nsDOMEvent.h"
#include "nsEventListenerManager.h"
#include "nsCaret.h"
#include "nsIDOMNSEvent.h"
#include "nsIDOMEventListener.h"
#include "nsITextControlFrame.h"
#include "nsGkAtoms.h"
#include "nsPIDOMWindow.h"
#include "nsIPrivateDOMEvent.h"
#include "nsIJSEventListener.h"
#include "prmem.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptRuntime.h"
#include "nsLayoutUtils.h"
#include "nsINameSpaceManager.h"
#include "nsIContent.h"
#include "mozilla/dom/Element.h"
#include "nsIFrame.h"
#include "nsIView.h"
#include "nsIViewManager.h"
#include "nsCOMPtr.h"
#include "nsIServiceManager.h"
#include "nsIScriptSecurityManager.h"
#include "nsDOMError.h"
#include "nsIJSContextStack.h"
#include "nsIDocument.h"
#include "nsIPresShell.h"
#include "nsMutationEvent.h"
#include "nsIXPConnect.h"
#include "nsDOMCID.h"
#include "nsIScriptObjectOwner.h" // for nsIScriptEventHandlerOwner
#include "nsFocusManager.h"
#include "nsIDOMElement.h"
#include "nsContentUtils.h"
#include "nsJSUtils.h"
#include "nsContentCID.h"
#include "nsEventDispatcher.h"
#include "nsDOMJSUtils.h"
#include "nsDOMScriptObjectHolder.h"
#include "nsDataHashtable.h"
#include "nsCOMArray.h"
#include "nsEventListenerService.h"
#include "nsDOMEvent.h"
#include "nsIContentSecurityPolicy.h"
#include "nsJSEnvironment.h"

using namespace mozilla::dom;

#define EVENT_TYPE_EQUALS( ls, type, userType ) \
  (ls->mEventType == type && \
  (ls->mEventType != NS_USER_DEFINED_EVENT || ls->mTypeAtom == userType))

static NS_DEFINE_CID(kDOMScriptObjectFactoryCID,
                     NS_DOM_SCRIPT_OBJECT_FACTORY_CID);

static const PRUint32 kAllMutationBits =
  NS_EVENT_BITS_MUTATION_SUBTREEMODIFIED |
  NS_EVENT_BITS_MUTATION_NODEINSERTED |
  NS_EVENT_BITS_MUTATION_NODEREMOVED |
  NS_EVENT_BITS_MUTATION_NODEREMOVEDFROMDOCUMENT |
  NS_EVENT_BITS_MUTATION_NODEINSERTEDINTODOCUMENT |
  NS_EVENT_BITS_MUTATION_ATTRMODIFIED |
  NS_EVENT_BITS_MUTATION_CHARACTERDATAMODIFIED;

static PRUint32
MutationBitForEventType(PRUint32 aEventType)
{
  switch (aEventType) {
    case NS_MUTATION_SUBTREEMODIFIED:
      return NS_EVENT_BITS_MUTATION_SUBTREEMODIFIED;
    case NS_MUTATION_NODEINSERTED:
      return NS_EVENT_BITS_MUTATION_NODEINSERTED;
    case NS_MUTATION_NODEREMOVED:
      return NS_EVENT_BITS_MUTATION_NODEREMOVED;
    case NS_MUTATION_NODEREMOVEDFROMDOCUMENT:
      return NS_EVENT_BITS_MUTATION_NODEREMOVEDFROMDOCUMENT;
    case NS_MUTATION_NODEINSERTEDINTODOCUMENT:
      return NS_EVENT_BITS_MUTATION_NODEINSERTEDINTODOCUMENT;
    case NS_MUTATION_ATTRMODIFIED:
      return NS_EVENT_BITS_MUTATION_ATTRMODIFIED;
    case NS_MUTATION_CHARACTERDATAMODIFIED:
      return NS_EVENT_BITS_MUTATION_CHARACTERDATAMODIFIED;
    default:
      break;
  }
  return 0;
}

PRUint32 nsEventListenerManager::sCreatedCount = 0;

nsEventListenerManager::nsEventListenerManager(nsISupports* aTarget) :
  mMayHavePaintEventListener(PR_FALSE),
  mMayHaveMutationListeners(PR_FALSE),
  mMayHaveCapturingListeners(PR_FALSE),
  mMayHaveSystemGroupListeners(PR_FALSE),
  mMayHaveAudioAvailableEventListener(PR_FALSE),
  mMayHaveTouchEventListener(PR_FALSE),
  mNoListenerForEvent(0),
  mTarget(aTarget)
{
  NS_ASSERTION(aTarget, "unexpected null pointer");

  ++sCreatedCount;
}

nsEventListenerManager::~nsEventListenerManager() 
{
  // If your code fails this assertion, a possible reason is that
  // a class did not call our Disconnect() manually. Note that
  // this class can have Disconnect called in one of two ways:
  // if it is part of a cycle, then in Unlink() (such a cycle
  // would be with one of the listeners, not mTarget which is weak).
  // If not part of a cycle, then Disconnect must be called manually,
  // typically from the destructor of the owner class (mTarget).
  // XXX azakai: Is there any reason to not just call Disconnect
  //             from right here, if not previously called?
  NS_ASSERTION(!mTarget, "didn't call Disconnect");
  RemoveAllListeners();

}

void
nsEventListenerManager::RemoveAllListeners()
{
  mListeners.Clear();
}

void
nsEventListenerManager::Shutdown()
{
  nsDOMEvent::Shutdown();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsEventListenerManager)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(nsEventListenerManager, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(nsEventListenerManager, Release)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_BEGIN(nsEventListenerManager)
  PRUint32 count = tmp->mListeners.Length();
  for (PRUint32 i = 0; i < count; i++) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mListeners[i] mListener");
    cb.NoteXPCOMChild(tmp->mListeners.ElementAt(i).mListener.get());
  }  
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_NATIVE(nsEventListenerManager)
  tmp->Disconnect();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END


nsPIDOMWindow*
nsEventListenerManager::GetInnerWindowForTarget()
{
  nsCOMPtr<nsINode> node = do_QueryInterface(mTarget);
  if (node) {
    // XXX sXBL/XBL2 issue -- do we really want the owner here?  What
    // if that's the XBL document?
    nsIDocument* document = node->GetOwnerDoc();
    if (document)
      return document->GetInnerWindow();
  }

  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(mTarget);
  if (window) {
    NS_ASSERTION(window->IsInnerWindow(), "Target should not be an outer window");
    return window;
  }

  return nsnull;
}

void
nsEventListenerManager::AddEventListener(nsIDOMEventListener *aListener,
                                         PRUint32 aType,
                                         nsIAtom* aTypeAtom,
                                         PRInt32 aFlags)
{
  NS_ABORT_IF_FALSE(aType && aTypeAtom, "Missing type");

  if (!aListener) {
    return;
  }

  nsRefPtr<nsIDOMEventListener> kungFuDeathGrip = aListener;

  nsListenerStruct* ls;
  PRUint32 count = mListeners.Length();
  for (PRUint32 i = 0; i < count; i++) {
    ls = &mListeners.ElementAt(i);
    if (ls->mListener == aListener && ls->mFlags == aFlags &&
        EVENT_TYPE_EQUALS(ls, aType, aTypeAtom)) {
      return;
    }
  }

  mNoListenerForEvent = NS_EVENT_TYPE_NULL;
  mNoListenerForEventAtom = nsnull;

  ls = mListeners.AppendElement();
  ls->mListener = aListener;
  ls->mEventType = aType;
  ls->mTypeAtom = aTypeAtom;
  ls->mFlags = aFlags;
  ls->mHandlerIsString = PR_FALSE;

  if (aFlags & NS_EVENT_FLAG_SYSTEM_EVENT) {
    mMayHaveSystemGroupListeners = PR_TRUE;
  }
  if (aFlags & NS_EVENT_FLAG_CAPTURE) {
    mMayHaveCapturingListeners = PR_TRUE;
  }

  if (aType == NS_AFTERPAINT) {
    mMayHavePaintEventListener = PR_TRUE;
    nsPIDOMWindow* window = GetInnerWindowForTarget();
    if (window) {
      window->SetHasPaintEventListeners();
    }
#ifdef MOZ_MEDIA
  } else if (aType == NS_MOZAUDIOAVAILABLE) {
    mMayHaveAudioAvailableEventListener = PR_TRUE;
    nsPIDOMWindow* window = GetInnerWindowForTarget();
    if (window) {
      window->SetHasAudioAvailableEventListeners();
    }
#endif // MOZ_MEDIA
  } else if (aType >= NS_MUTATION_START && aType <= NS_MUTATION_END) {
    // For mutation listeners, we need to update the global bit on the DOM window.
    // Otherwise we won't actually fire the mutation event.
    mMayHaveMutationListeners = PR_TRUE;
    // Go from our target to the nearest enclosing DOM window.
    nsPIDOMWindow* window = GetInnerWindowForTarget();
    if (window) {
      // If aType is NS_MUTATION_SUBTREEMODIFIED, we need to listen all
      // mutations. nsContentUtils::HasMutationListeners relies on this.
      window->SetMutationListeners((aType == NS_MUTATION_SUBTREEMODIFIED) ?
                                   kAllMutationBits :
                                   MutationBitForEventType(aType));
    }
  } else if (aTypeAtom == nsGkAtoms::ondeviceorientation ||
             aTypeAtom == nsGkAtoms::ondevicemotion) {
    nsPIDOMWindow* window = GetInnerWindowForTarget();
    if (window)
      window->SetHasOrientationEventListener();
  } else if ((aType >= NS_MOZTOUCH_DOWN && aType <= NS_MOZTOUCH_UP) ||
             (aTypeAtom == nsGkAtoms::ontouchstart ||
              aTypeAtom == nsGkAtoms::ontouchend ||
              aTypeAtom == nsGkAtoms::ontouchmove ||
              aTypeAtom == nsGkAtoms::ontouchenter ||
              aTypeAtom == nsGkAtoms::ontouchleave ||
              aTypeAtom == nsGkAtoms::ontouchcancel)) {
    mMayHaveTouchEventListener = PR_TRUE;
    nsPIDOMWindow* window = GetInnerWindowForTarget();
    if (window)
      window->SetHasTouchEventListeners();
  }
}

void
nsEventListenerManager::RemoveEventListener(nsIDOMEventListener *aListener, 
                                            PRUint32 aType,
                                            nsIAtom* aUserType,
                                            PRInt32 aFlags)
{
  if (!aListener || !aType) {
    return;
  }

  nsListenerStruct* ls;
  aFlags &= ~NS_PRIV_EVENT_UNTRUSTED_PERMITTED;

  PRUint32 count = mListeners.Length();
  for (PRUint32 i = 0; i < count; ++i) {
    ls = &mListeners.ElementAt(i);
    if (ls->mListener == aListener &&
        ((ls->mFlags & ~NS_PRIV_EVENT_UNTRUSTED_PERMITTED) == aFlags) &&
        EVENT_TYPE_EQUALS(ls, aType, aUserType)) {
      nsRefPtr<nsEventListenerManager> kungFuDeathGrip = this;
      mListeners.RemoveElementAt(i);
      mNoListenerForEvent = NS_EVENT_TYPE_NULL;
      mNoListenerForEventAtom = nsnull;
      break;
    }
  }
}

static inline PRBool
ListenerCanHandle(nsListenerStruct* aLs, nsEvent* aEvent)
{
  // This is slightly different from EVENT_TYPE_EQUALS in that it returns
  // true even when aEvent->message == NS_USER_DEFINED_EVENT and
  // aLs=>mEventType != NS_USER_DEFINED_EVENT as long as the atoms are the same
  return aEvent->message == NS_USER_DEFINED_EVENT ?
    (aLs->mTypeAtom == aEvent->userType) :
    (aLs->mEventType == aEvent->message);
}

void
nsEventListenerManager::AddEventListenerByType(nsIDOMEventListener *aListener, 
                                               const nsAString& aType,
                                               PRInt32 aFlags)
{
  nsCOMPtr<nsIAtom> atom = do_GetAtom(NS_LITERAL_STRING("on") + aType);
  PRUint32 type = nsContentUtils::GetEventId(atom);
  AddEventListener(aListener, type, atom, aFlags);
}

void
nsEventListenerManager::RemoveEventListenerByType(nsIDOMEventListener *aListener, 
                                                  const nsAString& aType,
                                                  PRInt32 aFlags)
{
  nsCOMPtr<nsIAtom> atom = do_GetAtom(NS_LITERAL_STRING("on") + aType);
  PRUint32 type = nsContentUtils::GetEventId(atom);
  RemoveEventListener(aListener, type, atom, aFlags);
}

nsListenerStruct*
nsEventListenerManager::FindJSEventListener(PRUint32 aEventType,
                                            nsIAtom* aTypeAtom)
{
  // Run through the listeners for this type and see if a script
  // listener is registered
  nsListenerStruct *ls;
  PRUint32 count = mListeners.Length();
  for (PRUint32 i = 0; i < count; ++i) {
    ls = &mListeners.ElementAt(i);
    if (EVENT_TYPE_EQUALS(ls, aEventType, aTypeAtom) &&
        ls->mFlags & NS_PRIV_EVENT_FLAG_SCRIPT) {
      return ls;
    }
  }
  return nsnull;
}

nsresult
nsEventListenerManager::SetJSEventListener(nsIScriptContext *aContext,
                                           void *aScopeObject,
                                           nsIAtom* aName,
                                           JSObject *aHandler,
                                           PRBool aPermitUntrustedEvents,
                                           nsListenerStruct **aListenerStruct)
{
  nsresult rv = NS_OK;
  PRUint32 eventType = nsContentUtils::GetEventId(aName);
  nsListenerStruct* ls = FindJSEventListener(eventType, aName);

  if (!ls) {
    // If we didn't find a script listener or no listeners existed
    // create and add a new one.
    nsCOMPtr<nsIDOMEventListener> scriptListener;
    rv = NS_NewJSEventListener(aContext, aScopeObject, mTarget, aName,
                               aHandler, getter_AddRefs(scriptListener));
    if (NS_SUCCEEDED(rv)) {
      AddEventListener(scriptListener, eventType, aName,
                       NS_EVENT_FLAG_BUBBLE | NS_PRIV_EVENT_FLAG_SCRIPT);

      ls = FindJSEventListener(eventType, aName);
    }
  } else {
    ls->GetJSListener()->SetHandler(aHandler);
  }

  if (NS_SUCCEEDED(rv) && ls) {
    // Set flag to indicate possible need for compilation later
    ls->mHandlerIsString = !aHandler;
    if (aPermitUntrustedEvents) {
      ls->mFlags |= NS_PRIV_EVENT_UNTRUSTED_PERMITTED;
    }

    *aListenerStruct = ls;
  } else {
    *aListenerStruct = nsnull;
  }

  return rv;
}

nsresult
nsEventListenerManager::AddScriptEventListener(nsIAtom *aName,
                                               const nsAString& aBody,
                                               PRUint32 aLanguage,
                                               PRBool aDeferCompilation,
                                               PRBool aPermitUntrustedEvents)
{
  NS_PRECONDITION(aLanguage != nsIProgrammingLanguage::UNKNOWN,
                  "Must know the language for the script event listener");
  nsIScriptContext *context = nsnull;

  // |aPermitUntrustedEvents| is set to False for chrome - events
  // *generated* from an unknown source are not allowed.
  // However, for script languages with no 'sandbox', we want to reject
  // such scripts based on the source of their code, not just the source
  // of the event.
  if (aPermitUntrustedEvents && 
      aLanguage != nsIProgrammingLanguage::JAVASCRIPT) {
    NS_WARNING("Discarding non-JS event listener from untrusted source");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsINode> node(do_QueryInterface(mTarget));

  nsCOMPtr<nsIDocument> doc;

  nsCOMPtr<nsIScriptGlobalObject> global;

  if (node) {
    // Try to get context from doc
    // XXX sXBL/XBL2 issue -- do we really want the owner here?  What
    // if that's the XBL document?
    doc = node->GetOwnerDoc();
    if (doc)
      global = doc->GetScriptGlobalObject();
  } else {
    nsCOMPtr<nsPIDOMWindow> win(do_QueryInterface(mTarget));
    if (win) {
      NS_ASSERTION(win->IsInnerWindow(),
                   "Event listener added to outer window!");

      nsCOMPtr<nsIDOMDocument> domdoc;
      win->GetDocument(getter_AddRefs(domdoc));
      doc = do_QueryInterface(domdoc);
      global = do_QueryInterface(win);
    } else {
      global = do_QueryInterface(mTarget);
    }
  }

  if (!global) {
    // This can happen; for example this document might have been
    // loaded as data.
    return NS_OK;
  }

  nsresult rv = NS_OK;
  // return early preventing the event listener from being added
  // 'doc' is fetched above
  if (doc) {
    nsCOMPtr<nsIContentSecurityPolicy> csp;
    rv = doc->NodePrincipal()->GetCsp(getter_AddRefs(csp));
    NS_ENSURE_SUCCESS(rv, rv);

    if (csp) {
      PRBool inlineOK;
      rv = csp->GetAllowsInlineScript(&inlineOK);
      NS_ENSURE_SUCCESS(rv, rv);

      if ( !inlineOK ) {
        // gather information to log with violation report
        nsIURI* uri = doc->GetDocumentURI();
        nsCAutoString asciiSpec;
        if (uri)
          uri->GetAsciiSpec(asciiSpec);
        nsAutoString scriptSample, attr, tagName(NS_LITERAL_STRING("UNKNOWN"));
        aName->ToString(attr);
        nsCOMPtr<nsIDOMNode> domNode(do_QueryInterface(mTarget));
        if (domNode)
          domNode->GetNodeName(tagName);
        // build a "script sample" based on what we know about this element
        scriptSample.Assign(attr);
        scriptSample.AppendLiteral(" attribute on ");
        scriptSample.Append(tagName);
        scriptSample.AppendLiteral(" element");
        csp->LogViolationDetails(nsIContentSecurityPolicy::VIOLATION_TYPE_INLINE_SCRIPT,
                                 NS_ConvertUTF8toUTF16(asciiSpec),
                                 scriptSample,
                                 nsnull);
        return NS_OK;
      }
    }
  }

  // This might be the first reference to this language in the global
  // We must init the language before we attempt to fetch its context.
  if (NS_FAILED(global->EnsureScriptEnvironment(aLanguage))) {
    NS_WARNING("Failed to setup script environment for this language");
    // but fall through and let the inevitable failure below handle it.
  }

  context = global->GetScriptContext(aLanguage);
  NS_ENSURE_TRUE(context, NS_ERROR_FAILURE);

  void *scope = global->GetScriptGlobal(aLanguage);

  nsListenerStruct *ls;
  rv = SetJSEventListener(context, scope, aName, nsnull,
                          aPermitUntrustedEvents, &ls);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!aDeferCompilation) {
    return CompileEventHandlerInternal(ls, PR_TRUE, &aBody);
  }

  return NS_OK;
}

void
nsEventListenerManager::RemoveScriptEventListener(nsIAtom* aName)
{
  PRUint32 eventType = nsContentUtils::GetEventId(aName);
  nsListenerStruct* ls = FindJSEventListener(eventType, aName);

  if (ls) {
    mListeners.RemoveElementAt(PRUint32(ls - &mListeners.ElementAt(0)));
    mNoListenerForEvent = NS_EVENT_TYPE_NULL;
    mNoListenerForEventAtom = nsnull;
  }
}

nsresult
nsEventListenerManager::CompileEventHandlerInternal(nsListenerStruct *aListenerStruct,
                                                    PRBool aNeedsCxPush,
                                                    const nsAString* aBody)
{
  NS_PRECONDITION(aListenerStruct->GetJSListener(),
                  "Why do we not have a JS listener?");
  NS_PRECONDITION(aListenerStruct->mHandlerIsString,
                  "Why are we compiling a non-string JS listener?");

  nsresult result = NS_OK;

  nsIJSEventListener *listener = aListenerStruct->GetJSListener();
  NS_ASSERTION(!listener->GetHandler(), "What is there to compile?");

  nsIScriptContext *context = listener->GetEventContext();
  nsCOMPtr<nsIScriptEventHandlerOwner> handlerOwner =
    do_QueryInterface(mTarget);
  nsScriptObjectHolder handler(context);

  if (handlerOwner) {
    result = handlerOwner->GetCompiledEventHandler(aListenerStruct->mTypeAtom,
                                                   handler);
    if (NS_SUCCEEDED(result) && handler) {
      aListenerStruct->mHandlerIsString = PR_FALSE;
    } else {
      // Make sure there's nothing in the holder in the failure case
      handler.set(nsnull);
    }
  }

  if (aListenerStruct->mHandlerIsString) {
    // OK, we didn't find an existing compiled event handler.  Flag us
    // as not a string so we don't keep trying to compile strings
    // which can't be compiled
    aListenerStruct->mHandlerIsString = PR_FALSE;

    // mTarget may not be an nsIContent if it's a window and we're
    // getting an inline event listener forwarded from <html:body> or
    // <html:frameset> or <xul:window> or the like.
    // XXX I don't like that we have to reference content from
    // here. The alternative is to store the event handler string on
    // the nsIJSEventListener itself, and that still doesn't address
    // the arg names issue.
    nsCOMPtr<nsIContent> content = do_QueryInterface(mTarget);
    nsAutoString handlerBody;
    const nsAString* body = aBody;
    if (content && !aBody) {
      nsIAtom* attrName = aListenerStruct->mTypeAtom;
      if (aListenerStruct->mTypeAtom == nsGkAtoms::onSVGLoad)
        attrName = nsGkAtoms::onload;
      else if (aListenerStruct->mTypeAtom == nsGkAtoms::onSVGUnload)
        attrName = nsGkAtoms::onunload;
      else if (aListenerStruct->mTypeAtom == nsGkAtoms::onSVGAbort)
        attrName = nsGkAtoms::onabort;
      else if (aListenerStruct->mTypeAtom == nsGkAtoms::onSVGError)
        attrName = nsGkAtoms::onerror;
      else if (aListenerStruct->mTypeAtom == nsGkAtoms::onSVGResize)
        attrName = nsGkAtoms::onresize;
      else if (aListenerStruct->mTypeAtom == nsGkAtoms::onSVGScroll)
        attrName = nsGkAtoms::onscroll;
      else if (aListenerStruct->mTypeAtom == nsGkAtoms::onSVGZoom)
        attrName = nsGkAtoms::onzoom;
#ifdef MOZ_SMIL
      else if (aListenerStruct->mTypeAtom == nsGkAtoms::onbeginEvent)
        attrName = nsGkAtoms::onbegin;
      else if (aListenerStruct->mTypeAtom == nsGkAtoms::onrepeatEvent)
        attrName = nsGkAtoms::onrepeat;
      else if (aListenerStruct->mTypeAtom == nsGkAtoms::onendEvent)
        attrName = nsGkAtoms::onend;
#endif // MOZ_SMIL

      content->GetAttr(kNameSpaceID_None, attrName, handlerBody);
      body = &handlerBody;
    }

    PRUint32 lineNo = 0;
    nsCAutoString url (NS_LITERAL_CSTRING("-moz-evil:lying-event-listener"));
    nsCOMPtr<nsIDocument> doc;
    if (content) {
      doc = content->GetOwnerDoc();
    } else {
      nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface(mTarget);
      if (win) {
        doc = do_QueryInterface(win->GetExtantDocument());
      }
    }

    if (doc) {
      nsIURI *uri = doc->GetDocumentURI();
      if (uri) {
        uri->GetSpec(url);
        lineNo = 1;
      }
    }

    nsCxPusher pusher;
    if (aNeedsCxPush && !pusher.Push(context->GetNativeContext())) {
      return NS_ERROR_FAILURE;
    }


    if (handlerOwner) {
      // Always let the handler owner compile the event
      // handler, as it may want to use a special
      // context or scope object.
      result = handlerOwner->CompileEventHandler(context,
                                                 aListenerStruct->mTypeAtom,
                                                 *body,
                                                 url.get(), lineNo,
                                                 handler);
    } else {
      PRUint32 argCount;
      const char **argNames;
      // If no content, then just use kNameSpaceID_None for the
      // namespace ID.  In practice, it doesn't matter since SVG is
      // the only thing with weird arg names and SVG doesn't map event
      // listeners to the window.
      nsContentUtils::GetEventArgNames(content ?
                                         content->GetNameSpaceID() :
                                         kNameSpaceID_None,
                                       aListenerStruct->mTypeAtom,
                                       &argCount, &argNames);

      result = context->CompileEventHandler(aListenerStruct->mTypeAtom,
                                            argCount, argNames,
                                            *body,
                                            url.get(), lineNo,
                                            SCRIPTVERSION_DEFAULT, // for now?
                                            handler);
      if (result == NS_ERROR_ILLEGAL_VALUE) {
        NS_WARNING("Probably a syntax error in the event handler!");
        return NS_SUCCESS_LOSS_OF_INSIGNIFICANT_DATA;
      }
      NS_ENSURE_SUCCESS(result, result);
    }
  }

  if (handler) {
    // Bind it
    nsScriptObjectHolder boundHandler(context);
    context->BindCompiledEventHandler(mTarget, listener->GetEventScope(),
                                      handler, boundHandler);
    listener->SetHandler(boundHandler);
  }    

  return result;
}

nsresult
nsEventListenerManager::HandleEventSubType(nsListenerStruct* aListenerStruct,
                                           nsIDOMEventListener* aListener,
                                           nsIDOMEvent* aDOMEvent,
                                           nsIDOMEventTarget* aCurrentTarget,
                                           PRUint32 aPhaseFlags,
                                           nsCxPusher* aPusher)
{
  nsresult result = NS_OK;

  // If this is a script handler and we haven't yet
  // compiled the event handler itself
  if ((aListenerStruct->mFlags & NS_PRIV_EVENT_FLAG_SCRIPT) &&
      aListenerStruct->mHandlerIsString) {
    nsIJSEventListener *jslistener = aListenerStruct->GetJSListener();
    result = CompileEventHandlerInternal(aListenerStruct,
                                         jslistener->GetEventContext() !=
                                           aPusher->GetCurrentScriptContext(),
                                         nsnull);
  }

  if (NS_SUCCEEDED(result)) {
    // nsIDOMEvent::currentTarget is set in nsEventDispatcher.
    result = aListener->HandleEvent(aDOMEvent);
  }

  return result;
}

/**
* Causes a check for event listeners and processing by them if they exist.
* @param an event listener
*/

void
nsEventListenerManager::HandleEventInternal(nsPresContext* aPresContext,
                                            nsEvent* aEvent,
                                            nsIDOMEvent** aDOMEvent,
                                            nsIDOMEventTarget* aCurrentTarget,
                                            PRUint32 aFlags,
                                            nsEventStatus* aEventStatus,
                                            nsCxPusher* aPusher)
{
  //Set the value of the internal PreventDefault flag properly based on aEventStatus
  if (*aEventStatus == nsEventStatus_eConsumeNoDefault) {
    aEvent->flags |= NS_EVENT_FLAG_NO_DEFAULT;
  }

  nsAutoTObserverArray<nsListenerStruct, 2>::EndLimitedIterator iter(mListeners);
  nsAutoPopupStatePusher popupStatePusher(nsDOMEvent::GetEventPopupControlState(aEvent));
  PRBool hasListener = PR_FALSE;
  while (iter.HasMore()) {
    nsListenerStruct* ls = &iter.GetNext();
    // Check that the phase is same in event and event listener.
    // Handle only trusted events, except when listener permits untrusted events.
    if (ListenerCanHandle(ls, aEvent)) {
      hasListener = PR_TRUE;
      // XXX The (mFlags & aFlags) test here seems fragile. Shouldn't we
      // specifically only test the capture/bubble flags.
      if ((ls->mFlags & aFlags & ~NS_EVENT_FLAG_SYSTEM_EVENT) &&
          (ls->mFlags & NS_EVENT_FLAG_SYSTEM_EVENT) ==
          (aFlags & NS_EVENT_FLAG_SYSTEM_EVENT) &&
          (NS_IS_TRUSTED_EVENT(aEvent) ||
           ls->mFlags & NS_PRIV_EVENT_UNTRUSTED_PERMITTED)) {
        if (!*aDOMEvent) {
          nsEventDispatcher::CreateEvent(aPresContext, aEvent,
                                         EmptyString(), aDOMEvent);
        }
        if (*aDOMEvent) {
          if (!aEvent->currentTarget) {
            aEvent->currentTarget = aCurrentTarget->GetTargetForDOMEvent();
            if (!aEvent->currentTarget) {
              break;
            }
          }
          nsRefPtr<nsIDOMEventListener> kungFuDeathGrip = ls->mListener;
          if (aPusher->RePush(aCurrentTarget)) {
            if (NS_FAILED(HandleEventSubType(ls, ls->mListener, *aDOMEvent,
                                             aCurrentTarget, aFlags,
                                             aPusher))) {
              aEvent->flags |= NS_EVENT_FLAG_EXCEPTION_THROWN;
            }
          }
        }
      }
    }
  }

  aEvent->currentTarget = nsnull;

  if (!hasListener) {
    mNoListenerForEvent = aEvent->message;
    mNoListenerForEventAtom = aEvent->userType;
  }

  if (aEvent->flags & NS_EVENT_FLAG_NO_DEFAULT) {
    *aEventStatus = nsEventStatus_eConsumeNoDefault;
  }
}

void
nsEventListenerManager::Disconnect()
{
  mTarget = nsnull;
  RemoveAllListeners();
}

void
nsEventListenerManager::AddEventListener(const nsAString& aType,
                                         nsIDOMEventListener* aListener,
                                         PRBool aUseCapture,
                                         PRBool aWantsUntrusted)
{
  PRInt32 flags = aUseCapture ? NS_EVENT_FLAG_CAPTURE : NS_EVENT_FLAG_BUBBLE;

  if (aWantsUntrusted) {
    flags |= NS_PRIV_EVENT_UNTRUSTED_PERMITTED;
  }

  return AddEventListenerByType(aListener, aType, flags);
}

void
nsEventListenerManager::RemoveEventListener(const nsAString& aType, 
                                            nsIDOMEventListener* aListener, 
                                            PRBool aUseCapture)
{
  PRInt32 flags = aUseCapture ? NS_EVENT_FLAG_CAPTURE : NS_EVENT_FLAG_BUBBLE;
  
  RemoveEventListenerByType(aListener, aType, flags);
}

PRBool
nsEventListenerManager::HasMutationListeners()
{
  if (mMayHaveMutationListeners) {
    PRUint32 count = mListeners.Length();
    for (PRUint32 i = 0; i < count; ++i) {
      nsListenerStruct* ls = &mListeners.ElementAt(i);
      if (ls->mEventType >= NS_MUTATION_START &&
          ls->mEventType <= NS_MUTATION_END) {
        return PR_TRUE;
      }
    }
  }

  return PR_FALSE;
}

PRUint32
nsEventListenerManager::MutationListenerBits()
{
  PRUint32 bits = 0;
  if (mMayHaveMutationListeners) {
    PRUint32 count = mListeners.Length();
    for (PRUint32 i = 0; i < count; ++i) {
      nsListenerStruct* ls = &mListeners.ElementAt(i);
      if (ls->mEventType >= NS_MUTATION_START &&
          ls->mEventType <= NS_MUTATION_END) {
        if (ls->mEventType == NS_MUTATION_SUBTREEMODIFIED) {
          return kAllMutationBits;
        }
        bits |= MutationBitForEventType(ls->mEventType);
      }
    }
  }
  return bits;
}

PRBool
nsEventListenerManager::HasListenersFor(const nsAString& aEventName)
{
  nsCOMPtr<nsIAtom> atom = do_GetAtom(NS_LITERAL_STRING("on") + aEventName);

  PRUint32 count = mListeners.Length();
  for (PRUint32 i = 0; i < count; ++i) {
    nsListenerStruct* ls = &mListeners.ElementAt(i);
    if (ls->mTypeAtom == atom) {
      return PR_TRUE;
    }
  }
  return PR_FALSE;
}

PRBool
nsEventListenerManager::HasListeners()
{
  return !mListeners.IsEmpty();
}

nsresult
nsEventListenerManager::GetListenerInfo(nsCOMArray<nsIEventListenerInfo>* aList)
{
  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(mTarget);
  NS_ENSURE_STATE(target);
  aList->Clear();
  PRUint32 count = mListeners.Length();
  for (PRUint32 i = 0; i < count; ++i) {
    const nsListenerStruct& ls = mListeners.ElementAt(i);
    PRBool capturing = !!(ls.mFlags & NS_EVENT_FLAG_CAPTURE);
    PRBool systemGroup = !!(ls.mFlags & NS_EVENT_FLAG_SYSTEM_EVENT);
    PRBool allowsUntrusted = !!(ls.mFlags & NS_PRIV_EVENT_UNTRUSTED_PERMITTED);
    // If this is a script handler and we haven't yet
    // compiled the event handler itself go ahead and compile it
    if ((ls.mFlags & NS_PRIV_EVENT_FLAG_SCRIPT) && ls.mHandlerIsString) {
      CompileEventHandlerInternal(const_cast<nsListenerStruct*>(&ls),
                                  PR_TRUE, nsnull);
    }
    const nsDependentSubstring& eventType =
      Substring(nsDependentAtomString(ls.mTypeAtom), 2);
    nsRefPtr<nsEventListenerInfo> info =
      new nsEventListenerInfo(eventType, ls.mListener, capturing,
                              allowsUntrusted, systemGroup);
    NS_ENSURE_TRUE(info, NS_ERROR_OUT_OF_MEMORY);
    aList->AppendObject(info);
  }
  return NS_OK;
}

PRBool
nsEventListenerManager::HasUnloadListeners()
{
  PRUint32 count = mListeners.Length();
  for (PRUint32 i = 0; i < count; ++i) {
    nsListenerStruct* ls = &mListeners.ElementAt(i);
    if (ls->mEventType == NS_PAGE_UNLOAD ||
        ls->mEventType == NS_BEFORE_PAGE_UNLOAD) {
      return PR_TRUE;
    }
  }
  return PR_FALSE;
}

nsresult
nsEventListenerManager::SetJSEventListenerToJsval(nsIAtom *aEventName,
                                                  JSContext *cx,
                                                  JSObject* aScope,
                                                  const jsval & v)
{
  JSObject *handler;
  if (JSVAL_IS_PRIMITIVE(v) ||
      !JS_ObjectIsCallable(cx, handler = JSVAL_TO_OBJECT(v))) {
    RemoveScriptEventListener(aEventName);
    return NS_OK;
  }

  // We might not have a script context, e.g. if we're setting a listener
  // on a dead Window.
  nsIScriptContext *context = nsJSUtils::GetStaticScriptContext(cx, aScope);
  NS_ENSURE_TRUE(context, NS_ERROR_FAILURE);

  JSObject *scope = ::JS_GetGlobalForObject(cx, aScope);
  // Untrusted events are always permitted for non-chrome script
  // handlers.
  nsListenerStruct *ignored;
  return SetJSEventListener(context, scope, aEventName, handler,
                            !nsContentUtils::IsCallerChrome(), &ignored);
}

void
nsEventListenerManager::GetJSEventListener(nsIAtom *aEventName, jsval *vp)
{
  PRUint32 eventType = nsContentUtils::GetEventId(aEventName);
  nsListenerStruct* ls = FindJSEventListener(eventType, aEventName);

  *vp = JSVAL_NULL;

  if (!ls) {
    return;
  }

  nsIJSEventListener *listener = ls->GetJSListener();
  if (listener->GetEventContext()->GetScriptTypeID() !=
        nsIProgrammingLanguage::JAVASCRIPT) {
    // Not JS, so no point doing anything with it.
    return;
  }
    
  if (ls->mHandlerIsString) {
    CompileEventHandlerInternal(ls, PR_TRUE, nsnull);
  }

  *vp = OBJECT_TO_JSVAL(static_cast<JSObject*>(listener->GetHandler()));
}

PRInt64
nsEventListenerManager::SizeOf() const
{
  PRInt64 size = sizeof(*this);
  PRUint32 count = mListeners.Length();
  for (PRUint32 i = 0; i < count; ++i) {
    const nsListenerStruct& ls = mListeners.ElementAt(i);
    size += sizeof(ls);
    nsIJSEventListener* jsl = ls.GetJSListener();
    if (jsl) {
      size += jsl->SizeOf();
    }
  }
  return size;
}

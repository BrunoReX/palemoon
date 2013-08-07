/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=80: */
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
 *   Travis Bogard <travis@netscape.com>
 *   Dan Rosen <dr@netscape.com>
 *   Vidur Apparao <vidur@netscape.com>
 *   Johnny Stenback <jst@netscape.com>
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

#ifndef nsGlobalWindow_h___
#define nsGlobalWindow_h___

#include "mozilla/XPCOM.h" // for TimeStamp/TimeDuration

// Local Includes
// Helper Classes
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsWeakReference.h"
#include "nsHashtable.h"
#include "nsDataHashtable.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDOMScriptObjectHolder.h"

// Interfaces Needed
#include "nsDOMWindowList.h"
#include "nsIBaseWindow.h"
#include "nsIBrowserDOMWindow.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDOMClientInformation.h"
#include "nsIDOMEventTarget.h"
#include "nsIDOMNavigator.h"
#include "nsIDOMNavigatorGeolocation.h"
#include "nsIDOMNavigatorDesktopNotification.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIDOMJSWindow.h"
#include "nsIDOMChromeWindow.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptContext.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIScriptTimeoutHandler.h"
#include "nsITimer.h"
#include "nsIWebBrowserChrome.h"
#include "nsPIDOMWindow.h"
#include "nsIDOMModalContentWindow.h"
#include "nsIScriptSecurityManager.h"
#include "nsEventListenerManager.h"
#include "nsIDOMDocument.h"
#ifndef MOZ_DISABLE_DOMCRYPTO
#include "nsIDOMCrypto.h"
#endif
#include "nsIPrincipal.h"
#include "nsPluginArray.h"
#include "nsMimeTypeArray.h"
#include "nsIXPCScriptable.h"
#include "nsPoint.h"
#include "nsSize.h"
#include "nsRect.h"
#include "mozFlushType.h"
#include "prclist.h"
#include "nsIDOMStorageObsolete.h"
#include "nsIDOMStorageList.h"
#include "nsIDOMStorageEvent.h"
#include "nsIDOMStorageIndexedDB.h"
#include "nsIDOMOfflineResourceList.h"
#include "nsIArray.h"
#include "nsIContent.h"
#include "nsIIDBFactory.h"
#include "nsFrameMessageManager.h"
#include "mozilla/TimeStamp.h"
#include "nsIDOMTouchEvent.h"
#include "nsIInlineEventHandlers.h"

// JS includes
#include "jsapi.h"
#include "jswrapper.h"

#define DEFAULT_HOME_PAGE "www.mozilla.org"
#define PREF_BROWSER_STARTUP_HOMEPAGE "browser.startup.homepage"

// Amount of time allowed between alert/prompt/confirm before enabling
// the stop dialog checkbox.
#define SUCCESSIVE_DIALOG_TIME_LIMIT 3 // 3 sec

// During click or mousedown events (and others, see nsDOMEvent) we allow modal
// dialogs up to this limit, even if they were disabled.
#define MAX_DIALOG_COUNT 10

class nsIDOMBarProp;
class nsIDocument;
class nsPresContext;
class nsIDOMEvent;
class nsIScrollableFrame;
class nsIControllers;

class nsBarProp;
class nsLocation;
class nsNavigator;
class nsScreen;
class nsHistory;
class nsPerformance;
class nsIDocShellLoadInfo;
class WindowStateHolder;
class nsGlobalWindowObserver;
class nsGlobalWindow;
class nsDummyJavaPluginOwner;
class PostMessageEvent;
class nsRunnable;

class nsDOMOfflineResourceList;
class nsGeolocation;
class nsDesktopNotificationCenter;
class nsDOMMozURLProperty;

#ifdef MOZ_DISABLE_DOMCRYPTO
class nsIDOMCrypto;
#endif

extern nsresult
NS_CreateJSTimeoutHandler(nsGlobalWindow *aWindow,
                          PRBool *aIsInterval,
                          PRInt32 *aInterval,
                          nsIScriptTimeoutHandler **aRet);

/*
 * Timeout struct that holds information about each script
 * timeout.  Holds a strong reference to an nsIScriptTimeoutHandler, which
 * abstracts the language specific cruft.
 */
struct nsTimeout : PRCList
{
  nsTimeout();
  ~nsTimeout();

  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(nsTimeout)

  nsrefcnt Release();
  nsrefcnt AddRef();

  nsTimeout* Next() {
    // Note: might not actually return an nsTimeout.  Use IsTimeout to check.
    return static_cast<nsTimeout*>(PR_NEXT_LINK(this));
  }

  nsTimeout* Prev() {
    // Note: might not actually return an nsTimeout.  Use IsTimeout to check.
    return static_cast<nsTimeout*>(PR_PREV_LINK(this));
  }

  // Window for which this timeout fires
  nsRefPtr<nsGlobalWindow> mWindow;

  // The actual timer object
  nsCOMPtr<nsITimer> mTimer;

  // True if the timeout was cleared
  PRPackedBool mCleared;

  // True if this is one of the timeouts that are currently running
  PRPackedBool mRunning;

  // True if this is a repeating/interval timer
  PRPackedBool mIsInterval;

  // Returned as value of setTimeout()
  PRUint32 mPublicId;

  // Interval in milliseconds
  PRUint32 mInterval;

  // mWhen and mTimeRemaining can't be in a union, sadly, because they
  // have constructors.
  // Nominal time to run this timeout.  Use only when timeouts are not
  // suspended.
  mozilla::TimeStamp mWhen;
  // Remaining time to wait.  Used only when timeouts are suspended.
  mozilla::TimeDuration mTimeRemaining;

  // Principal with which to execute
  nsCOMPtr<nsIPrincipal> mPrincipal;

  // stack depth at which timeout is firing
  PRUint32 mFiringDepth;

  // 
  PRUint32 mNestingLevel;

  // The popup state at timeout creation time if not created from
  // another timeout
  PopupControlState mPopupState;

  // The language-specific information about the callback.
  nsCOMPtr<nsIScriptTimeoutHandler> mScriptHandler;

private:
  // reference count for shared usage
  nsAutoRefCnt mRefCnt;
};

//*****************************************************************************
// nsOuterWindow: Outer Window Proxy
//*****************************************************************************

class nsOuterWindowProxy : public JSWrapper
{
public:
  nsOuterWindowProxy() : JSWrapper((uintN)0) {}

  virtual bool isOuterWindow() {
    return true;
  }
  JSString *obj_toString(JSContext *cx, JSObject *wrapper);

  static nsOuterWindowProxy singleton;
};

JSObject *NS_NewOuterWindowProxy(JSContext *cx, JSObject *parent);

//*****************************************************************************
// nsGlobalWindow: Global Object for Scripting
//*****************************************************************************
// Beware that all scriptable interfaces implemented by
// nsGlobalWindow will be reachable from JS, if you make this class
// implement new interfaces you better know what you're
// doing. Security wise this is very sensitive code. --
// jst@netscape.com

// nsGlobalWindow inherits PRCList for maintaining a list of all inner
// windows still in memory for any given outer window. This list is
// needed to ensure that mOuterWindow doesn't end up dangling. The
// nature of PRCList means that the window itself is always in the
// list, and an outer window's list will also contain all inner window
// objects that are still in memory (and in reality all inner window
// object's lists also contain its outer and all other inner windows
// belonging to the same outer window, but that's an unimportant
// side effect of inheriting PRCList).

class nsGlobalWindow : public nsPIDOMWindow,
                       public nsIScriptGlobalObject,
                       public nsIDOMJSWindow,
                       public nsIScriptObjectPrincipal,
                       public nsIDOMEventTarget,
                       public nsIDOMStorageIndexedDB,
                       public nsSupportsWeakReference,
                       public nsIInterfaceRequestor,
                       public nsWrapperCache,
                       public PRCListStr,
                       public nsIDOMWindowPerformance,
                       public nsITouchEventReceiver,
                       public nsIInlineEventHandlers
{
public:
  friend class nsDOMMozURLProperty;

  typedef mozilla::TimeStamp TimeStamp;
  typedef mozilla::TimeDuration TimeDuration;
  typedef nsDataHashtable<nsUint64HashKey, nsGlobalWindow*> WindowByIdTable;

  // public methods
  nsPIDOMWindow* GetPrivateParent();
  // callback for close event
  void ReallyCloseWindow();

  // nsISupports
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  // nsIScriptGlobalObject
  virtual nsIScriptContext *GetContext();
  virtual JSObject *GetGlobalJSObject();
  JSObject *FastGetGlobalJSObject()
  {
    return mJSObject;
  }

  virtual nsresult EnsureScriptEnvironment(PRUint32 aLangID);

  virtual nsIScriptContext *GetScriptContext(PRUint32 lang);
  virtual void *GetScriptGlobal(PRUint32 lang);

  // Set a new script language context for this global.  The native global
  // for the context is created by the context's GetNativeGlobal() method.
  virtual nsresult SetScriptContext(PRUint32 lang, nsIScriptContext *aContext);
  
  virtual void OnFinalize(JSObject* aObject);
  virtual void SetScriptsEnabled(PRBool aEnabled, PRBool aFireTimeouts);

  // nsIScriptObjectPrincipal
  virtual nsIPrincipal* GetPrincipal();

  // nsIDOMWindow
  NS_DECL_NSIDOMWINDOW

  // nsIDOMWindowPerformance
  NS_DECL_NSIDOMWINDOWPERFORMANCE

  // nsIDOMJSWindow
  NS_DECL_NSIDOMJSWINDOW

  // nsIDOMEventTarget
  NS_DECL_NSIDOMEVENTTARGET

  // nsITouchEventReceiver
  NS_DECL_NSITOUCHEVENTRECEIVER

  // nsIInlineEventHandlers
  NS_DECL_NSIINLINEEVENTHANDLERS

  // nsPIDOMWindow
  virtual NS_HIDDEN_(nsPIDOMWindow*) GetPrivateRoot();
  virtual NS_HIDDEN_(void) ActivateOrDeactivate(PRBool aActivate);
  virtual NS_HIDDEN_(void) SetActive(PRBool aActive);
  virtual NS_HIDDEN_(void) SetIsBackground(PRBool aIsBackground);
  virtual NS_HIDDEN_(void) SetChromeEventHandler(nsIDOMEventTarget* aChromeEventHandler);

  virtual NS_HIDDEN_(void) SetOpenerScriptPrincipal(nsIPrincipal* aPrincipal);
  virtual NS_HIDDEN_(nsIPrincipal*) GetOpenerScriptPrincipal();

  virtual NS_HIDDEN_(PopupControlState) PushPopupControlState(PopupControlState state, PRBool aForce) const;
  virtual NS_HIDDEN_(void) PopPopupControlState(PopupControlState state) const;
  virtual NS_HIDDEN_(PopupControlState) GetPopupControlState() const;

  virtual NS_HIDDEN_(nsresult) SaveWindowState(nsISupports **aState);
  virtual NS_HIDDEN_(nsresult) RestoreWindowState(nsISupports *aState);
  virtual NS_HIDDEN_(void) SuspendTimeouts(PRUint32 aIncrease = 1,
                                           PRBool aFreezeChildren = PR_TRUE);
  virtual NS_HIDDEN_(nsresult) ResumeTimeouts(PRBool aThawChildren = PR_TRUE);
  virtual NS_HIDDEN_(PRUint32) TimeoutSuspendCount();
  virtual NS_HIDDEN_(nsresult) FireDelayedDOMEvents();
  virtual NS_HIDDEN_(PRBool) IsFrozen() const
  {
    return mIsFrozen;
  }

  virtual NS_HIDDEN_(PRBool) WouldReuseInnerWindow(nsIDocument *aNewDocument);

  virtual NS_HIDDEN_(void) SetDocShell(nsIDocShell* aDocShell);
  virtual NS_HIDDEN_(nsresult) SetNewDocument(nsIDocument *aDocument,
                                              nsISupports *aState,
                                              PRBool aForceReuseInnerWindow);
  void DispatchDOMWindowCreated();
  virtual NS_HIDDEN_(void) SetOpenerWindow(nsIDOMWindow* aOpener,
                                           PRBool aOriginalOpener);
  virtual NS_HIDDEN_(void) EnsureSizeUpToDate();

  virtual NS_HIDDEN_(nsIDOMWindow*) EnterModalState();
  virtual NS_HIDDEN_(void) LeaveModalState(nsIDOMWindow* aWindow);

  virtual NS_HIDDEN_(PRBool) CanClose();
  virtual NS_HIDDEN_(nsresult) ForceClose();

  virtual NS_HIDDEN_(void) SetHasOrientationEventListener();
  virtual NS_HIDDEN_(void) MaybeUpdateTouchState();
  virtual NS_HIDDEN_(void) UpdateTouchState();
  virtual NS_HIDDEN_(PRBool) DispatchCustomEvent(const char *aEventName);

  // nsIDOMStorageIndexedDB
  NS_DECL_NSIDOMSTORAGEINDEXEDDB

  // nsIInterfaceRequestor
  NS_DECL_NSIINTERFACEREQUESTOR

  // Object Management
  nsGlobalWindow(nsGlobalWindow *aOuterWindow);

  static nsGlobalWindow *FromSupports(nsISupports *supports)
  {
    // Make sure this matches the casts we do in QueryInterface().
    return (nsGlobalWindow *)(nsIScriptGlobalObject *)supports;
  }
  static nsISupports *ToSupports(nsGlobalWindow *win)
  {
    // Make sure this matches the casts we do in QueryInterface().
    return (nsISupports *)(nsIScriptGlobalObject *)win;
  }
  static nsGlobalWindow *FromWrapper(nsIXPConnectWrappedNative *wrapper)
  {
    return FromSupports(wrapper->Native());
  }

  inline nsGlobalWindow *GetTop()
  {
    nsCOMPtr<nsIDOMWindow> top;
    GetTop(getter_AddRefs(top));
    if (top)
      return static_cast<nsGlobalWindow *>(static_cast<nsIDOMWindow *>(top.get()));
    return nsnull;
  }

  // Call this when a modal dialog is about to be opened.  Returns
  // true if we've reached the state in this top level window where we
  // ask the user if further dialogs should be blocked.
  bool DialogOpenAttempted();

  // Returns true if dialogs have already been blocked for this
  // window.
  bool AreDialogsBlocked();

  // Ask the user if further dialogs should be blocked. This is used
  // in the cases where we have no modifiable UI to show, in that case
  // we show a separate dialog when asking this question.
  bool ConfirmDialogAllowed();

  // Prevent further dialogs in this (top level) window
  void PreventFurtherDialogs();

  nsIScriptContext *GetContextInternal()
  {
    if (mOuterWindow) {
      return GetOuterWindowInternal()->mContext;
    }

    return mContext;
  }

  nsIScriptContext *GetScriptContextInternal(PRUint32 aLangID)
  {
    NS_ASSERTION(aLangID == nsIProgrammingLanguage::JAVASCRIPT,
                 "We don't support this language ID");
    if (mOuterWindow) {
      return GetOuterWindowInternal()->mContext;
    }

    return mContext;
  }

  nsGlobalWindow *GetOuterWindowInternal()
  {
    return static_cast<nsGlobalWindow *>(GetOuterWindow());
  }

  nsGlobalWindow *GetCurrentInnerWindowInternal()
  {
    return static_cast<nsGlobalWindow *>(mInnerWindow);
  }

  nsGlobalWindow *EnsureInnerWindowInternal()
  {
    return static_cast<nsGlobalWindow *>(EnsureInnerWindow());
  }

  PRBool IsCreatingInnerWindow() const
  {
    return  mCreatingInnerWindow;
  }

  PRBool IsChromeWindow() const
  {
    return mIsChrome;
  }

  nsresult Observe(nsISupports* aSubject, const char* aTopic,
                   const PRUnichar* aData);

  static void Init();
  static void ShutDown();
  static void CleanupCachedXBLHandlers(nsGlobalWindow* aWindow);
  static PRBool IsCallerChrome();
  static void CloseBlockScriptTerminationFunc(nsISupports *aRef);

  static void RunPendingTimeoutsRecursive(nsGlobalWindow *aTopWindow,
                                          nsGlobalWindow *aWindow);

  friend class WindowStateHolder;

  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_AMBIGUOUS(nsGlobalWindow,
                                                         nsIScriptGlobalObject)

  void InitJavaProperties();

  virtual NS_HIDDEN_(void*)
    GetCachedXBLPrototypeHandler(nsXBLPrototypeHandler* aKey);

  virtual NS_HIDDEN_(void)
    CacheXBLPrototypeHandler(nsXBLPrototypeHandler* aKey,
                             nsScriptObjectHolder& aHandler);

  virtual PRBool TakeFocus(PRBool aFocus, PRUint32 aFocusMethod);
  virtual void SetReadyForFocus();
  virtual void PageHidden();
  virtual nsresult DispatchAsyncHashchange(nsIURI *aOldURI, nsIURI *aNewURI);
  virtual nsresult DispatchSyncPopState();

  virtual nsresult SetArguments(nsIArray *aArguments, nsIPrincipal *aOrigin);

  static PRBool DOMWindowDumpEnabled();

  void MaybeForgiveSpamCount();
  PRBool IsClosedOrClosing() {
    return (mIsClosed ||
            mInClose ||
            mHavePendingClose ||
            mCleanedUp);
  }

  static void FirePopupBlockedEvent(nsIDOMDocument* aDoc,
                                    nsIDOMWindow *aRequestingWindow, nsIURI *aPopupURI,
                                    const nsAString &aPopupWindowName,
                                    const nsAString &aPopupWindowFeatures);

  virtual PRUint32 GetSerial() {
    return mSerial;
  }

  static nsGlobalWindow* GetOuterWindowWithId(PRUint64 aWindowID) {
    if (!sWindowsById) {
      return nsnull;
    }

    nsGlobalWindow* outerWindow = sWindowsById->Get(aWindowID);
    return outerWindow && !outerWindow->IsInnerWindow() ? outerWindow : nsnull;
  }

  static nsGlobalWindow* GetInnerWindowWithId(PRUint64 aInnerWindowID) {
    if (!sWindowsById) {
      return nsnull;
    }

    nsGlobalWindow* innerWindow = sWindowsById->Get(aInnerWindowID);
    return innerWindow && innerWindow->IsInnerWindow() ? innerWindow : nsnull;
  }

  static bool HasIndexedDBSupport();

  static bool HasPerformanceSupport();

  static WindowByIdTable* GetWindowsTable() {
    return sWindowsById;
  }

  PRInt64 SizeOf() const;

private:
  // Enable updates for the accelerometer.
  void EnableDeviceMotionUpdates();

  // Disables updates for the accelerometer.
  void DisableDeviceMotionUpdates();

protected:
  friend class HashchangeCallback;
  friend class nsBarProp;

  // Object Management
  virtual ~nsGlobalWindow();
  void CleanUp(PRBool aIgnoreModalDialog);
  void ClearControllers();
  static void TryClearWindowScope(nsISupports* aWindow);
  void ClearScopeWhenAllScriptsStop();
  nsresult FinalClose();

  void FreeInnerObjects(PRBool aClearScope);
  nsGlobalWindow *CallerInnerWindow();

  nsresult InnerSetNewDocument(nsIDocument* aDocument);

  nsresult DefineArgumentsProperty(nsIArray *aArguments);

  // Get the parent, returns null if this is a toplevel window
  nsIDOMWindow* GetParentInternal();

  // popup tracking
  PRBool IsPopupSpamWindow()
  {
    if (IsInnerWindow() && !mOuterWindow) {
      return PR_FALSE;
    }

    return GetOuterWindowInternal()->mIsPopupSpam;
  }

  void SetPopupSpamWindow(PRBool aPopup)
  {
    if (IsInnerWindow() && !mOuterWindow) {
      NS_ERROR("SetPopupSpamWindow() called on inner window w/o an outer!");

      return;
    }

    GetOuterWindowInternal()->mIsPopupSpam = aPopup;
  }

  // Window Control Functions
  /**
   * @param aURL the URL to load in the new window
   * @param aName the name to use for the new window
   * @param aOptions the window options to use for the new window
   * @param aDialog true when called from variants of OpenDialog.  If this is
   *                true, this method will skip popup blocking checks.  The
   *                aDialog argument is passed on to the window watcher.
   * @param aCalledNoScript true when called via the [noscript] open()
   *                        and openDialog() methods.  When this is true, we do
   *                        NOT want to use the JS stack for things like caller
   *                        determination.
   * @param aDoJSFixups true when this is the content-accessible JS version of
   *                    window opening.  When true, popups do not cause us to
   *                    throw, we save the caller's principal in the new window
   *                    for later consumption, and we make sure that there is a
   *                    document in the newly-opened window.  Note that this
   *                    last will only be done if the newly-opened window is
   *                    non-chrome.
   * @param argv The arguments to pass to the new window.  The first
   *             three args, if present, will be aURL, aName, and aOptions.  So
   *             this param only matters if there are more than 3 arguments.
   * @param argc The number of arguments in argv.
   * @param aExtraArgument Another way to pass arguments in.  This is mutually
   *                       exclusive with the argv/argc approach.
   * @param aJSCallerContext The calling script's context. This must be nsnull
   *                         when aCalledNoScript is true.
   * @param aReturn [out] The window that was opened, if any.
   *
   * @note that the boolean args are const because the function shouldn't be
   * messing with them.  That also makes it easier for the compiler to sort out
   * its build warning stuff.
   */
  NS_HIDDEN_(nsresult) OpenInternal(const nsAString& aUrl,
                                    const nsAString& aName,
                                    const nsAString& aOptions,
                                    PRBool aDialog,
                                    PRBool aContentModal,
                                    PRBool aCalledNoScript,
                                    PRBool aDoJSFixups,
                                    nsIArray *argv,
                                    nsISupports *aExtraArgument,
                                    nsIPrincipal *aCalleePrincipal,
                                    JSContext *aJSCallerContext,
                                    nsIDOMWindow **aReturn);

  static void CloseWindow(nsISupports* aWindow);
  static void ClearWindowScope(nsISupports* aWindow);

  // Timeout Functions
  // Language agnostic timeout function (all args passed).
  // |interval| is in milliseconds.
  nsresult SetTimeoutOrInterval(nsIScriptTimeoutHandler *aHandler,
                                PRInt32 interval,
                                PRBool aIsInterval, PRInt32 *aReturn);
  nsresult ClearTimeoutOrInterval(PRInt32 aTimerID);

  // JS specific timeout functions (JS args grabbed from context).
  nsresult SetTimeoutOrInterval(PRBool aIsInterval, PRInt32* aReturn);
  nsresult ClearTimeoutOrInterval();
  nsresult ResetTimersForNonBackgroundWindow();

  // The timeout implementation functions.
  void RunTimeout(nsTimeout *aTimeout);
  void RunTimeout() { RunTimeout(nsnull); }

  void ClearAllTimeouts();
  // Insert aTimeout into the list, before all timeouts that would
  // fire after it, but no earlier than mTimeoutInsertionPoint, if any.
  void InsertTimeoutIntoList(nsTimeout *aTimeout);
  static void TimerCallback(nsITimer *aTimer, void *aClosure);

  // Helper Functions
  nsresult GetTreeOwner(nsIDocShellTreeOwner** aTreeOwner);
  nsresult GetTreeOwner(nsIBaseWindow** aTreeOwner);
  nsresult GetWebBrowserChrome(nsIWebBrowserChrome** aBrowserChrome);
  // GetScrollFrame does not flush.  Callers should do it themselves as needed,
  // depending on which info they actually want off the scrollable frame.
  nsIScrollableFrame *GetScrollFrame();
  nsresult SecurityCheckURL(const char *aURL);
  nsresult BuildURIfromBase(const char *aURL,
                            nsIURI **aBuiltURI,
                            PRBool *aFreeSecurityPass, JSContext **aCXused);
  PRBool PopupWhitelisted();
  PopupControlState RevisePopupAbuseLevel(PopupControlState);
  void     FireAbuseEvents(PRBool aBlocked, PRBool aWindow,
                           const nsAString &aPopupURL,
                           const nsAString &aPopupWindowName,
                           const nsAString &aPopupWindowFeatures);
  void FireOfflineStatusEvent();
  nsresult FireHashchange(const nsAString &aOldURL, const nsAString &aNewURL);

  void FlushPendingNotifications(mozFlushType aType);
  void EnsureReflowFlushAndPaint();
  nsresult CheckSecurityWidthAndHeight(PRInt32* width, PRInt32* height);
  nsresult CheckSecurityLeftAndTop(PRInt32* left, PRInt32* top);

  // Arguments to this function should have values in app units
  nsresult SetCSSViewportWidthAndHeight(nscoord width, nscoord height);
  // Arguments to this function should have values in device pixels
  nsresult SetDocShellWidthAndHeight(PRInt32 width, PRInt32 height);

  static PRBool CanSetProperty(const char *aPrefName);

  static void MakeScriptDialogTitle(nsAString &aOutTitle);

  PRBool CanMoveResizeWindows();

  PRBool   GetBlurSuppression();

  // If aDoFlush is true, we'll flush our own layout; otherwise we'll try to
  // just flush our parent and only flush ourselves if we think we need to.
  nsresult GetScrollXY(PRInt32* aScrollX, PRInt32* aScrollY,
                       PRBool aDoFlush);
  nsresult GetScrollMaxXY(PRInt32* aScrollMaxX, PRInt32* aScrollMaxY);
  
  nsresult GetOuterSize(nsIntSize* aSizeCSSPixels);
  nsresult SetOuterSize(PRInt32 aLengthCSSPixels, PRBool aIsWidth);
  nsRect GetInnerScreenRect();

  PRBool IsFrame()
  {
    return GetParentInternal() != nsnull;
  }

  // If aLookForCallerOnJSStack is true, this method will look at the JS stack
  // to determine who the caller is.  If it's false, it'll use |this| as the
  // caller.
  PRBool WindowExists(const nsAString& aName, PRBool aLookForCallerOnJSStack);

  already_AddRefed<nsIWidget> GetMainWidget();
  nsIWidget* GetNearestWidget();

  void Freeze()
  {
    NS_ASSERTION(!IsFrozen(), "Double-freezing?");
    mIsFrozen = PR_TRUE;
    NotifyDOMWindowFrozen(this);
  }

  void Thaw()
  {
    mIsFrozen = PR_FALSE;
    NotifyDOMWindowThawed(this);
  }

  PRBool IsInModalState();

  nsTimeout* FirstTimeout() {
    // Note: might not actually return an nsTimeout.  Use IsTimeout to check.
    return static_cast<nsTimeout*>(PR_LIST_HEAD(&mTimeouts));
  }

  nsTimeout* LastTimeout() {
    // Note: might not actually return an nsTimeout.  Use IsTimeout to check.
    return static_cast<nsTimeout*>(PR_LIST_TAIL(&mTimeouts));
  }

  PRBool IsTimeout(PRCList* aList) {
    return aList != &mTimeouts;
  }

  // Convenience functions for the many methods that need to scale
  // from device to CSS pixels or vice versa.  Note: if a presentation
  // context is not available, they will assume a 1:1 ratio.
  PRInt32 DevToCSSIntPixels(PRInt32 px);
  PRInt32 CSSToDevIntPixels(PRInt32 px);
  nsIntSize DevToCSSIntPixels(nsIntSize px);
  nsIntSize CSSToDevIntPixels(nsIntSize px);

  virtual void SetFocusedNode(nsIContent* aNode,
                              PRUint32 aFocusMethod = 0,
                              PRBool aNeedsFocus = PR_FALSE);

  virtual PRUint32 GetFocusMethod();

  virtual PRBool ShouldShowFocusRing();

  virtual void SetKeyboardIndicators(UIStateChangeType aShowAccelerators,
                                     UIStateChangeType aShowFocusRings);
  virtual void GetKeyboardIndicators(PRBool* aShowAccelerators,
                                     PRBool* aShowFocusRings);

  void UpdateCanvasFocus(PRBool aFocusChanged, nsIContent* aNewContent);

  already_AddRefed<nsPIWindowRoot> GetTopWindowRoot();

  static void NotifyDOMWindowDestroyed(nsGlobalWindow* aWindow);
  void NotifyWindowIDDestroyed(const char* aTopic);

  static void NotifyDOMWindowFrozen(nsGlobalWindow* aWindow);
  static void NotifyDOMWindowThawed(nsGlobalWindow* aWindow);
  
  void ClearStatus();

  virtual void UpdateParentTarget();

  PRBool GetIsTabModalPromptAllowed();

  inline PRInt32 DOMMinTimeoutValue() const;

  // When adding new member variables, be careful not to create cycles
  // through JavaScript.  If there is any chance that a member variable
  // could own objects that are implemented in JavaScript, then those
  // objects will keep the global object (this object) alive.  To prevent
  // these cycles, ownership of such members must be released in
  // |CleanUp| and |SetDocShell|.

  // This member is also used on both inner and outer windows, but
  // for slightly different purposes. On inner windows it means the
  // inner window is held onto by session history and should not
  // change. On outer windows it means that the window is in a state
  // where we don't want to force creation of a new inner window since
  // we're in the middle of doing just that.
  PRPackedBool                  mIsFrozen : 1;

  // True if the Java properties have been initialized on this
  // window. Only used on inner windows.
  PRPackedBool                  mDidInitJavaProperties : 1;
  
  // These members are only used on outer window objects. Make sure
  // you never set any of these on an inner object!
  PRPackedBool                  mFullScreen : 1;
  PRPackedBool                  mIsClosed : 1;
  PRPackedBool                  mInClose : 1;
  // mHavePendingClose means we've got a termination function set to
  // close us when the JS stops executing or that we have a close
  // event posted.  If this is set, just ignore window.close() calls.
  PRPackedBool                  mHavePendingClose : 1;
  PRPackedBool                  mHadOriginalOpener : 1;
  PRPackedBool                  mIsPopupSpam : 1;

  // Indicates whether scripts are allowed to close this window.
  PRPackedBool                  mBlockScriptedClosingFlag : 1;

  // Track what sorts of events we need to fire when thawed
  PRPackedBool                  mFireOfflineStatusChangeEventOnThaw : 1;

  // Indicates whether we're in the middle of creating an initializing
  // a new inner window object.
  PRPackedBool                  mCreatingInnerWindow : 1;

  // Fast way to tell if this is a chrome window (without having to QI).
  PRPackedBool                  mIsChrome : 1;

  // Hack to indicate whether a chrome window needs its message manager
  // to be disconnected, since clean up code is shared in the global
  // window superclass.
  PRPackedBool                  mCleanMessageManager : 1;

  // Indicates that the current document has never received a document focus
  // event.
  PRPackedBool           mNeedsFocus : 1;
  PRPackedBool           mHasFocus : 1;

  // whether to show keyboard accelerators
  PRPackedBool           mShowAccelerators : 1;

  // whether to show focus rings
  PRPackedBool           mShowFocusRings : 1;

  // when true, show focus rings for the current focused content only.
  // This will be reset when another element is focused
  PRPackedBool           mShowFocusRingForContent : 1;

  // true if tab navigation has occurred for this window. Focus rings
  // should be displayed.
  PRPackedBool           mFocusByKeyOccurred : 1;

  // Indicates whether this window is getting device motion change events
  PRPackedBool           mHasDeviceMotion : 1;

  // whether we've sent the destroy notification for our window id
  PRPackedBool           mNotifiedIDDestroyed : 1;

  nsCOMPtr<nsIScriptContext>    mContext;
  nsWeakPtr                     mOpener;
  nsCOMPtr<nsIControllers>      mControllers;
  nsCOMPtr<nsIArray>            mArguments;
  nsCOMPtr<nsIArray>            mArgumentsLast;
  nsCOMPtr<nsIPrincipal>        mArgumentsOrigin;
  nsRefPtr<nsNavigator>         mNavigator;
  nsRefPtr<nsScreen>            mScreen;
  nsRefPtr<nsPerformance>       mPerformance;
  nsRefPtr<nsDOMWindowList>     mFrames;
  nsRefPtr<nsBarProp>           mMenubar;
  nsRefPtr<nsBarProp>           mToolbar;
  nsRefPtr<nsBarProp>           mLocationbar;
  nsRefPtr<nsBarProp>           mPersonalbar;
  nsRefPtr<nsBarProp>           mStatusbar;
  nsRefPtr<nsBarProp>           mScrollbars;
  nsCOMPtr<nsIWeakReference>    mWindowUtils;
  nsString                      mStatus;
  nsString                      mDefaultStatus;
  // index 0->language_id 1, so index MAX-1 == language_id MAX
  nsGlobalWindowObserver*       mObserver;
#ifndef MOZ_DISABLE_DOMCRYPTO
  nsCOMPtr<nsIDOMCrypto>        mCrypto;
#endif
  nsCOMPtr<nsIDOMStorage>      mLocalStorage;
  nsCOMPtr<nsIDOMStorage>      mSessionStorage;

  nsCOMPtr<nsIXPConnectJSObjectHolder> mInnerWindowHolder;
  nsCOMPtr<nsIPrincipal> mOpenerScriptPrincipal; // strong; used to determine
                                                 // whether to clear scope

  // These member variable are used only on inner windows.
  nsRefPtr<nsEventListenerManager> mListenerManager;
  // mTimeouts is generally sorted by mWhen, unless mTimeoutInsertionPoint is
  // non-null.  In that case, the dummy timeout pointed to by
  // mTimeoutInsertionPoint may have a later mWhen than some of the timeouts
  // that come after it.
  PRCList                       mTimeouts;
  // If mTimeoutInsertionPoint is non-null, insertions should happen after it.
  // This is a dummy timeout at the moment; if that ever changes, the logic in
  // ResetTimersForNonBackgroundWindow needs to change.
  nsTimeout*                    mTimeoutInsertionPoint;
  PRUint32                      mTimeoutPublicIdCounter;
  PRUint32                      mTimeoutFiringDepth;
  nsRefPtr<nsLocation>          mLocation;
  nsRefPtr<nsHistory>           mHistory;

  // Holder of the dummy java plugin, used to expose window.java and
  // window.packages.
  nsRefPtr<nsDummyJavaPluginOwner> mDummyJavaPluginOwner;

  // These member variables are used on both inner and the outer windows.
  nsCOMPtr<nsIPrincipal> mDocumentPrincipal;
  nsCOMPtr<nsIDocument> mDoc;  // For fast access to principals
  JSObject* mJSObject;

  typedef nsCOMArray<nsIDOMStorageEvent> nsDOMStorageEventArray;
  nsDOMStorageEventArray mPendingStorageEvents;
  nsAutoPtr< nsDataHashtable<nsStringHashKey, PRBool> > mPendingStorageEventsObsolete;

  PRUint32 mTimeoutsSuspendDepth;

  // the method that was used to focus mFocusedNode
  PRUint32 mFocusMethod;

  PRUint32 mSerial;

#ifdef DEBUG
  PRBool mSetOpenerWindowCalled;
  nsCOMPtr<nsIURI> mLastOpenedURI;
#endif

  PRBool mCleanedUp, mCallCleanUpAfterModalDialogCloses;

  nsCOMPtr<nsIDOMOfflineResourceList> mApplicationCache;

  nsDataHashtable<nsVoidPtrHashKey, void*> mCachedXBLPrototypeHandlers;

  nsCOMPtr<nsIDocument> mSuspendedDoc;

  nsCOMPtr<nsIIDBFactory> mIndexedDB;

  // In the case of a "trusted" dialog (@see PopupControlState), we
  // set this counter to ensure a max of MAX_DIALOG_LIMIT
  PRUint32                      mDialogAbuseCount;

  // This holds the time when the last modal dialog was shown, if two
  // dialogs are shown within CONCURRENT_DIALOG_TIME_LIMIT the
  // checkbox is shown. In the case of ShowModalDialog another Confirm
  // dialog will be shown, the result of the checkbox/confirm dialog
  // will be stored in mDialogDisabled variable.
  TimeStamp                     mLastDialogQuitTime;
  PRPackedBool                  mDialogDisabled;

  nsRefPtr<nsDOMMozURLProperty> mURLProperty;

  friend class nsDOMScriptableHelper;
  friend class nsDOMWindowUtils;
  friend class PostMessageEvent;
  static nsIDOMStorageList* sGlobalStorageList;

  static WindowByIdTable* sWindowsById;
  static bool sWarnedAboutWindowInternal;
};

/*
 * nsGlobalChromeWindow inherits from nsGlobalWindow. It is the global
 * object created for a Chrome Window only.
 */
class nsGlobalChromeWindow : public nsGlobalWindow,
                             public nsIDOMChromeWindow
{
public:
  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMChromeWindow interface
  NS_DECL_NSIDOMCHROMEWINDOW

  nsGlobalChromeWindow(nsGlobalWindow *aOuterWindow)
    : nsGlobalWindow(aOuterWindow)
  {
    mIsChrome = PR_TRUE;
    mCleanMessageManager = PR_TRUE;
  }

  ~nsGlobalChromeWindow()
  {
    NS_ABORT_IF_FALSE(mCleanMessageManager,
                      "chrome windows may always disconnect the msg manager");
    if (mMessageManager) {
      static_cast<nsFrameMessageManager *>(
        mMessageManager.get())->Disconnect();
    }

    mCleanMessageManager = PR_FALSE;
  }

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsGlobalChromeWindow,
                                           nsGlobalWindow)

  nsCOMPtr<nsIBrowserDOMWindow> mBrowserDOMWindow;
  nsCOMPtr<nsIChromeFrameMessageManager> mMessageManager;
};

/*
 * nsGlobalModalWindow inherits from nsGlobalWindow. It is the global
 * object created for a modal content windows only (i.e. not modal
 * chrome dialogs).
 */
class nsGlobalModalWindow : public nsGlobalWindow,
                            public nsIDOMModalContentWindow
{
public:
  nsGlobalModalWindow(nsGlobalWindow *aOuterWindow)
    : nsGlobalWindow(aOuterWindow)
  {
    mIsModalContentWindow = PR_TRUE;
  }

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMMODALCONTENTWINDOW

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsGlobalModalWindow, nsGlobalWindow)

  virtual NS_HIDDEN_(nsresult) SetNewDocument(nsIDocument *aDocument,
                                              nsISupports *aState,
                                              PRBool aForceReuseInnerWindow);

protected:
  nsCOMPtr<nsIVariant> mReturnValue;
};


//*****************************************************************************
// nsNavigator: Script "navigator" object
//*****************************************************************************

class nsNavigator : public nsIDOMNavigator,
                    public nsIDOMClientInformation,
                    public nsIDOMNavigatorGeolocation,
                    public nsIDOMNavigatorDesktopNotification
{
public:
  nsNavigator(nsIDocShell *aDocShell);
  virtual ~nsNavigator();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMNAVIGATOR
  NS_DECL_NSIDOMCLIENTINFORMATION
  NS_DECL_NSIDOMNAVIGATORGEOLOCATION
  NS_DECL_NSIDOMNAVIGATORDESKTOPNOTIFICATION
  
  void SetDocShell(nsIDocShell *aDocShell);
  nsIDocShell *GetDocShell()
  {
    return mDocShell;
  }

  void LoadingNewDocument();
  nsresult RefreshMIMEArray();

  static bool HasDesktopNotificationSupport();

  PRInt64 SizeOf() const;

protected:
  nsRefPtr<nsMimeTypeArray> mMimeTypes;
  nsRefPtr<nsPluginArray> mPlugins;
  nsRefPtr<nsGeolocation> mGeolocation;
  nsRefPtr<nsDesktopNotificationCenter> mNotification;
  nsIDocShell* mDocShell; // weak reference
};

nsresult NS_GetNavigatorUserAgent(nsAString& aUserAgent);
nsresult NS_GetNavigatorPlatform(nsAString& aPlatform);
nsresult NS_GetNavigatorAppVersion(nsAString& aAppVersion);
nsresult NS_GetNavigatorAppName(nsAString& aAppName);

/* factory function */
nsresult
NS_NewScriptGlobalObject(PRBool aIsChrome, PRBool aIsModalContentWindow,
                         nsIScriptGlobalObject **aResult);

#endif /* nsGlobalWindow_h___ */

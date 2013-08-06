/* -*- Mode: c++; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Android code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
 *   Matt Brubeck <mbrubeck@mozilla.com>
 *   Vivien Nicolas <vnicolas@mozilla.com>
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

#include <android/log.h>
#include <math.h>

#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/unused.h"
#include "mozilla/Preferences.h"

using mozilla::dom::ContentParent;
using mozilla::dom::ContentChild;
using mozilla::unused;

#include "nsAppShell.h"
#include "nsIdleService.h"
#include "nsWindow.h"
#include "nsIObserverService.h"

#include "nsRenderingContext.h"
#include "nsIDOMSimpleGestureEvent.h"

#include "nsWidgetAtoms.h"
#include "nsWidgetsCID.h"
#include "nsGfxCIID.h"

#include "gfxImageSurface.h"
#include "gfxContext.h"

#include "Layers.h"
#include "BasicLayers.h"
#include "LayerManagerOGL.h"
#include "GLContext.h"
#include "GLContextProvider.h"

#include "nsTArray.h"

#include "AndroidBridge.h"

using namespace mozilla;

NS_IMPL_ISUPPORTS_INHERITED0(nsWindow, nsBaseWidget)

// The dimensions of the current android view
static gfxIntSize gAndroidBounds;
static gfxIntSize gAndroidScreenBounds;

class ContentCreationNotifier;
static nsCOMPtr<ContentCreationNotifier> gContentCreationNotifier;
// A helper class to send updates when content processes
// are created. Currently an update for the screen size is sent.
class ContentCreationNotifier : public nsIObserver
{
    NS_DECL_ISUPPORTS

    NS_IMETHOD Observe(nsISupports* aSubject,
                       const char* aTopic,
                       const PRUnichar* aData)
    {
        if (!strcmp(aTopic, "ipc:content-created")) {
            ContentParent *cp = ContentParent::GetSingleton(PR_FALSE);
            NS_ABORT_IF_FALSE(cp, "Must have content process if notified of its creation");
            unused << cp->SendScreenSizeChanged(gAndroidScreenBounds);
        } else if (!strcmp(aTopic, "xpcom-shutdown")) {
            nsCOMPtr<nsIObserverService>
                obs(do_GetService("@mozilla.org/observer-service;1"));
            if (obs) {
                obs->RemoveObserver(static_cast<nsIObserver*>(this),
                                    "xpcom-shutdown");
                obs->RemoveObserver(static_cast<nsIObserver*>(this),
                                    "ipc:content-created");
            }
            gContentCreationNotifier = nsnull;
        }

        return NS_OK;
    }
};

NS_IMPL_ISUPPORTS1(ContentCreationNotifier,
                   nsIObserver)

static PRBool gMenu;
static PRBool gMenuConsumed;

// All the toplevel windows that have been created; these are in
// stacking order, so the window at gAndroidBounds[0] is the topmost
// one.
static nsTArray<nsWindow*> gTopLevelWindows;

static nsRefPtr<gl::GLContext> sGLContext;
static bool sFailedToCreateGLContext = false;
static bool sValidSurface;

// Multitouch swipe thresholds in inches
static const double SWIPE_MAX_PINCH_DELTA_INCHES = 0.4;
static const double SWIPE_MIN_DISTANCE_INCHES = 0.6;

static nsWindow*
TopWindow()
{
    if (!gTopLevelWindows.IsEmpty())
        return gTopLevelWindows[0];
    return nsnull;
}

void
nsWindow::LogWindow(nsWindow *win, int index, int indent)
{
    char spaces[] = "                    ";
    spaces[indent < 20 ? indent : 20] = 0;
    ALOG("%s [% 2d] 0x%08x [parent 0x%08x] [% 3d,% 3d % 3dx% 3d] vis %d type %d",
         spaces, index, (intptr_t)win, (intptr_t)win->mParent,
         win->mBounds.x, win->mBounds.y,
         win->mBounds.width, win->mBounds.height,
         win->mIsVisible, win->mWindowType);
}

void
nsWindow::DumpWindows()
{
    DumpWindows(gTopLevelWindows);
}

void
nsWindow::DumpWindows(const nsTArray<nsWindow*>& wins, int indent)
{
    for (PRUint32 i = 0; i < wins.Length(); ++i) {
        nsWindow *w = wins[i];
        LogWindow(w, i, indent);
        DumpWindows(w->mChildren, indent+1);
    }
}

nsWindow::nsWindow() :
    mIsVisible(PR_FALSE),
    mParent(nsnull),
    mFocus(nsnull)
{
}

nsWindow::~nsWindow()
{
    gTopLevelWindows.RemoveElement(this);
    nsWindow *top = FindTopLevel();
    if (top->mFocus == this)
        top->mFocus = nsnull;
    ALOG("nsWindow %p destructor", (void*)this);
}

PRBool
nsWindow::IsTopLevel()
{
    return mWindowType == eWindowType_toplevel ||
        mWindowType == eWindowType_dialog ||
        mWindowType == eWindowType_invisible;
}

NS_IMETHODIMP
nsWindow::Create(nsIWidget *aParent,
                 nsNativeWidget aNativeParent,
                 const nsIntRect &aRect,
                 EVENT_CALLBACK aHandleEventFunction,
                 nsDeviceContext *aContext,
                 nsIAppShell *aAppShell,
                 nsIToolkit *aToolkit,
                 nsWidgetInitData *aInitData)
{
    ALOG("nsWindow[%p]::Create %p [%d %d %d %d]", (void*)this, (void*)aParent, aRect.x, aRect.y, aRect.width, aRect.height);
    nsWindow *parent = (nsWindow*) aParent;

    if (!AndroidBridge::Bridge()) {
        aNativeParent = nsnull;
    }

    if (aNativeParent) {
        if (parent) {
            ALOG("Ignoring native parent on Android window [%p], since parent was specified (%p %p)", (void*)this, (void*)aNativeParent, (void*)aParent);
        } else {
            parent = (nsWindow*) aNativeParent;
        }
    }

    mBounds = aRect;

    // for toplevel windows, bounds are fixed to full screen size
    if (!parent) {
        mBounds.x = 0;
        mBounds.y = 0;
        mBounds.width = gAndroidBounds.width;
        mBounds.height = gAndroidBounds.height;
    }

    BaseCreate(nsnull, mBounds, aHandleEventFunction, aContext,
               aAppShell, aToolkit, aInitData);

    NS_ASSERTION(IsTopLevel() || parent, "non top level windowdoesn't have a parent!");

    if (IsTopLevel()) {
        gTopLevelWindows.AppendElement(this);
    }

    if (parent) {
        parent->mChildren.AppendElement(this);
        mParent = parent;
    }

    float dpi = GetDPI();
    mSwipeMaxPinchDelta = SWIPE_MAX_PINCH_DELTA_INCHES * dpi;
    mSwipeMinDistance = SWIPE_MIN_DISTANCE_INCHES * dpi;

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Destroy(void)
{
    for (PRUint32 i = 0; i < mChildren.Length(); ++i) {
        // why do we still have children?
        ALOG("### Warning: Destroying window %p and reparenting child %p to null!", (void*)this, (void*)mChildren[i]);
        mChildren[i]->SetParent(nsnull);
    }

    if (IsTopLevel())
        gTopLevelWindows.RemoveElement(this);

    if (mParent)
        mParent->mChildren.RemoveElement(this);

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::ConfigureChildren(const nsTArray<nsIWidget::Configuration>& config)
{
    for (PRUint32 i = 0; i < config.Length(); ++i) {
        nsWindow *childWin = (nsWindow*) config[i].mChild;
        childWin->Resize(config[i].mBounds.x,
                         config[i].mBounds.y,
                         config[i].mBounds.width,
                         config[i].mBounds.height,
                         PR_FALSE);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetParent(nsIWidget *aNewParent)
{
    if ((nsIWidget*)mParent == aNewParent)
        return NS_OK;

    // If we had a parent before, remove ourselves from its list of
    // children.  If we didn't have a parent, then remove ourselves
    // from the list of toplevel windows if we're about to get a
    // parent.
    if (mParent)
        mParent->mChildren.RemoveElement(this);

    mParent = (nsWindow*)aNewParent;

    if (mParent)
        mParent->mChildren.AppendElement(this);

    // if we are now in the toplevel window's hierarchy, schedule a redraw
    if (FindTopLevel() == TopWindow())
        nsAppShell::gAppShell->PostEvent(new AndroidGeckoEvent(-1, -1, -1, -1));

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::ReparentNativeWidget(nsIWidget *aNewParent)
{
    NS_PRECONDITION(aNewParent, "");
    return NS_OK;
}

nsIWidget*
nsWindow::GetParent()
{
    return mParent;
}

float
nsWindow::GetDPI()
{
    if (AndroidBridge::Bridge())
        return AndroidBridge::Bridge()->GetDPI();
    return 160.0f;
}

NS_IMETHODIMP
nsWindow::Show(PRBool aState)
{
    ALOG("nsWindow[%p]::Show %d", (void*)this, aState);

    if (mWindowType == eWindowType_invisible) {
        ALOG("trying to show invisible window! ignoring..");
        return NS_ERROR_FAILURE;
    }

    if (aState == mIsVisible)
        return NS_OK;

    mIsVisible = aState;

    if (IsTopLevel()) {
        // XXX should we bring this to the front when it's shown,
        // if it's a toplevel widget?

        // XXX we should synthesize a NS_MOUSE_EXIT (for old top
        // window)/NS_MOUSE_ENTER (for new top window) since we need
        // to pretend that the top window always has focus.  Not sure
        // if Show() is the right place to do this, though.

        if (aState) {
            // It just became visible, so send a resize update if necessary
            // and bring it to the front.
            Resize(0, 0, gAndroidBounds.width, gAndroidBounds.height, PR_FALSE);
            BringToFront();
        } else if (TopWindow() == this) {
            // find the next visible window to show
            unsigned int i;
            for (i = 1; i < gTopLevelWindows.Length(); i++) {
                nsWindow *win = gTopLevelWindows[i];
                if (!win->mIsVisible)
                    continue;

                win->BringToFront();
                break;
            }
        }
    } else if (FindTopLevel() == TopWindow()) {
        nsAppShell::gAppShell->PostEvent(new AndroidGeckoEvent(-1, -1, -1, -1));
    }

#ifdef ANDROID_DEBUG_WIDGET
    DumpWindows();
#endif

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetModal(PRBool aState)
{
    ALOG("nsWindow[%p]::SetModal %d ignored", (void*)this, aState);

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::IsVisible(PRBool& aState)
{
    aState = mIsVisible;
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::ConstrainPosition(PRBool aAllowSlop,
                            PRInt32 *aX,
                            PRInt32 *aY)
{
    ALOG("nsWindow[%p]::ConstrainPosition %d [%d %d]", (void*)this, aAllowSlop, *aX, *aY);

    // constrain toplevel windows; children we don't care about
    if (IsTopLevel()) {
        *aX = 0;
        *aY = 0;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Move(PRInt32 aX,
               PRInt32 aY)
{
    if (IsTopLevel())
        return NS_OK;

    return Resize(aX,
                  aY,
                  mBounds.width,
                  mBounds.height,
                  PR_TRUE);
}

NS_IMETHODIMP
nsWindow::Resize(PRInt32 aWidth,
                 PRInt32 aHeight,
                 PRBool aRepaint)
{
    return Resize(mBounds.x,
                  mBounds.y,
                  aWidth,
                  aHeight,
                  aRepaint);
}

NS_IMETHODIMP
nsWindow::Resize(PRInt32 aX,
                 PRInt32 aY,
                 PRInt32 aWidth,
                 PRInt32 aHeight,
                 PRBool aRepaint)
{
    ALOG("nsWindow[%p]::Resize [%d %d %d %d] (repaint %d)", (void*)this, aX, aY, aWidth, aHeight, aRepaint);

    PRBool needSizeDispatch = aWidth != mBounds.width || aHeight != mBounds.height;

    if (IsTopLevel()) {
        ALOG("... ignoring Resize sizes on toplevel window");
        aX = 0;
        aY = 0;
        aWidth = gAndroidBounds.width;
        aHeight = gAndroidBounds.height;
    }

    mBounds.x = aX;
    mBounds.y = aY;
    mBounds.width = aWidth;
    mBounds.height = aHeight;

    if (needSizeDispatch)
        OnSizeChanged(gfxIntSize(aWidth, aHeight));

    // Should we skip honoring aRepaint here?
    if (aRepaint && FindTopLevel() == TopWindow())
        nsAppShell::gAppShell->PostEvent(new AndroidGeckoEvent(-1, -1, -1, -1));

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetZIndex(PRInt32 aZIndex)
{
    ALOG("nsWindow[%p]::SetZIndex %d ignored", (void*)this, aZIndex);

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::PlaceBehind(nsTopLevelWidgetZPlacement aPlacement,
                      nsIWidget *aWidget,
                      PRBool aActivate)
{
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetSizeMode(PRInt32 aMode)
{
    switch (aMode) {
        case nsSizeMode_Minimized:
            AndroidBridge::Bridge()->MoveTaskToBack();
            break;
        case nsSizeMode_Fullscreen:
            MakeFullScreen(PR_TRUE);
            break;
    }
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Enable(PRBool aState)
{
    ALOG("nsWindow[%p]::Enable %d ignored", (void*)this, aState);
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::IsEnabled(PRBool *aState)
{
    *aState = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Invalidate(const nsIntRect &aRect,
                     PRBool aIsSynchronous)
{
    nsAppShell::gAppShell->PostEvent(new AndroidGeckoEvent(-1, -1, -1, -1));
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Update()
{
    return NS_OK;
}

nsWindow*
nsWindow::FindTopLevel()
{
    nsWindow *toplevel = this;
    while (toplevel) {
        if (toplevel->IsTopLevel())
            return toplevel;

        toplevel = toplevel->mParent;
    }

    ALOG("nsWindow::FindTopLevel(): couldn't find a toplevel or dialog window in this [%p] widget's hierarchy!", (void*)this);
    return this;
}

NS_IMETHODIMP
nsWindow::SetFocus(PRBool aRaise)
{
    if (!aRaise)
        ALOG("nsWindow::SetFocus: can't set focus without raising, ignoring aRaise = false!");

    if (!AndroidBridge::Bridge())
        return NS_OK;

    nsWindow *top = FindTopLevel();
    top->mFocus = this;
    top->BringToFront();

    return NS_OK;
}

void
nsWindow::BringToFront()
{
    if (FindTopLevel() == TopWindow())
        return;

    if (!IsTopLevel()) {
        FindTopLevel()->BringToFront();
        return;
    }

    nsWindow *oldTop = nsnull;
    if (!gTopLevelWindows.IsEmpty())
        oldTop = gTopLevelWindows[0];

    gTopLevelWindows.RemoveElement(this);
    gTopLevelWindows.InsertElementAt(0, this);

    if (oldTop) {
        nsGUIEvent event(PR_TRUE, NS_DEACTIVATE, oldTop);
        DispatchEvent(&event);
    }

    nsGUIEvent event(PR_TRUE, NS_ACTIVATE, this);
    DispatchEvent(&event);

    nsAppShell::gAppShell->PostEvent(new AndroidGeckoEvent(-1, -1, -1, -1));
}

NS_IMETHODIMP
nsWindow::GetScreenBounds(nsIntRect &aRect)
{
    nsIntPoint p = WidgetToScreenOffset();

    aRect.x = p.x;
    aRect.y = p.y;
    aRect.width = mBounds.width;
    aRect.height = mBounds.height;
    
    return NS_OK;
}

nsIntPoint
nsWindow::WidgetToScreenOffset()
{
    nsIntPoint p(0, 0);
    nsWindow *w = this;

    while (w && !w->IsTopLevel()) {
        p.x += w->mBounds.x;
        p.y += w->mBounds.y;

        w = w->mParent;
    }

    return p;
}

NS_IMETHODIMP
nsWindow::DispatchEvent(nsGUIEvent *aEvent,
                        nsEventStatus &aStatus)
{
    aStatus = DispatchEvent(aEvent);
    return NS_OK;
}

nsEventStatus
nsWindow::DispatchEvent(nsGUIEvent *aEvent)
{
    if (mEventCallback) {
        nsEventStatus status = (*mEventCallback)(aEvent);

        switch (aEvent->message) {
        case NS_COMPOSITION_START:
            mIMEComposing = PR_TRUE;
            break;
        case NS_COMPOSITION_END:
            mIMEComposing = PR_FALSE;
            break;
        case NS_TEXT_TEXT:
            mIMEComposingText = static_cast<nsTextEvent*>(aEvent)->theText;
            break;
        }
        return status;
    }
    return nsEventStatus_eIgnore;
}

NS_IMETHODIMP
nsWindow::MakeFullScreen(PRBool aFullScreen)
{
    AndroidBridge::Bridge()->SetFullScreen(aFullScreen);
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetWindowClass(const nsAString& xulWinType)
{
    return NS_OK;
}

mozilla::layers::LayerManager*
nsWindow::GetLayerManager(PLayersChild*, LayersBackend, LayerManagerPersistence, 
                          bool* aAllowRetaining)
{
    if (aAllowRetaining) {
        *aAllowRetaining = true;
    }
    if (mLayerManager) {
        return mLayerManager;
    }

    printf_stderr("nsWindow::GetLayerManager\n");

    nsWindow *topWindow = TopWindow();

    if (!topWindow) {
        printf_stderr(" -- no topwindow\n");
        mLayerManager = CreateBasicLayerManager();
        return mLayerManager;
    }

    mUseAcceleratedRendering = GetShouldAccelerate();

    if (!mUseAcceleratedRendering ||
        sFailedToCreateGLContext)
    {
        printf_stderr(" -- creating basic, not accelerated\n");
        mLayerManager = CreateBasicLayerManager();
        return mLayerManager;
    }

    if (!sGLContext) {
        // the window we give doesn't matter here
        sGLContext = mozilla::gl::GLContextProvider::CreateForWindow(this);
    }

    if (sGLContext) {
        nsRefPtr<mozilla::layers::LayerManagerOGL> layerManager =
            new mozilla::layers::LayerManagerOGL(this);

        if (layerManager && layerManager->Initialize(sGLContext))
            mLayerManager = layerManager;
        sValidSurface = true;
    }

    if (!sGLContext || !mLayerManager) {
        sGLContext = nsnull;
        sFailedToCreateGLContext = PR_TRUE;

        mLayerManager = CreateBasicLayerManager();
    }

    return mLayerManager;
}

gfxASurface*
nsWindow::GetThebesSurface()
{
    /* This is really a dummy surface; this is only used when doing reflow, because
     * we need a RenderingContext to measure text against.
     */

    // XXX this really wants to return already_AddRefed, but this only really gets used
    // on direct assignment to a gfxASurface
    return new gfxImageSurface(gfxIntSize(5,5), gfxImageSurface::ImageFormatRGB24);
}

void
nsWindow::OnGlobalAndroidEvent(AndroidGeckoEvent *ae)
{
    if (!AndroidBridge::Bridge())
        return;

    nsWindow *win = TopWindow();
    if (!win)
        return;

    switch (ae->Type()) {
        case AndroidGeckoEvent::SIZE_CHANGED: {
            int nw = ae->P0().x;
            int nh = ae->P0().y;

            if (nw != gAndroidBounds.width ||
                nh != gAndroidBounds.height) {

                gAndroidBounds.width = nw;
                gAndroidBounds.height = nh;

                // tell all the windows about the new size
                for (size_t i = 0; i < gTopLevelWindows.Length(); ++i) {
                    if (gTopLevelWindows[i]->mIsVisible)
                        gTopLevelWindows[i]->Resize(gAndroidBounds.width,
                                                    gAndroidBounds.height,
                                                    PR_TRUE);
                }
            }

            int newScreenWidth = ae->P1().x;
            int newScreenHeight = ae->P1().y;

            if (newScreenWidth == gAndroidScreenBounds.width &&
                newScreenHeight == gAndroidScreenBounds.height)
                break;

            gAndroidScreenBounds.width = newScreenWidth;
            gAndroidScreenBounds.height = newScreenHeight;

            if (XRE_GetProcessType() != GeckoProcessType_Default)
                break;

            // Tell the content process the new screen size.
            ContentParent *cp = ContentParent::GetSingleton(PR_FALSE);
            if (cp)
                unused << cp->SendScreenSizeChanged(gAndroidScreenBounds);

            if (gContentCreationNotifier)
                break;

            // If the content process is not created yet, wait until it's
            // created and then tell it the screen size.
            nsCOMPtr<nsIObserverService> obs = do_GetService("@mozilla.org/observer-service;1");
            if (!obs)
                break;

            nsCOMPtr<ContentCreationNotifier> notifier = new ContentCreationNotifier;
            if (NS_SUCCEEDED(obs->AddObserver(notifier, "ipc:content-created", PR_FALSE))) {
                if (NS_SUCCEEDED(obs->AddObserver(notifier, "xpcom-shutdown", PR_FALSE)))
                    gContentCreationNotifier = notifier;
                else
                    obs->RemoveObserver(notifier, "ipc:content-created");
            }
        }

        case AndroidGeckoEvent::MOTION_EVENT: {
            win->UserActivity();
            if (!gTopLevelWindows.IsEmpty()) {
                nsIntPoint pt(ae->P0());
                pt.x = NS_MIN(NS_MAX(pt.x, 0), gAndroidBounds.width - 1);
                pt.y = NS_MIN(NS_MAX(pt.y, 0), gAndroidBounds.height - 1);
                nsWindow *target = win->FindWindowForPoint(pt);

#if 0
                ALOG("MOTION_EVENT %f,%f -> %p (visible: %d children: %d)", ae->P0().x, ae->P0().y, (void*)target,
                     target ? target->mIsVisible : 0,
                     target ? target->mChildren.Length() : 0);

                DumpWindows();
#endif

                if (target) {
                    if (ae->Count() > 1)
                        target->OnMultitouchEvent(ae);
                    else
                        target->OnMotionEvent(ae);
                }
            }
            break;
        }

        case AndroidGeckoEvent::KEY_EVENT:
            win->UserActivity();
            if (win->mFocus)
                win->mFocus->OnKeyEvent(ae);
            break;

        case AndroidGeckoEvent::DRAW:
            win->OnDraw(ae);
            break;

        case AndroidGeckoEvent::IME_EVENT:
            win->UserActivity();
            if (win->mFocus) {
                win->mFocus->OnIMEEvent(ae);
            } else {
                NS_WARNING("Sending unexpected IME event to top window");
                win->OnIMEEvent(ae);
            }
            break;

        case AndroidGeckoEvent::SURFACE_CREATED:
            break;

        case AndroidGeckoEvent::SURFACE_DESTROYED:
            sValidSurface = false;
            break;

        case AndroidGeckoEvent::GECKO_EVENT_SYNC:
            AndroidBridge::Bridge()->AcknowledgeEventSync();
            break;

        default:
            break;
    }
}

void
nsWindow::OnAndroidEvent(AndroidGeckoEvent *ae)
{
    if (!AndroidBridge::Bridge())
        return;

    switch (ae->Type()) {
        case AndroidGeckoEvent::DRAW:
            OnDraw(ae);
            break;

        default:
            ALOG("Window got targetted android event type %d, but didn't handle!", ae->Type());
            break;
    }
}

PRBool
nsWindow::DrawTo(gfxASurface *targetSurface)
{
    if (!mIsVisible)
        return PR_FALSE;

    nsEventStatus status;
    nsIntRect boundsRect(0, 0, mBounds.width, mBounds.height);

    // Figure out if any of our children cover this widget completely
    PRInt32 coveringChildIndex = -1;
    for (PRUint32 i = 0; i < mChildren.Length(); ++i) {
        if (mChildren[i]->mBounds.IsEmpty())
            continue;

        if (mChildren[i]->mBounds.Contains(boundsRect)) {
            coveringChildIndex = PRInt32(i);
        }
    }

    // If we have no covering child, then we need to render this.
    if (coveringChildIndex == -1) {
        nsPaintEvent event(PR_TRUE, NS_PAINT, this);
        event.region = boundsRect;
        switch (GetLayerManager(nsnull)->GetBackendType()) {
            case LayerManager::LAYERS_BASIC: {
                nsRefPtr<gfxContext> ctx = new gfxContext(targetSurface);

                {
                    AutoLayerManagerSetup
                      setupLayerManager(this, ctx, BasicLayerManager::BUFFER_NONE);
                    status = DispatchEvent(&event);
                }

                // XXX uhh.. we can't just ignore this because we no longer have
                // what we needed before, but let's keep drawing the children anyway?
#if 0
                if (status == nsEventStatus_eIgnore)
                    return PR_FALSE;
#endif

                // XXX if we got an ignore for the parent, do we still want to draw the children?
                // We don't really have a good way not to...
                break;
            }

            case LayerManager::LAYERS_OPENGL: {
                static_cast<mozilla::layers::LayerManagerOGL*>(GetLayerManager(nsnull))->
                    SetClippingRegion(nsIntRegion(boundsRect));

                status = DispatchEvent(&event);
                break;
            }

            default:
                NS_ERROR("Invalid layer manager");
        }

        // We had no covering child, so make sure we draw all the children,
        // starting from index 0.
        coveringChildIndex = 0;
    }

    gfxPoint offset;

    if (targetSurface)
        offset = targetSurface->GetDeviceOffset();

    for (PRUint32 i = coveringChildIndex; i < mChildren.Length(); ++i) {
        if (mChildren[i]->mBounds.IsEmpty() ||
            !mChildren[i]->mBounds.Intersects(boundsRect)) {
            continue;
        }

        if (targetSurface)
            targetSurface->SetDeviceOffset(offset + gfxPoint(mChildren[i]->mBounds.x,
                                                             mChildren[i]->mBounds.y));

        PRBool ok = mChildren[i]->DrawTo(targetSurface);

        if (!ok) {
            ALOG("nsWindow[%p]::DrawTo child %d[%p] returned FALSE!", (void*) this, i, (void*)mChildren[i]);
        }
    }

    if (targetSurface)
        targetSurface->SetDeviceOffset(offset);

    return PR_TRUE;
}

void
nsWindow::OnDraw(AndroidGeckoEvent *ae)
{
    if (!IsTopLevel()) {
        ALOG("##### redraw for window %p, which is not a toplevel window -- sending to toplevel!", (void*) this);
        DumpWindows();
        return;
    }

    if (!mIsVisible) {
        ALOG("##### redraw for window %p, which is not visible -- ignoring!", (void*) this);
        DumpWindows();
        return;
    }

    AndroidBridge::AutoLocalJNIFrame jniFrame;

    AndroidGeckoSurfaceView& sview(AndroidBridge::Bridge()->SurfaceView());

    NS_ASSERTION(!sview.isNull(), "SurfaceView is null!");

    AndroidBridge::Bridge()->HideProgressDialogOnce();

    if (GetLayerManager(nsnull)->GetBackendType() == LayerManager::LAYERS_BASIC) {
        if (AndroidBridge::Bridge()->HasNativeBitmapAccess()) {
            jobject bitmap = sview.GetSoftwareDrawBitmap();
            if (!bitmap) {
                ALOG("no bitmap to draw into - skipping draw");
                return;
            }

            if (!AndroidBridge::Bridge()->ValidateBitmap(bitmap, mBounds.width, mBounds.height))
                return;

            void *buf = AndroidBridge::Bridge()->LockBitmap(bitmap);
            if (buf == nsnull) {
                ALOG("### Software drawing, but failed to lock bitmap.");
                return;
            }

            nsRefPtr<gfxImageSurface> targetSurface =
                new gfxImageSurface((unsigned char *)buf,
                                    gfxIntSize(mBounds.width, mBounds.height),
                                    mBounds.width * 2,
                                    gfxASurface::ImageFormatRGB16_565);
            if (targetSurface->CairoStatus()) {
                ALOG("### Failed to create a valid surface from the bitmap");
            } else {
                DrawTo(targetSurface);
            }

            AndroidBridge::Bridge()->UnlockBitmap(bitmap);
            sview.Draw2D(bitmap, mBounds.width, mBounds.height);
        } else {
            jobject bytebuf = sview.GetSoftwareDrawBuffer();
            if (!bytebuf) {
                ALOG("no buffer to draw into - skipping draw");
                return;
            }

            void *buf = AndroidBridge::JNI()->GetDirectBufferAddress(bytebuf);
            int cap = AndroidBridge::JNI()->GetDirectBufferCapacity(bytebuf);
            if (!buf || cap != (mBounds.width * mBounds.height * 2)) {
                ALOG("### Software drawing, but unexpected buffer size %d expected %d (or no buffer %p)!", cap, mBounds.width * mBounds.height * 2, buf);
                return;
            }

            nsRefPtr<gfxImageSurface> targetSurface =
                new gfxImageSurface((unsigned char *)buf,
                                    gfxIntSize(mBounds.width, mBounds.height),
                                    mBounds.width * 2,
                                    gfxASurface::ImageFormatRGB16_565);
            if (targetSurface->CairoStatus()) {
                ALOG("### Failed to create a valid surface");
            } else {
                DrawTo(targetSurface);
            }

            sview.Draw2D(bytebuf, mBounds.width * 2);
        }
    } else {
        int drawType = sview.BeginDrawing();

        if (drawType == AndroidGeckoSurfaceView::DRAW_ERROR) {
            ALOG("##### BeginDrawing failed!");
            return;
        }

        if (!sValidSurface) {
            sGLContext->RenewSurface();
            sValidSurface = true;
        }


        NS_ASSERTION(sGLContext, "Drawing with GLES without a GL context?");

        DrawTo(nsnull);

        sview.EndDrawing();
    }
}

void
nsWindow::OnSizeChanged(const gfxIntSize& aSize)
{
    int w = aSize.width;
    int h = aSize.height;

    ALOG("nsWindow: %p OnSizeChanged [%d %d]", (void*)this, w, h);

    nsSizeEvent event(PR_TRUE, NS_SIZE, this);
    InitEvent(event);

    nsIntRect wsz(0, 0, w, h);
    event.windowSize = &wsz;
    event.mWinWidth = w;
    event.mWinHeight = h;

    mBounds.width = w;
    mBounds.height = h;

    DispatchEvent(&event);
}

void
nsWindow::InitEvent(nsGUIEvent& event, nsIntPoint* aPoint)
{
    if (aPoint) {
        event.refPoint.x = aPoint->x;
        event.refPoint.y = aPoint->y;
    } else {
        event.refPoint.x = 0;
        event.refPoint.y = 0;
    }

    event.time = PR_Now() / 1000;
}

gfxIntSize
nsWindow::GetAndroidScreenBounds()
{
    if (XRE_GetProcessType() == GeckoProcessType_Content) {
        return ContentChild::GetSingleton()->GetScreenSize();
    }
    return gAndroidScreenBounds;
}

void *
nsWindow::GetNativeData(PRUint32 aDataType)
{
    switch (aDataType) {
        // used by GLContextProviderEGL, NULL is EGL_DEFAULT_DISPLAY
        case NS_NATIVE_DISPLAY:
            return NULL;

        case NS_NATIVE_WIDGET:
            return (void *) this;
    }

    return nsnull;
}

void
nsWindow::OnMotionEvent(AndroidGeckoEvent *ae)
{
    PRUint32 msg;
    switch (ae->Action() & AndroidMotionEvent::ACTION_MASK) {
        case AndroidMotionEvent::ACTION_DOWN:
            msg = NS_MOUSE_BUTTON_DOWN;
            break;

        case AndroidMotionEvent::ACTION_MOVE:
            msg = NS_MOUSE_MOVE;
            break;

        case AndroidMotionEvent::ACTION_UP:
        case AndroidMotionEvent::ACTION_CANCEL:
            msg = NS_MOUSE_BUTTON_UP;
            break;

        default:
            return;
    }

    nsIntPoint pt(ae->P0());
    nsIntPoint offset = WidgetToScreenOffset();

    //ALOG("#### motion pt: %d %d offset: %d %d", pt.x, pt.y, offset.x, offset.y);

    pt.x -= offset.x;
    pt.y -= offset.y;

    // XXX possibly bound the range of pt here. some code may get confused.

send_again:

    nsMouseEvent event(PR_TRUE,
                       msg, this,
                       nsMouseEvent::eReal, nsMouseEvent::eNormal);
    InitEvent(event, &pt);

    event.time = ae->Time();
    event.isShift = !!(ae->MetaState() & AndroidKeyEvent::META_SHIFT_ON);
    event.isControl = PR_FALSE;
    event.isMeta = PR_FALSE;
    event.isAlt = !!(ae->MetaState() & AndroidKeyEvent::META_ALT_ON);

    // XXX can we synthesize different buttons?
    event.button = nsMouseEvent::eLeftButton;

    if (msg != NS_MOUSE_MOVE)
        event.clickCount = 1;

    // XXX add the double-click handling logic here

    DispatchEvent(&event);

    if (msg == NS_MOUSE_BUTTON_DOWN) {
        msg = NS_MOUSE_MOVE;
        goto send_again;
    }
}

static double
getDistance(const nsIntPoint &p1, const nsIntPoint &p2)
{
    double deltaX = p2.x - p1.x;
    double deltaY = p2.y - p1.y;
    return sqrt(deltaX*deltaX + deltaY*deltaY);
}

void nsWindow::OnMultitouchEvent(AndroidGeckoEvent *ae)
{
    PRUint32 msg = 0;

    nsIntPoint midPoint;
    midPoint.x = ((ae->P0().x + ae->P1().x) / 2);
    midPoint.y = ((ae->P0().y + ae->P1().y) / 2);
    nsIntPoint refPoint = midPoint - WidgetToScreenOffset();

    double pinchDist = getDistance(ae->P0(), ae->P1());
    double pinchDelta = 0;

    switch (ae->Action() & AndroidMotionEvent::ACTION_MASK) {
        case AndroidMotionEvent::ACTION_POINTER_DOWN:
            msg = NS_SIMPLE_GESTURE_MAGNIFY_START;
            mStartPoint = new nsIntPoint(midPoint);
            mStartDist = mLastDist = pinchDist;
            mGestureFinished = false;
            break;
        case AndroidMotionEvent::ACTION_MOVE:
            msg = NS_SIMPLE_GESTURE_MAGNIFY_UPDATE;
            pinchDelta = pinchDist - mLastDist;
            mLastDist = pinchDist;
            break;
        case AndroidMotionEvent::ACTION_POINTER_UP:
            msg = NS_SIMPLE_GESTURE_MAGNIFY;
            pinchDelta = pinchDist - mStartDist;
            mStartPoint = nsnull;
            break;
        default:
            return;
    }

    if (!mGestureFinished) {
        DispatchGestureEvent(msg, 0, pinchDelta, refPoint, ae->Time());

        // If the cumulative pinch delta goes past the threshold, treat this
        // as a pinch only, and not a swipe.
        if (fabs(pinchDist - mStartDist) > mSwipeMaxPinchDelta)
            mStartPoint = nsnull;

        // If we have traveled more than SWIPE_MIN_DISTANCE from the start
        // point, stop the pinch gesture and fire a swipe event.
        if (mStartPoint) {
            double swipeDistance = getDistance(midPoint, *mStartPoint);
            if (swipeDistance > mSwipeMinDistance) {
                PRUint32 direction = 0;
                nsIntPoint motion = midPoint - *mStartPoint;

                if (motion.x < -swipeDistance/2)
                    direction |= nsIDOMSimpleGestureEvent::DIRECTION_LEFT;
                if (motion.x > swipeDistance/2)
                    direction |= nsIDOMSimpleGestureEvent::DIRECTION_RIGHT;
                if (motion.y < -swipeDistance/2)
                    direction |= nsIDOMSimpleGestureEvent::DIRECTION_UP;
                if (motion.y > swipeDistance/2)
                    direction |= nsIDOMSimpleGestureEvent::DIRECTION_DOWN;

                // Finish the pinch gesture, then fire the swipe event:
                msg = NS_SIMPLE_GESTURE_MAGNIFY;
                DispatchGestureEvent(msg, 0, pinchDist - mStartDist, refPoint, ae->Time());
                msg = NS_SIMPLE_GESTURE_SWIPE;
                DispatchGestureEvent(msg, direction, 0, refPoint, ae->Time());

                // Don't generate any more gesture events for this touch.
                mGestureFinished = true;
            }
        }
    }
}

void
nsWindow::DispatchGestureEvent(PRUint32 msg, PRUint32 direction, double delta,
                               const nsIntPoint &refPoint, PRUint64 time)
{
    nsSimpleGestureEvent event(PR_TRUE, msg, this, direction, delta);

    event.isShift = PR_FALSE;
    event.isControl = PR_FALSE;
    event.isMeta = PR_FALSE;
    event.isAlt = PR_FALSE;
    event.time = time;
    event.refPoint = refPoint;

    DispatchEvent(&event);
}

void
nsWindow::InitKeyEvent(nsKeyEvent& event, AndroidGeckoEvent& key)
{
    switch (key.KeyCode()) {
    case AndroidKeyEvent::KEYCODE_UNKNOWN:
    case AndroidKeyEvent::KEYCODE_HOME:
        break;
    case AndroidKeyEvent::KEYCODE_BACK:
        event.keyCode = NS_VK_ESCAPE;
        break;
    case AndroidKeyEvent::KEYCODE_CALL:
    case AndroidKeyEvent::KEYCODE_ENDCALL:
        break;
    case AndroidKeyEvent::KEYCODE_0:
    case AndroidKeyEvent::KEYCODE_1:
    case AndroidKeyEvent::KEYCODE_2:
    case AndroidKeyEvent::KEYCODE_3:
    case AndroidKeyEvent::KEYCODE_4:
    case AndroidKeyEvent::KEYCODE_5:
    case AndroidKeyEvent::KEYCODE_6:
    case AndroidKeyEvent::KEYCODE_7:
    case AndroidKeyEvent::KEYCODE_8:
    case AndroidKeyEvent::KEYCODE_9:
        event.keyCode = key.KeyCode() - AndroidKeyEvent::KEYCODE_0 + NS_VK_0;
        break;
    case AndroidKeyEvent::KEYCODE_STAR:
        event.keyCode = NS_VK_MULTIPLY;
        break;
    case AndroidKeyEvent::KEYCODE_POUND:
        break;
    case AndroidKeyEvent::KEYCODE_DPAD_UP:
        event.keyCode = NS_VK_UP;
        break;
    case AndroidKeyEvent::KEYCODE_DPAD_DOWN:
        event.keyCode = NS_VK_DOWN;
        break;
    case AndroidKeyEvent::KEYCODE_SOFT_LEFT:
    case AndroidKeyEvent::KEYCODE_DPAD_LEFT:
        event.keyCode = NS_VK_LEFT;
        break;
    case AndroidKeyEvent::KEYCODE_SOFT_RIGHT:
    case AndroidKeyEvent::KEYCODE_DPAD_RIGHT:
        event.keyCode = NS_VK_RIGHT;
        break;
    case AndroidKeyEvent::KEYCODE_VOLUME_UP:
    case AndroidKeyEvent::KEYCODE_VOLUME_DOWN:
    case AndroidKeyEvent::KEYCODE_POWER:
    case AndroidKeyEvent::KEYCODE_CAMERA:
    case AndroidKeyEvent::KEYCODE_CLEAR:
        break;
    case AndroidKeyEvent::KEYCODE_A:
    case AndroidKeyEvent::KEYCODE_B:
    case AndroidKeyEvent::KEYCODE_C:
    case AndroidKeyEvent::KEYCODE_D:
    case AndroidKeyEvent::KEYCODE_E:
    case AndroidKeyEvent::KEYCODE_F:
    case AndroidKeyEvent::KEYCODE_G:
    case AndroidKeyEvent::KEYCODE_H:
    case AndroidKeyEvent::KEYCODE_I:
    case AndroidKeyEvent::KEYCODE_J:
    case AndroidKeyEvent::KEYCODE_K:
    case AndroidKeyEvent::KEYCODE_L:
    case AndroidKeyEvent::KEYCODE_M:
    case AndroidKeyEvent::KEYCODE_N:
    case AndroidKeyEvent::KEYCODE_O:
    case AndroidKeyEvent::KEYCODE_P:
    case AndroidKeyEvent::KEYCODE_Q:
    case AndroidKeyEvent::KEYCODE_R:
    case AndroidKeyEvent::KEYCODE_S:
    case AndroidKeyEvent::KEYCODE_T:
    case AndroidKeyEvent::KEYCODE_U:
    case AndroidKeyEvent::KEYCODE_V:
    case AndroidKeyEvent::KEYCODE_W:
    case AndroidKeyEvent::KEYCODE_X:
    case AndroidKeyEvent::KEYCODE_Y:
    case AndroidKeyEvent::KEYCODE_Z:
        event.keyCode = key.KeyCode() - AndroidKeyEvent::KEYCODE_A + NS_VK_A;
        break;
    case AndroidKeyEvent::KEYCODE_COMMA:
        event.keyCode = NS_VK_COMMA;
        break;
    case AndroidKeyEvent::KEYCODE_PERIOD:
        event.keyCode = NS_VK_PERIOD;
        break;
    case AndroidKeyEvent::KEYCODE_ALT_LEFT:
    case AndroidKeyEvent::KEYCODE_ALT_RIGHT:
    case AndroidKeyEvent::KEYCODE_SHIFT_LEFT:
    case AndroidKeyEvent::KEYCODE_SHIFT_RIGHT:
        break;
    case AndroidKeyEvent::KEYCODE_TAB:
        event.keyCode = NS_VK_TAB;
        break;
    case AndroidKeyEvent::KEYCODE_SPACE:
        event.keyCode = NS_VK_SPACE;
        break;
    case AndroidKeyEvent::KEYCODE_SYM:
    case AndroidKeyEvent::KEYCODE_EXPLORER:
    case AndroidKeyEvent::KEYCODE_ENVELOPE:
        break;
    case AndroidKeyEvent::KEYCODE_DPAD_CENTER:
    case AndroidKeyEvent::KEYCODE_ENTER:
        event.keyCode = NS_VK_RETURN;
        break;
    case AndroidKeyEvent::KEYCODE_DEL:
        event.keyCode = NS_VK_BACK;
        break;
    case AndroidKeyEvent::KEYCODE_GRAVE:
        break;
    case AndroidKeyEvent::KEYCODE_MINUS:
        event.keyCode = NS_VK_SUBTRACT;
        break;
    case AndroidKeyEvent::KEYCODE_EQUALS:
        event.keyCode = NS_VK_EQUALS;
        break;
    case AndroidKeyEvent::KEYCODE_LEFT_BRACKET:
        event.keyCode = NS_VK_OPEN_BRACKET;
        break;
    case AndroidKeyEvent::KEYCODE_RIGHT_BRACKET:
        event.keyCode = NS_VK_CLOSE_BRACKET;
        break;
    case AndroidKeyEvent::KEYCODE_BACKSLASH:
        event.keyCode = NS_VK_BACK_SLASH;
        break;
    case AndroidKeyEvent::KEYCODE_SEMICOLON:
        event.keyCode = NS_VK_SEMICOLON;
        break;
    case AndroidKeyEvent::KEYCODE_APOSTROPHE:
        event.keyCode = NS_VK_QUOTE;
        break;
    case AndroidKeyEvent::KEYCODE_SLASH:
        event.keyCode = NS_VK_SLASH;
        break;
    case AndroidKeyEvent::KEYCODE_AT:
    case AndroidKeyEvent::KEYCODE_NUM:
    case AndroidKeyEvent::KEYCODE_HEADSETHOOK:
    case AndroidKeyEvent::KEYCODE_FOCUS:
        break;
    case AndroidKeyEvent::KEYCODE_PLUS:
        event.keyCode = NS_VK_ADD;
        break;
    case AndroidKeyEvent::KEYCODE_MENU:
    case AndroidKeyEvent::KEYCODE_NOTIFICATION:
    case AndroidKeyEvent::KEYCODE_SEARCH:
    case AndroidKeyEvent::KEYCODE_MEDIA_PLAY_PAUSE:
    case AndroidKeyEvent::KEYCODE_MEDIA_STOP:
    case AndroidKeyEvent::KEYCODE_MEDIA_NEXT:
    case AndroidKeyEvent::KEYCODE_MEDIA_PREVIOUS:
    case AndroidKeyEvent::KEYCODE_MEDIA_REWIND:
    case AndroidKeyEvent::KEYCODE_MEDIA_FAST_FORWARD:
    case AndroidKeyEvent::KEYCODE_MUTE:
        break;
    default:
        ALOG("Unknown key code!");
        break;
    }

    // Android gives us \n, so filter out some control characters.
    if (event.message == NS_KEY_PRESS &&
        key.UnicodeChar() >= ' ') {
        event.charCode = key.UnicodeChar();
        if (key.UnicodeChar())
            event.keyCode = 0;
    }
    event.isShift = !!(key.MetaState() & AndroidKeyEvent::META_SHIFT_ON);
    event.isControl = gMenu;
    event.isAlt = !!(key.MetaState() & AndroidKeyEvent::META_ALT_ON);
    event.isMeta = PR_FALSE;
    event.time = key.Time();

    if (gMenu)
        gMenuConsumed = PR_TRUE;
}

void
nsWindow::HandleSpecialKey(AndroidGeckoEvent *ae)
{
    nsCOMPtr<nsIAtom> command;
    PRBool isDown = ae->Action() == AndroidKeyEvent::ACTION_DOWN;
    PRBool isLongPress = !!(ae->Flags() & AndroidKeyEvent::FLAG_LONG_PRESS);
    PRBool doCommand = PR_FALSE;
    PRUint32 keyCode = ae->KeyCode();

    if (isDown) {
        switch (keyCode) {
            case AndroidKeyEvent::KEYCODE_BACK:
                if (isLongPress) {
                    command = nsWidgetAtoms::Clear;
                    doCommand = PR_TRUE;
                }
                break;
            case AndroidKeyEvent::KEYCODE_VOLUME_UP:
                command = nsWidgetAtoms::VolumeUp;
                doCommand = PR_TRUE;
                break;
            case AndroidKeyEvent::KEYCODE_VOLUME_DOWN:
                command = nsWidgetAtoms::VolumeDown;
                doCommand = PR_TRUE;
                break;
            case AndroidKeyEvent::KEYCODE_MENU:
                gMenu = PR_TRUE;
                gMenuConsumed = isLongPress;
                break;
        }
    } else {
        switch (keyCode) {
            case AndroidKeyEvent::KEYCODE_BACK: {
                nsKeyEvent pressEvent(PR_TRUE, NS_KEY_PRESS, this);
                InitKeyEvent(pressEvent, *ae);
                DispatchEvent(&pressEvent);
                return;
            }
            case AndroidKeyEvent::KEYCODE_MENU:
                gMenu = PR_FALSE;
                if (!gMenuConsumed) {
                    command = nsWidgetAtoms::Menu;
                    doCommand = PR_TRUE;
                }
                break;
            case AndroidKeyEvent::KEYCODE_SEARCH:
                command = nsWidgetAtoms::Search;
                doCommand = PR_TRUE;
                break;
            default:
                ALOG("Unknown special key code!");
                return;
        }
    }
    if (doCommand) {
        nsCommandEvent event(PR_TRUE, nsWidgetAtoms::onAppCommand, command, this);
        InitEvent(event);
        DispatchEvent(&event);
    }
}

void
nsWindow::OnKeyEvent(AndroidGeckoEvent *ae)
{
    PRUint32 msg;
    switch (ae->Action()) {
    case AndroidKeyEvent::ACTION_DOWN:
        msg = NS_KEY_DOWN;
        break;
    case AndroidKeyEvent::ACTION_UP:
        msg = NS_KEY_UP;
        break;
    case AndroidKeyEvent::ACTION_MULTIPLE:
        {
            nsTextEvent event(PR_TRUE, NS_TEXT_TEXT, this);
            event.theText.Assign(ae->Characters());
            DispatchEvent(&event);
        }
        return;
    default:
        ALOG("Unknown key action event!");
        return;
    }

    bool firePress = ae->Action() == AndroidKeyEvent::ACTION_DOWN;
    switch (ae->KeyCode()) {
    case AndroidKeyEvent::KEYCODE_SHIFT_LEFT:
    case AndroidKeyEvent::KEYCODE_SHIFT_RIGHT:
    case AndroidKeyEvent::KEYCODE_ALT_LEFT:
    case AndroidKeyEvent::KEYCODE_ALT_RIGHT:
        firePress = false;
        break;
    case AndroidKeyEvent::KEYCODE_BACK:
    case AndroidKeyEvent::KEYCODE_MENU:
    case AndroidKeyEvent::KEYCODE_SEARCH:
    case AndroidKeyEvent::KEYCODE_VOLUME_UP:
    case AndroidKeyEvent::KEYCODE_VOLUME_DOWN:
        HandleSpecialKey(ae);
        return;
    }

    nsEventStatus status;
    nsKeyEvent event(PR_TRUE, msg, this);
    InitKeyEvent(event, *ae);
    DispatchEvent(&event, status);

    if (!firePress)
        return;

    nsKeyEvent pressEvent(PR_TRUE, NS_KEY_PRESS, this);
    InitKeyEvent(pressEvent, *ae);
    if (status == nsEventStatus_eConsumeNoDefault) {
        pressEvent.flags |= NS_EVENT_FLAG_NO_DEFAULT;
    }
#ifdef ANDROID_DEBUG_WIDGET
    __android_log_print(ANDROID_LOG_INFO, "Gecko", "Dispatching key pressEvent with keyCode %d charCode %d shift %d alt %d sym/ctrl %d metamask %d", pressEvent.keyCode, pressEvent.charCode, pressEvent.isShift, pressEvent.isAlt, pressEvent.isControl, ae->MetaState());
#endif
    DispatchEvent(&pressEvent);
}

#ifdef ANDROID_DEBUG_IME
#define ALOGIME(args...) ALOG(args)
#else
#define ALOGIME(args...)
#endif

void
nsWindow::OnIMEAddRange(AndroidGeckoEvent *ae)
{
    //ALOGIME("IME: IME_ADD_RANGE");
    nsTextRange range;
    range.mStartOffset = ae->Offset();
    range.mEndOffset = range.mStartOffset + ae->Count();
    range.mRangeType = ae->RangeType();
    range.mRangeStyle.mDefinedStyles = ae->RangeStyles();
    range.mRangeStyle.mLineStyle = nsTextRangeStyle::LINESTYLE_SOLID;
    range.mRangeStyle.mForegroundColor = NS_RGBA(
        ((ae->RangeForeColor() >> 16) & 0xff),
        ((ae->RangeForeColor() >> 8) & 0xff),
        (ae->RangeForeColor() & 0xff),
        ((ae->RangeForeColor() >> 24) & 0xff));
    range.mRangeStyle.mBackgroundColor = NS_RGBA(
        ((ae->RangeBackColor() >> 16) & 0xff),
        ((ae->RangeBackColor() >> 8) & 0xff),
        (ae->RangeBackColor() & 0xff),
        ((ae->RangeBackColor() >> 24) & 0xff));
    mIMERanges.AppendElement(range);
    return;
}

void
nsWindow::OnIMEEvent(AndroidGeckoEvent *ae)
{
    switch (ae->Action()) {
    case AndroidGeckoEvent::IME_COMPOSITION_END:
        {
            ALOGIME("IME: IME_COMPOSITION_END");
            nsCompositionEvent event(PR_TRUE, NS_COMPOSITION_END, this);
            InitEvent(event, nsnull);
            DispatchEvent(&event);
        }
        return;
    case AndroidGeckoEvent::IME_COMPOSITION_BEGIN:
        {
            ALOGIME("IME: IME_COMPOSITION_BEGIN");
            nsCompositionEvent event(PR_TRUE, NS_COMPOSITION_START, this);
            InitEvent(event, nsnull);
            DispatchEvent(&event);
        }
        return;
    case AndroidGeckoEvent::IME_ADD_RANGE:
        {
            OnIMEAddRange(ae);
        }
        return;
    case AndroidGeckoEvent::IME_SET_TEXT:
        {
            OnIMEAddRange(ae);

            nsTextEvent event(PR_TRUE, NS_TEXT_TEXT, this);
            InitEvent(event, nsnull);

            event.theText.Assign(ae->Characters());
            event.rangeArray = mIMERanges.Elements();
            event.rangeCount = mIMERanges.Length();

            ALOGIME("IME: IME_SET_TEXT: l=%u, r=%u",
                event.theText.Length(), mIMERanges.Length());

            DispatchEvent(&event);
            mIMERanges.Clear();
        }
        return;
    case AndroidGeckoEvent::IME_GET_TEXT:
        {
            ALOGIME("IME: IME_GET_TEXT: o=%u, l=%u", ae->Offset(), ae->Count());

            nsQueryContentEvent event(PR_TRUE, NS_QUERY_TEXT_CONTENT, this);
            InitEvent(event, nsnull);

            event.InitForQueryTextContent(ae->Offset(), ae->Count());
            
            DispatchEvent(&event);

            if (!event.mSucceeded) {
                ALOGIME("IME:     -> failed");
                AndroidBridge::Bridge()->ReturnIMEQueryResult(
                    nsnull, 0, 0, 0);
                return;
            } else if (!event.mWasAsync) {
                AndroidBridge::Bridge()->ReturnIMEQueryResult(
                    event.mReply.mString.get(), 
                    event.mReply.mString.Length(), 0, 0);
            }
            //ALOGIME("IME:     -> l=%u", event.mReply.mString.Length());
        }
        return;
    case AndroidGeckoEvent::IME_DELETE_TEXT:
        {   
            ALOGIME("IME: IME_DELETE_TEXT");
            nsKeyEvent event(PR_TRUE, NS_KEY_PRESS, this);
            InitEvent(event, nsnull);
            event.keyCode = NS_VK_BACK;
            DispatchEvent(&event);
        }
        return;
    case AndroidGeckoEvent::IME_SET_SELECTION:
        {
            ALOGIME("IME: IME_SET_SELECTION: o=%u, l=%d", ae->Offset(), ae->Count());

            nsSelectionEvent selEvent(PR_TRUE, NS_SELECTION_SET, this);
            InitEvent(selEvent, nsnull);

            selEvent.mOffset = PRUint32(ae->Count() >= 0 ?
                                        ae->Offset() :
                                        ae->Offset() + ae->Count());
            selEvent.mLength = PRUint32(NS_ABS(ae->Count()));
            selEvent.mReversed = ae->Count() >= 0 ? PR_FALSE : PR_TRUE;

            DispatchEvent(&selEvent);
        }
        return;
    case AndroidGeckoEvent::IME_GET_SELECTION:
        {
            ALOGIME("IME: IME_GET_SELECTION");

            nsQueryContentEvent event(PR_TRUE, NS_QUERY_SELECTED_TEXT, this);
            InitEvent(event, nsnull);
            DispatchEvent(&event);

            if (!event.mSucceeded) {
                ALOGIME("IME:     -> failed");
                AndroidBridge::Bridge()->ReturnIMEQueryResult(
                    nsnull, 0, 0, 0);
                return;
            } else if (!event.mWasAsync) {
                AndroidBridge::Bridge()->ReturnIMEQueryResult(
                    event.mReply.mString.get(),
                    event.mReply.mString.Length(), 
                    event.GetSelectionStart(),
                    event.GetSelectionEnd() - event.GetSelectionStart());
            }
            //ALOGIME("IME:     -> o=%u, l=%u", event.mReply.mOffset, event.mReply.mString.Length());
        }
        return;
    }
}

nsWindow *
nsWindow::FindWindowForPoint(const nsIntPoint& pt)
{
    if (!mBounds.Contains(pt))
        return nsnull;

    // children mBounds are relative to their parent
    nsIntPoint childPoint(pt.x - mBounds.x, pt.y - mBounds.y);

    for (PRUint32 i = 0; i < mChildren.Length(); ++i) {
        if (mChildren[i]->mBounds.Contains(childPoint))
            return mChildren[i]->FindWindowForPoint(childPoint);
    }

    return this;
}

void
nsWindow::UserActivity()
{
  if (!mIdleService) {
    mIdleService = do_GetService("@mozilla.org/widget/idleservice;1");
  }

  if (mIdleService) {
    mIdleService->ResetIdleTimeOut();
  }
}

NS_IMETHODIMP
nsWindow::ResetInputState()
{
    //ALOGIME("IME: ResetInputState: s=%d", aState);

    // Cancel composition on Gecko side
    if (mIMEComposing) {
        nsTextEvent textEvent(PR_TRUE, NS_TEXT_TEXT, this);
        InitEvent(textEvent, nsnull);
        textEvent.theText = mIMEComposingText;
        DispatchEvent(&textEvent);
        mIMEComposingText.Truncate(0);

        nsCompositionEvent event(PR_TRUE, NS_COMPOSITION_END, this);
        InitEvent(event, nsnull);
        DispatchEvent(&event);
    }

    AndroidBridge::NotifyIME(AndroidBridge::NOTIFY_IME_RESETINPUTSTATE, 0);
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetInputMode(const IMEContext& aContext)
{
    ALOGIME("IME: SetInputMode: s=%d trusted=%d", aContext.mStatus, aContext.mReason);

    mIMEContext = aContext;

    // Ensure that opening the virtual keyboard is allowed for this specific
    // IMEContext depending on the content.ime.strict.policy pref
    if (aContext.mStatus != nsIWidget::IME_STATUS_DISABLED && 
        aContext.mStatus != nsIWidget::IME_STATUS_PLUGIN) {
      if (Preferences::GetBool("content.ime.strict_policy", PR_FALSE) &&
          !aContext.FocusMovedByUser() &&
          aContext.FocusMovedInContentProcess()) {
        return NS_OK;
      }
    }

    AndroidBridge::NotifyIMEEnabled(int(aContext.mStatus), aContext.mHTMLInputType, aContext.mActionHint);
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::GetInputMode(IMEContext& aContext)
{
    aContext = mIMEContext;
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::CancelIMEComposition()
{
    ALOGIME("IME: CancelIMEComposition");

    // Cancel composition on Gecko side
    if (mIMEComposing) {
        nsTextEvent textEvent(PR_TRUE, NS_TEXT_TEXT, this);
        InitEvent(textEvent, nsnull);
        DispatchEvent(&textEvent);
        mIMEComposingText.Truncate(0);

        nsCompositionEvent compEvent(PR_TRUE, NS_COMPOSITION_END, this);
        InitEvent(compEvent, nsnull);
        DispatchEvent(&compEvent);
    }

    AndroidBridge::NotifyIME(AndroidBridge::NOTIFY_IME_CANCELCOMPOSITION, 0);
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::OnIMEFocusChange(PRBool aFocus)
{
    ALOGIME("IME: OnIMEFocusChange: f=%d", aFocus);

    AndroidBridge::NotifyIME(AndroidBridge::NOTIFY_IME_FOCUSCHANGE, 
                             int(aFocus));

    if (aFocus) {
        OnIMETextChange(0, 0, 0);
        OnIMESelectionChange();
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::OnIMETextChange(PRUint32 aStart, PRUint32 aOldEnd, PRUint32 aNewEnd)
{
    ALOGIME("IME: OnIMETextChange: s=%d, oe=%d, ne=%d",
            aStart, aOldEnd, aNewEnd);

    // A quirk in Android makes it necessary to pass the whole text.
    // The more efficient way would have been passing the substring from index
    // aStart to index aNewEnd

    nsQueryContentEvent event(PR_TRUE, NS_QUERY_TEXT_CONTENT, this);
    InitEvent(event, nsnull);
    event.InitForQueryTextContent(0, PR_UINT32_MAX);

    DispatchEvent(&event);
    if (!event.mSucceeded)
        return NS_OK;

    AndroidBridge::NotifyIMEChange(event.mReply.mString.get(),
                                   event.mReply.mString.Length(),
                                   aStart, aOldEnd, aNewEnd);

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::OnIMESelectionChange(void)
{
    ALOGIME("IME: OnIMESelectionChange");

    nsQueryContentEvent event(PR_TRUE, NS_QUERY_SELECTED_TEXT, this);
    InitEvent(event, nsnull);

    DispatchEvent(&event);
    if (!event.mSucceeded)
        return NS_OK;

    AndroidBridge::NotifyIMEChange(nsnull, 0, int(event.mReply.mOffset),
                                   int(event.mReply.mOffset + 
                                       event.mReply.mString.Length()), -1);
    return NS_OK;
}

nsIMEUpdatePreference
nsWindow::GetIMEUpdatePreference()
{
    return nsIMEUpdatePreference(PR_TRUE, PR_TRUE);
}


/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
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
 * The Initial Developer of the Original Code is Christopher Blizzard
 * <blizzard@mozilla.org>.  Portions created by the Initial Developer
 * are Copyright (C) 2001 the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mats Palmgren <matspal@gmail.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *   Martin Stransky <stransky@redhat.com>
 *   Jan Horak <jhorak@redhat.com>
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

#ifdef MOZ_PLATFORM_MAEMO
// needed to include hildon parts in gtk.h
#define MAEMO_CHANGES
#endif

#include "prlink.h"
#include "nsWindow.h"
#include "nsGTKToolkit.h"
#include "nsIRollupListener.h"
#include "nsIMenuRollup.h"
#include "nsIDOMNode.h"

#include "nsWidgetsCID.h"
#include "nsDragService.h"
#include "nsIDragSessionGTK.h"

#include "nsGtkKeyUtils.h"
#include "nsGtkCursors.h"

#include <gtk/gtk.h>
#if defined(MOZ_WIDGET_GTK3)
#include <gtk/gtkx.h>
#endif
#ifdef MOZ_X11
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/shape.h>
#if defined(MOZ_WIDGET_GTK3)
#include <gdk/gdkkeysyms-compat.h>
#endif

#ifdef AIX
#include <X11/keysym.h>
#else
#include <X11/XF86keysym.h>
#endif

#include "gtk2xtbin.h"
#endif /* MOZ_X11 */
#include <gdk/gdkkeysyms.h>
#if defined(MOZ_WIDGET_GTK2)
#include <gtk/gtkprivate.h>
#endif

#if defined(MOZ_WIDGET_GTK2)
#include "gtk2compat.h"
#endif

#include "nsWidgetAtoms.h"

#ifdef MOZ_ENABLE_STARTUP_NOTIFICATION
#define SN_API_NOT_YET_FROZEN
#include <startup-notification-1.0/libsn/sn.h>
#endif

#include "mozilla/Preferences.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsIServiceManager.h"
#include "nsIStringBundle.h"
#include "nsGfxCIID.h"
#include "nsIObserverService.h"

#include "nsIdleService.h"
#include "nsIPropertyBag2.h"

#ifdef ACCESSIBILITY
#include "nsIAccessibilityService.h"
#include "nsIAccessibleDocument.h"
#include "prenv.h"
#include "stdlib.h"

using namespace mozilla;

static PRBool sAccessibilityChecked = PR_FALSE;
/* static */
PRBool nsWindow::sAccessibilityEnabled = PR_FALSE;
static const char sSysPrefService [] = "@mozilla.org/system-preference-service;1";
static const char sAccEnv [] = "GNOME_ACCESSIBILITY";
static const char sAccessibilityKey [] = "config.use_system_prefs.accessibility";
#endif

/* For SetIcon */
#include "nsAppDirectoryServiceDefs.h"
#include "nsXPIDLString.h"
#include "nsIFile.h"
#include "nsILocalFile.h"

/* SetCursor(imgIContainer*) */
#include <gdk/gdk.h>
#include <wchar.h>
#include "imgIContainer.h"
#include "nsGfxCIID.h"
#include "nsImageToPixbuf.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsAutoPtr.h"

extern "C" {
#include "pixman.h"
}
#include "gfxPlatformGtk.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "gfxUtils.h"
#include "Layers.h"
#include "LayerManagerOGL.h"
#include "GLContextProvider.h"

#ifdef MOZ_X11
#include "gfxXlibSurface.h"
#include "cairo-xlib.h"
#endif

#include "nsShmImage.h"

#ifdef MOZ_DFB
extern "C" {
#ifdef MOZ_DIRECT_DEBUG
#define DIRECT_ENABLE_DEBUG
#endif

#include <direct/debug.h>

D_DEBUG_DOMAIN( ns_Window, "nsWindow", "nsWindow" );
}
#include "gfxDirectFBSurface.h"
#define GDK_WINDOW_XWINDOW(_win) _win
#endif

using namespace mozilla;
using mozilla::gl::GLContext;
using mozilla::layers::LayerManagerOGL;

// Don't put more than this many rects in the dirty region, just fluff
// out to the bounding-box if there are more
#define MAX_RECTS_IN_REGION 100

/* utility functions */
static PRBool     check_for_rollup(GdkWindow *aWindow,
                                   gdouble aMouseX, gdouble aMouseY,
                                   PRBool aIsWheel, PRBool aAlwaysRollup);
static PRBool     is_mouse_in_window(GdkWindow* aWindow,
                                     gdouble aMouseX, gdouble aMouseY);
static nsWindow  *get_window_for_gtk_widget(GtkWidget *widget);
static nsWindow  *get_window_for_gdk_window(GdkWindow *window);
static GtkWidget *get_gtk_widget_for_gdk_window(GdkWindow *window);
static GdkCursor *get_gtk_cursor(nsCursor aCursor);

static GdkWindow *get_inner_gdk_window (GdkWindow *aWindow,
                                        gint x, gint y,
                                        gint *retx, gint *rety);

static inline PRBool is_context_menu_key(const nsKeyEvent& inKeyEvent);
static void   key_event_to_context_menu_event(nsMouseEvent &aEvent,
                                              GdkEventKey *aGdkEvent);

static int    is_parent_ungrab_enter(GdkEventCrossing *aEvent);
static int    is_parent_grab_leave(GdkEventCrossing *aEvent);

/* callbacks from widgets */
#if defined(MOZ_WIDGET_GTK2)
static gboolean expose_event_cb           (GtkWidget *widget,
                                           GdkEventExpose *event);
#else
static gboolean expose_event_cb           (GtkWidget *widget,
                                           cairo_t *rect);
#endif
static gboolean configure_event_cb        (GtkWidget *widget,
                                           GdkEventConfigure *event);
static void     container_unrealize_cb    (GtkWidget *widget);
static void     size_allocate_cb          (GtkWidget *widget,
                                           GtkAllocation *allocation);
static gboolean delete_event_cb           (GtkWidget *widget,
                                           GdkEventAny *event);
static gboolean enter_notify_event_cb     (GtkWidget *widget,
                                           GdkEventCrossing *event);
static gboolean leave_notify_event_cb     (GtkWidget *widget,
                                           GdkEventCrossing *event);
static gboolean motion_notify_event_cb    (GtkWidget *widget,
                                           GdkEventMotion *event);
static gboolean button_press_event_cb     (GtkWidget *widget,
                                           GdkEventButton *event);
static gboolean button_release_event_cb   (GtkWidget *widget,
                                           GdkEventButton *event);
static gboolean focus_in_event_cb         (GtkWidget *widget,
                                           GdkEventFocus *event);
static gboolean focus_out_event_cb        (GtkWidget *widget,
                                           GdkEventFocus *event);
static gboolean key_press_event_cb        (GtkWidget *widget,
                                           GdkEventKey *event);
static gboolean key_release_event_cb      (GtkWidget *widget,
                                           GdkEventKey *event);
static gboolean scroll_event_cb           (GtkWidget *widget,
                                           GdkEventScroll *event);
static gboolean visibility_notify_event_cb(GtkWidget *widget,
                                           GdkEventVisibility *event);
static void     hierarchy_changed_cb      (GtkWidget *widget,
                                           GtkWidget *previous_toplevel);
static gboolean window_state_event_cb     (GtkWidget *widget,
                                           GdkEventWindowState *event);
static void     theme_changed_cb          (GtkSettings *settings,
                                           GParamSpec *pspec,
                                           nsWindow *data);
static nsWindow* GetFirstNSWindowForGDKWindow (GdkWindow *aGdkWindow);

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#ifdef MOZ_X11
static GdkFilterReturn popup_take_focus_filter (GdkXEvent *gdk_xevent,
                                                GdkEvent *event,
                                                gpointer data);
static GdkFilterReturn plugin_window_filter_func (GdkXEvent *gdk_xevent,
                                                  GdkEvent *event,
                                                  gpointer data);
static GdkFilterReturn plugin_client_message_filter (GdkXEvent *xevent,
                                                     GdkEvent *event,
                                                     gpointer data);
#endif /* MOZ_X11 */
#ifdef __cplusplus
}
#endif /* __cplusplus */

static gboolean drag_motion_event_cb      (GtkWidget *aWidget,
                                           GdkDragContext *aDragContext,
                                           gint aX,
                                           gint aY,
                                           guint aTime,
                                           gpointer aData);
static void     drag_leave_event_cb       (GtkWidget *aWidget,
                                           GdkDragContext *aDragContext,
                                           guint aTime,
                                           gpointer aData);
static gboolean drag_drop_event_cb        (GtkWidget *aWidget,
                                           GdkDragContext *aDragContext,
                                           gint aX,
                                           gint aY,
                                           guint aTime,
                                           gpointer aData);
static void    drag_data_received_event_cb(GtkWidget *aWidget,
                                           GdkDragContext *aDragContext,
                                           gint aX,
                                           gint aY,
                                           GtkSelectionData  *aSelectionData,
                                           guint aInfo,
                                           guint32 aTime,
                                           gpointer aData);

static GdkModifierType gdk_keyboard_get_modifiers();
#ifdef MOZ_X11
static PRBool gdk_keyboard_get_modmap_masks(Display*  aDisplay,
                                            PRUint32* aCapsLockMask,
                                            PRUint32* aNumLockMask,
                                            PRUint32* aScrollLockMask);
#endif /* MOZ_X11 */

/* initialization static functions */
static nsresult    initialize_prefs        (void);

static void
UpdateLastInputEventTime()
{
  nsCOMPtr<nsIdleService> idleService = do_GetService("@mozilla.org/widget/idleservice;1");
  if (idleService) {
    idleService->ResetIdleTimeOut();
  }
}

// this is the last window that had a drag event happen on it.
nsWindow *nsWindow::sLastDragMotionWindow = NULL;
PRBool nsWindow::sIsDraggingOutOf = PR_FALSE;

// This is the time of the last button press event.  The drag service
// uses it as the time to start drags.
guint32   nsWindow::sLastButtonPressTime = 0;
// Time of the last button release event. We use it to detect when the
// drag ended before we could properly setup drag and drop.
guint32   nsWindow::sLastButtonReleaseTime = 0;

static NS_DEFINE_IID(kCDragServiceCID,  NS_DRAGSERVICE_CID);

// The window from which the focus manager asks us to dispatch key events.
static nsWindow         *gFocusWindow          = NULL;
static PRBool            gBlockActivateEvent   = PR_FALSE;
static PRBool            gGlobalsInitialized   = PR_FALSE;
static PRBool            gRaiseWindows         = PR_TRUE;
static nsWindow         *gPluginFocusWindow    = NULL;

static nsIRollupListener*          gRollupListener;
static nsIMenuRollup*              gMenuRollup;
static nsWeakPtr                   gRollupWindow;
static PRBool                      gConsumeRollupEvent;


#define NS_WINDOW_TITLE_MAX_LENGTH 4095

// If after selecting profile window, the startup fail, please refer to
// http://bugzilla.gnome.org/show_bug.cgi?id=88940

// needed for imgIContainer cursors
// GdkDisplay* was added in 2.2
typedef struct _GdkDisplay GdkDisplay;

#define kWindowPositionSlop 20

// cursor cache
static GdkCursor *gCursorCache[eCursorCount];

// imported in nsWidgetFactory.cpp
PRBool gDisableNativeTheme = PR_FALSE;

static GtkWidget *gInvisibleContainer = NULL;

// Sometimes this actually also includes the state of the modifier keys, but
// only the button state bits are used.
static guint gButtonState;

// Some gobject functions expect functions for gpointer arguments.
// gpointer is void* but C++ doesn't like casting functions to void*.
template<class T> static inline gpointer
FuncToGpointer(T aFunction)
{
    return reinterpret_cast<gpointer>
        (reinterpret_cast<uintptr_t>
         // This cast just provides a warning if T is not a function.
         (reinterpret_cast<void (*)()>(aFunction)));
}

// nsAutoRef<pixman_region32> uses nsSimpleRef<> to know how to automatically
// destroy regions.
template <>
class nsSimpleRef<pixman_region32> : public pixman_region32 {
protected:
    typedef pixman_region32 RawRef;

    nsSimpleRef() { data = nsnull; }
    nsSimpleRef(const RawRef &aRawRef) : pixman_region32(aRawRef) { }

    static void Release(pixman_region32& region) {
        pixman_region32_fini(&region);
    }
    // Whether this needs to be released:
    PRBool HaveResource() const { return data != nsnull; }

    pixman_region32& get() { return *this; }
};

static inline PRInt32
GetBitmapStride(PRInt32 width)
{
#if defined(MOZ_X11) || defined(MOZ_WIDGET_GTK2)
  return (width+7)/8;
#else
  return cairo_format_stride_for_width(CAIRO_FORMAT_A1, width);
#endif
}

nsWindow::nsWindow()
{
    mIsTopLevel       = PR_FALSE;
    mIsDestroyed      = PR_FALSE;
    mNeedsResize      = PR_FALSE;
    mNeedsMove        = PR_FALSE;
    mListenForResizes = PR_FALSE;
    mIsShown          = PR_FALSE;
    mNeedsShow        = PR_FALSE;
    mEnabled          = PR_TRUE;
    mCreated          = PR_FALSE;

    mContainer           = nsnull;
    mGdkWindow           = nsnull;
    mShell               = nsnull;
    mWindowGroup         = nsnull;
    mHasMappedToplevel   = PR_FALSE;
    mIsFullyObscured     = PR_FALSE;
    mRetryPointerGrab    = PR_FALSE;
    mTransientParent     = nsnull;
    mWindowType          = eWindowType_child;
    mSizeState           = nsSizeMode_Normal;
    mLastSizeMode        = nsSizeMode_Normal;

#ifdef MOZ_X11
    mOldFocusWindow      = 0;
#endif /* MOZ_X11 */
    mPluginType          = PluginType_NONE;

    if (!gGlobalsInitialized) {
        gGlobalsInitialized = PR_TRUE;

        // It's OK if either of these fail, but it may not be one day.
        initialize_prefs();
    }

    mLastMotionPressure = 0;

#ifdef ACCESSIBILITY
    mRootAccessible  = nsnull;
#endif

    mIsTransparent = PR_FALSE;
    mTransparencyBitmap = nsnull;

    mTransparencyBitmapWidth  = 0;
    mTransparencyBitmapHeight = 0;

#ifdef MOZ_DFB
    mDFBCursorX     = 0;
    mDFBCursorY     = 0;

    mDFBCursorCount = 0;

    mDFB            = NULL;
    mDFBLayer       = NULL;
#endif
}

nsWindow::~nsWindow()
{
    LOG(("nsWindow::~nsWindow() [%p]\n", (void *)this));
    if (sLastDragMotionWindow == this) {
        sLastDragMotionWindow = NULL;
    }

    delete[] mTransparencyBitmap;
    mTransparencyBitmap = nsnull;

#ifdef MOZ_DFB
    if (mDFBLayer)
         mDFBLayer->Release( mDFBLayer );

    if (mDFB)
         mDFB->Release( mDFB );
#endif

    Destroy();
}

/* static */ void
nsWindow::ReleaseGlobals()
{
  for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(gCursorCache); ++i) {
    if (gCursorCache[i]) {
      gdk_cursor_unref(gCursorCache[i]);
      gCursorCache[i] = nsnull;
    }
  }
}

NS_IMPL_ISUPPORTS_INHERITED1(nsWindow, nsBaseWidget,
                             nsISupportsWeakReference)

void
nsWindow::CommonCreate(nsIWidget *aParent, PRBool aListenForResizes)
{
    mParent = aParent;
    mListenForResizes = aListenForResizes;
    mCreated = PR_TRUE;
}

void
nsWindow::InitKeyEvent(nsKeyEvent &aEvent, GdkEventKey *aGdkEvent)
{
    aEvent.keyCode   = GdkKeyCodeToDOMKeyCode(aGdkEvent->keyval);
    // NOTE: The state of given key event indicates adjacent state of
    // modifier keys.  E.g., even if the event is Shift key press event,
    // the bit for Shift is still false.  By the same token, even if the
    // event is Shift key release event, the bit for Shift is still true.
    // Unfortunately, gdk_keyboard_get_modifiers() returns current modifier
    // state.  It means if there're some pending modifier key press or
    // key release events, the result isn't what we want.
    // Temporarily, we should compute the state only when the key event
    // is GDK_KEY_PRESS.
    guint modifierState = aGdkEvent->state;
    guint changingMask = 0;
    switch (aEvent.keyCode) {
        case NS_VK_SHIFT:
            changingMask = GDK_SHIFT_MASK;
            break;
        case NS_VK_CONTROL:
            changingMask = GDK_CONTROL_MASK;
            break;
        case NS_VK_ALT:
            changingMask = GDK_MOD1_MASK;
            break;
        case NS_VK_META:
            changingMask = GDK_MOD4_MASK;
            break;
    }
    if (changingMask != 0) {
        // This key event is caused by pressing or releasing a modifier key.
        if (aGdkEvent->type == GDK_KEY_PRESS) {
            // If new modifier key is pressed, add the pressed mod mask.
            modifierState |= changingMask;
        } else {
            // XXX If we could know the modifier keys state at the key release
            // event, we should cut out changingMask from modifierState.
        }
    }
    aEvent.isShift   = (modifierState & GDK_SHIFT_MASK) != 0;
    aEvent.isControl = (modifierState & GDK_CONTROL_MASK) != 0;
    aEvent.isAlt     = (modifierState & GDK_MOD1_MASK) != 0;
    aEvent.isMeta    = (modifierState & GDK_MOD4_MASK) != 0;

    // The transformations above and in gdk for the keyval are not invertible
    // so link to the GdkEvent (which will vanish soon after return from the
    // event callback) to give plugins access to hardware_keycode and state.
    // (An XEvent would be nice but the GdkEvent is good enough.)
    aEvent.pluginEvent = (void *)aGdkEvent;

    aEvent.time      = aGdkEvent->time;
}

void
nsWindow::DispatchResizeEvent(nsIntRect &aRect, nsEventStatus &aStatus)
{
    nsSizeEvent event(PR_TRUE, NS_SIZE, this);

    event.windowSize = &aRect;
    event.refPoint.x = aRect.x;
    event.refPoint.y = aRect.y;
    event.mWinWidth = aRect.width;
    event.mWinHeight = aRect.height;

    nsEventStatus status;
    DispatchEvent(&event, status);
}

void
nsWindow::DispatchActivateEvent(void)
{
    NS_ASSERTION(mContainer || mIsDestroyed,
                 "DispatchActivateEvent only intended for container windows");

#ifdef ACCESSIBILITY
    DispatchActivateEventAccessible();
#endif //ACCESSIBILITY
    nsGUIEvent event(PR_TRUE, NS_ACTIVATE, this);
    nsEventStatus status;
    DispatchEvent(&event, status);
}

void
nsWindow::DispatchDeactivateEvent(void)
{
    nsGUIEvent event(PR_TRUE, NS_DEACTIVATE, this);
    nsEventStatus status;
    DispatchEvent(&event, status);

#ifdef ACCESSIBILITY
    DispatchDeactivateEventAccessible();
#endif //ACCESSIBILITY
}



nsresult
nsWindow::DispatchEvent(nsGUIEvent *aEvent, nsEventStatus &aStatus)
{
#ifdef DEBUG
    debug_DumpEvent(stdout, aEvent->widget, aEvent,
                    nsCAutoString("something"), 0);
#endif

    aStatus = nsEventStatus_eIgnore;

    // send it to the standard callback
    if (mEventCallback)
        aStatus = (* mEventCallback)(aEvent);

    return NS_OK;
}

void
nsWindow::OnDestroy(void)
{
    if (mOnDestroyCalled)
        return;

    mOnDestroyCalled = PR_TRUE;
    
    // Prevent deletion.
    nsCOMPtr<nsIWidget> kungFuDeathGrip = this;

    // release references to children, device context, toolkit + app shell
    nsBaseWidget::OnDestroy(); 
    
    // Remove association between this object and its parent and siblings.
    nsBaseWidget::Destroy();
    mParent = nsnull;

    nsGUIEvent event(PR_TRUE, NS_DESTROY, this);
    nsEventStatus status;
    DispatchEvent(&event, status);
}

PRBool
nsWindow::AreBoundsSane(void)
{
    if (mBounds.width > 0 && mBounds.height > 0)
        return PR_TRUE;

    return PR_FALSE;
}

static GtkWidget*
EnsureInvisibleContainer()
{
    if (!gInvisibleContainer) {
        // GtkWidgets need to be anchored to a GtkWindow to be realized (to
        // have a window).  Using GTK_WINDOW_POPUP rather than
        // GTK_WINDOW_TOPLEVEL in the hope that POPUP results in less
        // initialization and window manager interaction.
        GtkWidget* window = gtk_window_new(GTK_WINDOW_POPUP);
        gInvisibleContainer = moz_container_new();
        gtk_container_add(GTK_CONTAINER(window), gInvisibleContainer);
        gtk_widget_realize(gInvisibleContainer);

    }
    return gInvisibleContainer;
}

static void
CheckDestroyInvisibleContainer()
{
    NS_PRECONDITION(gInvisibleContainer, "oh, no");

    if (!gdk_window_peek_children(gtk_widget_get_window(gInvisibleContainer))) {
        // No children, so not in use.
        // Make sure to destroy the GtkWindow also.
        gtk_widget_destroy(gtk_widget_get_parent(gInvisibleContainer));
        gInvisibleContainer = NULL;
    }
}

// Change the containing GtkWidget on a sub-hierarchy of GdkWindows belonging
// to aOldWidget and rooted at aWindow, and reparent any child GtkWidgets of
// the GdkWindow hierarchy to aNewWidget.
static void
SetWidgetForHierarchy(GdkWindow *aWindow,
                      GtkWidget *aOldWidget,
                      GtkWidget *aNewWidget)
{
    gpointer data;
    gdk_window_get_user_data(aWindow, &data);

    if (data != aOldWidget) {
        if (!GTK_IS_WIDGET(data))
            return;

        GtkWidget* widget = static_cast<GtkWidget*>(data);
        if (gtk_widget_get_parent(widget) != aOldWidget)
            return;

        // This window belongs to a child widget, which will no longer be a
        // child of aOldWidget.
        gtk_widget_reparent(widget, aNewWidget);

        return;
    }

    GList *children = gdk_window_get_children(aWindow);
    for(GList *list = children; list; list = list->next) {
        SetWidgetForHierarchy(GDK_WINDOW(list->data), aOldWidget, aNewWidget);
    }
    g_list_free(children);

    gdk_window_set_user_data(aWindow, aNewWidget);
}

// Walk the list of child windows and call destroy on them.
void
nsWindow::DestroyChildWindows()
{
    if (!mGdkWindow)
        return;

    while (GList *children = gdk_window_peek_children(mGdkWindow)) {
        GdkWindow *child = GDK_WINDOW(children->data);
        nsWindow *kid = get_window_for_gdk_window(child);
        if (kid) {
            kid->Destroy();
        } else {
            // This child is not an nsWindow.
            // Destroy the child GtkWidget.
            gpointer data;
            gdk_window_get_user_data(child, &data);
            if (GTK_IS_WIDGET(data)) {
                gtk_widget_destroy(static_cast<GtkWidget*>(data));
            }
        }
    }
}

NS_IMETHODIMP
nsWindow::Destroy(void)
{
    if (mIsDestroyed || !mCreated)
        return NS_OK;

    LOG(("nsWindow::Destroy [%p]\n", (void *)this));
    mIsDestroyed = PR_TRUE;
    mCreated = PR_FALSE;

    /** Need to clean our LayerManager up while still alive */
    if (mLayerManager) {
        nsRefPtr<GLContext> gl = nsnull;
        if (mLayerManager->GetBackendType() == LayerManager::LAYERS_OPENGL) {
            LayerManagerOGL *ogllm = static_cast<LayerManagerOGL*>(mLayerManager.get());
            gl = ogllm->gl();
        }

        mLayerManager->Destroy();

        if (gl) {
            gl->MarkDestroyed();
        }
    }
    mLayerManager = nsnull;

    ClearCachedResources();

    g_signal_handlers_disconnect_by_func(gtk_settings_get_default(),
                                         FuncToGpointer(theme_changed_cb),
                                         this);

    // ungrab if required
    nsCOMPtr<nsIWidget> rollupWidget = do_QueryReferent(gRollupWindow);
    if (static_cast<nsIWidget *>(this) == rollupWidget.get()) {
        if (gRollupListener)
            gRollupListener->Rollup(nsnull, nsnull);
        NS_IF_RELEASE(gMenuRollup);
        gRollupWindow = nsnull;
        gRollupListener = nsnull;
    }

    NativeShow(PR_FALSE);

    if (mIMModule) {
        mIMModule->OnDestroyWindow(this);
    }

    // make sure that we remove ourself as the focus window
    if (gFocusWindow == this) {
        LOGFOCUS(("automatically losing focus...\n"));
        gFocusWindow = nsnull;
    }

#if defined(MOZ_WIDGET_GTK2) && defined(MOZ_X11)
    // make sure that we remove ourself as the plugin focus window
    if (gPluginFocusWindow == this) {
        gPluginFocusWindow->LoseNonXEmbedPluginFocus();
    }
#endif /* MOZ_X11 && MOZ_WIDGET_GTK2 */
  
    if (mWindowGroup) {
        g_object_unref(mWindowGroup);
        mWindowGroup = nsnull;
    }

    // Destroy thebes surface now. Badness can happen if we destroy
    // the surface after its X Window.
    mThebesSurface = nsnull;

    if (mDragLeaveTimer) {
        mDragLeaveTimer->Cancel();
        mDragLeaveTimer = nsnull;
    }

    GtkWidget *owningWidget = GetMozContainerWidget();
    if (mShell) {
        gtk_widget_destroy(mShell);
        mShell = nsnull;
        mContainer = nsnull;
        NS_ABORT_IF_FALSE(!mGdkWindow,
                          "mGdkWindow should be NULL when mContainer is destroyed");
    }
    else if (mContainer) {
        gtk_widget_destroy(GTK_WIDGET(mContainer));
        mContainer = nsnull;
        NS_ABORT_IF_FALSE(!mGdkWindow,
                          "mGdkWindow should be NULL when mContainer is destroyed");
    }
    else if (mGdkWindow) {
        // Destroy child windows to ensure that their mThebesSurfaces are
        // released and to remove references from GdkWindows back to their
        // container widget.  (OnContainerUnrealize() does this when the
        // MozContainer widget is destroyed.)
        DestroyChildWindows();

        gdk_window_set_user_data(mGdkWindow, NULL);
        g_object_set_data(G_OBJECT(mGdkWindow), "nsWindow", NULL);
        gdk_window_destroy(mGdkWindow);
        mGdkWindow = nsnull;
    }

    if (gInvisibleContainer && owningWidget == gInvisibleContainer) {
        CheckDestroyInvisibleContainer();
    }

#ifdef ACCESSIBILITY
     if (mRootAccessible) {
         mRootAccessible = nsnull;
     }
#endif

    // Save until last because OnDestroy() may cause us to be deleted.
    OnDestroy();

    return NS_OK;
}

nsIWidget *
nsWindow::GetParent(void)
{
    return mParent;
}

float
nsWindow::GetDPI()
{

#ifdef MOZ_PLATFORM_MAEMO
    static float sDPI = 0;

    if (!sDPI) {
        // X on Maemo does not report true DPI: https://bugs.maemo.org/show_bug.cgi?id=4825
        nsCOMPtr<nsIPropertyBag2> infoService = do_GetService("@mozilla.org/system-info;1");
        NS_ASSERTION(infoService, "Could not find a system info service");

        nsCString deviceType;
        infoService->GetPropertyAsACString(NS_LITERAL_STRING("device"), deviceType);
        if (deviceType.EqualsLiteral("Nokia N900")) {
            sDPI = 265.0f;
        } else if (deviceType.EqualsLiteral("Nokia N8xx")) {
            sDPI = 225.0f;
        } else {
            // Fall back to something sane.
            NS_WARNING("Unknown device - using default DPI");
            sDPI = 96.0f;
        }
    }
    return sDPI;
#else
    Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    int defaultScreen = DefaultScreen(dpy);
    double heightInches = DisplayHeightMM(dpy, defaultScreen)/MM_PER_INCH_FLOAT;
    if (heightInches < 0.25) {
        // Something's broken, but we'd better not crash.
        return 96.0f;
    }
    return float(DisplayHeight(dpy, defaultScreen)/heightInches);
#endif
}

NS_IMETHODIMP
nsWindow::SetParent(nsIWidget *aNewParent)
{
    if (mContainer || !mGdkWindow || !mParent) {
        NS_NOTREACHED("nsWindow::SetParent - reparenting a non-child window");
        return NS_ERROR_NOT_IMPLEMENTED;
    }

    NS_ASSERTION(!mTransientParent, "child widget with transient parent");

    nsCOMPtr<nsIWidget> kungFuDeathGrip = this;
    mParent->RemoveChild(this);

    mParent = aNewParent;

    GtkWidget* oldContainer = GetMozContainerWidget();
    if (!oldContainer) {
        // The GdkWindows have been destroyed so there is nothing else to
        // reparent.
        NS_ABORT_IF_FALSE(gdk_window_is_destroyed(mGdkWindow),
                          "live GdkWindow with no widget");
        return NS_OK;
    }

    if (aNewParent) {
        aNewParent->AddChild(this);
        ReparentNativeWidget(aNewParent);
    } else {
        // aNewParent is NULL, but reparent to a hidden window to avoid
        // destroying the GdkWindow and its descendants.
        // An invisible container widget is needed to hold descendant
        // GtkWidgets.
        GtkWidget* newContainer = EnsureInvisibleContainer();
        GdkWindow* newParentWindow = gtk_widget_get_window(newContainer);
        ReparentNativeWidgetInternal(aNewParent, newContainer, newParentWindow,
                                     oldContainer);
    }
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::ReparentNativeWidget(nsIWidget* aNewParent)
{
    NS_PRECONDITION(aNewParent, "");
    NS_ASSERTION(!mIsDestroyed, "");
    NS_ASSERTION(!static_cast<nsWindow*>(aNewParent)->mIsDestroyed, "");

    GtkWidget* oldContainer = GetMozContainerWidget();
    if (!oldContainer) {
        // The GdkWindows have been destroyed so there is nothing else to
        // reparent.
        NS_ABORT_IF_FALSE(gdk_window_is_destroyed(mGdkWindow),
                          "live GdkWindow with no widget");
        return NS_OK;
    }
    NS_ABORT_IF_FALSE(!gdk_window_is_destroyed(mGdkWindow),
                      "destroyed GdkWindow with widget");
    
    nsWindow* newParent = static_cast<nsWindow*>(aNewParent);
    GdkWindow* newParentWindow = newParent->mGdkWindow;
    GtkWidget* newContainer = NULL;
    if (newParentWindow) {
        newContainer = get_gtk_widget_for_gdk_window(newParentWindow);
    }

    if (mTransientParent) {
      GtkWindow* topLevelParent =
          GTK_WINDOW(gtk_widget_get_toplevel(newContainer));
      gtk_window_set_transient_for(GTK_WINDOW(mShell), topLevelParent);
      mTransientParent = topLevelParent;
      if (mWindowGroup) {
          g_object_unref(mWindowGroup);
          mWindowGroup = NULL;
      }
      if (gtk_window_get_group(mTransientParent)) {
          gtk_window_group_add_window(gtk_window_get_group(mTransientParent),
                                      GTK_WINDOW(mShell));
          mWindowGroup = gtk_window_get_group(mTransientParent);
          g_object_ref(mWindowGroup);
      }
      else if (gtk_window_get_group(GTK_WINDOW(mShell))) {
          gtk_window_group_remove_window(gtk_window_get_group(GTK_WINDOW(mShell)),
                                         GTK_WINDOW(mShell));
      }
    }

    ReparentNativeWidgetInternal(aNewParent, newContainer, newParentWindow,
                                 oldContainer);
    return NS_OK;
}

void
nsWindow::ReparentNativeWidgetInternal(nsIWidget* aNewParent,
                                       GtkWidget* aNewContainer,
                                       GdkWindow* aNewParentWindow,
                                       GtkWidget* aOldContainer)
{
    if (!aNewContainer) {
        // The new parent GdkWindow has been destroyed.
        NS_ABORT_IF_FALSE(!aNewParentWindow ||
                          gdk_window_is_destroyed(aNewParentWindow),
                          "live GdkWindow with no widget");
        Destroy();
    } else {
        if (aNewContainer != aOldContainer) {
            NS_ABORT_IF_FALSE(!gdk_window_is_destroyed(aNewParentWindow),
                              "destroyed GdkWindow with widget");
            SetWidgetForHierarchy(mGdkWindow, aOldContainer, aNewContainer);
        }

        if (!mIsTopLevel) {
            gdk_window_reparent(mGdkWindow, aNewParentWindow, mBounds.x,
                                mBounds.y);
        }
    }

    nsWindow* newParent = static_cast<nsWindow*>(aNewParent);
    PRBool parentHasMappedToplevel =
        newParent && newParent->mHasMappedToplevel;
    if (mHasMappedToplevel != parentHasMappedToplevel) {
        SetHasMappedToplevel(parentHasMappedToplevel);
    }
}

NS_IMETHODIMP
nsWindow::SetModal(PRBool aModal)
{
    LOG(("nsWindow::SetModal [%p] %d\n", (void *)this, aModal));
    if (mIsDestroyed)
        return aModal ? NS_ERROR_NOT_AVAILABLE : NS_OK;
    if (!mIsTopLevel || !mShell)
        return NS_ERROR_FAILURE;
    gtk_window_set_modal(GTK_WINDOW(mShell), aModal ? TRUE : FALSE);
    return NS_OK;
}

// nsIWidget method, which means IsShown.
NS_IMETHODIMP
nsWindow::IsVisible(PRBool& aState)
{
    aState = mIsShown;
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::ConstrainPosition(PRBool aAllowSlop, PRInt32 *aX, PRInt32 *aY)
{
    if (mIsTopLevel && mShell) {
        PRInt32 screenWidth = gdk_screen_width();
        PRInt32 screenHeight = gdk_screen_height();
        if (aAllowSlop) {
            if (*aX < (kWindowPositionSlop - mBounds.width))
                *aX = kWindowPositionSlop - mBounds.width;
            if (*aX > (screenWidth - kWindowPositionSlop))
                *aX = screenWidth - kWindowPositionSlop;
            if (*aY < (kWindowPositionSlop - mBounds.height))
                *aY = kWindowPositionSlop - mBounds.height;
            if (*aY > (screenHeight - kWindowPositionSlop))
                *aY = screenHeight - kWindowPositionSlop;
        } else {
            if (*aX < 0)
                *aX = 0;
            if (*aX > (screenWidth - mBounds.width))
                *aX = screenWidth - mBounds.width;
            if (*aY < 0)
                *aY = 0;
            if (*aY > (screenHeight - mBounds.height))
                *aY = screenHeight - mBounds.height;
        }
    }
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Show(PRBool aState)
{
    if (aState == mIsShown)
        return NS_OK;

    // Clear our cached resources when the window is hidden.
    if (mIsShown && !aState) {
        ClearCachedResources();
    }

    mIsShown = aState;

    LOG(("nsWindow::Show [%p] state %d\n", (void *)this, aState));

    if (aState) {
        // Now that this window is shown, mHasMappedToplevel needs to be
        // tracked on viewable descendants.
        SetHasMappedToplevel(mHasMappedToplevel);
    }

    // Ok, someone called show on a window that isn't sized to a sane
    // value.  Mark this window as needing to have Show() called on it
    // and return.
    if ((aState && !AreBoundsSane()) || !mCreated) {
        LOG(("\tbounds are insane or window hasn't been created yet\n"));
        mNeedsShow = PR_TRUE;
        return NS_OK;
    }

    // If someone is hiding this widget, clear any needing show flag.
    if (!aState)
        mNeedsShow = PR_FALSE;

    // If someone is showing this window and it needs a resize then
    // resize the widget.
    if (aState) {
        if (mNeedsMove) {
            NativeResize(mBounds.x, mBounds.y, mBounds.width, mBounds.height,
                         PR_FALSE);
        } else if (mNeedsResize) {
            NativeResize(mBounds.width, mBounds.height, PR_FALSE);
        }
    }

#ifdef ACCESSIBILITY
    if (aState && sAccessibilityEnabled) {
        CreateRootAccessible();
    }
#endif

    NativeShow(aState);

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Resize(PRInt32 aWidth, PRInt32 aHeight, PRBool aRepaint)
{
    // For top-level windows, aWidth and aHeight should possibly be
    // interpreted as frame bounds, but NativeResize treats these as window
    // bounds (Bug 581866).

    mBounds.SizeTo(GetSafeWindowSize(nsIntSize(aWidth, aHeight)));

    if (!mCreated)
        return NS_OK;

    // There are several cases here that we need to handle, based on a
    // matrix of the visibility of the widget, the sanity of this resize
    // and whether or not the widget was previously sane.

    // Has this widget been set to visible?
    if (mIsShown) {
        // Are the bounds sane?
        if (AreBoundsSane()) {
            // Yep?  Resize the window
            //Maybe, the toplevel has moved

            // Note that if the widget needs to be positioned because its
            // size was previously insane in Resize(x,y,w,h), then we need
            // to set the x and y here too, because the widget wasn't
            // moved back then
            if (mNeedsMove)
                NativeResize(mBounds.x, mBounds.y,
                             mBounds.width, mBounds.height, aRepaint);
            else
                NativeResize(mBounds.width, mBounds.height, aRepaint);

            // Does it need to be shown because it was previously insane?
            if (mNeedsShow)
                NativeShow(PR_TRUE);
        }
        else {
            // If someone has set this so that the needs show flag is false
            // and it needs to be hidden, update the flag and hide the
            // window.  This flag will be cleared the next time someone
            // hides the window or shows it.  It also prevents us from
            // calling NativeShow(PR_FALSE) excessively on the window which
            // causes unneeded X traffic.
            if (!mNeedsShow) {
                mNeedsShow = PR_TRUE;
                NativeShow(PR_FALSE);
            }
        }
    }
    // If the widget hasn't been shown, mark the widget as needing to be
    // resized before it is shown.
    else {
        if (AreBoundsSane() && mListenForResizes) {
            // For widgets that we listen for resizes for (widgets created
            // with native parents) we apparently _always_ have to resize.  I
            // dunno why, but apparently we're lame like that.
            NativeResize(aWidth, aHeight, aRepaint);
        }
        else {
            mNeedsResize = PR_TRUE;
        }
    }

    // synthesize a resize event if this isn't a toplevel
    if (mIsTopLevel || mListenForResizes) {
        nsIntRect rect(mBounds.x, mBounds.y, aWidth, aHeight);
        nsEventStatus status;
        DispatchResizeEvent(rect, status);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Resize(PRInt32 aX, PRInt32 aY, PRInt32 aWidth, PRInt32 aHeight,
                       PRBool aRepaint)
{
    mBounds.x = aX;
    mBounds.y = aY;
    mBounds.SizeTo(GetSafeWindowSize(nsIntSize(aWidth, aHeight)));

    mNeedsMove = PR_TRUE;

    if (!mCreated)
        return NS_OK;

    // There are several cases here that we need to handle, based on a
    // matrix of the visibility of the widget, the sanity of this resize
    // and whether or not the widget was previously sane.

    // Has this widget been set to visible?
    if (mIsShown) {
        // Are the bounds sane?
        if (AreBoundsSane()) {
            // Yep?  Resize the window
            NativeResize(aX, aY, aWidth, aHeight, aRepaint);
            // Does it need to be shown because it was previously insane?
            if (mNeedsShow)
                NativeShow(PR_TRUE);
        }
        else {
            // If someone has set this so that the needs show flag is false
            // and it needs to be hidden, update the flag and hide the
            // window.  This flag will be cleared the next time someone
            // hides the window or shows it.  It also prevents us from
            // calling NativeShow(PR_FALSE) excessively on the window which
            // causes unneeded X traffic.
            if (!mNeedsShow) {
                mNeedsShow = PR_TRUE;
                NativeShow(PR_FALSE);
            }
        }
    }
    // If the widget hasn't been shown, mark the widget as needing to be
    // resized before it is shown
    else {
        if (AreBoundsSane() && mListenForResizes){
            // For widgets that we listen for resizes for (widgets created
            // with native parents) we apparently _always_ have to resize.  I
            // dunno why, but apparently we're lame like that.
            NativeResize(aX, aY, aWidth, aHeight, aRepaint);
        }
        else {
            mNeedsResize = PR_TRUE;
        }
    }

    if (mIsTopLevel || mListenForResizes) {
        // synthesize a resize event
        nsIntRect rect(aX, aY, aWidth, aHeight);
        nsEventStatus status;
        DispatchResizeEvent(rect, status);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Enable(PRBool aState)
{
    mEnabled = aState;

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::IsEnabled(PRBool *aState)
{
    *aState = mEnabled;

    return NS_OK;
}



NS_IMETHODIMP
nsWindow::Move(PRInt32 aX, PRInt32 aY)
{
    LOG(("nsWindow::Move [%p] %d %d\n", (void *)this,
         aX, aY));

    if (mWindowType == eWindowType_toplevel ||
        mWindowType == eWindowType_dialog) {
        SetSizeMode(nsSizeMode_Normal);
    }

    // Since a popup window's x/y coordinates are in relation to to
    // the parent, the parent might have moved so we always move a
    // popup window.
    if (aX == mBounds.x && aY == mBounds.y &&
        mWindowType != eWindowType_popup)
        return NS_OK;

    // XXX Should we do some AreBoundsSane check here?

    mBounds.x = aX;
    mBounds.y = aY;

    if (!mCreated)
        return NS_OK;

    mNeedsMove = PR_FALSE;

    if (mIsTopLevel) {
        gtk_window_move(GTK_WINDOW(mShell), aX, aY);
    }
    else if (mGdkWindow) {
        gdk_window_move(mGdkWindow, aX, aY);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::PlaceBehind(nsTopLevelWidgetZPlacement  aPlacement,
                      nsIWidget                  *aWidget,
                      PRBool                      aActivate)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindow::SetZIndex(PRInt32 aZIndex)
{
    nsIWidget* oldPrev = GetPrevSibling();

    nsBaseWidget::SetZIndex(aZIndex);

    if (GetPrevSibling() == oldPrev) {
        return NS_OK;
    }

    NS_ASSERTION(!mContainer, "Expected Mozilla child widget");

    // We skip the nsWindows that don't have mGdkWindows.
    // These are probably in the process of being destroyed.

    if (!GetNextSibling()) {
        // We're to be on top.
        if (mGdkWindow)
            gdk_window_raise(mGdkWindow);
    } else {
        // All the siblings before us need to be below our widget.
        for (nsWindow* w = this; w;
             w = static_cast<nsWindow*>(w->GetPrevSibling())) {
            if (w->mGdkWindow)
                gdk_window_lower(w->mGdkWindow);
        }
    }
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetSizeMode(PRInt32 aMode)
{
    nsresult rv;

    LOG(("nsWindow::SetSizeMode [%p] %d\n", (void *)this, aMode));

    // Save the requested state.
    rv = nsBaseWidget::SetSizeMode(aMode);

    // return if there's no shell or our current state is the same as
    // the mode we were just set to.
    if (!mShell || mSizeState == mSizeMode) {
        return rv;
    }

    switch (aMode) {
    case nsSizeMode_Maximized:
        gtk_window_maximize(GTK_WINDOW(mShell));
        break;
    case nsSizeMode_Minimized:
        gtk_window_iconify(GTK_WINDOW(mShell));
        break;
    case nsSizeMode_Fullscreen:
        MakeFullScreen(PR_TRUE);
        break;

    default:
        // nsSizeMode_Normal, really.
        if (mSizeState == nsSizeMode_Minimized)
            gtk_window_deiconify(GTK_WINDOW(mShell));
        else if (mSizeState == nsSizeMode_Maximized)
            gtk_window_unmaximize(GTK_WINDOW(mShell));
        break;
    }

    mSizeState = mSizeMode;

    return rv;
}

typedef void (* SetUserTimeFunc)(GdkWindow* aWindow, guint32 aTimestamp);

// This will become obsolete when new GTK APIs are widely supported,
// as described here: http://bugzilla.gnome.org/show_bug.cgi?id=347375
static void
SetUserTimeAndStartupIDForActivatedWindow(GtkWidget* aWindow)
{
    nsCOMPtr<nsIToolkit> toolkit;
    NS_GetCurrentToolkit(getter_AddRefs(toolkit));
    if (!toolkit)
        return;

    nsGTKToolkit* GTKToolkit = static_cast<nsGTKToolkit*>
                                          (static_cast<nsIToolkit*>(toolkit));
    nsCAutoString desktopStartupID;
    GTKToolkit->GetDesktopStartupID(&desktopStartupID);
    if (desktopStartupID.IsEmpty()) {
        // We don't have the data we need. Fall back to an
        // approximation ... using the timestamp of the remote command
        // being received as a guess for the timestamp of the user event
        // that triggered it.
        PRUint32 timestamp = GTKToolkit->GetFocusTimestamp();
        if (timestamp) {
            gdk_window_focus(gtk_widget_get_window(aWindow), timestamp);
            GTKToolkit->SetFocusTimestamp(0);
        }
        return;
    }

#if defined(MOZ_ENABLE_STARTUP_NOTIFICATION)
    GdkWindow* gdkWindow = gtk_widget_get_window(aWindow);
  
    GdkScreen* screen = gdk_window_get_screen(gdkWindow);
    SnDisplay* snd =
        sn_display_new(gdk_x11_display_get_xdisplay(gdk_window_get_display(gdkWindow)), 
                       nsnull, nsnull);
    if (!snd)
        return;
    SnLauncheeContext* ctx =
        sn_launchee_context_new(snd, gdk_screen_get_number(screen),
                                desktopStartupID.get());
    if (!ctx) {
        sn_display_unref(snd);
        return;
    }

    if (sn_launchee_context_get_id_has_timestamp(ctx)) {
        PRLibrary* gtkLibrary;
        SetUserTimeFunc setUserTimeFunc = (SetUserTimeFunc)
            PR_FindFunctionSymbolAndLibrary("gdk_x11_window_set_user_time", &gtkLibrary);
        if (setUserTimeFunc) {
            setUserTimeFunc(gdkWindow, sn_launchee_context_get_timestamp(ctx));
            PR_UnloadLibrary(gtkLibrary);
        }
    }

    sn_launchee_context_setup_window(ctx, gdk_x11_window_get_xid(gdkWindow));
    sn_launchee_context_complete(ctx);

    sn_launchee_context_unref(ctx);
    sn_display_unref(snd);
#endif

    GTKToolkit->SetDesktopStartupID(EmptyCString());
}

NS_IMETHODIMP
nsWindow::SetFocus(PRBool aRaise)
{
    // Make sure that our owning widget has focus.  If it doesn't try to
    // grab it.  Note that we don't set our focus flag in this case.

    LOGFOCUS(("  SetFocus %d [%p]\n", aRaise, (void *)this));

    GtkWidget *owningWidget = GetMozContainerWidget();
    if (!owningWidget)
        return NS_ERROR_FAILURE;

    // Raise the window if someone passed in PR_TRUE and the prefs are
    // set properly.
    GtkWidget *toplevelWidget = gtk_widget_get_toplevel(owningWidget);

    if (gRaiseWindows && aRaise && toplevelWidget &&
        !gtk_widget_has_focus(owningWidget) &&
        !gtk_widget_has_focus(toplevelWidget)) {
        GtkWidget* top_window = nsnull;
        GetToplevelWidget(&top_window);
        if (top_window && (gtk_widget_get_visible(top_window)))
        {
            gdk_window_show_unraised(gtk_widget_get_window(top_window));
            // Unset the urgency hint if possible.
            SetUrgencyHint(top_window, PR_FALSE);
        }
    }

    nsRefPtr<nsWindow> owningWindow = get_window_for_gtk_widget(owningWidget);
    if (!owningWindow)
        return NS_ERROR_FAILURE;

    if (aRaise) {
        // aRaise == PR_TRUE means request toplevel activation.

        // This is asynchronous.
        // If and when the window manager accepts the request, then the focus
        // widget will get a focus-in-event signal.
        if (gRaiseWindows && owningWindow->mIsShown && owningWindow->mShell &&
            !gtk_window_is_active(GTK_WINDOW(owningWindow->mShell))) {

            LOGFOCUS(("  requesting toplevel activation [%p]\n", (void *)this));
            NS_ASSERTION(owningWindow->mWindowType != eWindowType_popup
                         || mParent,
                         "Presenting an override-redirect window");
            gtk_window_present(GTK_WINDOW(owningWindow->mShell));
        }

        return NS_OK;
    }

    // aRaise == PR_FALSE means that keyboard events should be dispatched
    // from this widget.

    // Ensure owningWidget is the focused GtkWidget within its toplevel window.
    //
    // For eWindowType_popup, this GtkWidget may not actually be the one that
    // receives the key events as it may be the parent window that is active.
    if (!gtk_widget_is_focus(owningWidget)) {
        // This is synchronous.  It takes focus from a plugin or from a widget
        // in an embedder.  The focus manager already knows that this window
        // is active so gBlockActivateEvent avoids another (unnecessary)
        // NS_ACTIVATE event.
        gBlockActivateEvent = PR_TRUE;
        gtk_widget_grab_focus(owningWidget);
        gBlockActivateEvent = PR_FALSE;
    }

    // If this is the widget that already has focus, return.
    if (gFocusWindow == this) {
        LOGFOCUS(("  already have focus [%p]\n", (void *)this));
        return NS_OK;
    }

    // Set this window to be the focused child window
    gFocusWindow = this;

    if (mIMModule) {
        mIMModule->OnFocusWindow(this);
    }

    LOGFOCUS(("  widget now has focus in SetFocus() [%p]\n",
              (void *)this));

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::GetScreenBounds(nsIntRect &aRect)
{
    if (mIsTopLevel && mContainer) {
        // use the point including window decorations
        gint x, y;
        gdk_window_get_root_origin(gtk_widget_get_window(GTK_WIDGET(mContainer)), &x, &y);
        aRect.MoveTo(x, y);
    }
    else {
        aRect.MoveTo(WidgetToScreenOffset());
    }
    // mBounds.Size() is the window bounds, not the window-manager frame
    // bounds (bug 581863).  gdk_window_get_frame_extents would give the
    // frame bounds, but mBounds.Size() is returned here for consistency
    // with Resize.
    aRect.SizeTo(mBounds.Size());
    LOG(("GetScreenBounds %d,%d | %dx%d\n",
         aRect.x, aRect.y, aRect.width, aRect.height));
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetForegroundColor(const nscolor &aColor)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindow::SetBackgroundColor(const nscolor &aColor)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindow::SetCursor(nsCursor aCursor)
{
    // if we're not the toplevel window pass up the cursor request to
    // the toplevel window to handle it.
    if (!mContainer && mGdkWindow) {
        nsWindow *window = GetContainerWindow();
        if (!window)
            return NS_ERROR_FAILURE;

        return window->SetCursor(aCursor);
    }

    // Only change cursor if it's actually been changed
    if (aCursor != mCursor) {
        GdkCursor *newCursor = NULL;

        newCursor = get_gtk_cursor(aCursor);

        if (nsnull != newCursor) {
            mCursor = aCursor;

            if (!mContainer)
                return NS_OK;

            gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(mContainer)), newCursor);
        }
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetCursor(imgIContainer* aCursor,
                    PRUint32 aHotspotX, PRUint32 aHotspotY)
{
    // if we're not the toplevel window pass up the cursor request to
    // the toplevel window to handle it.
    if (!mContainer && mGdkWindow) {
        nsWindow *window = GetContainerWindow();
        if (!window)
            return NS_ERROR_FAILURE;

        return window->SetCursor(aCursor, aHotspotX, aHotspotY);
    }

    mCursor = nsCursor(-1);

    // Get the image's current frame
    GdkPixbuf* pixbuf = nsImageToPixbuf::ImageToPixbuf(aCursor);
    if (!pixbuf)
        return NS_ERROR_NOT_AVAILABLE;

    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);
    // Reject cursors greater than 128 pixels in some direction, to prevent
    // spoofing.
    // XXX ideally we should rescale. Also, we could modify the API to
    // allow trusted content to set larger cursors.
    if (width > 128 || height > 128) {
        g_object_unref(pixbuf);
        return NS_ERROR_NOT_AVAILABLE;
    }

    // Looks like all cursors need an alpha channel (tested on Gtk 2.4.4). This
    // is of course not documented anywhere...
    // So add one if there isn't one yet
    if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
        GdkPixbuf* alphaBuf = gdk_pixbuf_add_alpha(pixbuf, FALSE, 0, 0, 0);
        g_object_unref(pixbuf);
        if (!alphaBuf) {
            return NS_ERROR_OUT_OF_MEMORY;
        }
        pixbuf = alphaBuf;
    }

    GdkCursor* cursor = gdk_cursor_new_from_pixbuf(gdk_display_get_default(),
                                                   pixbuf,
                                                   aHotspotX, aHotspotY);
    g_object_unref(pixbuf);
    nsresult rv = NS_ERROR_OUT_OF_MEMORY;
    if (cursor) {
        if (mContainer) {
            gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(mContainer)), cursor);
            rv = NS_OK;
        }
        gdk_cursor_unref(cursor);
    }

    return rv;
}

NS_IMETHODIMP
nsWindow::Invalidate(const nsIntRect &aRect,
                     PRBool           aIsSynchronous)
{
    if (!mGdkWindow)
        return NS_OK;

    GdkRectangle rect;
    rect.x = aRect.x;
    rect.y = aRect.y;
    rect.width = aRect.width;
    rect.height = aRect.height;

    LOGDRAW(("Invalidate (rect) [%p]: %d %d %d %d (sync: %d)\n", (void *)this,
             rect.x, rect.y, rect.width, rect.height, aIsSynchronous));

    gdk_window_invalidate_rect(mGdkWindow, &rect, FALSE);
    if (aIsSynchronous)
        gdk_window_process_updates(mGdkWindow, FALSE);

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Update()
{
    if (!mGdkWindow)
        return NS_OK;

    LOGDRAW(("Update [%p] %p\n", this, mGdkWindow));

    gdk_window_process_updates(mGdkWindow, FALSE);
    // Send the updates to the server.
    gdk_display_flush(gdk_window_get_display(mGdkWindow));
    return NS_OK;
}

void*
nsWindow::GetNativeData(PRUint32 aDataType)
{
    switch (aDataType) {
    case NS_NATIVE_WINDOW:
    case NS_NATIVE_WIDGET: {
        if (!mGdkWindow)
            return nsnull;

        return mGdkWindow;
        break;
    }

    case NS_NATIVE_PLUGIN_PORT:
        return SetupPluginPort();
        break;

    case NS_NATIVE_DISPLAY:
#ifdef MOZ_X11
        return GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
#else
        return nsnull;
#endif /* MOZ_X11 */
        break;

    case NS_NATIVE_GRAPHIC: {
#if defined(MOZ_WIDGET_GTK2)
        NS_ASSERTION(nsnull != mToolkit, "NULL toolkit, unable to get a GC");    
        return (void *)static_cast<nsGTKToolkit *>(mToolkit)->GetSharedGC();
#else
        return nsnull;
#endif
        break;
    }

    case NS_NATIVE_SHELLWIDGET:
        return (void *) mShell;

    case NS_NATIVE_SHAREABLE_WINDOW:
        return (void *) GDK_WINDOW_XID(gdk_window_get_toplevel(mGdkWindow));

    default:
        NS_WARNING("nsWindow::GetNativeData called with bad value");
        return nsnull;
    }
}

NS_IMETHODIMP
nsWindow::SetTitle(const nsAString& aTitle)
{
    if (!mShell)
        return NS_OK;

    // convert the string into utf8 and set the title.
#define UTF8_FOLLOWBYTE(ch) (((ch) & 0xC0) == 0x80)
    NS_ConvertUTF16toUTF8 titleUTF8(aTitle);
    if (titleUTF8.Length() > NS_WINDOW_TITLE_MAX_LENGTH) {
        // Truncate overlong titles (bug 167315). Make sure we chop after a
        // complete sequence by making sure the next char isn't a follow-byte.
        PRUint32 len = NS_WINDOW_TITLE_MAX_LENGTH;
        while(UTF8_FOLLOWBYTE(titleUTF8[len]))
            --len;
        titleUTF8.Truncate(len);
    }
    gtk_window_set_title(GTK_WINDOW(mShell), (const char *)titleUTF8.get());

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetIcon(const nsAString& aIconSpec)
{
    if (!mShell)
        return NS_OK;

    nsCOMPtr<nsILocalFile> iconFile;
    nsCAutoString path;
    nsTArray<nsCString> iconList;

    // Look for icons with the following suffixes appended to the base name.
    // The last two entries (for the old XPM format) will be ignored unless
    // no icons are found using the other suffixes. XPM icons are depricated.

    const char extensions[6][7] = { ".png", "16.png", "32.png", "48.png",
                                    ".xpm", "16.xpm" };

    for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(extensions); i++) {
        // Don't bother looking for XPM versions if we found a PNG.
        if (i == NS_ARRAY_LENGTH(extensions) - 2 && iconList.Length())
            break;

        nsAutoString extension;
        extension.AppendASCII(extensions[i]);

        ResolveIconName(aIconSpec, extension, getter_AddRefs(iconFile));
        if (iconFile) {
            iconFile->GetNativePath(path);
            iconList.AppendElement(path);
        }
    }

    // leave the default icon intact if no matching icons were found
    if (iconList.Length() == 0)
        return NS_OK;

    return SetWindowIconList(iconList);
}

nsIntPoint
nsWindow::WidgetToScreenOffset()
{
    gint x = 0, y = 0;

    if (mGdkWindow) {
        gdk_window_get_origin(mGdkWindow, &x, &y);
    }

    return nsIntPoint(x, y);
}

NS_IMETHODIMP
nsWindow::EnableDragDrop(PRBool aEnable)
{
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::CaptureMouse(PRBool aCapture)
{
    LOG(("CaptureMouse %p\n", (void *)this));

    if (!mGdkWindow)
        return NS_OK;

    GtkWidget *widget = GetMozContainerWidget();
    if (!widget)
        return NS_ERROR_FAILURE;

    if (aCapture) {
        gtk_grab_add(widget);
        GrabPointer();
    }
    else {
        ReleaseGrabs();
        gtk_grab_remove(widget);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::CaptureRollupEvents(nsIRollupListener *aListener,
                              nsIMenuRollup     *aMenuRollup,
                              PRBool             aDoCapture,
                              PRBool             aConsumeRollupEvent)
{
    if (!mGdkWindow)
        return NS_OK;

    GtkWidget *widget = GetMozContainerWidget();
    if (!widget)
        return NS_ERROR_FAILURE;

    LOG(("CaptureRollupEvents %p\n", (void *)this));

    if (aDoCapture) {
        gConsumeRollupEvent = aConsumeRollupEvent;
        gRollupListener = aListener;
        NS_IF_RELEASE(gMenuRollup);
        gMenuRollup = aMenuRollup;
        NS_IF_ADDREF(aMenuRollup);
        gRollupWindow = do_GetWeakReference(static_cast<nsIWidget*>
                                                       (this));
        // real grab is only done when there is no dragging
        if (!nsWindow::DragInProgress()) {
            gtk_grab_add(widget);
            GrabPointer();
        }
    }
    else {
        if (!nsWindow::DragInProgress()) {
            ReleaseGrabs();
        }
        // There may not have been a drag in process when aDoCapture was set,
        // so make sure to remove any added grab.  This is a no-op if the grab
        // was not added to this widget.
        gtk_grab_remove(widget);
        gRollupListener = nsnull;
        NS_IF_RELEASE(gMenuRollup);
        gRollupWindow = nsnull;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::GetAttention(PRInt32 aCycleCount)
{
    LOG(("nsWindow::GetAttention [%p]\n", (void *)this));

    GtkWidget* top_window = nsnull;
    GtkWidget* top_focused_window = nsnull;
    GetToplevelWidget(&top_window);
    if (gFocusWindow)
        gFocusWindow->GetToplevelWidget(&top_focused_window);

    // Don't get attention if the window is focused anyway.
    if (top_window && (gtk_widget_get_visible(top_window)) &&
        top_window != top_focused_window) {
        SetUrgencyHint(top_window, PR_TRUE);
    }

    return NS_OK;
}

PRBool
nsWindow::HasPendingInputEvent()
{
    // This sucks, but gtk/gdk has no way to answer the question we want while
    // excluding paint events, and there's no X API that will let us peek
    // without blocking or removing.  To prevent event reordering, peek
    // anything except expose events.  Reordering expose and others should be
    // ok, hopefully.
    PRBool haveEvent;
#ifdef MOZ_X11
    XEvent ev;
    Display *display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    haveEvent =
        XCheckMaskEvent(display,
                        KeyPressMask | KeyReleaseMask | ButtonPressMask |
                        ButtonReleaseMask | EnterWindowMask | LeaveWindowMask |
                        PointerMotionMask | PointerMotionHintMask |
                        Button1MotionMask | Button2MotionMask |
                        Button3MotionMask | Button4MotionMask |
                        Button5MotionMask | ButtonMotionMask | KeymapStateMask |
                        VisibilityChangeMask | StructureNotifyMask |
                        ResizeRedirectMask | SubstructureNotifyMask |
                        SubstructureRedirectMask | FocusChangeMask |
                        PropertyChangeMask | ColormapChangeMask |
                        OwnerGrabButtonMask, &ev);
    if (haveEvent) {
        XPutBackEvent(display, &ev);
    }
#else
    haveEvent = PR_FALSE;
#endif
    return haveEvent;
}

#if 0
#ifdef DEBUG
// Paint flashing code (disabled for cairo - see below)

#define CAPS_LOCK_IS_ON \
(gdk_keyboard_get_modifiers() & GDK_LOCK_MASK)

#define WANT_PAINT_FLASHING \
(debug_WantPaintFlashing() && CAPS_LOCK_IS_ON)

#ifdef MOZ_X11
static void
gdk_window_flash(GdkWindow *    aGdkWindow,
                 unsigned int   aTimes,
                 unsigned int   aInterval,  // Milliseconds
                 GdkRegion *    aRegion)
{
  gint         x;
  gint         y;
  gint         width;
  gint         height;
  guint        i;
  GdkGC *      gc = 0;
  GdkColor     white;

#if defined(MOZ_WIDGET_GTK2)
  gdk_window_get_geometry(aGdkWindow,NULL,NULL,&width,&height,NULL);
#else
  gdk_window_get_geometry(aGdkWindow,NULL,NULL,&width,&height);
#endif

  gdk_window_get_origin (aGdkWindow,
                         &x,
                         &y);

  gc = gdk_gc_new(GDK_ROOT_PARENT());

  white.pixel = WhitePixel(gdk_display,DefaultScreen(gdk_display));

  gdk_gc_set_foreground(gc,&white);
  gdk_gc_set_function(gc,GDK_XOR);
  gdk_gc_set_subwindow(gc,GDK_INCLUDE_INFERIORS);

  gdk_region_offset(aRegion, x, y);
  gdk_gc_set_clip_region(gc, aRegion);

  /*
   * Need to do this twice so that the XOR effect can replace
   * the original window contents.
   */
  for (i = 0; i < aTimes * 2; i++)
  {
    gdk_draw_rectangle(GDK_ROOT_PARENT(),
                       gc,
                       TRUE,
                       x,
                       y,
                       width,
                       height);

    gdk_flush();

    PR_Sleep(PR_MillisecondsToInterval(aInterval));
  }

  gdk_gc_destroy(gc);

  gdk_region_offset(aRegion, -x, -y);
}
#endif /* MOZ_X11 */
#endif // DEBUG
#endif

static void
DispatchDidPaint(nsIWidget* aWidget)
{
    nsEventStatus status;
    nsPaintEvent didPaintEvent(PR_TRUE, NS_DID_PAINT, aWidget);
    aWidget->DispatchEvent(&didPaintEvent, status);
}

#if defined(MOZ_WIDGET_GTK2)
gboolean
nsWindow::OnExposeEvent(GdkEventExpose *aEvent)
#else
gboolean
nsWindow::OnExposeEvent(cairo_t *cr)
#endif
{
    if (mIsDestroyed) {
        return FALSE;
    }

    // Windows that are not visible will be painted after they become visible.
    if (!mGdkWindow || mIsFullyObscured || !mHasMappedToplevel)
        return FALSE;

    // Dispatch WILL_PAINT to allow scripts etc. to run before we
    // dispatch PAINT
    {
        nsEventStatus status;
        nsPaintEvent willPaintEvent(PR_TRUE, NS_WILL_PAINT, this);
        willPaintEvent.willSendDidPaint = PR_TRUE;
        DispatchEvent(&willPaintEvent, status);

        // If the window has been destroyed during WILL_PAINT, there is
        // nothing left to do.
        if (!mGdkWindow)
            return TRUE;
    }

    nsPaintEvent event(PR_TRUE, NS_PAINT, this);
    event.willSendDidPaint = PR_TRUE;

#if defined(MOZ_WIDGET_GTK2)
    GdkRectangle *rects;
    gint nrects;
    gdk_region_get_rectangles(aEvent->region, &rects, &nrects);
    if (NS_UNLIKELY(!rects)) // OOM
        return FALSE;
#else
#ifdef cairo_copy_clip_rectangle_list
#error "Looks like we're including Mozilla's cairo instead of system cairo"
#else
    cairo_rectangle_list_t *rects;
    rects = cairo_copy_clip_rectangle_list(cr);  
    if (NS_UNLIKELY(rects->status != CAIRO_STATUS_SUCCESS)) {
       NS_WARNING("Failed to obtain cairo rectangle list.");
       return FALSE;
    }
#endif
#endif

// GTK3 TODO?
#if defined(MOZ_WIDGET_GTK2)
    if (nrects > MAX_RECTS_IN_REGION) {
        // Just use the bounding box
        rects[0] = aEvent->area;
        nrects = 1;
    }
#endif

    LOGDRAW(("sending expose event [%p] %p 0x%lx (rects follow):\n",
             (void *)this, (void *)mGdkWindow,
             gdk_x11_window_get_xid(mGdkWindow)));
  
#if defined(MOZ_WIDGET_GTK2)
    GdkRectangle *r = rects;
    GdkRectangle *r_end = rects + nrects;
#else
    cairo_rectangle_t *r = rects->rectangles;
    cairo_rectangle_t *r_end = r + rects->num_rectangles;
#endif
    for (; r < r_end; ++r) {
        event.region.Or(event.region, nsIntRect(r->x, r->y, r->width, r->height));
        LOGDRAW(("\t%d %d %d %d\n", r->x, r->y, r->width, r->height));
    }

    // Our bounds may have changed after dispatching WILL_PAINT.  Clip
    // to the new bounds here.  The event region is relative to this
    // window.
    event.region.And(event.region,
                     nsIntRect(0, 0, mBounds.width, mBounds.height));

    PRBool translucent = eTransparencyTransparent == GetTransparencyMode();
    if (!translucent) {
        GList *children =
            gdk_window_peek_children(mGdkWindow);
        while (children) {
            GdkWindow *gdkWin = GDK_WINDOW(children->data);
            nsWindow *kid = get_window_for_gdk_window(gdkWin);
            if (kid && gdk_window_is_visible(gdkWin)) {
                nsAutoTArray<nsIntRect,1> clipRects;
                kid->GetWindowClipRegion(&clipRects);
                nsIntRect bounds;
                kid->GetBounds(bounds);
                for (PRUint32 i = 0; i < clipRects.Length(); ++i) {
                    nsIntRect r = clipRects[i] + bounds.TopLeft();
                    event.region.Sub(event.region, r);
                }
            }
            children = children->next;
        }
    }

    if (event.region.IsEmpty()) {
#if defined(MOZ_WIDGET_GTK2)
        g_free(rects);
#else
        cairo_rectangle_list_destroy(rects);
#endif
        return TRUE;
    }

    if (GetLayerManager()->GetBackendType() == LayerManager::LAYERS_OPENGL)
    {
        LayerManagerOGL *manager = static_cast<LayerManagerOGL*>(GetLayerManager());
        manager->SetClippingRegion(event.region);

        nsEventStatus status;
        DispatchEvent(&event, status);

        g_free(rects);

        DispatchDidPaint(this);

        return TRUE;
    }
            
#if defined(MOZ_WIDGET_GTK2)
    nsRefPtr<gfxContext> ctx = new gfxContext(GetThebesSurface());
#else
    nsRefPtr<gfxContext> ctx = new gfxContext(GetThebesSurface(cr));
#endif

#ifdef MOZ_DFB
    gfxPlatformGtk::SetGdkDrawable(ctx->OriginalSurface(),
                                   GDK_DRAWABLE(mGdkWindow));

    // clip to the update region
    gfxUtils::ClipToRegion(ctx, event.region);

    BasicLayerManager::BufferMode layerBuffering =
        BasicLayerManager::BUFFER_NONE;
#endif

#ifdef MOZ_X11
    nsIntRect boundsRect; // for translucent only

    ctx->NewPath();
    if (translucent) {
        // Collapse update area to the bounding box. This is so we only have to
        // call UpdateTranslucentWindowAlpha once. After we have dropped
        // support for non-Thebes graphics, UpdateTranslucentWindowAlpha will be
        // our private interface so we can rework things to avoid this.
        boundsRect = event.region.GetBounds();
        ctx->Rectangle(gfxRect(boundsRect.x, boundsRect.y,
                               boundsRect.width, boundsRect.height));
    } else {
        gfxUtils::PathFromRegion(ctx, event.region);
    }
    ctx->Clip();

    BasicLayerManager::BufferMode layerBuffering;
    if (translucent) {
        // The double buffering is done here to extract the shape mask.
        // (The shape mask won't be necessary when a visual with an alpha
        // channel is used on compositing window managers.)
        layerBuffering = BasicLayerManager::BUFFER_NONE;
        ctx->PushGroup(gfxASurface::CONTENT_COLOR_ALPHA);
#ifdef MOZ_HAVE_SHMIMAGE
    } else if (nsShmImage::UseShm()) {
        // We're using an xshm mapping as a back buffer.
        layerBuffering = BasicLayerManager::BUFFER_NONE;
#endif // MOZ_HAVE_SHMIMAGE
    } else {
        // Get the layer manager to do double buffering (if necessary).
        layerBuffering = BasicLayerManager::BUFFER_BUFFERED;
    }

#if 0
    // NOTE: Paint flashing region would be wrong for cairo, since
    // cairo inflates the update region, etc.  So don't paint flash
    // for cairo.
#ifdef DEBUG
    // XXX aEvent->region may refer to a newly-invalid area.  FIXME
    if (0 && WANT_PAINT_FLASHING && gtk_widget_get_window(aEvent))
        gdk_window_flash(mGdkWindow, 1, 100, aEvent->region);
#endif
#endif

#endif // MOZ_X11

    nsEventStatus status;
    {
      AutoLayerManagerSetup setupLayerManager(this, ctx, layerBuffering);
      DispatchEvent(&event, status);
    }

#ifdef MOZ_X11
    // DispatchEvent can Destroy us (bug 378273), avoid doing any paint
    // operations below if that happened - it will lead to XError and exit().
    if (translucent) {
        if (NS_LIKELY(!mIsDestroyed)) {
            if (status != nsEventStatus_eIgnore) {
                nsRefPtr<gfxPattern> pattern = ctx->PopGroup();

                nsRefPtr<gfxImageSurface> img =
                    new gfxImageSurface(gfxIntSize(boundsRect.width, boundsRect.height),
                                        gfxImageSurface::ImageFormatA8);
                if (img && !img->CairoStatus()) {
                    img->SetDeviceOffset(gfxPoint(-boundsRect.x, -boundsRect.y));

                    nsRefPtr<gfxContext> imgCtx = new gfxContext(img);
                    if (imgCtx) {
                        imgCtx->SetPattern(pattern);
                        imgCtx->SetOperator(gfxContext::OPERATOR_SOURCE);
                        imgCtx->Paint();
                    }

                    UpdateTranslucentWindowAlphaInternal(nsIntRect(boundsRect.x, boundsRect.y,
                                                                   boundsRect.width, boundsRect.height),
                                                         img->Data(), img->Stride());
                }

                ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
                ctx->SetPattern(pattern);
                ctx->Paint();
            }
        }
    }
#  ifdef MOZ_HAVE_SHMIMAGE
    if (nsShmImage::UseShm() && NS_LIKELY(!mIsDestroyed)) {
#if defined(MOZ_WIDGET_GTK2)
        mShmImage->Put(mGdkWindow, rects, r_end);
#else
        mShmImage->Put(mGdkWindow, rects);
#endif
    }
#  endif  // MOZ_HAVE_SHMIMAGE
#endif // MOZ_X11

#if defined(MOZ_WIDGET_GTK2)
    g_free(rects);
#else
    cairo_rectangle_list_destroy(rects);
#endif

    DispatchDidPaint(this);

    // Synchronously flush any new dirty areas
#if defined(MOZ_WIDGET_GTK2)
    GdkRegion* dirtyArea = gdk_window_get_update_area(mGdkWindow);
#else
    cairo_region_t* dirtyArea = gdk_window_get_update_area(mGdkWindow);
#endif

    if (dirtyArea) {
        gdk_window_invalidate_region(mGdkWindow, dirtyArea, PR_FALSE);
#if defined(MOZ_WIDGET_GTK2)
        gdk_region_destroy(dirtyArea);
#else
        cairo_region_destroy(dirtyArea);
#endif
        gdk_window_process_updates(mGdkWindow, PR_FALSE);
    }

    // check the return value!
    return TRUE;
}

gboolean
nsWindow::OnConfigureEvent(GtkWidget *aWidget, GdkEventConfigure *aEvent)
{
    // These events are only received on toplevel windows.
    //
    // GDK ensures that the coordinates are the client window top-left wrt the
    // root window.
    //
    //   GDK calculates the cordinates for real ConfigureNotify events on
    //   managed windows (that would normally be relative to the parent
    //   window).
    //
    //   Synthetic ConfigureNotify events are from the window manager and
    //   already relative to the root window.  GDK creates all X windows with
    //   border_width = 0, so synthetic events also indicate the top-left of
    //   the client window.
    //
    //   Override-redirect windows are children of the root window so parent
    //   coordinates are root coordinates.

    LOG(("configure event [%p] %d %d %d %d\n", (void *)this,
         aEvent->x, aEvent->y, aEvent->width, aEvent->height));

    // mBounds.x/y are set to the window manager frame top-left when Move() or
    // Resize()d from within Gecko, so comparing with the client window
    // top-left is weird.  However, mBounds.x/y are set to client window
    // position below, so this check avoids unwanted rollup on spurious
    // configure events from Cygwin/X (bug 672103).
    if (mBounds.x == aEvent->x &&
        mBounds.y == aEvent->y)
        return FALSE;

    if (mWindowType == eWindowType_toplevel || mWindowType == eWindowType_dialog) {
        check_for_rollup(aEvent->window, 0, 0, PR_FALSE, PR_TRUE);
    }

    // This event indicates that the window position may have changed.
    // mBounds.Size() is updated in OnSizeAllocate().

    // (The gtk_window_get_window_type() function is only available from
    // version 2.20.)
    NS_ASSERTION(GTK_IS_WINDOW(aWidget),
                 "Configure event on widget that is not a GtkWindow");
    gint type;
    g_object_get(aWidget, "type", &type, NULL);
    if (type == GTK_WINDOW_POPUP) {
        // Override-redirect window
        //
        // These windows should not be moved by the window manager, and so any
        // change in position is a result of our direction.  mBounds has
        // already been set in Move() or Resize(), and that is more
        // up-to-date than the position in the ConfigureNotify event if the
        // event is from an earlier window move.
        //
        // Skipping the NS_MOVE dispatch saves context menus from an infinite
        // loop when nsXULPopupManager::PopupMoved moves the window to the new
        // position and nsMenuPopupFrame::SetPopupPosition adds
        // offsetForContextMenu on each iteration.
        return FALSE;
    }

    // This is wrong, but noautohide titlebar panels currently depend on it
    // (bug 601545#c13).  mBounds.TopLeft() should refer to the
    // window-manager frame top-left, but WidgetToScreenOffset() gives the
    // client window origin.
    mBounds.MoveTo(WidgetToScreenOffset());

    nsGUIEvent event(PR_TRUE, NS_MOVE, this);

    event.refPoint = mBounds.TopLeft();

    // XXX mozilla will invalidate the entire window after this move
    // complete.  wtf?
    nsEventStatus status;
    DispatchEvent(&event, status);

    return FALSE;
}

void
nsWindow::OnContainerUnrealize(GtkWidget *aWidget)
{
    // The GdkWindows are about to be destroyed (but not deleted), so remove
    // their references back to their container widget while the GdkWindow
    // hierarchy is still available.

    NS_ASSERTION(mContainer == MOZ_CONTAINER(aWidget),
                 "unexpected \"unrealize\" signal");

    if (mGdkWindow) {
        DestroyChildWindows();

        g_object_set_data(G_OBJECT(mGdkWindow), "nsWindow", NULL);
        mGdkWindow = NULL;
    }
}

void
nsWindow::OnSizeAllocate(GtkWidget *aWidget, GtkAllocation *aAllocation)
{
    LOG(("size_allocate [%p] %d %d %d %d\n",
         (void *)this, aAllocation->x, aAllocation->y,
         aAllocation->width, aAllocation->height));

    nsIntRect rect(aAllocation->x, aAllocation->y,
                   aAllocation->width, aAllocation->height);

    ResizeTransparencyBitmap(rect.width, rect.height);

    mBounds.width = rect.width;
    mBounds.height = rect.height;

    if (!mGdkWindow)
        return;

    if (mTransparencyBitmap) {
      ApplyTransparencyBitmap();
    }

    nsEventStatus status;
    DispatchResizeEvent (rect, status);
}

void
nsWindow::OnDeleteEvent(GtkWidget *aWidget, GdkEventAny *aEvent)
{
    nsGUIEvent event(PR_TRUE, NS_XUL_CLOSE, this);

    event.refPoint.x = 0;
    event.refPoint.y = 0;

    nsEventStatus status;
    DispatchEvent(&event, status);
}

void
nsWindow::OnEnterNotifyEvent(GtkWidget *aWidget, GdkEventCrossing *aEvent)
{
    // This skips NotifyVirtual and NotifyNonlinearVirtual enter notify events
    // when the pointer enters a child window.  If the destination window is a
    // Gecko window then we'll catch the corresponding event on that window,
    // but we won't notice when the pointer directly enters a foreign (plugin)
    // child window without passing over a visible portion of a Gecko window.
    if (aEvent->subwindow != NULL)
        return;

    // Check before is_parent_ungrab_enter() as the button state may have
    // changed while a non-Gecko ancestor window had a pointer grab.
    DispatchMissedButtonReleases(aEvent);

    if (is_parent_ungrab_enter(aEvent))
        return;

    nsMouseEvent event(PR_TRUE, NS_MOUSE_ENTER, this, nsMouseEvent::eReal);

    event.refPoint.x = nscoord(aEvent->x);
    event.refPoint.y = nscoord(aEvent->y);

    event.time = aEvent->time;

    LOG(("OnEnterNotify: %p\n", (void *)this));

    nsEventStatus status;
    DispatchEvent(&event, status);
}

// XXX Is this the right test for embedding cases?
static PRBool
is_top_level_mouse_exit(GdkWindow* aWindow, GdkEventCrossing *aEvent)
{
    gint x = gint(aEvent->x_root);
    gint y = gint(aEvent->y_root);
    GdkDisplay* display = gdk_window_get_display(aWindow);
    GdkWindow* winAtPt = gdk_display_get_window_at_pointer(display, &x, &y);
    if (!winAtPt)
        return PR_TRUE;
    GdkWindow* topLevelAtPt = gdk_window_get_toplevel(winAtPt);
    GdkWindow* topLevelWidget = gdk_window_get_toplevel(aWindow);
    return topLevelAtPt != topLevelWidget;
}

void
nsWindow::OnLeaveNotifyEvent(GtkWidget *aWidget, GdkEventCrossing *aEvent)
{
    // This ignores NotifyVirtual and NotifyNonlinearVirtual leave notify
    // events when the pointer leaves a child window.  If the destination
    // window is a Gecko window then we'll catch the corresponding event on
    // that window.
    //
    // XXXkt However, we will miss toplevel exits when the pointer directly
    // leaves a foreign (plugin) child window without passing over a visible
    // portion of a Gecko window.
    if (aEvent->subwindow != NULL)
        return;

    nsMouseEvent event(PR_TRUE, NS_MOUSE_EXIT, this, nsMouseEvent::eReal);

    event.refPoint.x = nscoord(aEvent->x);
    event.refPoint.y = nscoord(aEvent->y);

    event.time = aEvent->time;

    event.exit = is_top_level_mouse_exit(mGdkWindow, aEvent)
        ? nsMouseEvent::eTopLevel : nsMouseEvent::eChild;

    LOG(("OnLeaveNotify: %p\n", (void *)this));

    nsEventStatus status;
    DispatchEvent(&event, status);
}

#ifdef MOZ_DFB
void
nsWindow::OnMotionNotifyEvent(GtkWidget *aWidget, GdkEventMotion *aEvent)
{
    int cursorX = (int) aEvent->x_root;
    int cursorY = (int) aEvent->y_root;

    D_DEBUG_AT( ns_Window, "%s( %4d,%4d - [%d] )\n", __FUNCTION__, cursorX, cursorY, mDFBCursorCount );

    D_ASSUME( mDFBLayer != NULL );

    if (mDFBLayer)
         mDFBLayer->GetCursorPosition( mDFBLayer, &cursorX, &cursorY );

    mDFBCursorCount++;

#if D_DEBUG_ENABLED
    if (cursorX != (int) aEvent->x_root || cursorY != (int) aEvent->y_root)
         D_DEBUG_AT( ns_Window, "  -> forward to %4d,%4d\n", cursorX, cursorY );
#endif

    if (cursorX == mDFBCursorX && cursorY == mDFBCursorY) {
         D_DEBUG_AT( ns_Window, "  -> dropping %4d,%4d\n", cursorX, cursorY );

         /* drop zero motion */
         return;
    }

    mDFBCursorX = cursorX;
    mDFBCursorY = cursorY;


    // when we receive this, it must be that the gtk dragging is over,
    // it is dropped either in or out of mozilla, clear the flag
    sIsDraggingOutOf = PR_FALSE;

    nsMouseEvent event(PR_TRUE, NS_MOUSE_MOVE, this, nsMouseEvent::eReal);

    // should we move this into !synthEvent?
    gdouble pressure = 0;
    gdk_event_get_axis ((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
    // Sometime gdk generate 0 pressure value between normal values
    // We have to ignore that and use last valid value
    if (pressure)
      mLastMotionPressure = pressure;
    event.pressure = mLastMotionPressure;

    event.refPoint = nsIntPoint(cursorX, cursorY) - WidgetToScreenOffset();

    event.isShift   = (aEvent->state & GDK_SHIFT_MASK)
        ? PR_TRUE : PR_FALSE;
    event.isControl = (aEvent->state & GDK_CONTROL_MASK)
        ? PR_TRUE : PR_FALSE;
    event.isAlt     = (aEvent->state & GDK_MOD1_MASK)
        ? PR_TRUE : PR_FALSE;

    event.time = aEvent->time;

    nsEventStatus status;
    DispatchEvent(&event, status);
}
#else
void
nsWindow::OnMotionNotifyEvent(GtkWidget *aWidget, GdkEventMotion *aEvent)
{
    // when we receive this, it must be that the gtk dragging is over,
    // it is dropped either in or out of mozilla, clear the flag
    sIsDraggingOutOf = PR_FALSE;

    // see if we can compress this event
    // XXXldb Why skip every other motion event when we have multiple,
    // but not more than that?
    PRPackedBool synthEvent = PR_FALSE;
#ifdef MOZ_X11
    XEvent xevent;

    while (XPending (GDK_WINDOW_XDISPLAY(aEvent->window))) {
        XEvent peeked;
        XPeekEvent (GDK_WINDOW_XDISPLAY(aEvent->window), &peeked);
        if (peeked.xany.window != gdk_x11_window_get_xid(aEvent->window)
            || peeked.type != MotionNotify)
            break;

        synthEvent = PR_TRUE;
        XNextEvent (GDK_WINDOW_XDISPLAY(aEvent->window), &xevent);
    }
#if defined(MOZ_WIDGET_GTK2)
    // if plugins still keeps the focus, get it back
    if (gPluginFocusWindow && gPluginFocusWindow != this) {
        nsRefPtr<nsWindow> kungFuDeathGrip = gPluginFocusWindow;
        gPluginFocusWindow->LoseNonXEmbedPluginFocus();
    }
#endif /* MOZ_WIDGET_GTK2 */
#endif /* MOZ_X11 */

    nsMouseEvent event(PR_TRUE, NS_MOUSE_MOVE, this, nsMouseEvent::eReal);

    gdouble pressure = 0;
    gdk_event_get_axis ((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
    // Sometime gdk generate 0 pressure value between normal values
    // We have to ignore that and use last valid value
    if (pressure)
      mLastMotionPressure = pressure;
    event.pressure = mLastMotionPressure;

    if (synthEvent) {
#ifdef MOZ_X11
        event.refPoint.x = nscoord(xevent.xmotion.x);
        event.refPoint.y = nscoord(xevent.xmotion.y);

        event.isShift   = (xevent.xmotion.state & GDK_SHIFT_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isControl = (xevent.xmotion.state & GDK_CONTROL_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isAlt     = (xevent.xmotion.state & GDK_MOD1_MASK)
            ? PR_TRUE : PR_FALSE;

        event.time = xevent.xmotion.time;
#else
        event.refPoint.x = nscoord(aEvent->x);
        event.refPoint.y = nscoord(aEvent->y);

        event.isShift   = (aEvent->state & GDK_SHIFT_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isControl = (aEvent->state & GDK_CONTROL_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isAlt     = (aEvent->state & GDK_MOD1_MASK)
            ? PR_TRUE : PR_FALSE;

        event.time = aEvent->time;
#endif /* MOZ_X11 */
    }
    else {
        // XXX see OnScrollEvent()
        if (aEvent->window == mGdkWindow) {
            event.refPoint.x = nscoord(aEvent->x);
            event.refPoint.y = nscoord(aEvent->y);
        } else {
            nsIntPoint point(NSToIntFloor(aEvent->x_root), NSToIntFloor(aEvent->y_root));
            event.refPoint = point - WidgetToScreenOffset();
        }

        event.isShift   = (aEvent->state & GDK_SHIFT_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isControl = (aEvent->state & GDK_CONTROL_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isAlt     = (aEvent->state & GDK_MOD1_MASK)
            ? PR_TRUE : PR_FALSE;

        event.time = aEvent->time;
    }

    nsEventStatus status;
    DispatchEvent(&event, status);
}
#endif

// If the automatic pointer grab on ButtonPress has deactivated before
// ButtonRelease, and the mouse button is released while the pointer is not
// over any a Gecko window, then the ButtonRelease event will not be received.
// (A similar situation exists when the pointer is grabbed with owner_events
// True as the ButtonRelease may be received on a foreign [plugin] window).
// Use this method to check for released buttons when the pointer returns to a
// Gecko window.
void
nsWindow::DispatchMissedButtonReleases(GdkEventCrossing *aGdkEvent)
{
    guint changed = aGdkEvent->state ^ gButtonState;
    // Only consider button releases.
    // (Ignore button presses that occurred outside Gecko.)
    guint released = changed & gButtonState;
    gButtonState = aGdkEvent->state;

    // Loop over each button, excluding mouse wheel buttons 4 and 5 for which
    // GDK ignores releases.
    for (guint buttonMask = GDK_BUTTON1_MASK;
         buttonMask <= GDK_BUTTON3_MASK;
         buttonMask <<= 1) {

        if (released & buttonMask) {
            PRInt16 buttonType;
            switch (buttonMask) {
            case GDK_BUTTON1_MASK:
                buttonType = nsMouseEvent::eLeftButton;
                break;
            case GDK_BUTTON2_MASK:
                buttonType = nsMouseEvent::eMiddleButton;
                break;
            default:
                NS_ASSERTION(buttonMask == GDK_BUTTON3_MASK,
                             "Unexpected button mask");
                buttonType = nsMouseEvent::eRightButton;
            }

            LOG(("Synthesized button %u release on %p\n",
                 guint(buttonType + 1), (void *)this));

            // Dispatch a synthesized button up event to tell Gecko about the
            // change in state.  This event is marked as synthesized so that
            // it is not dispatched as a DOM event, because we don't know the
            // position, widget, modifiers, or time/order.
            nsMouseEvent synthEvent(PR_TRUE, NS_MOUSE_BUTTON_UP, this,
                                    nsMouseEvent::eSynthesized);
            synthEvent.button = buttonType;
            nsEventStatus status;
            DispatchEvent(&synthEvent, status);

            sLastButtonReleaseTime = aGdkEvent->time;
        }
    }
}

void
nsWindow::InitButtonEvent(nsMouseEvent &aEvent,
                          GdkEventButton *aGdkEvent)
{
    // XXX see OnScrollEvent()
    if (aGdkEvent->window == mGdkWindow) {
        aEvent.refPoint.x = nscoord(aGdkEvent->x);
        aEvent.refPoint.y = nscoord(aGdkEvent->y);
    } else {
        nsIntPoint point(NSToIntFloor(aGdkEvent->x_root), NSToIntFloor(aGdkEvent->y_root));
        aEvent.refPoint = point - WidgetToScreenOffset();
    }

    aEvent.isShift   = (aGdkEvent->state & GDK_SHIFT_MASK) != 0;
    aEvent.isControl = (aGdkEvent->state & GDK_CONTROL_MASK) != 0;
    aEvent.isAlt     = (aGdkEvent->state & GDK_MOD1_MASK) != 0;
    aEvent.isMeta    = (aGdkEvent->state & GDK_MOD4_MASK) != 0;

    aEvent.time = aGdkEvent->time;

    switch (aGdkEvent->type) {
    case GDK_2BUTTON_PRESS:
        aEvent.clickCount = 2;
        break;
    case GDK_3BUTTON_PRESS:
        aEvent.clickCount = 3;
        break;
        // default is one click
    default:
        aEvent.clickCount = 1;
    }
}

static guint ButtonMaskFromGDKButton(guint button)
{
    return GDK_BUTTON1_MASK << (button - 1);
}

void
nsWindow::OnButtonPressEvent(GtkWidget *aWidget, GdkEventButton *aEvent)
{
    LOG(("Button %u press on %p\n", aEvent->button, (void *)this));

    nsEventStatus status;

    // If you double click in GDK, it will actually generate a second
    // GDK_BUTTON_PRESS before sending the GDK_2BUTTON_PRESS, and this is
    // different than the DOM spec.  GDK puts this in the queue
    // programatically, so it's safe to assume that if there's a
    // double click in the queue, it was generated so we can just drop
    // this click.
    GdkEvent *peekedEvent = gdk_event_peek();
    if (peekedEvent) {
        GdkEventType type = peekedEvent->any.type;
        gdk_event_free(peekedEvent);
        if (type == GDK_2BUTTON_PRESS || type == GDK_3BUTTON_PRESS)
            return;
    }

    // Always save the time of this event
    sLastButtonPressTime = aEvent->time;
    sLastButtonReleaseTime = 0;

    nsWindow *containerWindow = GetContainerWindow();
    if (!gFocusWindow && containerWindow) {
        containerWindow->DispatchActivateEvent();
    }

    // check to see if we should rollup
    PRBool rolledUp = check_for_rollup(aEvent->window, aEvent->x_root,
                                       aEvent->y_root, PR_FALSE, PR_FALSE);
    if (gConsumeRollupEvent && rolledUp)
        return;

    gdouble pressure = 0;
    gdk_event_get_axis ((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
    mLastMotionPressure = pressure;

    PRUint16 domButton;
    switch (aEvent->button) {
    case 1:
        domButton = nsMouseEvent::eLeftButton;
        break;
    case 2:
        domButton = nsMouseEvent::eMiddleButton;
        break;
    case 3:
        domButton = nsMouseEvent::eRightButton;
        break;
    // These are mapped to horizontal scroll
    case 6:
    case 7:
        {
            nsMouseScrollEvent event(PR_TRUE, NS_MOUSE_SCROLL, this);
            event.pressure = mLastMotionPressure;
            event.scrollFlags = nsMouseScrollEvent::kIsHorizontal;
            event.refPoint.x = nscoord(aEvent->x);
            event.refPoint.y = nscoord(aEvent->y);
            // XXX Why is this delta value different from the scroll event?
            event.delta = (aEvent->button == 6) ? -2 : 2;

            event.isShift   = (aEvent->state & GDK_SHIFT_MASK) != 0;
            event.isControl = (aEvent->state & GDK_CONTROL_MASK) != 0;
            event.isAlt     = (aEvent->state & GDK_MOD1_MASK) != 0;
            event.isMeta    = (aEvent->state & GDK_MOD4_MASK) != 0;

            event.time = aEvent->time;

            nsEventStatus status;
            DispatchEvent(&event, status);
            return;
        }
    // Map buttons 8-9 to back/forward
    case 8:
        DispatchCommandEvent(nsWidgetAtoms::Back);
        return;
    case 9:
        DispatchCommandEvent(nsWidgetAtoms::Forward);
        return;
    default:
        return;
    }

    gButtonState |= ButtonMaskFromGDKButton(aEvent->button);

    nsMouseEvent event(PR_TRUE, NS_MOUSE_BUTTON_DOWN, this, nsMouseEvent::eReal);
    event.button = domButton;
    InitButtonEvent(event, aEvent);
    event.pressure = mLastMotionPressure;

    DispatchEvent(&event, status);

    // right menu click on linux should also pop up a context menu
    if (domButton == nsMouseEvent::eRightButton &&
        NS_LIKELY(!mIsDestroyed)) {
        nsMouseEvent contextMenuEvent(PR_TRUE, NS_CONTEXTMENU, this,
                                      nsMouseEvent::eReal);
        InitButtonEvent(contextMenuEvent, aEvent);
        contextMenuEvent.pressure = mLastMotionPressure;
        DispatchEvent(&contextMenuEvent, status);
    }
}

void
nsWindow::OnButtonReleaseEvent(GtkWidget *aWidget, GdkEventButton *aEvent)
{
    LOG(("Button %u release on %p\n", aEvent->button, (void *)this));

    PRUint16 domButton;
    sLastButtonReleaseTime = aEvent->time;

    switch (aEvent->button) {
    case 1:
        domButton = nsMouseEvent::eLeftButton;
        break;
    case 2:
        domButton = nsMouseEvent::eMiddleButton;
        break;
    case 3:
        domButton = nsMouseEvent::eRightButton;
        break;
    default:
        return;
    }

    gButtonState &= ~ButtonMaskFromGDKButton(aEvent->button);

    nsMouseEvent event(PR_TRUE, NS_MOUSE_BUTTON_UP, this, nsMouseEvent::eReal);
    event.button = domButton;
    InitButtonEvent(event, aEvent);
    gdouble pressure = 0;
    gdk_event_get_axis ((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
    event.pressure = pressure ? pressure : mLastMotionPressure;

    nsEventStatus status;
    DispatchEvent(&event, status);
    mLastMotionPressure = pressure;
}

void
nsWindow::OnContainerFocusInEvent(GtkWidget *aWidget, GdkEventFocus *aEvent)
{
    NS_ASSERTION(mWindowType != eWindowType_popup,
                 "Unexpected focus on a popup window");

    LOGFOCUS(("OnContainerFocusInEvent [%p]\n", (void *)this));

    // Unset the urgency hint, if possible
    GtkWidget* top_window = nsnull;
    GetToplevelWidget(&top_window);
    if (top_window && (gtk_widget_get_visible(top_window)))
        SetUrgencyHint(top_window, PR_FALSE);

    // Return if being called within SetFocus because the focus manager
    // already knows that the window is active.
    if (gBlockActivateEvent) {
        LOGFOCUS(("NS_ACTIVATE event is blocked [%p]\n", (void *)this));
        return;
    }

    // This is not usually the correct window for dispatching key events,
    // but the focus manager will call SetFocus to set the correct window if
    // keyboard input will be accepted.  Setting a non-NULL value here
    // prevents OnButtonPressEvent() from dispatching NS_ACTIVATE if the
    // widget is already active.
    gFocusWindow = this;

    DispatchActivateEvent();

    LOGFOCUS(("Events sent from focus in event [%p]\n", (void *)this));
}

void
nsWindow::OnContainerFocusOutEvent(GtkWidget *aWidget, GdkEventFocus *aEvent)
{
    LOGFOCUS(("OnContainerFocusOutEvent [%p]\n", (void *)this));

    if (mWindowType == eWindowType_toplevel || mWindowType == eWindowType_dialog) {
        nsCOMPtr<nsIDragService> dragService = do_GetService(kCDragServiceCID);
        nsCOMPtr<nsIDragSession> dragSession;
        dragService->GetCurrentSession(getter_AddRefs(dragSession));

        // Rollup popups when a window is focused out unless a drag is occurring.
        // This check is because drags grab the keyboard and cause a focus out on
        // versions of GTK before 2.18.
        PRBool shouldRollup = !dragSession;
        if (!shouldRollup) {
            // we also roll up when a drag is from a different application
            nsCOMPtr<nsIDOMNode> sourceNode;
            dragSession->GetSourceNode(getter_AddRefs(sourceNode));
            shouldRollup = (sourceNode == nsnull);
        }

        if (shouldRollup) {
            check_for_rollup(aEvent->window, 0, 0, PR_FALSE, PR_TRUE);
        }
    }

#if defined(MOZ_WIDGET_GTK2) && defined(MOZ_X11)
    // plugin lose focus
    if (gPluginFocusWindow) {
        nsRefPtr<nsWindow> kungFuDeathGrip = gPluginFocusWindow;
        gPluginFocusWindow->LoseNonXEmbedPluginFocus();
    }
#endif /* MOZ_X11 && MOZ_WIDGET_GTK2 */

    if (gFocusWindow) {
        nsRefPtr<nsWindow> kungFuDeathGrip = gFocusWindow;
        if (gFocusWindow->mIMModule) {
            gFocusWindow->mIMModule->OnBlurWindow(gFocusWindow);
        }
        gFocusWindow = nsnull;
    }

    DispatchDeactivateEvent();

    LOGFOCUS(("Done with container focus out [%p]\n", (void *)this));
}

PRBool
nsWindow::DispatchCommandEvent(nsIAtom* aCommand)
{
    nsEventStatus status;
    nsCommandEvent event(PR_TRUE, nsWidgetAtoms::onAppCommand, aCommand, this);
    DispatchEvent(&event, status);
    return TRUE;
}

PRBool
nsWindow::DispatchContentCommandEvent(PRInt32 aMsg)
{
  nsEventStatus status;
  nsContentCommandEvent event(PR_TRUE, aMsg, this);
  DispatchEvent(&event, status);
  return TRUE;
}

static PRUint32
GetCharCodeFor(const GdkEventKey *aEvent, guint aShiftState,
               gint aGroup)
{
    guint keyval;
    GdkKeymap *keymap = gdk_keymap_get_default();

    if (gdk_keymap_translate_keyboard_state(keymap, aEvent->hardware_keycode,
                                            GdkModifierType(aShiftState),
                                            aGroup,
                                            &keyval, NULL, NULL, NULL)) {
        GdkEventKey tmpEvent = *aEvent;
        tmpEvent.state = guint(aShiftState);
        tmpEvent.keyval = keyval;
        tmpEvent.group = aGroup;
        return nsConvertCharCodeToUnicode(&tmpEvent);
    }
    return 0;
}

static gint
GetKeyLevel(GdkEventKey *aEvent)
{
    gint level;
    GdkKeymap *keymap = gdk_keymap_get_default();

    if (!gdk_keymap_translate_keyboard_state(keymap,
                                             aEvent->hardware_keycode,
                                             GdkModifierType(aEvent->state),
                                             aEvent->group,
                                             NULL, NULL, &level, NULL))
        return -1;
    return level;
}

static PRBool
IsBasicLatinLetterOrNumeral(PRUint32 aChar)
{
    return (aChar >= 'a' && aChar <= 'z') ||
           (aChar >= 'A' && aChar <= 'Z') ||
           (aChar >= '0' && aChar <= '9');
}

static PRBool
IsCtrlAltTab(GdkEventKey *aEvent)
{
    return aEvent->keyval == GDK_Tab &&
        aEvent->state & GDK_CONTROL_MASK && aEvent->state & GDK_MOD1_MASK;
}

PRBool
nsWindow::DispatchKeyDownEvent(GdkEventKey *aEvent, PRBool *aCancelled)
{
    NS_PRECONDITION(aCancelled, "aCancelled must not be null");

    *aCancelled = PR_FALSE;

    if (IsCtrlAltTab(aEvent)) {
        return PR_FALSE;
    }

    // send the key down event
    nsEventStatus status;
    nsKeyEvent downEvent(PR_TRUE, NS_KEY_DOWN, this);
    InitKeyEvent(downEvent, aEvent);
    DispatchEvent(&downEvent, status);
    *aCancelled = (status == nsEventStatus_eConsumeNoDefault);
    return PR_TRUE;
}

gboolean
nsWindow::OnKeyPressEvent(GtkWidget *aWidget, GdkEventKey *aEvent)
{
    LOGFOCUS(("OnKeyPressEvent [%p]\n", (void *)this));

    // if we are in the middle of composing text, XIM gets to see it
    // before mozilla does.
    PRBool IMEWasEnabled = PR_FALSE;
    if (mIMModule) {
        IMEWasEnabled = mIMModule->IsEnabled();
        if (mIMModule->OnKeyEvent(this, aEvent)) {
            return TRUE;
        }
    }

    nsEventStatus status;

    // work around for annoying things.
    if (IsCtrlAltTab(aEvent)) {
        return TRUE;
    }

    nsCOMPtr<nsIWidget> kungFuDeathGrip = this;

    // Dispatch keydown event always.  At auto repeating, we should send
    // KEYDOWN -> KEYPRESS -> KEYDOWN -> KEYPRESS ... -> KEYUP
    // However, old distributions (e.g., Ubuntu 9.10) sent native key
    // release event, so, on such platform, the DOM events will be:
    // KEYDOWN -> KEYPRESS -> KEYUP -> KEYDOWN -> KEYPRESS -> KEYUP...

    PRBool isKeyDownCancelled = PR_FALSE;
    if (DispatchKeyDownEvent(aEvent, &isKeyDownCancelled) &&
        NS_UNLIKELY(mIsDestroyed)) {
        return TRUE;
    }

    // If a keydown event handler causes to enable IME, i.e., it moves
    // focus from IME unusable content to IME usable editor, we should
    // send the native key event to IME for the first input on the editor.
    if (!IMEWasEnabled && mIMModule && mIMModule->IsEnabled()) {
        // Notice our keydown event was already dispatched.  This prevents
        // unnecessary DOM keydown event in the editor.
        if (mIMModule->OnKeyEvent(this, aEvent, PR_TRUE)) {
            return TRUE;
        }
    }

    // Don't pass modifiers as NS_KEY_PRESS events.
    // TODO: Instead of selectively excluding some keys from NS_KEY_PRESS events,
    //       we should instead selectively include (as per MSDN spec; no official
    //       spec covers KeyPress events).
    if (aEvent->keyval == GDK_Shift_L
        || aEvent->keyval == GDK_Shift_R
        || aEvent->keyval == GDK_Control_L
        || aEvent->keyval == GDK_Control_R
        || aEvent->keyval == GDK_Alt_L
        || aEvent->keyval == GDK_Alt_R
        || aEvent->keyval == GDK_Meta_L
        || aEvent->keyval == GDK_Meta_R) {
        return TRUE;
    }

#ifdef MOZ_X11
#if ! defined AIX // no XFree86 on AIX 5L
    // Look for specialized app-command keys
    switch (aEvent->keyval) {
        case XF86XK_Back:
            return DispatchCommandEvent(nsWidgetAtoms::Back);
        case XF86XK_Forward:
            return DispatchCommandEvent(nsWidgetAtoms::Forward);
        case XF86XK_Refresh:
            return DispatchCommandEvent(nsWidgetAtoms::Reload);
        case XF86XK_Stop:
            return DispatchCommandEvent(nsWidgetAtoms::Stop);
        case XF86XK_Search:
            return DispatchCommandEvent(nsWidgetAtoms::Search);
        case XF86XK_Favorites:
            return DispatchCommandEvent(nsWidgetAtoms::Bookmarks);
        case XF86XK_HomePage:
            return DispatchCommandEvent(nsWidgetAtoms::Home);
        case XF86XK_Copy:
        case GDK_F16:  // F16, F20, F18, F14 are old keysyms for Copy Cut Paste Undo
            return DispatchContentCommandEvent(NS_CONTENT_COMMAND_COPY);
        case XF86XK_Cut:
        case GDK_F20:
            return DispatchContentCommandEvent(NS_CONTENT_COMMAND_CUT);
        case XF86XK_Paste:
        case GDK_F18:
            return DispatchContentCommandEvent(NS_CONTENT_COMMAND_PASTE);
        case GDK_Redo:
            return DispatchContentCommandEvent(NS_CONTENT_COMMAND_REDO);
        case GDK_Undo:
        case GDK_F14:
            return DispatchContentCommandEvent(NS_CONTENT_COMMAND_UNDO);
    }
#endif /* ! AIX */
#endif /* MOZ_X11 */

    nsKeyEvent event(PR_TRUE, NS_KEY_PRESS, this);
    InitKeyEvent(event, aEvent);
    if (isKeyDownCancelled) {
      // If prevent default set for onkeydown, do the same for onkeypress
      event.flags |= NS_EVENT_FLAG_NO_DEFAULT;
    }
    event.charCode = nsConvertCharCodeToUnicode(aEvent);
    if (event.charCode) {
        event.keyCode = 0;
        gint level = GetKeyLevel(aEvent);
        if ((event.isControl || event.isAlt || event.isMeta) &&
            (level == 0 || level == 1)) {
            guint baseState =
                aEvent->state & ~(GDK_SHIFT_MASK | GDK_CONTROL_MASK |
                                  GDK_MOD1_MASK | GDK_MOD4_MASK);
            // We shold send both shifted char and unshifted char,
            // all keyboard layout users can use all keys.
            // Don't change event.charCode. On some keyboard layouts,
            // ctrl/alt/meta keys are used for inputting some characters.
            nsAlternativeCharCode altCharCodes(0, 0);
            // unshifted charcode of current keyboard layout.
            altCharCodes.mUnshiftedCharCode =
                GetCharCodeFor(aEvent, baseState, aEvent->group);
            PRBool isLatin = (altCharCodes.mUnshiftedCharCode <= 0xFF);
            // shifted charcode of current keyboard layout.
            altCharCodes.mShiftedCharCode =
                GetCharCodeFor(aEvent, baseState | GDK_SHIFT_MASK,
                               aEvent->group);
            isLatin = isLatin && (altCharCodes.mShiftedCharCode <= 0xFF);
            if (altCharCodes.mUnshiftedCharCode ||
                altCharCodes.mShiftedCharCode) {
                event.alternativeCharCodes.AppendElement(altCharCodes);
            }

            if (!isLatin) {
                // Next, find latin inputtable keyboard layout.
                GdkKeymapKey *keys;
                gint count;
                gint minGroup = -1;
                if (gdk_keymap_get_entries_for_keyval(NULL, GDK_a,
                                                      &keys, &count)) {
                    // find the minimum number group for latin inputtable layout
                    for (gint i = 0; i < count && minGroup != 0; ++i) {
                        if (keys[i].level != 0 && keys[i].level != 1)
                            continue;
                        if (minGroup >= 0 && keys[i].group > minGroup)
                            continue;
                        minGroup = keys[i].group;
                    }
                    g_free(keys);
                }
                if (minGroup >= 0) {
                    PRUint32 unmodifiedCh =
                               event.isShift ? altCharCodes.mShiftedCharCode :
                                               altCharCodes.mUnshiftedCharCode;
                    // unshifted charcode of found keyboard layout.
                    PRUint32 ch =
                        GetCharCodeFor(aEvent, baseState, minGroup);
                    altCharCodes.mUnshiftedCharCode =
                        IsBasicLatinLetterOrNumeral(ch) ? ch : 0;
                    // shifted charcode of found keyboard layout.
                    ch = GetCharCodeFor(aEvent, baseState | GDK_SHIFT_MASK,
                                        minGroup);
                    altCharCodes.mShiftedCharCode =
                        IsBasicLatinLetterOrNumeral(ch) ? ch : 0;
                    if (altCharCodes.mUnshiftedCharCode ||
                        altCharCodes.mShiftedCharCode) {
                        event.alternativeCharCodes.AppendElement(altCharCodes);
                    }
                    // If the charCode is not Latin, and the level is 0 or 1,
                    // we should replace the charCode to Latin char if Alt and
                    // Meta keys are not pressed. (Alt should be sent the
                    // localized char for accesskey like handling of Web
                    // Applications.)
                    ch = event.isShift ? altCharCodes.mShiftedCharCode :
                                         altCharCodes.mUnshiftedCharCode;
                    if (ch && !(event.isAlt || event.isMeta) &&
                        event.charCode == unmodifiedCh) {
                        event.charCode = ch;
                    }
                }
            }
        }
    }

    // before we dispatch a key, check if it's the context menu key.
    // If so, send a context menu key event instead.
    if (is_context_menu_key(event)) {
        nsMouseEvent contextMenuEvent(PR_TRUE, NS_CONTEXTMENU, this,
                                      nsMouseEvent::eReal,
                                      nsMouseEvent::eContextMenuKey);
        key_event_to_context_menu_event(contextMenuEvent, aEvent);
        DispatchEvent(&contextMenuEvent, status);
    }
    else {
        // If the character code is in the BMP, send the key press event.
        // Otherwise, send a text event with the equivalent UTF-16 string.
        if (IS_IN_BMP(event.charCode)) {
            DispatchEvent(&event, status);
        }
        else {
            nsTextEvent textEvent(PR_TRUE, NS_TEXT_TEXT, this);
            PRUnichar textString[3];
            textString[0] = H_SURROGATE(event.charCode);
            textString[1] = L_SURROGATE(event.charCode);
            textString[2] = 0;
            textEvent.theText = textString;
            textEvent.time = event.time;
            DispatchEvent(&textEvent, status);
        }
    }

    // If the event was consumed, return.
    if (status == nsEventStatus_eConsumeNoDefault) {
        return TRUE;
    }

    return FALSE;
}

gboolean
nsWindow::OnKeyReleaseEvent(GtkWidget *aWidget, GdkEventKey *aEvent)
{
    LOGFOCUS(("OnKeyReleaseEvent [%p]\n", (void *)this));

    if (mIMModule && mIMModule->OnKeyEvent(this, aEvent)) {
        return TRUE;
    }

    // send the key event as a key up event
    nsKeyEvent event(PR_TRUE, NS_KEY_UP, this);
    InitKeyEvent(event, aEvent);

    nsEventStatus status;
    DispatchEvent(&event, status);

    // If the event was consumed, return.
    if (status == nsEventStatus_eConsumeNoDefault) {
        return TRUE;
    }

    return FALSE;
}

void
nsWindow::OnScrollEvent(GtkWidget *aWidget, GdkEventScroll *aEvent)
{
    // check to see if we should rollup
    PRBool rolledUp =  check_for_rollup(aEvent->window, aEvent->x_root,
                                        aEvent->y_root, PR_TRUE, PR_FALSE);
    if (gConsumeRollupEvent && rolledUp)
        return;

    nsMouseScrollEvent event(PR_TRUE, NS_MOUSE_SCROLL, this);
    switch (aEvent->direction) {
    case GDK_SCROLL_UP:
        event.scrollFlags = nsMouseScrollEvent::kIsVertical;
        event.delta = -3;
        break;
    case GDK_SCROLL_DOWN:
        event.scrollFlags = nsMouseScrollEvent::kIsVertical;
        event.delta = 3;
        break;
    case GDK_SCROLL_LEFT:
        event.scrollFlags = nsMouseScrollEvent::kIsHorizontal;
        event.delta = -1;
        break;
    case GDK_SCROLL_RIGHT:
        event.scrollFlags = nsMouseScrollEvent::kIsHorizontal;
        event.delta = 1;
        break;
    }

    if (aEvent->window == mGdkWindow) {
        // we are the window that the event happened on so no need for expensive WidgetToScreenOffset
        event.refPoint.x = nscoord(aEvent->x);
        event.refPoint.y = nscoord(aEvent->y);
    } else {
        // XXX we're never quite sure which GdkWindow the event came from due to our custom bubbling
        // in scroll_event_cb(), so use ScreenToWidget to translate the screen root coordinates into
        // coordinates relative to this widget.
        nsIntPoint point(NSToIntFloor(aEvent->x_root), NSToIntFloor(aEvent->y_root));
        event.refPoint = point - WidgetToScreenOffset();
    }

    event.isShift   = (aEvent->state & GDK_SHIFT_MASK) != 0;
    event.isControl = (aEvent->state & GDK_CONTROL_MASK) != 0;
    event.isAlt     = (aEvent->state & GDK_MOD1_MASK) != 0;
    event.isMeta    = (aEvent->state & GDK_MOD4_MASK) != 0;

    event.time = aEvent->time;

    nsEventStatus status;
    DispatchEvent(&event, status);
}

void
nsWindow::OnVisibilityNotifyEvent(GtkWidget *aWidget,
                                  GdkEventVisibility *aEvent)
{
    LOGDRAW(("Visibility event %i on [%p] %p\n",
             aEvent->state, this, aEvent->window));

    if (!mGdkWindow)
        return;

    switch (aEvent->state) {
    case GDK_VISIBILITY_UNOBSCURED:
    case GDK_VISIBILITY_PARTIAL:
        if (mIsFullyObscured && mHasMappedToplevel) {
            // GDK_EXPOSE events have been ignored, so make sure GDK
            // doesn't think that the window has already been painted.
            gdk_window_invalidate_rect(mGdkWindow, NULL, FALSE);
        }

        mIsFullyObscured = PR_FALSE;

        // In Hildon/Maemo, a browser window will get into 'patially visible' state wheneven an
        // autocomplete feature is dropped down (from urlbar or from an entry form completion),
        // and there are no much further ways for that to happen in the plaftorm. In such cases, if hildon
        // virtual keyboard is up, we can not grab focus to any dropdown list. Reason: nsWindow::EnsureGrabs()
        // calls gdk_pointer_grab() which grabs the pointer (usually a mouse) so that all events are passed
        // to this it until the pointer is ungrabbed.
        if (!nsGtkIMModule::IsVirtualKeyboardOpened()) {
            // if we have to retry the grab, retry it.
            EnsureGrabs();
        }
        break;
    default: // includes GDK_VISIBILITY_FULLY_OBSCURED
        mIsFullyObscured = PR_TRUE;
        break;
    }
}

void
nsWindow::OnWindowStateEvent(GtkWidget *aWidget, GdkEventWindowState *aEvent)
{
    LOG(("nsWindow::OnWindowStateEvent [%p] changed %d new_window_state %d\n",
         (void *)this, aEvent->changed_mask, aEvent->new_window_state));

    if (IS_MOZ_CONTAINER(aWidget)) {
        // This event is notifying the container widget of changes to the
        // toplevel window.  Just detect changes affecting whether windows are
        // viewable.
        //
        // (A visibility notify event is sent to each window that becomes
        // viewable when the toplevel is mapped, but we can't rely on that for
        // setting mHasMappedToplevel because these toplevel window state
        // events are asynchronous.  The windows in the hierarchy now may not
        // be the same windows as when the toplevel was mapped, so they may
        // not get VisibilityNotify events.)
        PRBool mapped =
            !(aEvent->new_window_state &
              (GDK_WINDOW_STATE_ICONIFIED|GDK_WINDOW_STATE_WITHDRAWN));
        if (mHasMappedToplevel != mapped) {
            SetHasMappedToplevel(mapped);
        }
        return;
    }
    // else the widget is a shell widget.

    nsSizeModeEvent event(PR_TRUE, NS_SIZEMODE, this);

    // We don't care about anything but changes in the maximized/icon
    // states
    if ((aEvent->changed_mask
         & (GDK_WINDOW_STATE_ICONIFIED|GDK_WINDOW_STATE_MAXIMIZED)) == 0) {
        return;
    }

    if (aEvent->new_window_state & GDK_WINDOW_STATE_ICONIFIED) {
        LOG(("\tIconified\n"));
        event.mSizeMode = nsSizeMode_Minimized;
        mSizeState = nsSizeMode_Minimized;
#ifdef ACCESSIBILITY
        DispatchMinimizeEventAccessible();
#endif //ACCESSIBILITY
    }
    else if (aEvent->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) {
        LOG(("\tMaximized\n"));
        event.mSizeMode = nsSizeMode_Maximized;
        mSizeState = nsSizeMode_Maximized;
#ifdef ACCESSIBILITY
        DispatchMaximizeEventAccessible();
#endif //ACCESSIBILITY
    }
    else if (aEvent->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) {
        LOG(("\tFullscreen\n"));
        event.mSizeMode = nsSizeMode_Fullscreen;
        mSizeState = nsSizeMode_Fullscreen;
    }
    else {
        LOG(("\tNormal\n"));
        event.mSizeMode = nsSizeMode_Normal;
        mSizeState = nsSizeMode_Normal;
#ifdef ACCESSIBILITY
        DispatchRestoreEventAccessible();
#endif //ACCESSIBILITY
    }

    nsEventStatus status;
    DispatchEvent(&event, status);
}

void
nsWindow::ThemeChanged()
{
    nsGUIEvent event(PR_TRUE, NS_THEMECHANGED, this);
    nsEventStatus status = nsEventStatus_eIgnore;
    DispatchEvent(&event, status);

    if (!mGdkWindow || NS_UNLIKELY(mIsDestroyed))
        return;

    // Dispatch NS_THEMECHANGED to all child windows
    GList *children =
        gdk_window_peek_children(mGdkWindow);
    while (children) {
        GdkWindow *gdkWin = GDK_WINDOW(children->data);

        nsWindow *win = (nsWindow*) g_object_get_data(G_OBJECT(gdkWin),
                                                      "nsWindow");

        if (win && win != this) { // guard against infinite recursion
            nsRefPtr<nsWindow> kungFuDeathGrip = win;
            win->ThemeChanged();
        }

        children = children->next;
    }
}

void
nsWindow::CheckNeedDragLeaveEnter(nsWindow* aInnerMostWidget,
                                  nsIDragService* aDragService,
                                  GdkDragContext *aDragContext,
                                  nscoord aX, nscoord aY)
{
    // check to see if there was a drag motion window already in place
    if (sLastDragMotionWindow) {
        // same as the last window so no need for dragenter and dragleave events
        if (sLastDragMotionWindow == aInnerMostWidget) {
            UpdateDragStatus(aDragContext, aDragService);
            return;
        }

        // send a dragleave event to the last window that got a motion event
        nsRefPtr<nsWindow> kungFuDeathGrip = sLastDragMotionWindow;
        sLastDragMotionWindow->OnDragLeave();
    }

    // Make sure that the drag service knows we're now dragging
    aDragService->StartDragSession();

    // update our drag status and send a dragenter event to the window
    UpdateDragStatus(aDragContext, aDragService);
    aInnerMostWidget->OnDragEnter(aX, aY);

    // set the last window to the innerMostWidget
    sLastDragMotionWindow = aInnerMostWidget;
}

gboolean
nsWindow::OnDragMotionEvent(GtkWidget *aWidget,
                            GdkDragContext *aDragContext,
                            gint aX,
                            gint aY,
                            guint aTime,
                            gpointer aData)
{
    LOGDRAG(("nsWindow::OnDragMotionSignal\n"));

    if (sLastButtonReleaseTime) {
      // The drag ended before it was even setup to handle the end of the drag
      // So, we fake the button getting released again to release the drag
      GtkWidget *widget = gtk_grab_get_current();
      GdkEvent event;
      gboolean retval;
      memset(&event, 0, sizeof(event));
      event.type = GDK_BUTTON_RELEASE;
      event.button.time = sLastButtonReleaseTime;
      event.button.button = 1;
      sLastButtonReleaseTime = 0;
      if (widget) {
        g_signal_emit_by_name(widget, "button_release_event", &event, &retval);
        return TRUE;
      }
    }

    sIsDraggingOutOf = PR_FALSE;

    // get our drag context
    nsCOMPtr<nsIDragService> dragService = do_GetService(kCDragServiceCID);
    nsCOMPtr<nsIDragSessionGTK> dragSessionGTK = do_QueryInterface(dragService);

    // first, figure out which internal widget this drag motion actually
    // happened on
    nscoord retx = 0;
    nscoord rety = 0;

    GdkWindow *innerWindow = get_inner_gdk_window(gtk_widget_get_window(aWidget), aX, aY,
                                                  &retx, &rety);
    nsRefPtr<nsWindow> innerMostWidget = get_window_for_gdk_window(innerWindow);

    if (!innerMostWidget)
        innerMostWidget = this;

    // update the drag context
    dragSessionGTK->TargetSetLastContext(aWidget, aDragContext, aTime);

    // clear any drag leave timer that might be pending so that it
    // doesn't get processed when we actually go out to get data.
    if (mDragLeaveTimer) {
        mDragLeaveTimer->Cancel();
        mDragLeaveTimer = nsnull;
    }

    CheckNeedDragLeaveEnter(innerMostWidget, dragService, aDragContext, retx, rety);

    // notify the drag service that we are starting a drag motion.
    dragSessionGTK->TargetStartDragMotion();

    dragService->FireDragEventAtSource(NS_DRAGDROP_DRAG);

    nsDragEvent event(PR_TRUE, NS_DRAGDROP_OVER, innerMostWidget);

    InitDragEvent(event);

    event.refPoint.x = retx;
    event.refPoint.y = rety;
    event.time = aTime;

    nsEventStatus status;
    innerMostWidget->DispatchEvent(&event, status);

    // we're done with the drag motion event.  notify the drag service.
    dragSessionGTK->TargetEndDragMotion(aWidget, aDragContext, aTime);

    // and unset our context
    dragSessionGTK->TargetSetLastContext(0, 0, 0);

    return TRUE;
}

void
nsWindow::OnDragLeaveEvent(GtkWidget *aWidget,
                           GdkDragContext *aDragContext,
                           guint aTime,
                           gpointer aData)
{
    // XXX Do we want to pass this on only if the event's subwindow is null?

    LOGDRAG(("nsWindow::OnDragLeaveSignal(%p)\n", (void*)this));

    sIsDraggingOutOf = PR_TRUE;

    if (mDragLeaveTimer) {
        return;
    }

    // create a fast timer - we're delaying the drag leave until the
    // next mainloop in hopes that we might be able to get a drag drop
    // signal
    mDragLeaveTimer = do_CreateInstance("@mozilla.org/timer;1");
    NS_ASSERTION(mDragLeaveTimer, "Failed to create drag leave timer!");
    // fire this baby asafp, but not too quickly... see bug 216800 ;-)
    mDragLeaveTimer->InitWithFuncCallback(DragLeaveTimerCallback,
                                          (void *)this,
                                          20, nsITimer::TYPE_ONE_SHOT);
}

gboolean
nsWindow::OnDragDropEvent(GtkWidget *aWidget,
                          GdkDragContext *aDragContext,
                          gint aX,
                          gint aY,
                          guint aTime,
                          gpointer aData)

{
    LOGDRAG(("nsWindow::OnDragDropSignal\n"));

    // get our drag context
    nsCOMPtr<nsIDragService> dragService = do_GetService(kCDragServiceCID);
    nsCOMPtr<nsIDragSessionGTK> dragSessionGTK = do_QueryInterface(dragService);

    nscoord retx = 0;
    nscoord rety = 0;

    GdkWindow *innerWindow = get_inner_gdk_window(gtk_widget_get_window(aWidget), aX, aY,
                                                  &retx, &rety);
    nsRefPtr<nsWindow> innerMostWidget = get_window_for_gdk_window(innerWindow);

    if (!innerMostWidget)
        innerMostWidget = this;

    // set this now before any of the drag enter or leave events happen
    dragSessionGTK->TargetSetLastContext(aWidget, aDragContext, aTime);

    // clear any drag leave timer that might be pending so that it
    // doesn't get processed when we actually go out to get data.
    if (mDragLeaveTimer) {
        mDragLeaveTimer->Cancel();
        mDragLeaveTimer = nsnull;
    }

    CheckNeedDragLeaveEnter(innerMostWidget, dragService, aDragContext, retx, rety);

    // What we do here is dispatch a new drag motion event to
    // re-validate the drag target and then we do the drop.  The events
    // look the same except for the type.

    nsDragEvent event(PR_TRUE, NS_DRAGDROP_OVER, innerMostWidget);

    InitDragEvent(event);

    event.refPoint.x = retx;
    event.refPoint.y = rety;
    event.time = aTime;

    nsEventStatus status;
    innerMostWidget->DispatchEvent(&event, status);

    // We need to check innerMostWidget->mIsDestroyed here because the nsRefPtr
    // only protects innerMostWidget from being deleted, it does NOT protect
    // against nsView::~nsView() calling Destroy() on it, bug 378670.
    if (!innerMostWidget->mIsDestroyed) {
        nsDragEvent event(PR_TRUE, NS_DRAGDROP_DROP, innerMostWidget);
        event.refPoint.x = retx;
        event.refPoint.y = rety;

        nsEventStatus status = nsEventStatus_eIgnore;
        innerMostWidget->DispatchEvent(&event, status);
    }

    // before we unset the context we need to do a drop_finish

    gdk_drop_finish(aDragContext, TRUE, aTime);

    // after a drop takes place we need to make sure that the drag
    // service doesn't think that it still has a context.  if the other
    // way ( besides the drop ) to end a drag event is during the leave
    // event and and that case is handled in that handler.
    dragSessionGTK->TargetSetLastContext(0, 0, 0);

    // clear the sLastDragMotion window
    sLastDragMotionWindow = 0;

    // Make sure to end the drag session. If this drag started in a
    // different app, we won't get a drag_end signal to end it from.
    gint x, y;
    GdkDisplay* display = gdk_display_get_default();
    if (display) {
      // get the current cursor position
      gdk_display_get_pointer(display, NULL, &x, &y, NULL);
      ((nsDragService *)dragService.get())->SetDragEndPoint(nsIntPoint(x, y));
    }
    dragService->EndDragSession(PR_TRUE);

    return TRUE;
}

void
nsWindow::OnDragDataReceivedEvent(GtkWidget *aWidget,
                                  GdkDragContext *aDragContext,
                                  gint aX,
                                  gint aY,
                                  GtkSelectionData  *aSelectionData,
                                  guint aInfo,
                                  guint aTime,
                                  gpointer aData)
{
    LOGDRAG(("nsWindow::OnDragDataReceived(%p)\n", (void*)this));

    // get our drag context
    nsCOMPtr<nsIDragService> dragService = do_GetService(kCDragServiceCID);
    nsCOMPtr<nsIDragSessionGTK> dragSessionGTK = do_QueryInterface(dragService);

    dragSessionGTK->TargetDataReceived(aWidget, aDragContext, aX, aY,
                                       aSelectionData, aInfo, aTime);
}

void
nsWindow::OnDragLeave(void)
{
    LOGDRAG(("nsWindow::OnDragLeave(%p)\n", (void*)this));

    nsDragEvent event(PR_TRUE, NS_DRAGDROP_EXIT, this);

    nsEventStatus status;
    DispatchEvent(&event, status);

    nsCOMPtr<nsIDragService> dragService = do_GetService(kCDragServiceCID);

    if (dragService) {
        nsCOMPtr<nsIDragSession> currentDragSession;
        dragService->GetCurrentSession(getter_AddRefs(currentDragSession));

        if (currentDragSession) {
            nsCOMPtr<nsIDOMNode> sourceNode;
            currentDragSession->GetSourceNode(getter_AddRefs(sourceNode));

            if (!sourceNode) {
                // We're leaving a window while doing a drag that was
                // initiated in a different app. End the drag session,
                // since we're done with it for now (until the user
                // drags back into mozilla).
                dragService->EndDragSession(PR_FALSE);
            }
        }
    }
}

void
nsWindow::OnDragEnter(nscoord aX, nscoord aY)
{
    // XXX Do we want to pass this on only if the event's subwindow is null?

    LOGDRAG(("nsWindow::OnDragEnter(%p)\n", (void*)this));

    nsDragEvent event(PR_TRUE, NS_DRAGDROP_ENTER, this);

    event.refPoint.x = aX;
    event.refPoint.y = aY;

    nsEventStatus status;
    DispatchEvent(&event, status);
}

static void
GetBrandName(nsXPIDLString& brandName)
{
    nsCOMPtr<nsIStringBundleService> bundleService =
        do_GetService(NS_STRINGBUNDLE_CONTRACTID);

    nsCOMPtr<nsIStringBundle> bundle;
    if (bundleService)
        bundleService->CreateBundle(
            "chrome://branding/locale/brand.properties",
            getter_AddRefs(bundle));

    if (bundle)
        bundle->GetStringFromName(
            NS_LITERAL_STRING("brandShortName").get(),
            getter_Copies(brandName));

    if (brandName.IsEmpty())
        brandName.Assign(NS_LITERAL_STRING("Mozilla"));
}

static GdkWindow *
CreateGdkWindow(GdkWindow *parent, GtkWidget *widget)
{
    GdkWindowAttr attributes;
    gint          attributes_mask = GDK_WA_VISUAL;

    attributes.event_mask = (GDK_EXPOSURE_MASK | GDK_STRUCTURE_MASK |
                             GDK_VISIBILITY_NOTIFY_MASK |
                             GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
                             GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
#ifdef HAVE_GTK_MOTION_HINTS
                             GDK_POINTER_MOTION_HINT_MASK |
#endif
                             GDK_POINTER_MOTION_MASK);

    attributes.width = 1;
    attributes.height = 1;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.visual = gtk_widget_get_visual(widget);
    attributes.window_type = GDK_WINDOW_CHILD;

#if defined(MOZ_WIDGET_GTK2)
    attributes_mask |= GDK_WA_COLORMAP;
    attributes.colormap = gtk_widget_get_colormap(widget);
#endif

    GdkWindow *window = gdk_window_new(parent, &attributes, attributes_mask);
    gdk_window_set_user_data(window, widget);

// GTK3 TODO?
#if defined(MOZ_WIDGET_GTK2)
    /* set the default pixmap to None so that you don't end up with the
       gtk default which is BlackPixel. */
    gdk_window_set_back_pixmap(window, NULL, FALSE);
#endif

    return window;
}

nsresult
nsWindow::Create(nsIWidget        *aParent,
                 nsNativeWidget    aNativeParent,
                 const nsIntRect  &aRect,
                 EVENT_CALLBACK    aHandleEventFunction,
                 nsDeviceContext *aContext,
                 nsIAppShell      *aAppShell,
                 nsIToolkit       *aToolkit,
                 nsWidgetInitData *aInitData)
{
    // only set the base parent if we're going to be a dialog or a
    // toplevel
    nsIWidget *baseParent = aInitData &&
        (aInitData->mWindowType == eWindowType_dialog ||
         aInitData->mWindowType == eWindowType_toplevel ||
         aInitData->mWindowType == eWindowType_invisible) ?
        nsnull : aParent;

    NS_ASSERTION(!mWindowGroup, "already have window group (leaking it)");

    // initialize all the common bits of this class
    BaseCreate(baseParent, aRect, aHandleEventFunction, aContext,
               aAppShell, aToolkit, aInitData);

    // Do we need to listen for resizes?
    PRBool listenForResizes = PR_FALSE;;
    if (aNativeParent || (aInitData && aInitData->mListenForResizes))
        listenForResizes = PR_TRUE;

    // and do our common creation
    CommonCreate(aParent, listenForResizes);

    // save our bounds
    mBounds = aRect;

    // figure out our parent window
    GtkWidget      *parentMozContainer = nsnull;
    GtkContainer   *parentGtkContainer = nsnull;
    GdkWindow      *parentGdkWindow = nsnull;
    GtkWindow      *topLevelParent = nsnull;

    if (aParent)
        parentGdkWindow = GDK_WINDOW(aParent->GetNativeData(NS_NATIVE_WINDOW));
    else if (aNativeParent && GDK_IS_WINDOW(aNativeParent))
        parentGdkWindow = GDK_WINDOW(aNativeParent);
    else if (aNativeParent && GTK_IS_CONTAINER(aNativeParent))
        parentGtkContainer = GTK_CONTAINER(aNativeParent);

    if (parentGdkWindow) {
        // get the widget for the window - it should be a moz container
        parentMozContainer = get_gtk_widget_for_gdk_window(parentGdkWindow);

        if (!IS_MOZ_CONTAINER(parentMozContainer))
            return NS_ERROR_FAILURE;

        // get the toplevel window just in case someone needs to use it
        // for setting transients or whatever.
        topLevelParent =
            GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(parentMozContainer)));
    }

    // ok, create our windows
    switch (mWindowType) {
    case eWindowType_dialog:
    case eWindowType_popup:
    case eWindowType_toplevel:
    case eWindowType_invisible: {
        mIsTopLevel = PR_TRUE;

        // We only move a general managed toplevel window if someone has
        // actually placed the window somewhere.  If no placement has taken
        // place, we just let the window manager Do The Right Thing.
        //
        // Indicate that if we're shown, we at least need to have our size set.
        // If we get explicitly moved, the position will also be set.
        mNeedsResize = PR_TRUE;

        nsXPIDLString brandName;
        GetBrandName(brandName);
        NS_ConvertUTF16toUTF8 cBrand(brandName);

        if (mWindowType == eWindowType_dialog) {
            mShell = gtk_window_new(GTK_WINDOW_TOPLEVEL);
            SetDefaultIcon();
            gtk_window_set_wmclass(GTK_WINDOW(mShell), "Dialog", cBrand.get());
            gtk_window_set_type_hint(GTK_WINDOW(mShell),
                                     GDK_WINDOW_TYPE_HINT_DIALOG);
            gtk_window_set_transient_for(GTK_WINDOW(mShell),
                                         topLevelParent);
            mTransientParent = topLevelParent;
            // add ourselves to the parent window's window group
            if (!topLevelParent) {
                gtk_widget_realize(mShell);
                GdkWindow* dialoglead = gtk_widget_get_window(mShell);
                gdk_window_set_group(dialoglead, dialoglead);
            }
            if (parentGdkWindow) {
                nsWindow *parentnsWindow =
                    get_window_for_gdk_window(parentGdkWindow);
                NS_ASSERTION(parentnsWindow,
                             "no nsWindow for parentGdkWindow!");
                if (parentnsWindow && parentnsWindow->mWindowGroup) {
                    gtk_window_group_add_window(parentnsWindow->mWindowGroup,
                                                GTK_WINDOW(mShell));
                    // store this in case any children are created
                    mWindowGroup = parentnsWindow->mWindowGroup;
                    g_object_ref(mWindowGroup);
                    LOG(("adding window %p to group %p\n",
                         (void *)mShell, (void *)mWindowGroup));
                }
            }
        }
        else if (mWindowType == eWindowType_popup) {
            // With popup windows, we want to control their position, so don't
            // wait for the window manager to place them (which wouldn't
            // happen with override-redirect windows anyway).
            mNeedsMove = PR_TRUE;

            // Popups that are not noautohide are only temporary. The are used
            // for menus and the like and disappear when another window is used.
            if (!aInitData->mNoAutoHide) {
                // For most popups, use the standard GtkWindowType
                // GTK_WINDOW_POPUP, which will use a Window with the
                // override-redirect attribute (for temporary windows).
                mShell = gtk_window_new(GTK_WINDOW_POPUP);
                gtk_window_set_wmclass(GTK_WINDOW(mShell), "Popup", cBrand.get());
            } else {
                // For long-lived windows, their stacking order is managed by
                // the window manager, as indicated by GTK_WINDOW_TOPLEVEL ...
                mShell = gtk_window_new(GTK_WINDOW_TOPLEVEL);
                gtk_window_set_wmclass(GTK_WINDOW(mShell), "Popup", cBrand.get());
                // ... but the window manager does not decorate this window,
                // nor provide a separate taskbar icon.
                if (mBorderStyle == eBorderStyle_default) {
                  gtk_window_set_decorated(GTK_WINDOW(mShell), FALSE);
                }
                else {
                  PRBool decorate = mBorderStyle & eBorderStyle_title;
                  gtk_window_set_decorated(GTK_WINDOW(mShell), decorate);
                  if (decorate) {
                    gtk_window_set_deletable(GTK_WINDOW(mShell), mBorderStyle & eBorderStyle_close);
                  }
                }
                gtk_window_set_skip_taskbar_hint(GTK_WINDOW(mShell), TRUE);
                // Element focus is managed by the parent window so the
                // WM_HINTS input field is set to False to tell the window
                // manager not to set input focus to this window ...
                gtk_window_set_accept_focus(GTK_WINDOW(mShell), FALSE);
#ifdef MOZ_X11
                // ... but when the window manager offers focus through
                // WM_TAKE_FOCUS, focus is requested on the parent window.
                gtk_widget_realize(mShell);
                gdk_window_add_filter(gtk_widget_get_window(mShell),
                                      popup_take_focus_filter, NULL); 
#endif
            }

            GdkWindowTypeHint gtkTypeHint;
            if (aInitData->mIsDragPopup) {
                gtkTypeHint = GDK_WINDOW_TYPE_HINT_DND;
            }
            else {
                switch (aInitData->mPopupHint) {
                    case ePopupTypeMenu:
                        gtkTypeHint = GDK_WINDOW_TYPE_HINT_POPUP_MENU;
                        break;
                    case ePopupTypeTooltip:
                        gtkTypeHint = GDK_WINDOW_TYPE_HINT_TOOLTIP;
                        break;
                    default:
                        gtkTypeHint = GDK_WINDOW_TYPE_HINT_UTILITY;
                        break;
                }
            }
            gtk_window_set_type_hint(GTK_WINDOW(mShell), gtkTypeHint);

            if (topLevelParent) {
                gtk_window_set_transient_for(GTK_WINDOW(mShell),
                                            topLevelParent);
                mTransientParent = topLevelParent;

                GtkWindowGroup *groupParent = gtk_window_get_group(topLevelParent);
                if (groupParent) {
                    gtk_window_group_add_window(groupParent, GTK_WINDOW(mShell));
                    mWindowGroup = groupParent;
                    g_object_ref(mWindowGroup);
                }
            }
        }
        else { // must be eWindowType_toplevel
            mShell = gtk_window_new(GTK_WINDOW_TOPLEVEL);
            SetDefaultIcon();
            gtk_window_set_wmclass(GTK_WINDOW(mShell), "Toplevel", cBrand.get());

            // each toplevel window gets its own window group
            mWindowGroup = gtk_window_group_new();

            // and add ourselves to the window group
            LOG(("adding window %p to new group %p\n",
                 (void *)mShell, (void *)mWindowGroup));
            gtk_window_group_add_window(mWindowGroup, GTK_WINDOW(mShell));
        }

        // create our container
        GtkWidget *container = moz_container_new();
        mContainer = MOZ_CONTAINER(container);
        gtk_container_add(GTK_CONTAINER(mShell), container);
        gtk_widget_realize(container);

#if defined(MOZ_WIDGET_GTK2)
        // Don't let GTK mess with the shapes of our GdkWindows
        GTK_PRIVATE_SET_FLAG(container, GTK_HAS_SHAPE_MASK);
#endif

        // make sure this is the focus widget in the container
        gtk_window_set_focus(GTK_WINDOW(mShell), container);

        // the drawing window
        mGdkWindow = gtk_widget_get_window(container);

        if (mWindowType == eWindowType_popup) {
            // gdk does not automatically set the cursor for "temporary"
            // windows, which are what gtk uses for popups.

            mCursor = eCursor_wait; // force SetCursor to actually set the
                                    // cursor, even though our internal state
                                    // indicates that we already have the
                                    // standard cursor.
            SetCursor(eCursor_standard);

            if (aInitData->mNoAutoHide) {
                gint wmd = ConvertBorderStyles(mBorderStyle);
                if (wmd != -1)
                  gdk_window_set_decorations(gtk_widget_get_window(mShell), (GdkWMDecoration) wmd);
            }
        }
    }
        break;
    case eWindowType_plugin:
    case eWindowType_child: {
        if (parentMozContainer) {
            mGdkWindow = CreateGdkWindow(parentGdkWindow, parentMozContainer);
            nsWindow *parentnsWindow =
                get_window_for_gdk_window(parentGdkWindow);
            if (parentnsWindow)
                mHasMappedToplevel = parentnsWindow->mHasMappedToplevel;
        }
        else if (parentGtkContainer) {
            GtkWidget *container = moz_container_new();
            mContainer = MOZ_CONTAINER(container);
            gtk_container_add(parentGtkContainer, container);
            gtk_widget_realize(container);

#if defined(MOZ_WIDGET_GTK2)
            // Don't let GTK mess with the shapes of our GdkWindows
            GTK_PRIVATE_SET_FLAG(container, GTK_HAS_SHAPE_MASK);
#endif

            mGdkWindow = gtk_widget_get_window(container);
        }
        else {
            NS_WARNING("Warning: tried to create a new child widget with no parent!");
            return NS_ERROR_FAILURE;
        }
    }
        break;
    default:
        break;
    }
    // Disable the double buffer because it will make the caret crazy
    // For bug#153805 (Gtk2 double buffer makes carets misbehave)
    // DirectFB's expose code depends on gtk double buffering
    // XXX - I think this bug is probably dead, we can just use gtk's
    // double-buffering everywhere
#ifdef MOZ_X11
    if (mContainer)
        gtk_widget_set_double_buffered (GTK_WIDGET(mContainer),FALSE);
#endif

    // label the drawing window with this object so we can find our way home
    g_object_set_data(G_OBJECT(mGdkWindow), "nsWindow", this);

    if (mContainer)
        g_object_set_data(G_OBJECT(mContainer), "nsWindow", this);

    if (mShell)
        g_object_set_data(G_OBJECT(mShell), "nsWindow", this);

    // attach listeners for events
    if (mShell) {
        g_signal_connect(mShell, "configure_event",
                         G_CALLBACK(configure_event_cb), NULL);
        g_signal_connect(mShell, "delete_event",
                         G_CALLBACK(delete_event_cb), NULL);
        g_signal_connect(mShell, "window_state_event",
                         G_CALLBACK(window_state_event_cb), NULL);

        GtkSettings* default_settings = gtk_settings_get_default();
        g_signal_connect_after(default_settings,
                               "notify::gtk-theme-name",
                               G_CALLBACK(theme_changed_cb), this);
        g_signal_connect_after(default_settings,
                               "notify::gtk-font-name",
                               G_CALLBACK(theme_changed_cb), this);

#ifdef MOZ_PLATFORM_MAEMO
        if (mWindowType == eWindowType_toplevel) {
            GdkWindow *gdkwin = gtk_widget_get_window(mShell);

            // Tell the Hildon desktop that we support being rotated
            gulong portrait_set = 1;
            GdkAtom support = gdk_atom_intern("_HILDON_PORTRAIT_MODE_SUPPORT", FALSE);
            gdk_property_change(gdkwin, support, gdk_x11_xatom_to_atom(XA_CARDINAL),
                                32, GDK_PROP_MODE_REPLACE,
                                (const guchar *) &portrait_set, 1);

            // Tell maemo-status-volume daemon to ungrab keys
            gulong volume_set = 1;
            GdkAtom keys = gdk_atom_intern("_HILDON_ZOOM_KEY_ATOM", FALSE);
            gdk_property_change(gdkwin, keys, gdk_x11_xatom_to_atom(XA_INTEGER),
                                32, GDK_PROP_MODE_REPLACE, (const guchar *) &volume_set, 1);
        }
#endif
    }

    if (mContainer) {
        g_signal_connect(mContainer, "unrealize",
                         G_CALLBACK(container_unrealize_cb), NULL);
        g_signal_connect_after(mContainer, "size_allocate",
                               G_CALLBACK(size_allocate_cb), NULL);
#if defined(MOZ_WIDGET_GTK2)
        g_signal_connect(mContainer, "expose_event",
                         G_CALLBACK(expose_event_cb), NULL);
#else
        g_signal_connect(G_OBJECT(mContainer), "draw",
                         G_CALLBACK(expose_event_cb), NULL);
#endif
        g_signal_connect(mContainer, "enter_notify_event",
                         G_CALLBACK(enter_notify_event_cb), NULL);
        g_signal_connect(mContainer, "leave_notify_event",
                         G_CALLBACK(leave_notify_event_cb), NULL);
        g_signal_connect(mContainer, "motion_notify_event",
                         G_CALLBACK(motion_notify_event_cb), NULL);
        g_signal_connect(mContainer, "button_press_event",
                         G_CALLBACK(button_press_event_cb), NULL);
        g_signal_connect(mContainer, "button_release_event",
                         G_CALLBACK(button_release_event_cb), NULL);
        g_signal_connect(mContainer, "focus_in_event",
                         G_CALLBACK(focus_in_event_cb), NULL);
        g_signal_connect(mContainer, "focus_out_event",
                         G_CALLBACK(focus_out_event_cb), NULL);
        g_signal_connect(mContainer, "key_press_event",
                         G_CALLBACK(key_press_event_cb), NULL);
        g_signal_connect(mContainer, "key_release_event",
                         G_CALLBACK(key_release_event_cb), NULL);
        g_signal_connect(mContainer, "scroll_event",
                         G_CALLBACK(scroll_event_cb), NULL);
        g_signal_connect(mContainer, "visibility_notify_event",
                         G_CALLBACK(visibility_notify_event_cb), NULL);
        g_signal_connect(mContainer, "hierarchy_changed",
                         G_CALLBACK(hierarchy_changed_cb), NULL);
        // Initialize mHasMappedToplevel.
        hierarchy_changed_cb(GTK_WIDGET(mContainer), NULL);

        gtk_drag_dest_set((GtkWidget *)mContainer,
                          (GtkDestDefaults)0,
                          NULL,
                          0,
                          (GdkDragAction)0);

        g_signal_connect(mContainer, "drag_motion",
                         G_CALLBACK(drag_motion_event_cb), NULL);
        g_signal_connect(mContainer, "drag_leave",
                         G_CALLBACK(drag_leave_event_cb), NULL);
        g_signal_connect(mContainer, "drag_drop",
                         G_CALLBACK(drag_drop_event_cb), NULL);
        g_signal_connect(mContainer, "drag_data_received",
                         G_CALLBACK(drag_data_received_event_cb), NULL);

        // We create input contexts for all containers, except for
        // toplevel popup windows
        if (mWindowType != eWindowType_popup) {
            mIMModule = new nsGtkIMModule(this);
        }
    } else if (!mIMModule) {
        nsWindow *container = GetContainerWindow();
        if (container) {
            mIMModule = container->mIMModule;
        }
    }

    LOG(("nsWindow [%p]\n", (void *)this));
    if (mShell) {
        LOG(("\tmShell %p %p %lx\n", (void *)mShell, (void *)gtk_widget_get_window(mShell),
             gdk_x11_window_get_xid(gtk_widget_get_window(mShell))));
    }

    if (mContainer) {
        LOG(("\tmContainer %p %p %lx\n", (void *)mContainer,
             (void *)gtk_widget_get_window(GTK_WIDGET(mContainer)),
             gdk_x11_window_get_xid(gtk_widget_get_window(GTK_WIDGET(mContainer)))));
    }
    else if (mGdkWindow) {
        LOG(("\tmGdkWindow %p %lx\n", (void *)mGdkWindow,
             gdk_x11_window_get_xid(mGdkWindow)));
    }

    // resize so that everything is set to the right dimensions
    if (!mIsTopLevel)
        Resize(mBounds.x, mBounds.y, mBounds.width, mBounds.height, PR_FALSE);

#ifdef ACCESSIBILITY
    nsresult rv;
    if (!sAccessibilityChecked) {
        sAccessibilityChecked = PR_TRUE;

        //check if accessibility enabled/disabled by environment variable
        const char *envValue = PR_GetEnv(sAccEnv);
        if (envValue) {
            sAccessibilityEnabled = atoi(envValue) != 0;
            LOG(("Accessibility Env %s=%s\n", sAccEnv, envValue));
        }
        //check gconf-2 setting
        else {
            nsCOMPtr<nsIPrefBranch> sysPrefService =
                do_GetService(sSysPrefService, &rv);
            if (NS_SUCCEEDED(rv) && sysPrefService) {

                // do the work to get gconf setting.
                // will be done soon later.
                sysPrefService->GetBoolPref(sAccessibilityKey,
                                            &sAccessibilityEnabled);
            }

        }
    }
#endif

#ifdef MOZ_DFB
    if (!mDFB) {
         DirectFBCreate( &mDFB );

         D_ASSUME( mDFB != NULL );

         if (mDFB)
              mDFB->GetDisplayLayer( mDFB, DLID_PRIMARY, &mDFBLayer );

         D_ASSUME( mDFBLayer != NULL );

         if (mDFBLayer)
              mDFBLayer->GetCursorPosition( mDFBLayer, &mDFBCursorX, &mDFBCursorY );
    }
#endif

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetWindowClass(const nsAString &xulWinType)
{
  if (!mShell)
    return NS_ERROR_FAILURE;

#ifdef MOZ_X11
  nsXPIDLString brandName;
  GetBrandName(brandName);

  XClassHint *class_hint = XAllocClassHint();
  if (!class_hint)
    return NS_ERROR_OUT_OF_MEMORY;
  const char *role = NULL;
  class_hint->res_name = ToNewCString(xulWinType);
  if (!class_hint->res_name) {
    XFree(class_hint);
    return NS_ERROR_OUT_OF_MEMORY;
  }
  class_hint->res_class = ToNewCString(brandName);
  if (!class_hint->res_class) {
    nsMemory::Free(class_hint->res_name);
    XFree(class_hint);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Parse res_name into a name and role. Characters other than
  // [A-Za-z0-9_-] are converted to '_'. Anything after the first
  // colon is assigned to role; if there's no colon, assign the
  // whole thing to both role and res_name.
  for (char *c = class_hint->res_name; *c; c++) {
    if (':' == *c) {
      *c = 0;
      role = c + 1;
    }
    else if (!isascii(*c) || (!isalnum(*c) && ('_' != *c) && ('-' != *c)))
      *c = '_';
  }
  class_hint->res_name[0] = toupper(class_hint->res_name[0]);
  if (!role) role = class_hint->res_name;

  GdkWindow *shellWindow = gtk_widget_get_window(GTK_WIDGET(mShell));
  gdk_window_set_role(shellWindow, role);
  // Can't use gtk_window_set_wmclass() for this; it prints
  // a warning & refuses to make the change.
  XSetClassHint(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
                gdk_x11_window_get_xid(shellWindow),
                class_hint);
  nsMemory::Free(class_hint->res_class);
  nsMemory::Free(class_hint->res_name);
  XFree(class_hint);
#else /* MOZ_X11 */

  char *res_name;

  res_name = ToNewCString(xulWinType);
  if (!res_name)
    return NS_ERROR_OUT_OF_MEMORY;

  printf("WARN: res_name = '%s'\n", res_name);


  const char *role = NULL;

  // Parse res_name into a name and role. Characters other than
  // [A-Za-z0-9_-] are converted to '_'. Anything after the first
  // colon is assigned to role; if there's no colon, assign the
  // whole thing to both role and res_name.
  for (char *c = res_name; *c; c++) {
    if (':' == *c) {
      *c = 0;
      role = c + 1;
    }
    else if (!isascii(*c) || (!isalnum(*c) && ('_' != *c) && ('-' != *c)))
      *c = '_';
  }
  res_name[0] = toupper(res_name[0]);
  if (!role) role = res_name;

  gdk_window_set_role(gtk_widget_get_window(GTK_WIDGET(mShell)), role);

  nsMemory::Free(res_name);

#endif /* MOZ_X11 */
  return NS_OK;
}

void
nsWindow::NativeResize(PRInt32 aWidth, PRInt32 aHeight, PRBool  aRepaint)
{
    LOG(("nsWindow::NativeResize [%p] %d %d\n", (void *)this,
         aWidth, aHeight));

    ResizeTransparencyBitmap(aWidth, aHeight);

    // clear our resize flag
    mNeedsResize = PR_FALSE;

    if (mIsTopLevel) {
        gtk_window_resize(GTK_WINDOW(mShell), aWidth, aHeight);
    }
    else if (mContainer) {
        GtkWidget *widget = GTK_WIDGET(mContainer);
        GtkAllocation allocation, prev_allocation;
        gtk_widget_get_allocation(widget, &prev_allocation);
        allocation.x = prev_allocation.x;
        allocation.y = prev_allocation.y;
        allocation.width = aWidth;
        allocation.height = aHeight;
        gtk_widget_size_allocate(widget, &allocation);
    }
    else if (mGdkWindow) {
        gdk_window_resize(mGdkWindow, aWidth, aHeight);
    }
}

void
nsWindow::NativeResize(PRInt32 aX, PRInt32 aY,
                       PRInt32 aWidth, PRInt32 aHeight,
                       PRBool  aRepaint)
{
    mNeedsResize = PR_FALSE;
    mNeedsMove = PR_FALSE;

    LOG(("nsWindow::NativeResize [%p] %d %d %d %d\n", (void *)this,
         aX, aY, aWidth, aHeight));

    ResizeTransparencyBitmap(aWidth, aHeight);

    if (mIsTopLevel) {
        // aX and aY give the position of the window manager frame top-left.
        gtk_window_move(GTK_WINDOW(mShell), aX, aY);
        // This sets the client window size.
        gtk_window_resize(GTK_WINDOW(mShell), aWidth, aHeight);
    }
    else if (mContainer) {
        GtkAllocation allocation;
        allocation.x = aX;
        allocation.y = aY;
        allocation.width = aWidth;
        allocation.height = aHeight;
        gtk_widget_size_allocate(GTK_WIDGET(mContainer), &allocation);
    }
    else if (mGdkWindow) {
        gdk_window_move_resize(mGdkWindow, aX, aY, aWidth, aHeight);
    }
}

void
nsWindow::NativeShow (PRBool  aAction)
{
    if (aAction) {
        // GTK wants us to set the window mask before we show the window
        // for the first time, or setting the mask later won't work.
        // GTK also wants us to NOT set the window mask if we're not really
        // going to need it, because GTK won't let us unset the mask properly
        // later.
        // So, we delay setting the mask until the last moment: when the window
        // is shown.
        // XXX that may or may not be true for GTK+ 2.x
        if (mTransparencyBitmap) {
            ApplyTransparencyBitmap();
        }

        // unset our flag now that our window has been shown
        mNeedsShow = PR_FALSE;

        if (mIsTopLevel) {
            // Set up usertime/startupID metadata for the created window.
            if (mWindowType != eWindowType_invisible) {
                SetUserTimeAndStartupIDForActivatedWindow(mShell);
            }

            gtk_widget_show(GTK_WIDGET(mContainer));
            gtk_widget_show(mShell);
        }
        else if (mContainer) {
            gtk_widget_show(GTK_WIDGET(mContainer));
        }
        else if (mGdkWindow) {
            gdk_window_show_unraised(mGdkWindow);
        }
    }
    else {
        if (mIsTopLevel) {
            gtk_widget_hide(GTK_WIDGET(mShell));
            gtk_widget_hide(GTK_WIDGET(mContainer));
        }
        else if (mContainer) {
            gtk_widget_hide(GTK_WIDGET(mContainer));
        }
        else if (mGdkWindow) {
            gdk_window_hide(mGdkWindow);
        }
    }
}

void
nsWindow::SetHasMappedToplevel(PRBool aState)
{
    // Even when aState == mHasMappedToplevel (as when this method is called
    // from Show()), child windows need to have their state checked, so don't
    // return early.
    PRBool oldState = mHasMappedToplevel;
    mHasMappedToplevel = aState;

    // mHasMappedToplevel is not updated for children of windows that are
    // hidden; GDK knows not to send expose events for these windows.  The
    // state is recorded on the hidden window itself, but, for child trees of
    // hidden windows, their state essentially becomes disconnected from their
    // hidden parent.  When the hidden parent gets shown, the child trees are
    // reconnected, and the state of the window being shown can be easily
    // propagated.
    if (!mIsShown || !mGdkWindow)
        return;

    if (aState && !oldState && !mIsFullyObscured) {
        // GDK_EXPOSE events have been ignored but the window is now visible,
        // so make sure GDK doesn't think that the window has already been
        // painted.
        gdk_window_invalidate_rect(mGdkWindow, NULL, FALSE);

        // Check that a grab didn't fail due to the window not being
        // viewable.
        EnsureGrabs();
    }

    for (GList *children = gdk_window_peek_children(mGdkWindow);
         children;
         children = children->next) {
        GdkWindow *gdkWin = GDK_WINDOW(children->data);
        nsWindow *child = get_window_for_gdk_window(gdkWin);

        if (child && child->mHasMappedToplevel != aState) {
            child->SetHasMappedToplevel(aState);
        }
    }
}

nsIntSize
nsWindow::GetSafeWindowSize(nsIntSize aSize)
{
    nsIntSize result = aSize;
    const PRInt32 kInt16Max = 32767;
    if (result.width > kInt16Max) {
        NS_WARNING("Clamping huge window width");
        result.width = kInt16Max;
    }
    if (result.height > kInt16Max) {
        NS_WARNING("Clamping huge window height");
        result.height = kInt16Max;
    }
    return result;
}

void
nsWindow::EnsureGrabs(void)
{
    if (mRetryPointerGrab)
        GrabPointer();
}

void
nsWindow::SetTransparencyMode(nsTransparencyMode aMode)
{
    if (!mShell) {
        // Pass the request to the toplevel window
        GtkWidget *topWidget = nsnull;
        GetToplevelWidget(&topWidget);
        if (!topWidget)
            return;

        nsWindow *topWindow = get_window_for_gtk_widget(topWidget);
        if (!topWindow)
            return;

        topWindow->SetTransparencyMode(aMode);
        return;
    }
    PRBool isTransparent = aMode == eTransparencyTransparent;

    if (mIsTransparent == isTransparent)
        return;

    if (!isTransparent) {
        if (mTransparencyBitmap) {
            delete[] mTransparencyBitmap;
            mTransparencyBitmap = nsnull;
            mTransparencyBitmapWidth = 0;
            mTransparencyBitmapHeight = 0;
#if defined(MOZ_WIDGET_GTK2)
            gtk_widget_reset_shapes(mShell);
#else
            // GTK3 TODO
#endif
        }
    } // else the new default alpha values are "all 1", so we don't
    // need to change anything yet

    mIsTransparent = isTransparent;
}

nsTransparencyMode
nsWindow::GetTransparencyMode()
{
    if (!mShell) {
        // Pass the request to the toplevel window
        GtkWidget *topWidget = nsnull;
        GetToplevelWidget(&topWidget);
        if (!topWidget) {
            return eTransparencyOpaque;
        }

        nsWindow *topWindow = get_window_for_gtk_widget(topWidget);
        if (!topWindow) {
            return eTransparencyOpaque;
        }

        return topWindow->GetTransparencyMode();
    }

    return mIsTransparent ? eTransparencyTransparent : eTransparencyOpaque;
}

nsresult
nsWindow::ConfigureChildren(const nsTArray<Configuration>& aConfigurations)
{
    for (PRUint32 i = 0; i < aConfigurations.Length(); ++i) {
        const Configuration& configuration = aConfigurations[i];
        nsWindow* w = static_cast<nsWindow*>(configuration.mChild);
        NS_ASSERTION(w->GetParent() == this,
                     "Configured widget is not a child");
        w->SetWindowClipRegion(configuration.mClipRegion, PR_TRUE);
        if (w->mBounds.Size() != configuration.mBounds.Size()) {
            w->Resize(configuration.mBounds.x, configuration.mBounds.y,
                      configuration.mBounds.width, configuration.mBounds.height,
                      PR_TRUE);
        } else if (w->mBounds.TopLeft() != configuration.mBounds.TopLeft()) {
            w->Move(configuration.mBounds.x, configuration.mBounds.y);
        } 
        w->SetWindowClipRegion(configuration.mClipRegion, PR_FALSE);
    }
    return NS_OK;
}

static pixman_box32
ToPixmanBox(const nsIntRect& aRect)
{
    pixman_box32_t result;
    result.x1 = aRect.x;
    result.y1 = aRect.y;
    result.x2 = aRect.XMost();
    result.y2 = aRect.YMost();
    return result;
}

static nsIntRect
ToIntRect(const pixman_box32& aBox)
{
    nsIntRect result;
    result.x = aBox.x1;
    result.y = aBox.y1;
    result.width = aBox.x2 - aBox.x1;
    result.height = aBox.y2 - aBox.y1;
    return result;
}

static void
InitRegion(pixman_region32* aRegion,
           const nsTArray<nsIntRect>& aRects)
{
    nsAutoTArray<pixman_box32,10> rects;
    rects.SetCapacity(aRects.Length());
    for (PRUint32 i = 0; i < aRects.Length (); ++i) {
        if (!aRects[i].IsEmpty()) {
            rects.AppendElement(ToPixmanBox(aRects[i]));
        }
    }

    pixman_region32_init_rects(aRegion,
                               rects.Elements(), rects.Length());
}

static void
GetIntRects(pixman_region32& aRegion, nsTArray<nsIntRect>* aRects)
{
    int nRects;
    pixman_box32* boxes = pixman_region32_rectangles(&aRegion, &nRects);
    aRects->SetCapacity(aRects->Length() + nRects);
    for (int i = 0; i < nRects; ++i) {
        aRects->AppendElement(ToIntRect(boxes[i]));
    }
}

void
nsWindow::SetWindowClipRegion(const nsTArray<nsIntRect>& aRects,
                              PRBool aIntersectWithExisting)
{
    const nsTArray<nsIntRect>* newRects = &aRects;

    nsAutoTArray<nsIntRect,1> intersectRects;
    if (aIntersectWithExisting) {
        nsAutoTArray<nsIntRect,1> existingRects;
        GetWindowClipRegion(&existingRects);

        nsAutoRef<pixman_region32> existingRegion;
        InitRegion(&existingRegion, existingRects);
        nsAutoRef<pixman_region32> newRegion;
        InitRegion(&newRegion, aRects);
        nsAutoRef<pixman_region32> intersectRegion;
        pixman_region32_init(&intersectRegion);
        pixman_region32_intersect(&intersectRegion,
                                  &newRegion, &existingRegion);

        // If mClipRects is null we haven't set a clip rect yet, so we
        // need to set the clip even if it is equal.
        if (mClipRects &&
            pixman_region32_equal(&intersectRegion, &existingRegion)) {
            return;
        }

        if (!pixman_region32_equal(&intersectRegion, &newRegion)) {
            GetIntRects(intersectRegion, &intersectRects);
            newRects = &intersectRects;
        }
    }

    if (!StoreWindowClipRegion(*newRects))
        return;

    if (!mGdkWindow)
        return;

#if defined(MOZ_WIDGET_GTK2)
    GdkRegion *region = gdk_region_new(); // aborts on OOM
    for (PRUint32 i = 0; i < newRects->Length(); ++i) {
        const nsIntRect& r = newRects->ElementAt(i);
        GdkRectangle rect = { r.x, r.y, r.width, r.height };
        gdk_region_union_with_rect(region, &rect);
    }

    gdk_window_shape_combine_region(mGdkWindow, region, 0, 0);
    gdk_region_destroy(region);
#else
    cairo_region_t *region = cairo_region_create();
    for (PRUint32 i = 0; i < newRects->Length(); ++i) {
        const nsIntRect& r = newRects->ElementAt(i);
        cairo_rectangle_int_t rect = { r.x, r.y, r.width, r.height };
        cairo_region_union_rectangle(region, &rect);
    }

    gdk_window_shape_combine_region(mGdkWindow, region, 0, 0);
    cairo_region_destroy(region);
#endif
  
    return;
}

void
nsWindow::ResizeTransparencyBitmap(PRInt32 aNewWidth, PRInt32 aNewHeight)
{
    if (!mTransparencyBitmap)
        return;

    if (aNewWidth == mTransparencyBitmapWidth &&
        aNewHeight == mTransparencyBitmapHeight)
        return;

    PRInt32 newSize = GetBitmapStride(aNewWidth)*aNewHeight;
    gchar* newBits = new gchar[newSize];
    if (!newBits) {
        delete[] mTransparencyBitmap;
        mTransparencyBitmap = nsnull;
        mTransparencyBitmapWidth = 0;
        mTransparencyBitmapHeight = 0;
        return;
    }
    // fill new mask with "opaque", first
    memset(newBits, 255, newSize);

    // Now copy the intersection of the old and new areas into the new mask
    PRInt32 copyWidth = NS_MIN(aNewWidth, mTransparencyBitmapWidth);
    PRInt32 copyHeight = NS_MIN(aNewHeight, mTransparencyBitmapHeight);
    PRInt32 oldRowBytes = GetBitmapStride(mTransparencyBitmapWidth);
    PRInt32 newRowBytes = GetBitmapStride(aNewWidth);
    PRInt32 copyBytes = GetBitmapStride(copyWidth);

    PRInt32 i;
    gchar* fromPtr = mTransparencyBitmap;
    gchar* toPtr = newBits;
    for (i = 0; i < copyHeight; i++) {
        memcpy(toPtr, fromPtr, copyBytes);
        fromPtr += oldRowBytes;
        toPtr += newRowBytes;
    }

    delete[] mTransparencyBitmap;
    mTransparencyBitmap = newBits;
    mTransparencyBitmapWidth = aNewWidth;
    mTransparencyBitmapHeight = aNewHeight;
}

static PRBool
ChangedMaskBits(gchar* aMaskBits, PRInt32 aMaskWidth, PRInt32 aMaskHeight,
        const nsIntRect& aRect, PRUint8* aAlphas, PRInt32 aStride)
{
    PRInt32 x, y, xMax = aRect.XMost(), yMax = aRect.YMost();
    PRInt32 maskBytesPerRow = GetBitmapStride(aMaskWidth);
    for (y = aRect.y; y < yMax; y++) {
        gchar* maskBytes = aMaskBits + y*maskBytesPerRow;
        PRUint8* alphas = aAlphas;
        for (x = aRect.x; x < xMax; x++) {
            PRBool newBit = *alphas > 0;
            alphas++;

            gchar maskByte = maskBytes[x >> 3];
            PRBool maskBit = (maskByte & (1 << (x & 7))) != 0;

            if (maskBit != newBit) {
                return PR_TRUE;
            }
        }
        aAlphas += aStride;
    }

    return PR_FALSE;
}

static
void UpdateMaskBits(gchar* aMaskBits, PRInt32 aMaskWidth, PRInt32 aMaskHeight,
        const nsIntRect& aRect, PRUint8* aAlphas, PRInt32 aStride)
{
    PRInt32 x, y, xMax = aRect.XMost(), yMax = aRect.YMost();
    PRInt32 maskBytesPerRow = GetBitmapStride(aMaskWidth);
    for (y = aRect.y; y < yMax; y++) {
        gchar* maskBytes = aMaskBits + y*maskBytesPerRow;
        PRUint8* alphas = aAlphas;
        for (x = aRect.x; x < xMax; x++) {
            PRBool newBit = *alphas > 0;
            alphas++;

            gchar mask = 1 << (x & 7);
            gchar maskByte = maskBytes[x >> 3];
            // Note: '-newBit' turns 0 into 00...00 and 1 into 11...11
            maskBytes[x >> 3] = (maskByte & ~mask) | (-newBit & mask);
        }
        aAlphas += aStride;
    }
}

void
nsWindow::ApplyTransparencyBitmap()
{
#ifdef MOZ_X11
    // We use X11 calls where possible, because GDK handles expose events
    // for shaped windows in a way that's incompatible with us (Bug 635903).
    // It doesn't occur when the shapes are set through X.
    GdkWindow *shellWindow = gtk_widget_get_window(mShell);
    Display* xDisplay = GDK_WINDOW_XDISPLAY(shellWindow);
    Window xDrawable = GDK_WINDOW_XID(shellWindow);
    Pixmap maskPixmap = XCreateBitmapFromData(xDisplay,
                                              xDrawable,
                                              mTransparencyBitmap,
                                              mTransparencyBitmapWidth,
                                              mTransparencyBitmapHeight);
    XShapeCombineMask(xDisplay, xDrawable,
                      ShapeBounding, 0, 0,
                      maskPixmap, ShapeSet);
    XFreePixmap(xDisplay, maskPixmap);
#else
#if defined(MOZ_WIDGET_GTK2)
    gtk_widget_reset_shapes(mShell);
    GdkBitmap* maskBitmap = gdk_bitmap_create_from_data(gtk_widget_get_window(mShell),
            mTransparencyBitmap,
            mTransparencyBitmapWidth, mTransparencyBitmapHeight);
    if (!maskBitmap)
        return;

    gtk_widget_shape_combine_mask(mShell, maskBitmap, 0, 0);
    g_object_unref(maskBitmap);
#else
    cairo_surface_t *maskBitmap;
    maskBitmap = cairo_image_surface_create_for_data((unsigned char*)mTransparencyBitmap, 
                                                     CAIRO_FORMAT_A1, 
                                                     mTransparencyBitmapWidth, 
                                                     mTransparencyBitmapHeight,
                                                     GetBitmapStride(mTransparencyBitmapWidth));
    if (!maskBitmap)
        return;

    cairo_region_t * maskRegion = gdk_cairo_region_create_from_surface(maskBitmap);
    gtk_widget_shape_combine_region(mShell, maskRegion);
    cairo_region_destroy(maskRegion);
    cairo_surface_destroy(maskBitmap);
#endif // MOZ_WIDGET_GTK2
#endif // MOZ_X11
}

nsresult
nsWindow::UpdateTranslucentWindowAlphaInternal(const nsIntRect& aRect,
                                               PRUint8* aAlphas, PRInt32 aStride)
{
    if (!mShell) {
        // Pass the request to the toplevel window
        GtkWidget *topWidget = nsnull;
        GetToplevelWidget(&topWidget);
        if (!topWidget)
            return NS_ERROR_FAILURE;

        nsWindow *topWindow = get_window_for_gtk_widget(topWidget);
        if (!topWindow)
            return NS_ERROR_FAILURE;

        return topWindow->UpdateTranslucentWindowAlphaInternal(aRect, aAlphas, aStride);
    }

    NS_ASSERTION(mIsTransparent, "Window is not transparent");

    if (mTransparencyBitmap == nsnull) {
        PRInt32 size = GetBitmapStride(mBounds.width)*mBounds.height;
        mTransparencyBitmap = new gchar[size];
        if (mTransparencyBitmap == nsnull)
            return NS_ERROR_FAILURE;
        memset(mTransparencyBitmap, 255, size);
        mTransparencyBitmapWidth = mBounds.width;
        mTransparencyBitmapHeight = mBounds.height;
    }

    NS_ASSERTION(aRect.x >= 0 && aRect.y >= 0
            && aRect.XMost() <= mBounds.width && aRect.YMost() <= mBounds.height,
            "Rect is out of window bounds");

    if (!ChangedMaskBits(mTransparencyBitmap, mBounds.width, mBounds.height,
                         aRect, aAlphas, aStride))
        // skip the expensive stuff if the mask bits haven't changed; hopefully
        // this is the common case
        return NS_OK;

    UpdateMaskBits(mTransparencyBitmap, mBounds.width, mBounds.height,
                   aRect, aAlphas, aStride);

    if (!mNeedsShow) {
        ApplyTransparencyBitmap();
    }
    return NS_OK;
}

void
nsWindow::GrabPointer(void)
{
    LOG(("GrabPointer %d\n", mRetryPointerGrab));

    mRetryPointerGrab = PR_FALSE;

    // If the window isn't visible, just set the flag to retry the
    // grab.  When this window becomes visible, the grab will be
    // retried.
    if (!mHasMappedToplevel || mIsFullyObscured) {
        LOG(("GrabPointer: window not visible\n"));
        mRetryPointerGrab = PR_TRUE;
        return;
    }

    if (!mGdkWindow)
        return;

    gint retval;
    retval = gdk_pointer_grab(mGdkWindow, TRUE,
                              (GdkEventMask)(GDK_BUTTON_PRESS_MASK |
                                             GDK_BUTTON_RELEASE_MASK |
                                             GDK_ENTER_NOTIFY_MASK |
                                             GDK_LEAVE_NOTIFY_MASK |
#ifdef HAVE_GTK_MOTION_HINTS
                                             GDK_POINTER_MOTION_HINT_MASK |
#endif
                                             GDK_POINTER_MOTION_MASK),
                              (GdkWindow *)NULL, NULL, GDK_CURRENT_TIME);

    if (retval != GDK_GRAB_SUCCESS) {
        LOG(("GrabPointer: pointer grab failed\n"));
        mRetryPointerGrab = PR_TRUE;
    }
}

void
nsWindow::ReleaseGrabs(void)
{
    LOG(("ReleaseGrabs\n"));

    mRetryPointerGrab = PR_FALSE;
    gdk_pointer_ungrab(GDK_CURRENT_TIME);
}

void
nsWindow::GetToplevelWidget(GtkWidget **aWidget)
{
    *aWidget = nsnull;

    if (mShell) {
        *aWidget = mShell;
        return;
    }

    GtkWidget *widget = GetMozContainerWidget();
    if (!widget)
        return;

    *aWidget = gtk_widget_get_toplevel(widget);
}

GtkWidget *
nsWindow::GetMozContainerWidget()
{
    if (!mGdkWindow)
        return NULL;

    GtkWidget *owningWidget =
        get_gtk_widget_for_gdk_window(mGdkWindow);
    return owningWidget;
}

nsWindow *
nsWindow::GetContainerWindow()
{
    GtkWidget *owningWidget = GetMozContainerWidget();
    if (!owningWidget)
        return nsnull;

    nsWindow *window = get_window_for_gtk_widget(owningWidget);
    NS_ASSERTION(window, "No nsWindow for container widget");
    return window;
}

void
nsWindow::SetUrgencyHint(GtkWidget *top_window, PRBool state)
{
    if (!top_window)
        return;
        
    gdk_window_set_urgency_hint(gtk_widget_get_window(top_window), state);
}

void *
nsWindow::SetupPluginPort(void)
{
    if (!mGdkWindow)
        return nsnull;

    if (gdk_window_is_destroyed(mGdkWindow) == TRUE)
        return nsnull;

    Window window = gdk_x11_window_get_xid(mGdkWindow);
    
    // we have to flush the X queue here so that any plugins that
    // might be running on separate X connections will be able to use
    // this window in case it was just created
#ifdef MOZ_X11
    XWindowAttributes xattrs;    
    Display *display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    XGetWindowAttributes(display, window, &xattrs);
    XSelectInput (display, window,
                  xattrs.your_event_mask |
                  SubstructureNotifyMask);

    gdk_window_add_filter(mGdkWindow, plugin_window_filter_func, this);

    XSync(display, False);
#endif /* MOZ_X11 */
    
    return (void *)window;
}

nsresult
nsWindow::SetWindowIconList(const nsTArray<nsCString> &aIconList)
{
    GList *list = NULL;

    for (PRUint32 i = 0; i < aIconList.Length(); ++i) {
        const char *path = aIconList[i].get();
        LOG(("window [%p] Loading icon from %s\n", (void *)this, path));

        GdkPixbuf *icon = gdk_pixbuf_new_from_file(path, NULL);
        if (!icon)
            continue;

        list = g_list_append(list, icon);
    }

    if (!list)
        return NS_ERROR_FAILURE;

    gtk_window_set_icon_list(GTK_WINDOW(mShell), list);

    g_list_foreach(list, (GFunc) g_object_unref, NULL);
    g_list_free(list);

    return NS_OK;
}

void
nsWindow::SetDefaultIcon(void)
{
    SetIcon(NS_LITERAL_STRING("default"));
}

void
nsWindow::SetPluginType(PluginType aPluginType)
{
    mPluginType = aPluginType;
}

#ifdef MOZ_X11
void
nsWindow::SetNonXEmbedPluginFocus()
{
    if (gPluginFocusWindow == this || mPluginType!=PluginType_NONXEMBED) {
        return;
    }

    if (gPluginFocusWindow) {
        nsRefPtr<nsWindow> kungFuDeathGrip = gPluginFocusWindow;
        gPluginFocusWindow->LoseNonXEmbedPluginFocus();
    }

    LOGFOCUS(("nsWindow::SetNonXEmbedPluginFocus\n"));

    Window curFocusWindow;
    int focusState;
    
    GdkDisplay *gdkDisplay = gdk_window_get_display(mGdkWindow);
    XGetInputFocus(gdk_x11_display_get_xdisplay(gdkDisplay),
                   &curFocusWindow,
                   &focusState);

    LOGFOCUS(("\t curFocusWindow=%p\n", curFocusWindow));

    GdkWindow* toplevel = gdk_window_get_toplevel(mGdkWindow);
#if defined(MOZ_WIDGET_GTK2)
    GdkWindow *gdkfocuswin = gdk_window_lookup(curFocusWindow);
#else
    GdkWindow *gdkfocuswin = gdk_x11_window_lookup_for_display(gdkDisplay,
                                                               curFocusWindow);
#endif

    // lookup with the focus proxy window is supposed to get the
    // same GdkWindow as toplevel. If the current focused window
    // is not the focus proxy, we return without any change.
    if (gdkfocuswin != toplevel) {
        return;
    }

    // switch the focus from the focus proxy to the plugin window
    mOldFocusWindow = curFocusWindow;
    XRaiseWindow(GDK_WINDOW_XDISPLAY(mGdkWindow),
                 gdk_x11_window_get_xid(mGdkWindow));
    gdk_error_trap_push();
    XSetInputFocus(GDK_WINDOW_XDISPLAY(mGdkWindow),
                   gdk_x11_window_get_xid(mGdkWindow),
                   RevertToNone,
                   CurrentTime);
    gdk_flush();
    gdk_error_trap_pop();
    gPluginFocusWindow = this;
    gdk_window_add_filter(NULL, plugin_client_message_filter, this);

    LOGFOCUS(("nsWindow::SetNonXEmbedPluginFocus oldfocus=%p new=%p\n",
              mOldFocusWindow, gdk_x11_window_get_xid(mGdkWindow)));
}

void
nsWindow::LoseNonXEmbedPluginFocus()
{
    LOGFOCUS(("nsWindow::LoseNonXEmbedPluginFocus\n"));

    // This method is only for the nsWindow which contains a
    // Non-XEmbed plugin, for example, JAVA plugin.
    if (gPluginFocusWindow != this || mPluginType!=PluginType_NONXEMBED) {
        return;
    }

    Window curFocusWindow;
    int focusState;

    XGetInputFocus(GDK_WINDOW_XDISPLAY(mGdkWindow),
                   &curFocusWindow,
                   &focusState);

    // we only switch focus between plugin window and focus proxy. If the
    // current focused window is not the plugin window, just removing the
    // event filter that blocks the WM_TAKE_FOCUS is enough. WM and gtk2
    // will take care of the focus later.
    if (!curFocusWindow ||
        curFocusWindow == gdk_x11_window_get_xid(mGdkWindow)) {

        gdk_error_trap_push();
        XRaiseWindow(GDK_WINDOW_XDISPLAY(mGdkWindow),
                     mOldFocusWindow);
        XSetInputFocus(GDK_WINDOW_XDISPLAY(mGdkWindow),
                       mOldFocusWindow,
                       RevertToParent,
                       CurrentTime);
        gdk_flush();
        gdk_error_trap_pop();
    }
    gPluginFocusWindow = NULL;
    mOldFocusWindow = 0;
    gdk_window_remove_filter(NULL, plugin_client_message_filter, this);

    LOGFOCUS(("nsWindow::LoseNonXEmbedPluginFocus end\n"));
}
#endif /* MOZ_X11 */

gint
nsWindow::ConvertBorderStyles(nsBorderStyle aStyle)
{
    gint w = 0;

    if (aStyle == eBorderStyle_default)
        return -1;

    // note that we don't handle eBorderStyle_close yet
    if (aStyle & eBorderStyle_all)
        w |= GDK_DECOR_ALL;
    if (aStyle & eBorderStyle_border)
        w |= GDK_DECOR_BORDER;
    if (aStyle & eBorderStyle_resizeh)
        w |= GDK_DECOR_RESIZEH;
    if (aStyle & eBorderStyle_title)
        w |= GDK_DECOR_TITLE;
    if (aStyle & eBorderStyle_menu)
        w |= GDK_DECOR_MENU;
    if (aStyle & eBorderStyle_minimize)
        w |= GDK_DECOR_MINIMIZE;
    if (aStyle & eBorderStyle_maximize)
        w |= GDK_DECOR_MAXIMIZE;

    return w;
}

NS_IMETHODIMP
nsWindow::MakeFullScreen(PRBool aFullScreen)
{
    LOG(("nsWindow::MakeFullScreen [%p] aFullScreen %d\n",
         (void *)this, aFullScreen));

    if (aFullScreen) {
        if (mSizeMode != nsSizeMode_Fullscreen)
            mLastSizeMode = mSizeMode;

        mSizeMode = nsSizeMode_Fullscreen;
        gtk_window_fullscreen(GTK_WINDOW(mShell));
    }
    else {
        mSizeMode = mLastSizeMode;
        gtk_window_unfullscreen(GTK_WINDOW(mShell));
    }

    NS_ASSERTION(mLastSizeMode != nsSizeMode_Fullscreen,
                 "mLastSizeMode should never be fullscreen");
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::HideWindowChrome(PRBool aShouldHide)
{
    if (!mShell) {
        // Pass the request to the toplevel window
        GtkWidget *topWidget = nsnull;
        GetToplevelWidget(&topWidget);
        if (!topWidget)
            return NS_ERROR_FAILURE;

        nsWindow *topWindow = get_window_for_gtk_widget(topWidget);
        if (!topWindow)
            return NS_ERROR_FAILURE;

        return topWindow->HideWindowChrome(aShouldHide);
    }

    // Sawfish, metacity, and presumably other window managers get
    // confused if we change the window decorations while the window
    // is visible.
    PRBool wasVisible = PR_FALSE;
    GdkWindow *shellWindow = gtk_widget_get_window(mShell);
    if (gdk_window_is_visible(shellWindow)) {
        gdk_window_hide(shellWindow);
        wasVisible = PR_TRUE;
    }

    gint wmd;
    if (aShouldHide)
        wmd = 0;
    else
        wmd = ConvertBorderStyles(mBorderStyle);

    if (wmd != -1)
      gdk_window_set_decorations(shellWindow, (GdkWMDecoration) wmd);

    if (wasVisible)
        gdk_window_show(shellWindow);

    // For some window managers, adding or removing window decorations
    // requires unmapping and remapping our toplevel window.  Go ahead
    // and flush the queue here so that we don't end up with a BadWindow
    // error later when this happens (when the persistence timer fires
    // and GetWindowPos is called)
#ifdef MOZ_X11
    XSync(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()) , False);
#else
    gdk_flush ();
#endif /* MOZ_X11 */

    return NS_OK;
}

static PRBool
check_for_rollup(GdkWindow *aWindow, gdouble aMouseX, gdouble aMouseY,
                 PRBool aIsWheel, PRBool aAlwaysRollup)
{
    PRBool retVal = PR_FALSE;
    nsCOMPtr<nsIWidget> rollupWidget = do_QueryReferent(gRollupWindow);

    if (rollupWidget && gRollupListener) {
        GdkWindow *currentPopup =
            (GdkWindow *)rollupWidget->GetNativeData(NS_NATIVE_WINDOW);
        if (aAlwaysRollup || !is_mouse_in_window(currentPopup, aMouseX, aMouseY)) {
            PRBool rollup = PR_TRUE;
            if (aIsWheel) {
                gRollupListener->ShouldRollupOnMouseWheelEvent(&rollup);
                retVal = PR_TRUE;
            }
            // if we're dealing with menus, we probably have submenus and
            // we don't want to rollup if the click is in a parent menu of
            // the current submenu
            PRUint32 popupsToRollup = PR_UINT32_MAX;
            if (gMenuRollup && !aAlwaysRollup) {
                nsAutoTArray<nsIWidget*, 5> widgetChain;
                PRUint32 sameTypeCount = gMenuRollup->GetSubmenuWidgetChain(&widgetChain);
                for (PRUint32 i=0; i<widgetChain.Length(); ++i) {
                    nsIWidget* widget = widgetChain[i];
                    GdkWindow* currWindow =
                        (GdkWindow*) widget->GetNativeData(NS_NATIVE_WINDOW);
                    if (is_mouse_in_window(currWindow, aMouseX, aMouseY)) {
                      // don't roll up if the mouse event occurred within a
                      // menu of the same type. If the mouse event occurred
                      // in a menu higher than that, roll up, but pass the
                      // number of popups to Rollup so that only those of the
                      // same type close up.
                      if (i < sameTypeCount) {
                        rollup = PR_FALSE;
                      }
                      else {
                        popupsToRollup = sameTypeCount;
                      }
                      break;
                    }
                } // foreach parent menu widget
            } // if rollup listener knows about menus

            // if we've determined that we should still rollup, do it.
            if (rollup) {
                gRollupListener->Rollup(popupsToRollup, nsnull);
                if (popupsToRollup == PR_UINT32_MAX) {
                    retVal = PR_TRUE;
                }
            }
        }
    } else {
        gRollupWindow = nsnull;
        gRollupListener = nsnull;
        NS_IF_RELEASE(gMenuRollup);
    }

    return retVal;
}

/* static */
PRBool
nsWindow::DragInProgress(void)
{
    // sLastDragMotionWindow means the drag arrow is over mozilla
    // sIsDraggingOutOf means the drag arrow is out of mozilla
    // both cases mean the dragging is happenning.
    return (sLastDragMotionWindow || sIsDraggingOutOf);
}

static PRBool
is_mouse_in_window (GdkWindow* aWindow, gdouble aMouseX, gdouble aMouseY)
{
    gint x = 0;
    gint y = 0;
    gint w, h;

    gint offsetX = 0;
    gint offsetY = 0;

    GdkWindow *window = aWindow;

    while (window) {
        gint tmpX = 0;
        gint tmpY = 0;

        gdk_window_get_position(window, &tmpX, &tmpY);
        GtkWidget *widget = get_gtk_widget_for_gdk_window(window);

        // if this is a window, compute x and y given its origin and our
        // offset
        if (GTK_IS_WINDOW(widget)) {
            x = tmpX + offsetX;
            y = tmpY + offsetY;
            break;
        }

        offsetX += tmpX;
        offsetY += tmpY;
        window = gdk_window_get_parent(window);
    }

#if defined(MOZ_WIDGET_GTK2)
    gdk_drawable_get_size(aWindow, &w, &h);
#else
    w = gdk_window_get_width(aWindow);
    h = gdk_window_get_height(aWindow);
#endif

    if (aMouseX > x && aMouseX < x + w &&
        aMouseY > y && aMouseY < y + h)
        return PR_TRUE;

    return PR_FALSE;
}

static nsWindow *
get_window_for_gtk_widget(GtkWidget *widget)
{
    gpointer user_data = g_object_get_data(G_OBJECT(widget), "nsWindow");

    return static_cast<nsWindow *>(user_data);
}

static nsWindow *
get_window_for_gdk_window(GdkWindow *window)
{
    gpointer user_data = g_object_get_data(G_OBJECT(window), "nsWindow");

    return static_cast<nsWindow *>(user_data);
}

static GtkWidget *
get_gtk_widget_for_gdk_window(GdkWindow *window)
{
    gpointer user_data = NULL;
    gdk_window_get_user_data(window, &user_data);

    return GTK_WIDGET(user_data);
}

static GdkCursor *
get_gtk_cursor(nsCursor aCursor)
{
    GdkCursor *gdkcursor = nsnull;
    PRUint8 newType = 0xff;

    if ((gdkcursor = gCursorCache[aCursor])) {
        return gdkcursor;
    }

    switch (aCursor) {
    case eCursor_standard:
        gdkcursor = gdk_cursor_new(GDK_LEFT_PTR);
        break;
    case eCursor_wait:
        gdkcursor = gdk_cursor_new(GDK_WATCH);
        break;
    case eCursor_select:
        gdkcursor = gdk_cursor_new(GDK_XTERM);
        break;
    case eCursor_hyperlink:
        gdkcursor = gdk_cursor_new(GDK_HAND2);
        break;
    case eCursor_n_resize:
        gdkcursor = gdk_cursor_new(GDK_TOP_SIDE);
        break;
    case eCursor_s_resize:
        gdkcursor = gdk_cursor_new(GDK_BOTTOM_SIDE);
        break;
    case eCursor_w_resize:
        gdkcursor = gdk_cursor_new(GDK_LEFT_SIDE);
        break;
    case eCursor_e_resize:
        gdkcursor = gdk_cursor_new(GDK_RIGHT_SIDE);
        break;
    case eCursor_nw_resize:
        gdkcursor = gdk_cursor_new(GDK_TOP_LEFT_CORNER);
        break;
    case eCursor_se_resize:
        gdkcursor = gdk_cursor_new(GDK_BOTTOM_RIGHT_CORNER);
        break;
    case eCursor_ne_resize:
        gdkcursor = gdk_cursor_new(GDK_TOP_RIGHT_CORNER);
        break;
    case eCursor_sw_resize:
        gdkcursor = gdk_cursor_new(GDK_BOTTOM_LEFT_CORNER);
        break;
    case eCursor_crosshair:
        gdkcursor = gdk_cursor_new(GDK_CROSSHAIR);
        break;
    case eCursor_move:
        gdkcursor = gdk_cursor_new(GDK_FLEUR);
        break;
    case eCursor_help:
        gdkcursor = gdk_cursor_new(GDK_QUESTION_ARROW);
        break;
    case eCursor_copy: // CSS3
        newType = MOZ_CURSOR_COPY;
        break;
    case eCursor_alias:
        newType = MOZ_CURSOR_ALIAS;
        break;
    case eCursor_context_menu:
        newType = MOZ_CURSOR_CONTEXT_MENU;
        break;
    case eCursor_cell:
        gdkcursor = gdk_cursor_new(GDK_PLUS);
        break;
    case eCursor_grab:
        newType = MOZ_CURSOR_HAND_GRAB;
        break;
    case eCursor_grabbing:
        newType = MOZ_CURSOR_HAND_GRABBING;
        break;
    case eCursor_spinning:
        newType = MOZ_CURSOR_SPINNING;
        break;
    case eCursor_zoom_in:
        newType = MOZ_CURSOR_ZOOM_IN;
        break;
    case eCursor_zoom_out:
        newType = MOZ_CURSOR_ZOOM_OUT;
        break;
    case eCursor_not_allowed:
    case eCursor_no_drop:
        newType = MOZ_CURSOR_NOT_ALLOWED;
        break;
    case eCursor_vertical_text:
        newType = MOZ_CURSOR_VERTICAL_TEXT;
        break;
    case eCursor_all_scroll:
        gdkcursor = gdk_cursor_new(GDK_FLEUR);
        break;
    case eCursor_nesw_resize:
        newType = MOZ_CURSOR_NESW_RESIZE;
        break;
    case eCursor_nwse_resize:
        newType = MOZ_CURSOR_NWSE_RESIZE;
        break;
    case eCursor_ns_resize:
    case eCursor_row_resize:
        gdkcursor = gdk_cursor_new(GDK_SB_V_DOUBLE_ARROW);
        break;
    case eCursor_ew_resize:
    case eCursor_col_resize:
        gdkcursor = gdk_cursor_new(GDK_SB_H_DOUBLE_ARROW);
        break;
    case eCursor_none:
        newType = MOZ_CURSOR_NONE;
        break;
    default:
        NS_ASSERTION(aCursor, "Invalid cursor type");
        gdkcursor = gdk_cursor_new(GDK_LEFT_PTR);
        break;
    }

    // if by now we don't have a xcursor, this means we have to make a
    // custom one
    if (newType != 0xff) {
        GdkPixbuf * cursor_pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 32, 32);
        if (!cursor_pixbuf)
            return NULL;
      
        guchar *data = gdk_pixbuf_get_pixels(cursor_pixbuf);
        
        // Read data from GtkCursors and compose RGBA surface from 1bit bitmap and mask
        // GtkCursors bits and mask are 32x32 monochrome bitmaps (1 bit for each pixel)
        // so it's 128 byte array (4 bytes for are one bitmap row and there are 32 rows here).
        const unsigned char *bits = GtkCursors[newType].bits;
        const unsigned char *mask_bits = GtkCursors[newType].mask_bits;
        
        for (int i = 0; i < 128; i++) {
            char bit = *bits++;
            char mask = *mask_bits++;
            for (int j = 0; j < 8; j++) {
                unsigned char pix = ~(((bit >> j) & 0x01) * 0xff);
                *data++ = pix;
                *data++ = pix;
                *data++ = pix;
                *data++ = (((mask >> j) & 0x01) * 0xff);
            }
        }
      
        gdkcursor = gdk_cursor_new_from_pixbuf(gdk_display_get_default(), cursor_pixbuf,
                                               GtkCursors[newType].hot_x,
                                               GtkCursors[newType].hot_y);
        
        g_object_unref(cursor_pixbuf);
    }

    gCursorCache[aCursor] = gdkcursor;

    return gdkcursor;
}

// gtk callbacks

#if defined(MOZ_WIDGET_GTK2)
static gboolean
expose_event_cb(GtkWidget *widget, GdkEventExpose *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
    if (!window)
        return FALSE;

    // XXX We are so getting lucky here.  We are doing all of
    // mozilla's painting and then allowing default processing to occur.
    // This means that Mozilla paints in all of it's stuff and then
    // NO_WINDOW widgets (like scrollbars, for example) are painted by
    // Gtk on top of what we painted.

    // This return window->OnExposeEvent(widget, event); */

    window->OnExposeEvent(event);
    return FALSE;
}
#else
void
draw_window_of_widget(GtkWidget *widget, GdkWindow *aWindow, cairo_t *cr)
{
    gpointer windowWidget;
    gdk_window_get_user_data(aWindow, &windowWidget);

    // aWindow is in another widget
    if (windowWidget != widget)
        return;
    
    if (gtk_cairo_should_draw_window(cr, aWindow)) {
        nsRefPtr<nsWindow> window = get_window_for_gdk_window(aWindow);
        if (!window) {
            NS_WARNING("Cannot get nsWindow from GtkWidget");
        }
        else {      
            cairo_save(cr);      
            gtk_cairo_transform_to_window(cr, widget, aWindow);  
            // TODO - window->OnExposeEvent() can destroy this or other windows,
            // do we need to handle it somehow?
            window->OnExposeEvent(cr);
            cairo_restore(cr);
        }
    }
    
    GList *children = gdk_window_get_children(aWindow);
    GList *child = children;
    while (child) {
        draw_window_of_widget(widget, GDK_WINDOW(child->data), cr);
        child = g_list_next(child);
    }  
    g_list_free(children);

}

/* static */
gboolean
expose_event_cb(GtkWidget *widget, cairo_t *cr)
{
    draw_window_of_widget(widget, gtk_widget_get_window(widget), cr);
    return FALSE;
}
#endif //MOZ_WIDGET_GTK2

static gboolean
configure_event_cb(GtkWidget *widget,
                   GdkEventConfigure *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    return window->OnConfigureEvent(widget, event);
}

static void
container_unrealize_cb (GtkWidget *widget)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return;

    window->OnContainerUnrealize(widget);
}

static void
size_allocate_cb (GtkWidget *widget, GtkAllocation *allocation)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return;

    window->OnSizeAllocate(widget, allocation);
}

static gboolean
delete_event_cb(GtkWidget *widget, GdkEventAny *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    window->OnDeleteEvent(widget, event);

    return TRUE;
}

static gboolean
enter_notify_event_cb(GtkWidget *widget,
                      GdkEventCrossing *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
    if (!window)
        return TRUE;

    window->OnEnterNotifyEvent(widget, event);

    return TRUE;
}

static gboolean
leave_notify_event_cb(GtkWidget *widget,
                      GdkEventCrossing *event)
{
    if (is_parent_grab_leave(event)) {
        return TRUE;
    }

    // bug 369599: Suppress LeaveNotify events caused by pointer grabs to
    // avoid generating spurious mouse exit events.
    gint x = gint(event->x_root);
    gint y = gint(event->y_root);
    GdkDisplay* display = gtk_widget_get_display(widget);
    GdkWindow* winAtPt = gdk_display_get_window_at_pointer(display, &x, &y);
    if (winAtPt == event->window) {
        return TRUE;
    }

    nsRefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
    if (!window)
        return TRUE;

    window->OnLeaveNotifyEvent(widget, event);

    return TRUE;
}

static nsWindow*
GetFirstNSWindowForGDKWindow(GdkWindow *aGdkWindow)
{
    nsWindow* window;
    while (!(window = get_window_for_gdk_window(aGdkWindow))) {
        // The event has bubbled to the moz_container widget as passed into each caller's *widget parameter,
        // but its corresponding nsWindow is an ancestor of the window that we need.  Instead, look at
        // event->window and find the first ancestor nsWindow of it because event->window may be in a plugin.
        aGdkWindow = gdk_window_get_parent(aGdkWindow);
        if (!aGdkWindow) {
            window = nsnull;
            break;
        }
    }
    return window;
}

static gboolean
motion_notify_event_cb(GtkWidget *widget, GdkEventMotion *event)
{
    UpdateLastInputEventTime();

    nsWindow *window = GetFirstNSWindowForGDKWindow(event->window);
    if (!window)
        return FALSE;

    window->OnMotionNotifyEvent(widget, event);

#ifdef HAVE_GTK_MOTION_HINTS
    gdk_event_request_motions(event);
#endif
    return TRUE;
}

static gboolean
button_press_event_cb(GtkWidget *widget, GdkEventButton *event)
{
    UpdateLastInputEventTime();

    nsWindow *window = GetFirstNSWindowForGDKWindow(event->window);
    if (!window)
        return FALSE;

    window->OnButtonPressEvent(widget, event);

    return TRUE;
}

static gboolean
button_release_event_cb(GtkWidget *widget, GdkEventButton *event)
{
    UpdateLastInputEventTime();

    nsWindow *window = GetFirstNSWindowForGDKWindow(event->window);
    if (!window)
        return FALSE;

    window->OnButtonReleaseEvent(widget, event);

    return TRUE;
}

static gboolean
focus_in_event_cb(GtkWidget *widget, GdkEventFocus *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    window->OnContainerFocusInEvent(widget, event);

    return FALSE;
}

static gboolean
focus_out_event_cb(GtkWidget *widget, GdkEventFocus *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    window->OnContainerFocusOutEvent(widget, event);

    return FALSE;
}

#ifdef MOZ_X11
// For long-lived popup windows that don't really take focus themselves but
// may have elements that accept keyboard input when the parent window is
// active, focus is handled specially.  These windows include noautohide
// panels.  (This special handling is not necessary for temporary popups where
// the keyboard is grabbed.)
//
// Mousing over or clicking on these windows should not cause them to steal
// focus from their parent windows, so, the input field of WM_HINTS is set to
// False to request that the window manager not set the input focus to this
// window.  http://tronche.com/gui/x/icccm/sec-4.html#s-4.1.7
//
// However, these windows can still receive WM_TAKE_FOCUS messages from the
// window manager, so they can still detect when the user has indicated that
// they wish to direct keyboard input at these windows.  When the window
// manager offers focus to these windows (after a mouse over or click, for
// example), a request to make the parent window active is issued.  When the
// parent window becomes active, keyboard events will be received.

static GdkFilterReturn
popup_take_focus_filter(GdkXEvent *gdk_xevent,
                        GdkEvent *event,
                        gpointer data)
{
    XEvent* xevent = static_cast<XEvent*>(gdk_xevent);
    if (xevent->type != ClientMessage)
        return GDK_FILTER_CONTINUE;

    XClientMessageEvent& xclient = xevent->xclient;
    if (xclient.message_type != gdk_x11_get_xatom_by_name("WM_PROTOCOLS"))
        return GDK_FILTER_CONTINUE;

    Atom atom = xclient.data.l[0];
    if (atom != gdk_x11_get_xatom_by_name("WM_TAKE_FOCUS"))
        return GDK_FILTER_CONTINUE;

    guint32 timestamp = xclient.data.l[1];

    GtkWidget* widget = get_gtk_widget_for_gdk_window(event->any.window);
    if (!widget)
        return GDK_FILTER_CONTINUE;

    GtkWindow* parent = gtk_window_get_transient_for(GTK_WINDOW(widget));
    if (!parent)
        return GDK_FILTER_CONTINUE;

    if (gtk_window_is_active(parent))
        return GDK_FILTER_REMOVE; // leave input focus on the parent

    GdkWindow* parent_window = gtk_widget_get_window(GTK_WIDGET(parent));
    if (!parent_window)
        return GDK_FILTER_CONTINUE;

    // In case the parent has not been deconified.
    gdk_window_show_unraised(parent_window);

    // Request focus on the parent window.
    // Use gdk_window_focus rather than gtk_window_present to avoid
    // raising the parent window.
    gdk_window_focus(parent_window, timestamp);
    return GDK_FILTER_REMOVE;
}

static GdkFilterReturn
plugin_window_filter_func(GdkXEvent *gdk_xevent, GdkEvent *event, gpointer data)
{
    GdkWindow  *plugin_window;
    XEvent     *xevent;
    Window      xeventWindow;

    nsRefPtr<nsWindow> nswindow = (nsWindow*)data;
    GdkFilterReturn return_val;

    xevent = (XEvent *)gdk_xevent;
    return_val = GDK_FILTER_CONTINUE;

    switch (xevent->type)
    {
        case CreateNotify:
        case ReparentNotify:
            if (xevent->type==CreateNotify) {
                xeventWindow = xevent->xcreatewindow.window;
            }
            else {
                if (xevent->xreparent.event != xevent->xreparent.parent)
                    break;
                xeventWindow = xevent->xreparent.window;
            }
#if defined(MOZ_WIDGET_GTK2)
            plugin_window = gdk_window_lookup(xeventWindow);
#else
            plugin_window = gdk_x11_window_lookup_for_display(
                                  gdk_x11_lookup_xdisplay(xevent->xcreatewindow.display), xeventWindow);
#endif        
            if (plugin_window) {
                GtkWidget *widget =
                    get_gtk_widget_for_gdk_window(plugin_window);

// TODO GTK3
#if defined(MOZ_WIDGET_GTK2)
                if (GTK_IS_XTBIN(widget)) {
                    nswindow->SetPluginType(nsWindow::PluginType_NONXEMBED);
                    break;
                }
                else 
#endif
                if(GTK_IS_SOCKET(widget)) {
                    nswindow->SetPluginType(nsWindow::PluginType_XEMBED);
                    break;
                }
            }
            nswindow->SetPluginType(nsWindow::PluginType_NONXEMBED);
            return_val = GDK_FILTER_REMOVE;
            break;
        case EnterNotify:
            nswindow->SetNonXEmbedPluginFocus();
            break;
        case DestroyNotify:
            gdk_window_remove_filter
                ((GdkWindow*)(nswindow->GetNativeData(NS_NATIVE_WINDOW)),
                 plugin_window_filter_func,
                 nswindow);
            // Currently we consider all plugins are non-xembed and calls
            // LoseNonXEmbedPluginFocus without any checking.
            nswindow->LoseNonXEmbedPluginFocus();
            break;
        default:
            break;
    }
    return return_val;
}

static GdkFilterReturn
plugin_client_message_filter(GdkXEvent *gdk_xevent,
                             GdkEvent *event,
                             gpointer data)
{
    XEvent    *xevent;
    xevent = (XEvent *)gdk_xevent;

    GdkFilterReturn return_val;
    return_val = GDK_FILTER_CONTINUE;

    if (!gPluginFocusWindow || xevent->type!=ClientMessage) {
        return return_val;
    }

    // When WM sends out WM_TAKE_FOCUS, gtk2 will use XSetInputFocus
    // to set the focus to the focus proxy. To prevent this happen
    // while the focus is on the plugin, we filter the WM_TAKE_FOCUS
    // out.
    if (gdk_x11_get_xatom_by_name("WM_PROTOCOLS")
            != xevent->xclient.message_type) {
        return return_val;
    }

    if ((Atom) xevent->xclient.data.l[0] ==
            gdk_x11_get_xatom_by_name("WM_TAKE_FOCUS")) {
        // block it from gtk2.0 focus proxy
        return_val = GDK_FILTER_REMOVE;
    }

    return return_val;
}
#endif /* MOZ_X11 */

static gboolean
key_press_event_cb(GtkWidget *widget, GdkEventKey *event)
{
    LOG(("key_press_event_cb\n"));

    UpdateLastInputEventTime();

    // find the window with focus and dispatch this event to that widget
    nsWindow *window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    nsRefPtr<nsWindow> focusWindow = gFocusWindow ? gFocusWindow : window;

#ifdef MOZ_X11
    // Keyboard repeat can cause key press events to queue up when there are
    // slow event handlers (bug 301029).  Throttle these events by removing
    // consecutive pending duplicate KeyPress events to the same window.
    // We use the event time of the last one.
    // Note: GDK calls XkbSetDetectableAutorepeat so that KeyRelease events
    // are generated only when the key is physically released.
#define NS_GDKEVENT_MATCH_MASK 0x1FFF /* GDK_SHIFT_MASK .. GDK_BUTTON5_MASK */
    GdkDisplay* gdkDisplay = gtk_widget_get_display(widget);
    Display* dpy = GDK_DISPLAY_XDISPLAY(gdkDisplay);
    while (XPending(dpy)) {
        XEvent next_event;
        XPeekEvent(dpy, &next_event);
        GdkWindow* nextGdkWindow =
            gdk_x11_window_lookup_for_display(gdkDisplay, next_event.xany.window);
        if (nextGdkWindow != event->window ||
            next_event.type != KeyPress ||
            next_event.xkey.keycode != event->hardware_keycode ||
            next_event.xkey.state != (event->state & NS_GDKEVENT_MATCH_MASK)) {
            break;
        }
        XNextEvent(dpy, &next_event);
        event->time = next_event.xkey.time;
    }
#endif

    return focusWindow->OnKeyPressEvent(widget, event);
}

static gboolean
key_release_event_cb(GtkWidget *widget, GdkEventKey *event)
{
    LOG(("key_release_event_cb\n"));

    UpdateLastInputEventTime();

    // find the window with focus and dispatch this event to that widget
    nsWindow *window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    nsRefPtr<nsWindow> focusWindow = gFocusWindow ? gFocusWindow : window;

    return focusWindow->OnKeyReleaseEvent(widget, event);
}

static gboolean
scroll_event_cb(GtkWidget *widget, GdkEventScroll *event)
{
    nsWindow *window = GetFirstNSWindowForGDKWindow(event->window);
    if (!window)
        return FALSE;

    window->OnScrollEvent(widget, event);

    return TRUE;
}

static gboolean
visibility_notify_event_cb (GtkWidget *widget, GdkEventVisibility *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
    if (!window)
        return FALSE;

    window->OnVisibilityNotifyEvent(widget, event);

    return TRUE;
}

static void
hierarchy_changed_cb (GtkWidget *widget,
                      GtkWidget *previous_toplevel)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GdkWindowState old_window_state = GDK_WINDOW_STATE_WITHDRAWN;
    GdkEventWindowState event;

    event.new_window_state = GDK_WINDOW_STATE_WITHDRAWN;

    if (GTK_IS_WINDOW(previous_toplevel)) {
        g_signal_handlers_disconnect_by_func(previous_toplevel,
                                             FuncToGpointer(window_state_event_cb),
                                             widget);
        GdkWindow *win = gtk_widget_get_window(previous_toplevel);
        if (win) {
            old_window_state = gdk_window_get_state(win);
        }
    }

    if (GTK_IS_WINDOW(toplevel)) {
        g_signal_connect_swapped(toplevel, "window-state-event",
                                 G_CALLBACK(window_state_event_cb), widget);
        GdkWindow *win = gtk_widget_get_window(toplevel);
        if (win) {
            event.new_window_state = gdk_window_get_state(win);
        }
    }

    event.changed_mask = static_cast<GdkWindowState>
        (old_window_state ^ event.new_window_state);

    if (event.changed_mask) {
        event.type = GDK_WINDOW_STATE;
        event.window = NULL;
        event.send_event = TRUE;
        window_state_event_cb(widget, &event);
    }
}

static gboolean
window_state_event_cb (GtkWidget *widget, GdkEventWindowState *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    window->OnWindowStateEvent(widget, event);

    return FALSE;
}

static void
theme_changed_cb (GtkSettings *settings, GParamSpec *pspec, nsWindow *data)
{
    nsRefPtr<nsWindow> window = data;
    window->ThemeChanged();
}

//////////////////////////////////////////////////////////////////////
// These are all of our drag and drop operations

void
nsWindow::InitDragEvent(nsDragEvent &aEvent)
{
    // set the keyboard modifiers
    GdkModifierType state = (GdkModifierType)0;
    gdk_display_get_pointer(gdk_display_get_default(), NULL, NULL, NULL, &state);
    aEvent.isShift = (state & GDK_SHIFT_MASK) ? PR_TRUE : PR_FALSE;
    aEvent.isControl = (state & GDK_CONTROL_MASK) ? PR_TRUE : PR_FALSE;
    aEvent.isAlt = (state & GDK_MOD1_MASK) ? PR_TRUE : PR_FALSE;
    aEvent.isMeta = PR_FALSE; // GTK+ doesn't support the meta key
}

// This will update the drag action based on the information in the
// drag context.  Gtk gets this from a combination of the key settings
// and what the source is offering.

void
nsWindow::UpdateDragStatus(GdkDragContext *aDragContext,
                           nsIDragService *aDragService)
{
    // default is to do nothing
    int action = nsIDragService::DRAGDROP_ACTION_NONE;
    GdkDragAction gdkAction = gdk_drag_context_get_actions(aDragContext);

    // set the default just in case nothing matches below
    if (gdkAction & GDK_ACTION_DEFAULT)
        action = nsIDragService::DRAGDROP_ACTION_MOVE;

    // first check to see if move is set
    if (gdkAction & GDK_ACTION_MOVE)
        action = nsIDragService::DRAGDROP_ACTION_MOVE;

    // then fall to the others
    else if (gdkAction & GDK_ACTION_LINK)
        action = nsIDragService::DRAGDROP_ACTION_LINK;

    // copy is ctrl
    else if (gdkAction & GDK_ACTION_COPY)
        action = nsIDragService::DRAGDROP_ACTION_COPY;

    // update the drag information
    nsCOMPtr<nsIDragSession> session;
    aDragService->GetCurrentSession(getter_AddRefs(session));

    if (session)
        session->SetDragAction(action);
}


static gboolean
drag_motion_event_cb(GtkWidget *aWidget,
                     GdkDragContext *aDragContext,
                     gint aX,
                     gint aY,
                     guint aTime,
                     gpointer aData)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
    if (!window)
        return FALSE;

    return window->OnDragMotionEvent(aWidget,
                                     aDragContext,
                                     aX, aY, aTime, aData);
}

static void
drag_leave_event_cb(GtkWidget *aWidget,
                    GdkDragContext *aDragContext,
                    guint aTime,
                    gpointer aData)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
    if (!window)
        return;

    window->OnDragLeaveEvent(aWidget, aDragContext, aTime, aData);
}


static gboolean
drag_drop_event_cb(GtkWidget *aWidget,
                   GdkDragContext *aDragContext,
                   gint aX,
                   gint aY,
                   guint aTime,
                   gpointer aData)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
    if (!window)
        return FALSE;

    return window->OnDragDropEvent(aWidget,
                                   aDragContext,
                                   aX, aY, aTime, aData);
}

static void
drag_data_received_event_cb(GtkWidget *aWidget,
                            GdkDragContext *aDragContext,
                            gint aX,
                            gint aY,
                            GtkSelectionData  *aSelectionData,
                            guint aInfo,
                            guint aTime,
                            gpointer aData)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
    if (!window)
        return;

    window->OnDragDataReceivedEvent(aWidget,
                                    aDragContext,
                                    aX, aY,
                                    aSelectionData,
                                    aInfo, aTime, aData);
}

static nsresult
initialize_prefs(void)
{
    gRaiseWindows =
        Preferences::GetBool("mozilla.widget.raise-on-setfocus", PR_TRUE);
    gDisableNativeTheme =
        Preferences::GetBool("mozilla.widget.disable-native-theme", PR_FALSE);

    return NS_OK;
}

void
nsWindow::FireDragLeaveTimer(void)
{
    LOGDRAG(("nsWindow::FireDragLeaveTimer(%p)\n", (void*)this));

    mDragLeaveTimer = nsnull;

    // clean up any pending drag motion window info
    if (sLastDragMotionWindow) {
        nsRefPtr<nsWindow> kungFuDeathGrip = sLastDragMotionWindow;
        // send our leave signal
        sLastDragMotionWindow->OnDragLeave();
        sLastDragMotionWindow = 0;
    }
}

/* static */
void
nsWindow::DragLeaveTimerCallback(nsITimer *aTimer, void *aClosure)
{
    nsRefPtr<nsWindow> window = static_cast<nsWindow *>(aClosure);
    window->FireDragLeaveTimer();
}

static GdkWindow *
get_inner_gdk_window (GdkWindow *aWindow,
                      gint x, gint y,
                      gint *retx, gint *rety)
{
    gint cx, cy, cw, ch;
    GList *children = gdk_window_peek_children(aWindow);
    for (GList *child = g_list_last(children);
         child;
         child = g_list_previous(child)) {
        GdkWindow *childWindow = (GdkWindow *) child->data;
        if (get_window_for_gdk_window(childWindow)) {
#if defined(MOZ_WIDGET_GTK2)
            gdk_window_get_geometry(childWindow, &cx, &cy, &cw, &ch, NULL);
#else
            gdk_window_get_geometry(childWindow, &cx, &cy, &cw, &ch);
#endif
            if ((cx < x) && (x < (cx + cw)) &&
                (cy < y) && (y < (cy + ch)) &&
                gdk_window_is_visible(childWindow)) {
                return get_inner_gdk_window(childWindow,
                                            x - cx, y - cy,
                                            retx, rety);
            }
        }
    }
    *retx = x;
    *rety = y;
    return aWindow;
}

static inline PRBool
is_context_menu_key(const nsKeyEvent& aKeyEvent)
{
    return ((aKeyEvent.keyCode == NS_VK_F10 && aKeyEvent.isShift &&
             !aKeyEvent.isControl && !aKeyEvent.isMeta && !aKeyEvent.isAlt) ||
            (aKeyEvent.keyCode == NS_VK_CONTEXT_MENU && !aKeyEvent.isShift &&
             !aKeyEvent.isControl && !aKeyEvent.isMeta && !aKeyEvent.isAlt));
}

static void
key_event_to_context_menu_event(nsMouseEvent &aEvent,
                                GdkEventKey *aGdkEvent)
{
    aEvent.refPoint = nsIntPoint(0, 0);
    aEvent.isShift = PR_FALSE;
    aEvent.isControl = PR_FALSE;
    aEvent.isAlt = PR_FALSE;
    aEvent.isMeta = PR_FALSE;
    aEvent.time = aGdkEvent->time;
    aEvent.clickCount = 1;
}

static int
is_parent_ungrab_enter(GdkEventCrossing *aEvent)
{
    return (GDK_CROSSING_UNGRAB == aEvent->mode) &&
        ((GDK_NOTIFY_ANCESTOR == aEvent->detail) ||
         (GDK_NOTIFY_VIRTUAL == aEvent->detail));

}

static int
is_parent_grab_leave(GdkEventCrossing *aEvent)
{
    return (GDK_CROSSING_GRAB == aEvent->mode) &&
        ((GDK_NOTIFY_ANCESTOR == aEvent->detail) ||
            (GDK_NOTIFY_VIRTUAL == aEvent->detail));
}

static GdkModifierType
gdk_keyboard_get_modifiers()
{
    GdkModifierType m = (GdkModifierType) 0;

    gdk_window_get_pointer(NULL, NULL, NULL, &m);

    return m;
}

#ifdef MOZ_X11
// Get the modifier masks for GDK_Caps_Lock, GDK_Num_Lock and GDK_Scroll_Lock.
// Return PR_TRUE on success, PR_FALSE on error.
static PRBool
gdk_keyboard_get_modmap_masks(Display*  aDisplay,
                              PRUint32* aCapsLockMask,
                              PRUint32* aNumLockMask,
                              PRUint32* aScrollLockMask)
{
    *aCapsLockMask = 0;
    *aNumLockMask = 0;
    *aScrollLockMask = 0;

    int min_keycode = 0;
    int max_keycode = 0;
    XDisplayKeycodes(aDisplay, &min_keycode, &max_keycode);

    int keysyms_per_keycode = 0;
    KeySym* xkeymap = XGetKeyboardMapping(aDisplay, min_keycode,
                                          max_keycode - min_keycode + 1,
                                          &keysyms_per_keycode);
    if (!xkeymap) {
        return PR_FALSE;
    }

    XModifierKeymap* xmodmap = XGetModifierMapping(aDisplay);
    if (!xmodmap) {
        XFree(xkeymap);
        return PR_FALSE;
    }

    /*
      The modifiermap member of the XModifierKeymap structure contains 8 sets
      of max_keypermod KeyCodes, one for each modifier in the order Shift,
      Lock, Control, Mod1, Mod2, Mod3, Mod4, and Mod5.
      Only nonzero KeyCodes have meaning in each set, and zero KeyCodes are ignored.
    */
    const unsigned int map_size = 8 * xmodmap->max_keypermod;
    for (unsigned int i = 0; i < map_size; i++) {
        KeyCode keycode = xmodmap->modifiermap[i];
        if (!keycode || keycode < min_keycode || keycode > max_keycode)
            continue;

        const KeySym* syms = xkeymap + (keycode - min_keycode) * keysyms_per_keycode;
        const unsigned int mask = 1 << (i / xmodmap->max_keypermod);
        for (int j = 0; j < keysyms_per_keycode; j++) {
            switch (syms[j]) {
                case GDK_Caps_Lock:   *aCapsLockMask |= mask;   break;
                case GDK_Num_Lock:    *aNumLockMask |= mask;    break;
                case GDK_Scroll_Lock: *aScrollLockMask |= mask; break;
            }
        }
    }

    XFreeModifiermap(xmodmap);
    XFree(xkeymap);
    return PR_TRUE;
}
#endif /* MOZ_X11 */

#ifdef ACCESSIBILITY
void
nsWindow::CreateRootAccessible()
{
    if (mIsTopLevel && !mRootAccessible) {
        LOG(("nsWindow:: Create Toplevel Accessibility\n"));
        nsAccessible *acc = DispatchAccessibleEvent();

        if (acc) {
            mRootAccessible = acc;
        }
    }
}

nsAccessible*
nsWindow::DispatchAccessibleEvent()
{
    nsAccessibleEvent event(PR_TRUE, NS_GETACCESSIBLE, this);

    nsEventStatus status;
    DispatchEvent(&event, status);

    return event.mAccessible;
}

void
nsWindow::DispatchEventToRootAccessible(PRUint32 aEventType)
{
    if (!sAccessibilityEnabled) {
        return;
    }

    nsCOMPtr<nsIAccessibilityService> accService =
        do_GetService("@mozilla.org/accessibilityService;1");
    if (!accService) {
        return;
    }

    // Get the root document accessible and fire event to it.
    nsAccessible *acc = DispatchAccessibleEvent();
    if (acc) {
        accService->FireAccessibleEvent(aEventType, acc);
    }
}

void
nsWindow::DispatchActivateEventAccessible(void)
{
    DispatchEventToRootAccessible(nsIAccessibleEvent::EVENT_WINDOW_ACTIVATE);
}

void
nsWindow::DispatchDeactivateEventAccessible(void)
{
    DispatchEventToRootAccessible(nsIAccessibleEvent::EVENT_WINDOW_DEACTIVATE);
}

void
nsWindow::DispatchMaximizeEventAccessible(void)
{
    DispatchEventToRootAccessible(nsIAccessibleEvent::EVENT_WINDOW_MAXIMIZE);
}

void
nsWindow::DispatchMinimizeEventAccessible(void)
{
    DispatchEventToRootAccessible(nsIAccessibleEvent::EVENT_WINDOW_MINIMIZE);
}

void
nsWindow::DispatchRestoreEventAccessible(void)
{
    DispatchEventToRootAccessible(nsIAccessibleEvent::EVENT_WINDOW_RESTORE);
}

#endif /* #ifdef ACCESSIBILITY */

// nsChildWindow class

nsChildWindow::nsChildWindow()
{
}

nsChildWindow::~nsChildWindow()
{
}

NS_IMETHODIMP
nsWindow::ResetInputState()
{
    return mIMModule ? mIMModule->ResetInputState(this) : NS_OK;
}

NS_IMETHODIMP
nsWindow::SetInputMode(const IMEContext& aContext)
{
    return mIMModule ? mIMModule->SetInputMode(this, &aContext) : NS_OK;
}

NS_IMETHODIMP
nsWindow::GetInputMode(IMEContext& aContext)
{
  if (!mIMModule) {
      aContext.mStatus = nsIWidget::IME_STATUS_DISABLED;
      return NS_OK;
  }
  return mIMModule->GetInputMode(&aContext);
}

NS_IMETHODIMP
nsWindow::CancelIMEComposition()
{
    return mIMModule ? mIMModule->CancelIMEComposition(this) : NS_OK;
}

NS_IMETHODIMP
nsWindow::OnIMEFocusChange(PRBool aFocus)
{
    if (mIMModule) {
      mIMModule->OnFocusChangeInGecko(aFocus);
    }
    // XXX Return NS_ERROR_NOT_IMPLEMENTED, see bug 496360.
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindow::GetToggledKeyState(PRUint32 aKeyCode, PRBool* aLEDState)
{
    NS_ENSURE_ARG_POINTER(aLEDState);

#ifdef MOZ_X11

    GdkModifierType modifiers = gdk_keyboard_get_modifiers();
    PRUint32 capsLockMask, numLockMask, scrollLockMask;
    PRBool foundMasks = gdk_keyboard_get_modmap_masks(
                          GDK_WINDOW_XDISPLAY(mGdkWindow),
                          &capsLockMask, &numLockMask, &scrollLockMask);
    if (!foundMasks)
        return NS_ERROR_NOT_IMPLEMENTED;

    PRUint32 mask = 0;
    switch (aKeyCode) {
        case NS_VK_CAPS_LOCK:   mask = capsLockMask;   break;
        case NS_VK_NUM_LOCK:    mask = numLockMask;    break;
        case NS_VK_SCROLL_LOCK: mask = scrollLockMask; break;
    }
    if (mask == 0)
        return NS_ERROR_NOT_IMPLEMENTED;

    *aLEDState = (modifiers & mask) != 0;
    return NS_OK;
#else
    return NS_ERROR_NOT_IMPLEMENTED;
#endif /* MOZ_X11 */
}

#if defined(MOZ_X11) && defined(MOZ_WIDGET_GTK2)
/* static */ already_AddRefed<gfxASurface>
nsWindow::GetSurfaceForGdkDrawable(GdkDrawable* aDrawable,
                                   const nsIntSize& aSize)
{
    GdkVisual* visual = gdk_drawable_get_visual(aDrawable);
    Screen* xScreen =
        gdk_x11_screen_get_xscreen(gdk_drawable_get_screen(aDrawable));
    Display* xDisplay = DisplayOfScreen(xScreen);
    Drawable xDrawable = gdk_x11_drawable_get_xid(aDrawable);

    gfxASurface* result = nsnull;

    if (visual) {
        Visual* xVisual = gdk_x11_visual_get_xvisual(visual);

        result = new gfxXlibSurface(xDisplay, xDrawable, xVisual,
                                    gfxIntSize(aSize.width, aSize.height));
    } else {
        // no visual? we must be using an xrender format.  Find a format
        // for this depth.
        XRenderPictFormat *pf = NULL;
        switch (gdk_drawable_get_depth(aDrawable)) {
            case 32:
                pf = XRenderFindStandardFormat(xDisplay, PictStandardARGB32);
                break;
            case 24:
                pf = XRenderFindStandardFormat(xDisplay, PictStandardRGB24);
                break;
            default:
                NS_ERROR("Don't know how to handle the given depth!");
                break;
        }

        result = new gfxXlibSurface(xScreen, xDrawable, pf,
                                    gfxIntSize(aSize.width, aSize.height));
    }

    NS_IF_ADDREF(result);
    return result;
}
#endif

// return the gfxASurface for rendering to this widget
gfxASurface*
#if defined(MOZ_WIDGET_GTK2)
nsWindow::GetThebesSurface()
#else
nsWindow::GetThebesSurface(cairo_t *cr)
#endif
{
    if (!mGdkWindow)
        return nsnull;

#if !defined(MOZ_WIDGET_GTK2)
    cairo_surface_t *surf = cairo_get_target(cr);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
      NS_NOTREACHED("Missing cairo target?");
      return NULL;
    }
#endif // MOZ_WIDGET_GTK2

#ifdef MOZ_X11
    gint width, height;

#if defined(MOZ_WIDGET_GTK2)
    gdk_drawable_get_size(GDK_DRAWABLE(mGdkWindow), &width, &height);
#else
    width = gdk_window_get_width(mGdkWindow);
    height = gdk_window_get_height(mGdkWindow);
#endif

    // Owen Taylor says this is the right thing to do!
    width = NS_MIN(32767, width);
    height = NS_MIN(32767, height);
    gfxIntSize size(width, height);

    GdkVisual *gdkVisual = gdk_window_get_visual(mGdkWindow);
    Visual* visual = gdk_x11_visual_get_xvisual(gdkVisual);

#  ifdef MOZ_HAVE_SHMIMAGE
    PRBool usingShm = PR_FALSE;
    if (nsShmImage::UseShm()) {
        // EnsureShmImage() is a dangerous interface, but we guarantee
        // that the thebes surface and the shmimage have the same
        // lifetime
        mThebesSurface =
            nsShmImage::EnsureShmImage(size,
                                       visual, gdk_visual_get_depth(gdkVisual),
                                       mShmImage);
        usingShm = mThebesSurface != nsnull;
    }
    if (!usingShm)
#  endif  // MOZ_HAVE_SHMIMAGE

#if defined(MOZ_WIDGET_GTK2)
    mThebesSurface = new gfxXlibSurface
        (GDK_WINDOW_XDISPLAY(mGdkWindow),
         gdk_x11_window_get_xid(mGdkWindow),
         visual,
         size);
#else
#if MOZ_TREE_CAIRO
#error "cairo-gtk3 target must be built with --enable-system-cairo"
#else
    mThebesSurface = gfxASurface::Wrap(surf);
#endif
#endif

#endif
#ifdef MOZ_DFB
    // not supported
    mThebesSurface = nsnull;
#endif

    // if the surface creation is reporting an error, then
    // we don't have a surface to give back
    if (mThebesSurface && mThebesSurface->CairoStatus() != 0) {
        mThebesSurface = nsnull;
    }

    return mThebesSurface;
}

// Code shared begin BeginMoveDrag and BeginResizeDrag
PRBool
nsWindow::GetDragInfo(nsMouseEvent* aMouseEvent,
                      GdkWindow** aWindow, gint* aButton,
                      gint* aRootX, gint* aRootY)
{
    if (aMouseEvent->button != nsMouseEvent::eLeftButton) {
        // we can only begin a move drag with the left mouse button
        return PR_FALSE;
    }
    *aButton = 1;

    // get the gdk window for this widget
    GdkWindow* gdk_window = mGdkWindow;
    if (!gdk_window) {
        return PR_FALSE;
    }
    NS_ABORT_IF_FALSE(GDK_IS_WINDOW(gdk_window), "must really be window");

    // find the top-level window
    gdk_window = gdk_window_get_toplevel(gdk_window);
    NS_ABORT_IF_FALSE(gdk_window,
                      "gdk_window_get_toplevel should not return null");
    *aWindow = gdk_window;

    if (!aMouseEvent->widget) {
        return PR_FALSE;
    }

    // FIXME: It would be nice to have the widget position at the time
    // of the event, but it's relatively unlikely that the widget has
    // moved since the mousedown.  (On the other hand, it's quite likely
    // that the mouse has moved, which is why we use the mouse position
    // from the event.)
    nsIntPoint offset = aMouseEvent->widget->WidgetToScreenOffset();
    *aRootX = aMouseEvent->refPoint.x + offset.x;
    *aRootY = aMouseEvent->refPoint.y + offset.y;

    return PR_TRUE;
}

NS_IMETHODIMP
nsWindow::BeginMoveDrag(nsMouseEvent* aEvent)
{
    NS_ABORT_IF_FALSE(aEvent, "must have event");
    NS_ABORT_IF_FALSE(aEvent->eventStructType == NS_MOUSE_EVENT,
                      "event must have correct struct type");

    GdkWindow *gdk_window;
    gint button, screenX, screenY;
    if (!GetDragInfo(aEvent, &gdk_window, &button, &screenX, &screenY)) {
        return NS_ERROR_FAILURE;
    }

    // tell the window manager to start the move
    gdk_window_begin_move_drag(gdk_window, button, screenX, screenY,
                               aEvent->time);

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::BeginResizeDrag(nsGUIEvent* aEvent, PRInt32 aHorizontal, PRInt32 aVertical)
{
    NS_ENSURE_ARG_POINTER(aEvent);

    if (aEvent->eventStructType != NS_MOUSE_EVENT) {
        // you can only begin a resize drag with a mouse event
        return NS_ERROR_INVALID_ARG;
    }

    nsMouseEvent* mouse_event = static_cast<nsMouseEvent*>(aEvent);

    GdkWindow *gdk_window;
    gint button, screenX, screenY;
    if (!GetDragInfo(mouse_event, &gdk_window, &button, &screenX, &screenY)) {
        return NS_ERROR_FAILURE;
    }

    // work out what GdkWindowEdge we're talking about
    GdkWindowEdge window_edge;
    if (aVertical < 0) {
        if (aHorizontal < 0) {
            window_edge = GDK_WINDOW_EDGE_NORTH_WEST;
        } else if (aHorizontal == 0) {
            window_edge = GDK_WINDOW_EDGE_NORTH;
        } else {
            window_edge = GDK_WINDOW_EDGE_NORTH_EAST;
        }
    } else if (aVertical == 0) {
        if (aHorizontal < 0) {
            window_edge = GDK_WINDOW_EDGE_WEST;
        } else if (aHorizontal == 0) {
            return NS_ERROR_INVALID_ARG;
        } else {
            window_edge = GDK_WINDOW_EDGE_EAST;
        }
    } else {
        if (aHorizontal < 0) {
            window_edge = GDK_WINDOW_EDGE_SOUTH_WEST;
        } else if (aHorizontal == 0) {
            window_edge = GDK_WINDOW_EDGE_SOUTH;
        } else {
            window_edge = GDK_WINDOW_EDGE_SOUTH_EAST;
        }
    }

    // tell the window manager to start the resize
    gdk_window_begin_resize_drag(gdk_window, window_edge, button,
                                 screenX, screenY, aEvent->time);

    return NS_OK;
}

void
nsWindow::ClearCachedResources()
{
    if (mLayerManager &&
        mLayerManager->GetBackendType() == LayerManager::LAYERS_BASIC) {
        static_cast<BasicLayerManager*> (mLayerManager.get())->
            ClearCachedResources();
    }

    GList* children = gdk_window_peek_children(mGdkWindow);
    for (GList* list = children; list; list = list->next) {
        nsWindow* window = get_window_for_gdk_window(GDK_WINDOW(list->data));
        if (window) {
            window->ClearCachedResources();
        }
    }
}

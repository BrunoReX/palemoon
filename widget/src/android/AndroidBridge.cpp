/* -*- Mode: c++; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
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
#include <dlfcn.h>

#include "nsXULAppAPI.h"
#include <pthread.h>
#include <prthread.h>
#include "nsXPCOMStrings.h"

#include "AndroidBridge.h"
#include "nsAppShell.h"
#include "nsOSHelperAppService.h"
#include "nsWindow.h"
#include "mozilla/Preferences.h"

#ifdef DEBUG
#define ALOG_BRIDGE(args...) ALOG(args)
#else
#define ALOG_BRIDGE(args...)
#endif

#define IME_FULLSCREEN_PREF "widget.ime.android.landscape_fullscreen"
#define IME_FULLSCREEN_THRESHOLD_PREF "widget.ime.android.fullscreen_threshold"

using namespace mozilla;

static PRUintn sJavaEnvThreadIndex = 0;

AndroidBridge *AndroidBridge::sBridge = 0;

static void
JavaThreadDetachFunc(void *arg)
{
    JNIEnv *env = (JNIEnv*) arg;
    JavaVM *vm = NULL;
    env->GetJavaVM(&vm);
    vm->DetachCurrentThread();
}

AndroidBridge *
AndroidBridge::ConstructBridge(JNIEnv *jEnv,
                               jclass jGeckoAppShellClass)
{
    /* NSS hack -- bionic doesn't handle recursive unloads correctly,
     * because library finalizer functions are called with the dynamic
     * linker lock still held.  This results in a deadlock when trying
     * to call dlclose() while we're already inside dlclose().
     * Conveniently, NSS has an env var that can prevent it from unloading.
     */
    putenv(strdup("NSS_DISABLE_UNLOAD=1"));

    sBridge = new AndroidBridge();
    if (!sBridge->Init(jEnv, jGeckoAppShellClass)) {
        delete sBridge;
        sBridge = 0;
    }

    PR_NewThreadPrivateIndex(&sJavaEnvThreadIndex, JavaThreadDetachFunc);

    return sBridge;
}

PRBool
AndroidBridge::Init(JNIEnv *jEnv,
                    jclass jGeckoAppShellClass)
{
    ALOG_BRIDGE("AndroidBridge::Init");
    jEnv->GetJavaVM(&mJavaVM);

    mJNIEnv = nsnull;
    mThread = nsnull;
    mOpenedBitmapLibrary = false;
    mHasNativeBitmapAccess = false;

    mGeckoAppShellClass = (jclass) jEnv->NewGlobalRef(jGeckoAppShellClass);

    jNotifyIME = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "notifyIME", "(II)V");
    jNotifyIMEEnabled = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "notifyIMEEnabled", "(ILjava/lang/String;Ljava/lang/String;Z)V");
    jNotifyIMEChange = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "notifyIMEChange", "(Ljava/lang/String;III)V");
    jAcknowledgeEventSync = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "acknowledgeEventSync", "()V");

    jEnableDeviceMotion = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "enableDeviceMotion", "(Z)V");
    jEnableLocation = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "enableLocation", "(Z)V");
    jReturnIMEQueryResult = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "returnIMEQueryResult", "(Ljava/lang/String;II)V");
    jScheduleRestart = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "scheduleRestart", "()V");
    jNotifyAppShellReady = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "onAppShellReady", "()V");
    jNotifyXreExit = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "onXreExit", "()V");
    jGetHandlersForMimeType = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "getHandlersForMimeType", "(Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;");
    jGetHandlersForURL = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "getHandlersForURL", "(Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;");
    jOpenUriExternal = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "openUriExternal", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z");
    jGetMimeTypeFromExtensions = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "getMimeTypeFromExtensions", "(Ljava/lang/String;)Ljava/lang/String;");
    jGetExtensionFromMimeType = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "getExtensionFromMimeType", "(Ljava/lang/String;)Ljava/lang/String;");
    jMoveTaskToBack = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "moveTaskToBack", "()V");
    jGetClipboardText = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "getClipboardText", "()Ljava/lang/String;");
    jSetClipboardText = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "setClipboardText", "(Ljava/lang/String;)V");
    jShowAlertNotification = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "showAlertNotification", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    jShowFilePicker = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "showFilePicker", "(Ljava/lang/String;)Ljava/lang/String;");
    jAlertsProgressListener_OnProgress = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "alertsProgressListener_OnProgress", "(Ljava/lang/String;JJLjava/lang/String;)V");
    jAlertsProgressListener_OnCancel = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "alertsProgressListener_OnCancel", "(Ljava/lang/String;)V");
    jGetDpi = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "getDpi", "()I");
    jSetFullScreen = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "setFullScreen", "(Z)V");
    jShowInputMethodPicker = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "showInputMethodPicker", "()V");
    jHideProgressDialog = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "hideProgressDialog", "()V");
    jPerformHapticFeedback = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "performHapticFeedback", "(Z)V");
    jSetKeepScreenOn = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "setKeepScreenOn", "(Z)V");
    jIsNetworkLinkUp = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "isNetworkLinkUp", "()Z");
    jIsNetworkLinkKnown = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "isNetworkLinkKnown", "()Z");
    jSetSelectedLocale = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "setSelectedLocale", "(Ljava/lang/String;)V");
    jScanMedia = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "scanMedia", "(Ljava/lang/String;Ljava/lang/String;)V");
    jGetSystemColors = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "getSystemColors", "()[I");
    jGetIconForExtension = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "getIconForExtension", "(Ljava/lang/String;I)[B");
    jCreateShortcut = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "createShortcut", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    jGetShowPasswordSetting = (jmethodID) jEnv->GetStaticMethodID(jGeckoAppShellClass, "getShowPasswordSetting", "()Z");

    jEGLContextClass = (jclass) jEnv->NewGlobalRef(jEnv->FindClass("javax/microedition/khronos/egl/EGLContext"));
    jEGL10Class = (jclass) jEnv->NewGlobalRef(jEnv->FindClass("javax/microedition/khronos/egl/EGL10"));
    jEGLSurfaceImplClass = (jclass) jEnv->NewGlobalRef(jEnv->FindClass("com/google/android/gles_jni/EGLSurfaceImpl"));
    jEGLContextImplClass = (jclass) jEnv->NewGlobalRef(jEnv->FindClass("com/google/android/gles_jni/EGLContextImpl"));
    jEGLConfigImplClass = (jclass) jEnv->NewGlobalRef(jEnv->FindClass("com/google/android/gles_jni/EGLConfigImpl"));
    jEGLDisplayImplClass = (jclass) jEnv->NewGlobalRef(jEnv->FindClass("com/google/android/gles_jni/EGLDisplayImpl"));

    InitAndroidJavaWrappers(jEnv);

    // jEnv should NOT be cached here by anything -- the jEnv here
    // is not valid for the real gecko main thread, which is set
    // at SetMainThread time.

    return PR_TRUE;
}

JNIEnv *
AndroidBridge::AttachThread(PRBool asDaemon)
{
    ALOG_BRIDGE("AndroidBridge::AttachThread");
    JNIEnv *jEnv = (JNIEnv*) PR_GetThreadPrivate(sJavaEnvThreadIndex);
    if (jEnv)
        return jEnv;

    JavaVMAttachArgs args = {
        JNI_VERSION_1_2,
        "GeckoThread",
        NULL
    };

    jint res = 0;

    if (asDaemon) {
        res = mJavaVM->AttachCurrentThreadAsDaemon(&jEnv, &args);
    } else {
        res = mJavaVM->AttachCurrentThread(&jEnv, &args);
    }

    if (res != 0) {
        ALOG_BRIDGE("AttachCurrentThread failed!");
        return nsnull;
    }

    PR_SetThreadPrivate(sJavaEnvThreadIndex, jEnv);

    return jEnv;
}

PRBool
AndroidBridge::SetMainThread(void *thr)
{
    ALOG_BRIDGE("AndroidBridge::SetMainThread");
    if (thr) {
        mJNIEnv = AttachThread(PR_FALSE);
        if (!mJNIEnv)
            return PR_FALSE;

        mThread = thr;
    } else {
        mJNIEnv = nsnull;
        mThread = nsnull;
    }

    return PR_TRUE;
}

void
AndroidBridge::EnsureJNIThread()
{
    JNIEnv *env;
    if (mJavaVM->AttachCurrentThread(&env, NULL) != 0) {
        ALOG_BRIDGE("EnsureJNIThread: test Attach failed!");
        return;
    }

    if ((void*)pthread_self() != mThread) {
        ALOG_BRIDGE("###!!!!!!! Something's grabbing the JNIEnv from the wrong thread! (thr %p should be %p)",
             (void*)pthread_self(), (void*)mThread);
    }
}

void
AndroidBridge::NotifyIME(int aType, int aState)
{
    ALOG_BRIDGE("AndroidBridge::NotifyIME");
    if (sBridge)
        JNI()->CallStaticVoidMethod(sBridge->mGeckoAppShellClass, 
                                    sBridge->jNotifyIME,  aType, aState);
}

void
AndroidBridge::NotifyIMEEnabled(int aState, const nsAString& aTypeHint,
                                const nsAString& aActionHint)
{
    ALOG_BRIDGE("AndroidBridge::NotifyIMEEnabled");
    if (!sBridge)
        return;

    nsPromiseFlatString typeHint(aTypeHint);
    nsPromiseFlatString actionHint(aActionHint);

    jvalue args[4];
    AutoLocalJNIFrame jniFrame(1);
    args[0].i = aState;
    args[1].l = JNI()->NewString(typeHint.get(), typeHint.Length());
    args[2].l = JNI()->NewString(actionHint.get(), actionHint.Length());
    args[3].z = false;

    PRInt32 landscapeFS;
    if (NS_SUCCEEDED(Preferences::GetInt(IME_FULLSCREEN_PREF, &landscapeFS))) {
        if (landscapeFS == 1) {
            args[3].z = true;
        } else if (landscapeFS == -1){
            if (NS_SUCCEEDED(
                  Preferences::GetInt(IME_FULLSCREEN_THRESHOLD_PREF,
                                      &landscapeFS))) {
                // the threshold is hundreths of inches, so convert the 
                // threshold to pixels and multiply the height by 100
                if (nsWindow::GetAndroidScreenBounds().height  * 100 < 
                    landscapeFS * Bridge()->GetDPI()) {
                    args[3].z = true;
                }
            }

        }
    }

    JNI()->CallStaticVoidMethodA(sBridge->mGeckoAppShellClass,
                                 sBridge->jNotifyIMEEnabled, args);
}

void
AndroidBridge::NotifyIMEChange(const PRUnichar *aText, PRUint32 aTextLen,
                               int aStart, int aEnd, int aNewEnd)
{
    ALOG_BRIDGE("AndroidBridge::NotifyIMEChange");
    if (!sBridge) {
        return;
    }

    jvalue args[4];
    AutoLocalJNIFrame jniFrame(1);
    args[0].l = JNI()->NewString(aText, aTextLen);
    args[1].i = aStart;
    args[2].i = aEnd;
    args[3].i = aNewEnd;
    JNI()->CallStaticVoidMethodA(sBridge->mGeckoAppShellClass,
                                     sBridge->jNotifyIMEChange, args);
}

void
AndroidBridge::AcknowledgeEventSync()
{
    ALOG_BRIDGE("AndroidBridge::AcknowledgeEventSync");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jAcknowledgeEventSync);
}

void
AndroidBridge::EnableDeviceMotion(bool aEnable)
{
    ALOG_BRIDGE("AndroidBridge::EnableDeviceMotion");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jEnableDeviceMotion, aEnable);
}

void
AndroidBridge::EnableLocation(bool aEnable)
{
    ALOG_BRIDGE("AndroidBridge::EnableLocation");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jEnableLocation, aEnable);
}

void
AndroidBridge::ReturnIMEQueryResult(const PRUnichar *aResult, PRUint32 aLen,
                                    int aSelStart, int aSelLen)
{
    ALOG_BRIDGE("AndroidBridge::ReturnIMEQueryResult");
    jvalue args[3];
    AutoLocalJNIFrame jniFrame(1);
    args[0].l = mJNIEnv->NewString(aResult, aLen);
    args[1].i = aSelStart;
    args[2].i = aSelLen;
    mJNIEnv->CallStaticVoidMethodA(mGeckoAppShellClass,
                                   jReturnIMEQueryResult, args);
}

void
AndroidBridge::NotifyAppShellReady()
{
    ALOG_BRIDGE("AndroidBridge::NotifyAppShellReady");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jNotifyAppShellReady);
}

void
AndroidBridge::ScheduleRestart()
{
    ALOG_BRIDGE("scheduling reboot");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jScheduleRestart);
}

void
AndroidBridge::NotifyXreExit()
{
    ALOG_BRIDGE("xre exiting");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jNotifyXreExit);
}

static void 
getHandlersFromStringArray(JNIEnv *aJNIEnv, jobjectArray jArr, jsize aLen,
                           nsIMutableArray *aHandlersArray,
                           nsIHandlerApp **aDefaultApp,
                           const nsAString& aAction = EmptyString(),
                           const nsACString& aMimeType = EmptyCString())
{
    nsString empty = EmptyString();
    for (jsize i = 0; i < aLen; i+=4) {
        nsJNIString name( 
            static_cast<jstring>(aJNIEnv->GetObjectArrayElement(jArr, i)));
        nsJNIString isDefault(
            static_cast<jstring>(aJNIEnv->GetObjectArrayElement(jArr, i + 1)));
        nsJNIString packageName( 
            static_cast<jstring>(aJNIEnv->GetObjectArrayElement(jArr, i + 2)));
        nsJNIString className( 
            static_cast<jstring>(aJNIEnv->GetObjectArrayElement(jArr, i + 3)));
        nsIHandlerApp* app = nsOSHelperAppService::
            CreateAndroidHandlerApp(name, className, packageName,
                                    className, aMimeType, aAction);
        
        aHandlersArray->AppendElement(app, PR_FALSE);
        if (aDefaultApp && isDefault.Length() > 0)
            *aDefaultApp = app;
    }
}

PRBool
AndroidBridge::GetHandlersForMimeType(const char *aMimeType,
                                      nsIMutableArray *aHandlersArray,
                                      nsIHandlerApp **aDefaultApp,
                                      const nsAString& aAction)
{
    ALOG_BRIDGE("AndroidBridge::GetHandlersForMimeType");

    AutoLocalJNIFrame jniFrame;
    NS_ConvertUTF8toUTF16 wMimeType(aMimeType);
    jstring jstrMimeType =
        mJNIEnv->NewString(wMimeType.get(), wMimeType.Length());

    jstring jstrAction = mJNIEnv->NewString(nsPromiseFlatString(aAction).get(),
                                            aAction.Length());

    jobject obj = mJNIEnv->CallStaticObjectMethod(mGeckoAppShellClass,
                                                  jGetHandlersForMimeType,
                                                  jstrMimeType, jstrAction);
    jobjectArray arr = static_cast<jobjectArray>(obj);
    if (!arr)
        return PR_FALSE;

    jsize len = mJNIEnv->GetArrayLength(arr);

    if (!aHandlersArray)
        return len > 0;

    getHandlersFromStringArray(mJNIEnv, arr, len, aHandlersArray, 
                               aDefaultApp, aAction,
                               nsDependentCString(aMimeType));
    return PR_TRUE;
}

PRBool
AndroidBridge::GetHandlersForURL(const char *aURL,
                                      nsIMutableArray* aHandlersArray,
                                      nsIHandlerApp **aDefaultApp,
                                      const nsAString& aAction)
{
    ALOG_BRIDGE("AndroidBridge::GetHandlersForURL");

    AutoLocalJNIFrame jniFrame;
    NS_ConvertUTF8toUTF16 wScheme(aURL);
    jstring jstrScheme = mJNIEnv->NewString(wScheme.get(), wScheme.Length());
    jstring jstrAction = mJNIEnv->NewString(nsPromiseFlatString(aAction).get(),
                                            aAction.Length());

    jobject obj = mJNIEnv->CallStaticObjectMethod(mGeckoAppShellClass,
                                                  jGetHandlersForURL,
                                                  jstrScheme, jstrAction);
    jobjectArray arr = static_cast<jobjectArray>(obj);
    if (!arr)
        return PR_FALSE;

    jsize len = mJNIEnv->GetArrayLength(arr);

    if (!aHandlersArray)
        return len > 0;

    getHandlersFromStringArray(mJNIEnv, arr, len, aHandlersArray, 
                               aDefaultApp, aAction);
    return PR_TRUE;
}

PRBool
AndroidBridge::OpenUriExternal(const nsACString& aUriSpec, const nsACString& aMimeType,
                               const nsAString& aPackageName, const nsAString& aClassName,
                               const nsAString& aAction, const nsAString& aTitle)
{
    ALOG_BRIDGE("AndroidBridge::OpenUriExternal");

    AutoLocalJNIFrame jniFrame;
    NS_ConvertUTF8toUTF16 wUriSpec(aUriSpec);
    NS_ConvertUTF8toUTF16 wMimeType(aMimeType);

    jstring jstrUri = mJNIEnv->NewString(wUriSpec.get(), wUriSpec.Length());
    jstring jstrType = mJNIEnv->NewString(wMimeType.get(), wMimeType.Length());

    jstring jstrPackage = mJNIEnv->NewString(nsPromiseFlatString(aPackageName).get(),
                                             aPackageName.Length());
    jstring jstrClass = mJNIEnv->NewString(nsPromiseFlatString(aClassName).get(),
                                           aClassName.Length());
    jstring jstrAction = mJNIEnv->NewString(nsPromiseFlatString(aAction).get(),
                                            aAction.Length());
    jstring jstrTitle = mJNIEnv->NewString(nsPromiseFlatString(aTitle).get(),
                                           aTitle.Length());

    return mJNIEnv->CallStaticBooleanMethod(mGeckoAppShellClass,
                                            jOpenUriExternal,
                                            jstrUri, jstrType, jstrPackage, 
                                            jstrClass, jstrAction, jstrTitle);
}

void
AndroidBridge::GetMimeTypeFromExtensions(const nsACString& aFileExt, nsCString& aMimeType)
{
    ALOG_BRIDGE("AndroidBridge::GetMimeTypeFromExtensions");

    AutoLocalJNIFrame jniFrame;
    NS_ConvertUTF8toUTF16 wFileExt(aFileExt);
    jstring jstrExt = mJNIEnv->NewString(wFileExt.get(), wFileExt.Length());
    jstring jstrType =  static_cast<jstring>(
        mJNIEnv->CallStaticObjectMethod(mGeckoAppShellClass,
                                        jGetMimeTypeFromExtensions,
                                        jstrExt));
    nsJNIString jniStr(jstrType);
    aMimeType.Assign(NS_ConvertUTF16toUTF8(jniStr.get()));
}

void
AndroidBridge::GetExtensionFromMimeType(const nsCString& aMimeType, nsACString& aFileExt)
{
    ALOG_BRIDGE("AndroidBridge::GetExtensionFromMimeType");

    AutoLocalJNIFrame jniFrame;
    NS_ConvertUTF8toUTF16 wMimeType(aMimeType);
    jstring jstrType = mJNIEnv->NewString(wMimeType.get(), wMimeType.Length());
    jstring jstrExt = static_cast<jstring>(
        mJNIEnv->CallStaticObjectMethod(mGeckoAppShellClass,
                                        jGetExtensionFromMimeType,
                                        jstrType));
    nsJNIString jniStr(jstrExt);
    aFileExt.Assign(NS_ConvertUTF16toUTF8(jniStr.get()));
}

void
AndroidBridge::MoveTaskToBack()
{
    ALOG_BRIDGE("AndroidBridge::MoveTaskToBack");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jMoveTaskToBack);
}

bool
AndroidBridge::GetClipboardText(nsAString& aText)
{
    ALOG_BRIDGE("AndroidBridge::GetClipboardText");

    jstring jstrType =  
        static_cast<jstring>(mJNIEnv->
                             CallStaticObjectMethod(mGeckoAppShellClass,
                                                    jGetClipboardText));
    if (!jstrType)
        return PR_FALSE;
    nsJNIString jniStr(jstrType);
    aText.Assign(jniStr);
    return PR_TRUE;
}

void
AndroidBridge::SetClipboardText(const nsAString& aText)
{
    ALOG_BRIDGE("AndroidBridge::SetClipboardText");
    AutoLocalJNIFrame jniFrame;
    jstring jstr = mJNIEnv->NewString(nsPromiseFlatString(aText).get(),
                                      aText.Length());
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jSetClipboardText, jstr);
}

bool
AndroidBridge::ClipboardHasText()
{
    ALOG_BRIDGE("AndroidBridge::ClipboardHasText");

    jstring jstrType =  
        static_cast<jstring>(mJNIEnv->
                             CallStaticObjectMethod(mGeckoAppShellClass,
                                                    jGetClipboardText));
    if (!jstrType)
        return PR_FALSE;
    return PR_TRUE;
}

void
AndroidBridge::EmptyClipboard()
{
    ALOG_BRIDGE("AndroidBridge::EmptyClipboard");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jSetClipboardText, nsnull);
}

void
AndroidBridge::ShowAlertNotification(const nsAString& aImageUrl,
                                     const nsAString& aAlertTitle,
                                     const nsAString& aAlertText,
                                     const nsAString& aAlertCookie,
                                     nsIObserver *aAlertListener,
                                     const nsAString& aAlertName)
{
    ALOG_BRIDGE("ShowAlertNotification");
    AutoLocalJNIFrame jniFrame;

    if (nsAppShell::gAppShell && aAlertListener)
        nsAppShell::gAppShell->AddObserver(aAlertName, aAlertListener);

    jvalue args[5];
    args[0].l = mJNIEnv->NewString(nsPromiseFlatString(aImageUrl).get(), aImageUrl.Length());
    args[1].l = mJNIEnv->NewString(nsPromiseFlatString(aAlertTitle).get(), aAlertTitle.Length());
    args[2].l = mJNIEnv->NewString(nsPromiseFlatString(aAlertText).get(), aAlertText.Length());
    args[3].l = mJNIEnv->NewString(nsPromiseFlatString(aAlertCookie).get(), aAlertCookie.Length());
    args[4].l = mJNIEnv->NewString(nsPromiseFlatString(aAlertName).get(), aAlertName.Length());
    mJNIEnv->CallStaticVoidMethodA(mGeckoAppShellClass, jShowAlertNotification, args);
}

void
AndroidBridge::AlertsProgressListener_OnProgress(const nsAString& aAlertName,
                                                 PRInt64 aProgress,
                                                 PRInt64 aProgressMax,
                                                 const nsAString& aAlertText)
{
    ALOG_BRIDGE("AlertsProgressListener_OnProgress");
    AutoLocalJNIFrame jniFrame;

    jstring jstrName = mJNIEnv->NewString(nsPromiseFlatString(aAlertName).get(), aAlertName.Length());
    jstring jstrText = mJNIEnv->NewString(nsPromiseFlatString(aAlertText).get(), aAlertText.Length());
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jAlertsProgressListener_OnProgress,
                                  jstrName, aProgress, aProgressMax, jstrText);
}

void
AndroidBridge::AlertsProgressListener_OnCancel(const nsAString& aAlertName)
{
    ALOG_BRIDGE("AlertsProgressListener_OnCancel");
    AutoLocalJNIFrame jniFrame;

    jstring jstrName = mJNIEnv->NewString(nsPromiseFlatString(aAlertName).get(), aAlertName.Length());
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jAlertsProgressListener_OnCancel, jstrName);
}


int
AndroidBridge::GetDPI()
{
    ALOG_BRIDGE("AndroidBridge::GetDPI");
    return (int) mJNIEnv->CallStaticIntMethod(mGeckoAppShellClass, jGetDpi);
}

void
AndroidBridge::ShowFilePicker(nsAString& aFilePath, nsAString& aFilters)
{
    ALOG_BRIDGE("AndroidBridge::ShowFilePicker");

    AutoLocalJNIFrame jniFrame;
    jstring jstrFilers = mJNIEnv->NewString(nsPromiseFlatString(aFilters).get(),
                                            aFilters.Length());
    jstring jstr =  static_cast<jstring>(mJNIEnv->CallStaticObjectMethod(
                                             mGeckoAppShellClass,
                                             jShowFilePicker, jstrFilers));
    aFilePath.Assign(nsJNIString(jstr));
}

void
AndroidBridge::SetFullScreen(PRBool aFullScreen)
{
    ALOG_BRIDGE("AndroidBridge::SetFullScreen");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jSetFullScreen, aFullScreen);
}

void
AndroidBridge::HideProgressDialogOnce()
{
    static bool once = false;
    if (!once) {
        ALOG_BRIDGE("AndroidBridge::HideProgressDialogOnce");
        mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jHideProgressDialog);
        once = true;
    }
}

void
AndroidBridge::PerformHapticFeedback(PRBool aIsLongPress)
{
    ALOG_BRIDGE("AndroidBridge::PerformHapticFeedback");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass,
                                    jPerformHapticFeedback, aIsLongPress);
}

bool
AndroidBridge::IsNetworkLinkUp()
{
    ALOG_BRIDGE("AndroidBridge::IsNetworkLinkUp");
    return !!mJNIEnv->CallStaticBooleanMethod(mGeckoAppShellClass, jIsNetworkLinkUp);
}

bool
AndroidBridge::IsNetworkLinkKnown()
{
    ALOG_BRIDGE("AndroidBridge::IsNetworkLinkKnown");
    return !!mJNIEnv->CallStaticBooleanMethod(mGeckoAppShellClass, jIsNetworkLinkKnown);
}

void
AndroidBridge::SetSelectedLocale(const nsAString& aLocale)
{
    ALOG_BRIDGE("AndroidBridge::SetSelectedLocale");
    AutoLocalJNIFrame jniFrame;
    jstring jLocale = GetJNIForThread()->NewString(PromiseFlatString(aLocale).get(), aLocale.Length());
    GetJNIForThread()->CallStaticVoidMethod(mGeckoAppShellClass, jSetSelectedLocale, jLocale);
}

void
AndroidBridge::GetSystemColors(AndroidSystemColors *aColors)
{
    ALOG_BRIDGE("AndroidBridge::GetSystemColors");

    NS_ASSERTION(aColors != nsnull, "AndroidBridge::GetSystemColors: aColors is null!");
    if (!aColors)
        return;

    AutoLocalJNIFrame jniFrame;

    jobject obj = mJNIEnv->CallStaticObjectMethod(mGeckoAppShellClass, jGetSystemColors);
    jintArray arr = static_cast<jintArray>(obj);
    if (!arr)
        return;

    jsize len = mJNIEnv->GetArrayLength(arr);
    jint *elements = mJNIEnv->GetIntArrayElements(arr, 0);

    PRUint32 colorsCount = sizeof(AndroidSystemColors) / sizeof(nscolor);
    if (len < colorsCount)
        colorsCount = len;

    // Convert Android colors to nscolor by switching R and B in the ARGB 32 bit value
    nscolor *colors = (nscolor*)aColors;

    for (PRUint32 i = 0; i < colorsCount; i++) {
        PRUint32 androidColor = static_cast<PRUint32>(elements[i]);
        PRUint8 r = (androidColor & 0x00ff0000) >> 16;
        PRUint8 b = (androidColor & 0x000000ff);
        colors[i] = androidColor & 0xff00ff00 | b << 16 | r;
    }

    mJNIEnv->ReleaseIntArrayElements(arr, elements, 0);
}

void
AndroidBridge::GetIconForExtension(const nsACString& aFileExt, PRUint32 aIconSize, PRUint8 * const aBuf)
{
    ALOG_BRIDGE("AndroidBridge::GetIconForExtension");
    NS_ASSERTION(aBuf != nsnull, "AndroidBridge::GetIconForExtension: aBuf is null!");
    if (!aBuf)
        return;

    AutoLocalJNIFrame jniFrame;

    nsString fileExt;
    CopyUTF8toUTF16(aFileExt, fileExt);
    jstring jstrFileExt = mJNIEnv->NewString(nsPromiseFlatString(fileExt).get(), fileExt.Length());
    
    jobject obj = mJNIEnv->CallStaticObjectMethod(mGeckoAppShellClass, jGetIconForExtension, jstrFileExt, aIconSize);
    jbyteArray arr = static_cast<jbyteArray>(obj);
    NS_ASSERTION(arr != nsnull, "AndroidBridge::GetIconForExtension: Returned pixels array is null!");
    if (!arr)
        return;

    jsize len = mJNIEnv->GetArrayLength(arr);
    jbyte *elements = mJNIEnv->GetByteArrayElements(arr, 0);

    PRUint32 bufSize = aIconSize * aIconSize * 4;
    NS_ASSERTION(len == bufSize, "AndroidBridge::GetIconForExtension: Pixels array is incomplete!");
    if (len == bufSize)
        memcpy(aBuf, elements, bufSize);

    mJNIEnv->ReleaseByteArrayElements(arr, elements, 0);
}

bool
AndroidBridge::GetShowPasswordSetting()
{
    ALOG_BRIDGE("AndroidBridge::GetShowPasswordSetting");
    return mJNIEnv->CallStaticBooleanMethod(mGeckoAppShellClass, jGetShowPasswordSetting);
}

void
AndroidBridge::SetSurfaceView(jobject obj)
{
    mSurfaceView.Init(obj);
}

void
AndroidBridge::ShowInputMethodPicker()
{
    ALOG_BRIDGE("AndroidBridge::ShowInputMethodPicker");
    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jShowInputMethodPicker);
}

void *
AndroidBridge::CallEglCreateWindowSurface(void *dpy, void *config, AndroidGeckoSurfaceView &sview)
{
    ALOG_BRIDGE("AndroidBridge::CallEglCreateWindowSurface");
    AutoLocalJNIFrame jniFrame;

    /*
     * This is basically:
     *
     *    s = EGLContext.getEGL().eglCreateWindowSurface(new EGLDisplayImpl(dpy),
     *                                                   new EGLConfigImpl(config),
     *                                                   view.getHolder(), null);
     *    return s.mEGLSurface;
     *
     * We can't do it from java, because the EGLConfigImpl constructor is private.
     */

    jobject surfaceHolder = sview.GetSurfaceHolder();
    if (!surfaceHolder)
        return nsnull;

    // grab some fields and methods we'll need
    jmethodID constructConfig = mJNIEnv->GetMethodID(jEGLConfigImplClass, "<init>", "(I)V");
    jmethodID constructDisplay = mJNIEnv->GetMethodID(jEGLDisplayImplClass, "<init>", "(I)V");

    jmethodID getEgl = mJNIEnv->GetStaticMethodID(jEGLContextClass, "getEGL", "()Ljavax/microedition/khronos/egl/EGL;");
    jmethodID createWindowSurface = mJNIEnv->GetMethodID(jEGL10Class, "eglCreateWindowSurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljava/lang/Object;[I)Ljavax/microedition/khronos/egl/EGLSurface;");

    jobject egl = mJNIEnv->CallStaticObjectMethod(jEGLContextClass, getEgl);

    jobject jdpy = mJNIEnv->NewObject(jEGLDisplayImplClass, constructDisplay, (int) dpy);
    jobject jconf = mJNIEnv->NewObject(jEGLConfigImplClass, constructConfig, (int) config);

    // make the call
    jobject surf = mJNIEnv->CallObjectMethod(egl, createWindowSurface, jdpy, jconf, surfaceHolder, NULL);
    if (!surf)
        return nsnull;

    jfieldID sfield = mJNIEnv->GetFieldID(jEGLSurfaceImplClass, "mEGLSurface", "I");

    jint realSurface = mJNIEnv->GetIntField(surf, sfield);

    return (void*) realSurface;
}

bool
AndroidBridge::GetStaticIntField(const char *className, const char *fieldName, PRInt32* aInt)
{
    ALOG_BRIDGE("AndroidBridge::GetStaticIntField %s", fieldName);
    AutoLocalJNIFrame jniFrame(3);
    jclass cls = mJNIEnv->FindClass(className);
    if (!cls)
        return false;

    jfieldID field = mJNIEnv->GetStaticFieldID(cls, fieldName, "I");
    if (!field)
        return false;

    *aInt = static_cast<PRInt32>(mJNIEnv->GetStaticIntField(cls, field));

    return true;
}

bool
AndroidBridge::GetStaticStringField(const char *className, const char *fieldName, nsAString &result)
{
    ALOG_BRIDGE("AndroidBridge::GetStaticIntField %s", fieldName);

    AutoLocalJNIFrame jniFrame(3);
    jclass cls = mJNIEnv->FindClass(className);
    if (!cls)
        return false;

    jfieldID field = mJNIEnv->GetStaticFieldID(cls, fieldName, "Ljava/lang/String;");
    if (!field)
        return false;

    jstring jstr = (jstring) mJNIEnv->GetStaticObjectField(cls, field);
    if (!jstr)
        return false;

    result.Assign(nsJNIString(jstr));
    return true;
}

void
AndroidBridge::SetKeepScreenOn(bool on)
{
    ALOG_BRIDGE("AndroidBridge::SetKeepScreenOn");
    JNI()->CallStaticVoidMethod(sBridge->mGeckoAppShellClass,
                                sBridge->jSetKeepScreenOn, on);
}

// Available for places elsewhere in the code to link to.
PRBool
mozilla_AndroidBridge_SetMainThread(void *thr)
{
    return AndroidBridge::Bridge()->SetMainThread(thr);
}

JavaVM *
mozilla_AndroidBridge_GetJavaVM()
{
    return AndroidBridge::Bridge()->VM();
}

JNIEnv *
mozilla_AndroidBridge_AttachThread(PRBool asDaemon)
{
    return AndroidBridge::Bridge()->AttachThread(asDaemon);
}

extern "C" JNIEnv * GetJNIForThread()
{
    return mozilla::AndroidBridge::JNIForThread();
}

jclass GetGeckoAppShellClass()
{
    return mozilla::AndroidBridge::GetGeckoAppShellClass();
}

void
AndroidBridge::ScanMedia(const nsAString& aFile, const nsACString& aMimeType)
{
    AutoLocalJNIFrame jniFrame;
    jstring jstrFile = mJNIEnv->NewString(nsPromiseFlatString(aFile).get(), aFile.Length());

    nsString mimeType2;
    CopyUTF8toUTF16(aMimeType, mimeType2);
    jstring jstrMimeTypes = mJNIEnv->NewString(nsPromiseFlatString(mimeType2).get(), mimeType2.Length());

    mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jScanMedia, jstrFile, jstrMimeTypes);
}

void
AndroidBridge::CreateShortcut(const nsAString& aTitle, const nsAString& aURI, const nsAString& aIconData, const nsAString& aIntent)
{
  AutoLocalJNIFrame jniFrame;
  jstring jstrTitle = mJNIEnv->NewString(nsPromiseFlatString(aTitle).get(), aTitle.Length());
  jstring jstrURI = mJNIEnv->NewString(nsPromiseFlatString(aURI).get(), aURI.Length());
  jstring jstrIconData = mJNIEnv->NewString(nsPromiseFlatString(aIconData).get(), aIconData.Length());
  jstring jstrIntent = mJNIEnv->NewString(nsPromiseFlatString(aIntent).get(), aIntent.Length());
  
  if (!jstrURI || !jstrTitle || !jstrIconData)
    return;
    
  mJNIEnv->CallStaticVoidMethod(mGeckoAppShellClass, jCreateShortcut, jstrTitle, jstrURI, jstrIconData, jstrIntent);
}

bool
AndroidBridge::HasNativeBitmapAccess()
{
    if (!mOpenedBitmapLibrary) {
        // Try to dlopen libjnigraphics.so for direct bitmap access on
        // Android 2.2+ (API level 8)
        mOpenedBitmapLibrary = true;

        void *handle = dlopen("/system/lib/libjnigraphics.so", RTLD_LAZY | RTLD_LOCAL);
        if (handle == nsnull)
            return false;

        AndroidBitmap_getInfo = (int (*)(JNIEnv *, jobject, void *))dlsym(handle, "AndroidBitmap_getInfo");
        if (AndroidBitmap_getInfo == nsnull)
            return false;

        AndroidBitmap_lockPixels = (int (*)(JNIEnv *, jobject, void **))dlsym(handle, "AndroidBitmap_lockPixels");
        if (AndroidBitmap_lockPixels == nsnull)
            return false;

        AndroidBitmap_unlockPixels = (int (*)(JNIEnv *, jobject))dlsym(handle, "AndroidBitmap_unlockPixels");
        if (AndroidBitmap_unlockPixels == nsnull)
            return false;

        ALOG_BRIDGE("Successfully opened libjnigraphics.so");
        mHasNativeBitmapAccess = true;
    }

    return mHasNativeBitmapAccess;
}

bool
AndroidBridge::ValidateBitmap(jobject bitmap, int width, int height)
{
    // This structure is defined in Android API level 8's <android/bitmap.h>
    // Because we can't depend on this, we get the function pointers via dlsym
    // and define this struct ourselves.
    struct BitmapInfo {
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        uint32_t format;
        uint32_t flags;
    };

    int err;
    struct BitmapInfo info = { 0, };

    if ((err = AndroidBitmap_getInfo(JNI(), bitmap, &info)) != 0) {
        ALOG_BRIDGE("AndroidBitmap_getInfo failed! (error %d)", err);
        return false;
    }

    if (info.width != width || info.height != height)
        return false;

    return true;
}

void *
AndroidBridge::LockBitmap(jobject bitmap)
{
    int err;
    void *buf;

    if ((err = AndroidBitmap_lockPixels(JNI(), bitmap, &buf)) != 0) {
        ALOG_BRIDGE("AndroidBitmap_lockPixels failed! (error %d)", err);
        buf = nsnull;
    }

    return buf;
}

void
AndroidBridge::UnlockBitmap(jobject bitmap)
{
    int err;
    if ((err = AndroidBitmap_unlockPixels(JNI(), bitmap)) != 0)
        ALOG_BRIDGE("AndroidBitmap_unlockPixels failed! (error %d)", err);
}

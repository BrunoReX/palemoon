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
 *   Josh Aas <josh@mozilla.com>
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

#ifdef MOZ_IPC
#include "base/basictypes.h"
#endif 

#include "prtypes.h"
#include "prmem.h"
#include "prenv.h"
#include "prclist.h"
#include "nsAutoLock.h"
#include "nsNPAPIPlugin.h"
#include "nsNPAPIPluginInstance.h"
#include "nsNPAPIPluginStreamListener.h"
#include "nsIServiceManager.h"
#include "nsThreadUtils.h"
#include "nsIPrivateBrowsingService.h"

#include "nsIPluginStreamListener.h"
#include "nsPluginsDir.h"
#include "nsPluginSafety.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsPluginLogging.h"

#include "nsIJSContextStack.h"

#include "nsIDOMElement.h"
#include "nsIDOMDocument.h"
#include "nsPIDOMWindow.h"
#include "nsIDocument.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptContext.h"
#include "nsDOMJSUtils.h"
#include "nsIPrincipal.h"

#include "jscntxt.h"

#include "nsIXPConnect.h"

#include "nsIObserverService.h"
#include <prinrval.h>

#ifdef XP_MACOSX
#include <Carbon/Carbon.h>
#endif

// needed for nppdf plugin
#ifdef MOZ_WIDGET_GTK2
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include "gtk2xtbin.h"
#endif

#include "nsJSNPRuntime.h"
#include "nsIHttpAuthManager.h"
#include "nsICookieService.h"

#include "mozilla/PluginLibrary.h"
using mozilla::PluginLibrary;

#include "mozilla/PluginPRLibrary.h"
using mozilla::PluginPRLibrary;

#ifdef MOZ_IPC
#include "mozilla/plugins/PluginModuleParent.h"
using mozilla::plugins::PluginModuleParent;
#endif

static PRLock *sPluginThreadAsyncCallLock = nsnull;
static PRCList sPendingAsyncCalls = PR_INIT_STATIC_CLIST(&sPendingAsyncCalls);

// POST/GET stream type
enum eNPPStreamTypeInternal {
  eNPPStreamTypeInternal_Get,
  eNPPStreamTypeInternal_Post
};

static NS_DEFINE_IID(kMemoryCID, NS_MEMORY_CID);

using namespace mozilla::plugins::parent;

// This function sends a notification using the observer service to any object
// registered to listen to the "experimental-notify-plugin-call" subject.
// Each "experimental-notify-plugin-call" notification carries with it the run
// time value in milliseconds that the call took to execute.
void NS_NotifyPluginCall(PRIntervalTime startTime) 
{
  PRIntervalTime endTime = PR_IntervalNow() - startTime;
  nsCOMPtr<nsIObserverService> notifyUIService =
    do_GetService("@mozilla.org/observer-service;1");
  if (!notifyUIService)
    return;

  float runTimeInSeconds = float(endTime) / PR_TicksPerSecond();
  nsAutoString runTimeString;
  runTimeString.AppendFloat(runTimeInSeconds);
  const PRUnichar* runTime = runTimeString.get();
  notifyUIService->NotifyObservers(nsnull, "experimental-notify-plugin-call",
                                   runTime);
}

NPNetscapeFuncs nsNPAPIPlugin::CALLBACKS;

void
nsNPAPIPlugin::CheckClassInitialized(void)
{
  static PRBool initialized = PR_FALSE;

  if (initialized)
    return;

  // XXX It'd be nice to make this const and initialize it statically...
  CALLBACKS.size = sizeof(CALLBACKS);
  CALLBACKS.version = (NP_VERSION_MAJOR << 8) + NP_VERSION_MINOR;
  CALLBACKS.geturl = ((NPN_GetURLProcPtr)_geturl);
  CALLBACKS.posturl = ((NPN_PostURLProcPtr)_posturl);
  CALLBACKS.requestread = ((NPN_RequestReadProcPtr)_requestread);
  CALLBACKS.newstream = ((NPN_NewStreamProcPtr)_newstream);
  CALLBACKS.write = ((NPN_WriteProcPtr)_write);
  CALLBACKS.destroystream = ((NPN_DestroyStreamProcPtr)_destroystream);
  CALLBACKS.status = ((NPN_StatusProcPtr)_status);
  CALLBACKS.uagent = ((NPN_UserAgentProcPtr)_useragent);
  CALLBACKS.memalloc = ((NPN_MemAllocProcPtr)_memalloc);
  CALLBACKS.memfree = ((NPN_MemFreeProcPtr)_memfree);
  CALLBACKS.memflush = ((NPN_MemFlushProcPtr)_memflush);
  CALLBACKS.reloadplugins = ((NPN_ReloadPluginsProcPtr)_reloadplugins);

  // Deprecated API callbacks.
  CALLBACKS.getJavaEnv = ((NPN_GetJavaEnvProcPtr)_getJavaEnv);
  CALLBACKS.getJavaPeer = ((NPN_GetJavaPeerProcPtr)_getJavaPeer);
  CALLBACKS.geturlnotify = ((NPN_GetURLNotifyProcPtr)_geturlnotify);
  CALLBACKS.posturlnotify = ((NPN_PostURLNotifyProcPtr)_posturlnotify);
  CALLBACKS.getvalue = ((NPN_GetValueProcPtr)_getvalue);
  CALLBACKS.setvalue = ((NPN_SetValueProcPtr)_setvalue);
  CALLBACKS.invalidaterect = ((NPN_InvalidateRectProcPtr)_invalidaterect);
  CALLBACKS.invalidateregion = ((NPN_InvalidateRegionProcPtr)_invalidateregion);
  CALLBACKS.forceredraw = ((NPN_ForceRedrawProcPtr)_forceredraw);
  CALLBACKS.getstringidentifier = ((NPN_GetStringIdentifierProcPtr)_getstringidentifier);
  CALLBACKS.getstringidentifiers = ((NPN_GetStringIdentifiersProcPtr)_getstringidentifiers);
  CALLBACKS.getintidentifier = ((NPN_GetIntIdentifierProcPtr)_getintidentifier);
  CALLBACKS.identifierisstring = ((NPN_IdentifierIsStringProcPtr)_identifierisstring);
  CALLBACKS.utf8fromidentifier = ((NPN_UTF8FromIdentifierProcPtr)_utf8fromidentifier);
  CALLBACKS.intfromidentifier = ((NPN_IntFromIdentifierProcPtr)_intfromidentifier);
  CALLBACKS.createobject = ((NPN_CreateObjectProcPtr)_createobject);
  CALLBACKS.retainobject = ((NPN_RetainObjectProcPtr)_retainobject);
  CALLBACKS.releaseobject = ((NPN_ReleaseObjectProcPtr)_releaseobject);
  CALLBACKS.invoke = ((NPN_InvokeProcPtr)_invoke);
  CALLBACKS.invokeDefault = ((NPN_InvokeDefaultProcPtr)_invokeDefault);
  CALLBACKS.evaluate = ((NPN_EvaluateProcPtr)_evaluate);
  CALLBACKS.getproperty = ((NPN_GetPropertyProcPtr)_getproperty);
  CALLBACKS.setproperty = ((NPN_SetPropertyProcPtr)_setproperty);
  CALLBACKS.removeproperty = ((NPN_RemovePropertyProcPtr)_removeproperty);
  CALLBACKS.hasproperty = ((NPN_HasPropertyProcPtr)_hasproperty);
  CALLBACKS.hasmethod = ((NPN_HasMethodProcPtr)_hasmethod);
  CALLBACKS.enumerate = ((NPN_EnumerateProcPtr)_enumerate);
  CALLBACKS.construct = ((NPN_ConstructProcPtr)_construct);
  CALLBACKS.releasevariantvalue = ((NPN_ReleaseVariantValueProcPtr)_releasevariantvalue);
  CALLBACKS.setexception = ((NPN_SetExceptionProcPtr)_setexception);
  CALLBACKS.pushpopupsenabledstate = ((NPN_PushPopupsEnabledStateProcPtr)_pushpopupsenabledstate);
  CALLBACKS.poppopupsenabledstate = ((NPN_PopPopupsEnabledStateProcPtr)_poppopupsenabledstate);
  CALLBACKS.pluginthreadasynccall = ((NPN_PluginThreadAsyncCallProcPtr)_pluginthreadasynccall);
  CALLBACKS.getvalueforurl = ((NPN_GetValueForURLPtr)_getvalueforurl);
  CALLBACKS.setvalueforurl = ((NPN_SetValueForURLPtr)_setvalueforurl);
  CALLBACKS.getauthenticationinfo = ((NPN_GetAuthenticationInfoPtr)_getauthenticationinfo);

  if (!sPluginThreadAsyncCallLock)
    sPluginThreadAsyncCallLock = nsAutoLock::NewLock("sPluginThreadAsyncCallLock");

  initialized = PR_TRUE;

  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,("NPN callbacks initialized\n"));
}

#ifdef OJI
NS_IMPL_ISUPPORTS5(nsNPAPIPlugin,
                   nsIPlugin,
                   nsIPluginOld,
                   nsIFactory,
                   nsIJVMPlugin,
                   nsIJVMConsole)

#else
NS_IMPL_ISUPPORTS1(nsNPAPIPlugin, nsIPlugin)
#endif

#ifdef OJI
nsNPAPIPlugin::nsNPAPIPlugin(nsIPluginOld *aShadow)
{
  mShadow = aShadow;
  fLibrary = nsnull;
  memset((void*) &fCallbacks, 0, sizeof(fCallbacks));
  CheckClassInitialized();
}
#endif

nsNPAPIPlugin::nsNPAPIPlugin(NPPluginFuncs* callbacks,
                             PluginLibrary* aLibrary)
{
  memset((void*) &fCallbacks, 0, sizeof(fCallbacks));

  fCallbacks.size = sizeof(fCallbacks);
  fLibrary = nsnull;
#ifdef OJI
  mShadow = nsnull;
#endif

#if defined(XP_WIN) || defined(XP_OS2)
  // On Windows (and Mac) we need to keep a direct reference to the
  // fCallbacks and NOT just copy the struct. See Bugzilla 85334

  NPError gepError;
  nsresult gepResult = aLibrary->NP_GetEntryPoints(&fCallbacks, &gepError);
  if (gepResult != NS_OK || gepError != NPERR_NO_ERROR)
    return;

  NS_ASSERTION(HIBYTE(fCallbacks.version) >= NP_VERSION_MAJOR,
               "callback version is less than NP version");

#elif defined(XP_MACOSX)
  NPPluginFuncs np_callbacks;
  memset((void*) &np_callbacks, 0, sizeof(np_callbacks));
  np_callbacks.size = sizeof(np_callbacks);

  if (!aLibrary->HasRequiredFunctions()) {
    NS_WARNING("Not all necessary functions exposed by plugin, it will not load.");
    return;
  }

  // we call NP_Initialize before getting function pointers to match
  // WebKit's behavior. They implemented this first on Mac OS X.
  NPError initError;
  nsresult initResult = aLibrary->NP_Initialize(&(nsNPAPIPlugin::CALLBACKS), &initError);
  if (initResult != NS_OK || initError != NPERR_NO_ERROR)
    return;
  NPError gepError;
  nsresult gepResult = aLibrary->NP_GetEntryPoints(&np_callbacks, &gepError);
  if (gepResult != NS_OK || gepError != NPERR_NO_ERROR)
    return;

  fCallbacks.version = np_callbacks.version;
  fCallbacks.newp = (NPP_NewProcPtr)np_callbacks.newp;
  fCallbacks.destroy = (NPP_DestroyProcPtr)np_callbacks.destroy;
  fCallbacks.setwindow = (NPP_SetWindowProcPtr)np_callbacks.setwindow;
  fCallbacks.newstream = (NPP_NewStreamProcPtr)np_callbacks.newstream;
  fCallbacks.destroystream = (NPP_DestroyStreamProcPtr)np_callbacks.destroystream;
  fCallbacks.asfile = (NPP_StreamAsFileProcPtr)np_callbacks.asfile;
  fCallbacks.writeready = (NPP_WriteReadyProcPtr)np_callbacks.writeready;
  fCallbacks.write = (NPP_WriteProcPtr)np_callbacks.write;
  fCallbacks.print = (NPP_PrintProcPtr)np_callbacks.print;
  fCallbacks.event = (NPP_HandleEventProcPtr)np_callbacks.event;
  fCallbacks.urlnotify = (NPP_URLNotifyProcPtr)np_callbacks.urlnotify;
  fCallbacks.getvalue = (NPP_GetValueProcPtr)np_callbacks.getvalue;
  fCallbacks.setvalue = (NPP_SetValueProcPtr)np_callbacks.setvalue;
#else // for everyone else
  memcpy((void*) &fCallbacks, (void*) callbacks, sizeof(fCallbacks));
#endif

  fLibrary = aLibrary;
  fLibrary->SetPlugin(this);
}

nsNPAPIPlugin::~nsNPAPIPlugin()
{
  // reset the callbacks list
  memset((void*) &fCallbacks, 0, sizeof(fCallbacks));

#ifdef OJI
  NS_IF_RELEASE(mShadow);
#endif

  delete fLibrary;
  fLibrary = NULL;
}


#if defined(XP_MACOSX)
void
nsNPAPIPlugin::SetPluginRefNum(short aRefNum)
{
  fPluginRefNum = aRefNum;
}
#endif

#ifdef MOZ_IPC
void
nsNPAPIPlugin::PluginCrashed(const nsAString& pluginDumpID,
                             const nsAString& browserDumpID)
{
  nsRefPtr<nsPluginHost> host = dont_AddRef(nsPluginHost::GetInst());
  host->PluginCrashed(this, pluginDumpID, browserDumpID);
}
#endif

namespace {

#ifdef MOZ_IPC

inline PRBool
OOPPluginsEnabled(const char* aFilePath)
{
  if (PR_GetEnv("MOZ_DISABLE_OOP_PLUGINS")) {
    return PR_FALSE;
  }

#ifdef XP_WIN
  OSVERSIONINFO osVerInfo = {0};
  osVerInfo.dwOSVersionInfoSize = sizeof(osVerInfo);
  GetVersionEx(&osVerInfo);
  // Always disabled on 2K or less. (bug 536303)
  if (osVerInfo.dwMajorVersion < 5 ||
      (osVerInfo.dwMajorVersion == 5 && osVerInfo.dwMinorVersion == 0))
    return PR_FALSE;
#endif

  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (!prefs) {
    return PR_FALSE;
  }

  // Get per-library whitelist/blacklist pref string
  // "dom.ipc.plugins.enabled.filename.dll" and fall back to the default value
  // of "dom.ipc.plugins.enabled"

  nsCAutoString pluginLibPref(aFilePath);
  PRInt32 slashPos = pluginLibPref.RFindCharInSet("/\\");
  if (kNotFound == slashPos)
    return PR_FALSE;
  pluginLibPref.Cut(0, slashPos + 1);
  ToLowerCase(pluginLibPref);
  pluginLibPref.Insert("dom.ipc.plugins.enabled.", 0);

  PRBool oopPluginsEnabled = PR_FALSE;
  if (NS_SUCCEEDED(prefs->GetBoolPref(pluginLibPref.get(),
                                      &oopPluginsEnabled)))
    return oopPluginsEnabled;

  oopPluginsEnabled = PR_FALSE;
  prefs->GetBoolPref("dom.ipc.plugins.enabled", &oopPluginsEnabled);
  return oopPluginsEnabled;
}

#endif // MOZ_IPC

inline PluginLibrary*
GetNewPluginLibrary(const char* aFilePath,
                    PRLibrary* aLibrary)
{
#ifdef MOZ_IPC
  if (aFilePath && OOPPluginsEnabled(aFilePath)) {
    return PluginModuleParent::LoadModule(aFilePath);
  }
#endif
  return new PluginPRLibrary(aFilePath, aLibrary);
}

} /* anonymous namespace */


// Creates the nsNPAPIPlugin object. One nsNPAPIPlugin object exists per plugin (not instance).
nsresult
nsNPAPIPlugin::CreatePlugin(const char* aFilePath, PRLibrary* aLibrary,
                            nsIPlugin** aResult)
{
  CheckClassInitialized();

  nsRefPtr<nsNPAPIPlugin> plugin;

#if defined(XP_UNIX) && !defined(XP_MACOSX)
  NPPluginFuncs callbacks;
  memset((void*) &callbacks, 0, sizeof(callbacks));
  callbacks.size = sizeof(callbacks);

  PluginLibrary* pluginLib = GetNewPluginLibrary(aFilePath, aLibrary);

  // create the new plugin handler
  plugin = new nsNPAPIPlugin(&callbacks, pluginLib);
  if (!plugin)
    return NS_ERROR_OUT_OF_MEMORY;

  // Do not initialize if the file path is NULL.
  if (!aFilePath) {
    *aResult = plugin.forget().get();
    return NS_OK;
  }

  // we must init here because the plugin may call NPN functions
  // when we call into the NP_Initialize entry point - NPN functions
  // require that mBrowserManager be set up
  plugin->Initialize();

  NPError initError;
  nsresult initResult = pluginLib->NP_Initialize(&(nsNPAPIPlugin::CALLBACKS), &callbacks, &initError);
  if (initResult != NS_OK || initError != NPERR_NO_ERROR)
    return NS_ERROR_UNEXPECTED;

  // now copy function table back to nsNPAPIPlugin instance
  memcpy((void*) &(plugin->fCallbacks), (void*)&callbacks, sizeof(callbacks));
#endif

#ifdef XP_WIN
  PluginLibrary* pluginLib = GetNewPluginLibrary(aFilePath, aLibrary);

  // Note: on Windows, we must use the fCallback because plugins may
  // change the function table. The Shockwave installer makes changes
  // in the table while running
  plugin = new nsNPAPIPlugin(nsnull, pluginLib);
  if (!plugin)
    return NS_ERROR_OUT_OF_MEMORY;

  // we must init here because the plugin may call NPN functions
  // when we call into the NP_Initialize entry point - NPN functions
  // require that mBrowserManager be set up
  if (NS_FAILED(plugin->Initialize()))
    return NS_ERROR_FAILURE;

  NPError initError;
  nsresult initResult = pluginLib->NP_Initialize(&(nsNPAPIPlugin::CALLBACKS), &initError);
  if (initResult != NS_OK || initError != NPERR_NO_ERROR)
    return NS_ERROR_FAILURE;
#endif

#ifdef XP_OS2
  PluginLibrary* pluginLib = GetNewPluginLibrary(aFilePath, aLibrary);

  // create the new plugin handler
  plugin = new nsNPAPIPlugin(nsnull, pluginLib);
  if (!plugin)
    return NS_ERROR_OUT_OF_MEMORY;

  // we must init here because the plugin may call NPN functions
  // when we call into the NP_Initialize entry point - NPN functions
  // require that mBrowserManager be set up
  if (NS_FAILED(plugin->Initialize()))
    return NS_ERROR_FAILURE;

  NP_PLUGININIT pfnInitialize =
    (NP_PLUGININIT)PR_FindSymbol(aLibrary, "NP_Initialize");

  if (!pfnInitialize)
    return NS_ERROR_UNEXPECTED;

  // Fixes problem where the OS/2 native multimedia plugins weren't
  // working on mozilla though did work on 4.x.  Problem is that they
  // expect the current working directory to be the plugins dir.
  // Since these plugins are no longer maintained and they represent
  // the majority of the OS/2 plugin contingency, we'll have to make
  // them work here.

#define MAP_DISKNUM_TO_LETTER(n) ('A' + (n - 1))
#define MAP_LETTER_TO_DISKNUM(c) (toupper(c)-'A'+1)

  unsigned long origDiskNum, pluginDiskNum, logicalDisk;

  char pluginPath[CCHMAXPATH], origPath[CCHMAXPATH];
  strcpy(pluginPath, aFilePath);
  char* slash = strrchr(pluginPath, '\\');
  *slash = '\0';

  DosQueryCurrentDisk( &origDiskNum, &logicalDisk );
  pluginDiskNum = MAP_LETTER_TO_DISKNUM(pluginPath[0]);

  origPath[0] = MAP_DISKNUM_TO_LETTER(origDiskNum);
  origPath[1] = ':';
  origPath[2] = '\\';

  ULONG len = CCHMAXPATH-3;
  APIRET rc = DosQueryCurrentDir(0, &origPath[3], &len);
  NS_ASSERTION(NO_ERROR == rc,"DosQueryCurrentDir failed");

  BOOL bChangedDir = FALSE;
  BOOL bChangedDisk = FALSE;
  if (pluginDiskNum != origDiskNum) {
    rc = DosSetDefaultDisk(pluginDiskNum);
    NS_ASSERTION(NO_ERROR == rc,"DosSetDefaultDisk failed");
    bChangedDisk = TRUE;
  }

  if (stricmp(origPath, pluginPath) != 0) {
    rc = DosSetCurrentDir(pluginPath);
    NS_ASSERTION(NO_ERROR == rc,"DosSetCurrentDir failed");
    bChangedDir = TRUE;
  }

  nsresult rv = pfnInitialize(&(nsNPAPIPlugin::CALLBACKS));

  if (bChangedDisk) {
    rc= DosSetDefaultDisk(origDiskNum);
    NS_ASSERTION(NO_ERROR == rc,"DosSetDefaultDisk failed");
  }
  if (bChangedDir) {
    rc = DosSetCurrentDir(origPath);
    NS_ASSERTION(NO_ERROR == rc,"DosSetCurrentDir failed");
  }

  if (!NS_SUCCEEDED(rv)) {
    return NS_ERROR_UNEXPECTED;
  }
#endif

#if defined(XP_MACOSX)
#ifndef __LP64__
  short appRefNum = ::CurResFile();
  short pluginRefNum;
#endif

  nsCOMPtr<nsILocalFile> pluginPath;
  NS_NewNativeLocalFile(nsDependentCString(aFilePath), PR_TRUE,
                        getter_AddRefs(pluginPath));
  nsPluginFile pluginFile(pluginPath);

#ifndef __LP64__
  pluginRefNum = pluginFile.OpenPluginResource();
#endif

  PluginLibrary* pluginLib = GetNewPluginLibrary(aFilePath, aLibrary);
  plugin = new nsNPAPIPlugin(nsnull, pluginLib);
#ifndef __LP64__
  ::UseResFile(appRefNum);
#endif
  if (!plugin)
    return NS_ERROR_OUT_OF_MEMORY;

  if (NS_FAILED(plugin->Initialize()))
    return NS_ERROR_FAILURE;

#ifndef __LP64__
  plugin->SetPluginRefNum(pluginRefNum);
#endif
#endif

#ifdef XP_BEOS
  // I just copied UNIX version.
  // Makoto Hamanaka <VYA04230@nifty.com>

  // XXX this code won't compile with the new e10s changes

  NPPluginFuncs callbacks;
  memset((void*) &callbacks, 0, sizeof(callbacks));
  callbacks.size = sizeof(callbacks);

  NP_PLUGINSHUTDOWN pfnShutdown =
    (NP_PLUGINSHUTDOWN)PR_FindSymbol(aLibrary, "NP_Shutdown");

  // create the new plugin handler
  plugin = new nsNPAPIPlugin(&callbacks, aLibrary, pfnShutdown);
  if (!plugin)
    return NS_ERROR_OUT_OF_MEMORY;

  // we must init here because the plugin may call NPN functions
  // when we call into the NP_Initialize entry point - NPN functions
  // require that mBrowserManager be set up
  plugin->Initialize();

  NP_PLUGINUNIXINIT pfnInitialize =
    (NP_PLUGINUNIXINIT)PR_FindSymbol(aLibrary, "NP_Initialize");

  if (!pfnInitialize)
    return NS_ERROR_FAILURE;

  if (pfnInitialize(&(nsNPAPIPlugin::CALLBACKS),&callbacks) != NS_OK)
    return NS_ERROR_FAILURE;

  // now copy function table back to nsNPAPIPlugin instance
  memcpy((void*) &(plugin->fCallbacks), (void*)&callbacks, sizeof(callbacks));
#endif

  *aResult = plugin.forget().get();
  return NS_OK;
}

NS_METHOD
nsNPAPIPlugin::CreatePluginInstance(nsIPluginInstance **aResult)
{
  if (!aResult)
    return NS_ERROR_NULL_POINTER;

  *aResult = NULL;

  nsRefPtr<nsNPAPIPluginInstance> inst =
    new nsNPAPIPluginInstance(&fCallbacks, fLibrary);
  if (!inst)
    return NS_ERROR_OUT_OF_MEMORY;

  NS_ADDREF(inst);
  *aResult = static_cast<nsIPluginInstance*>(inst);
  return NS_OK;
}

#ifdef OJI

// nsIPluginOld methods not implemented elsewhere

NS_IMETHODIMP
nsNPAPIPlugin::CreatePluginInstance(nsISupports *aOuter, REFNSIID aIID,
                                    const char *aPluginMIMEType, void **aResult)
{
  if (mShadow)
    return mShadow->CreatePluginInstance(aOuter, aIID, aPluginMIMEType, aResult);
  return NS_ERROR_NOT_IMPLEMENTED;
}

// nsIFactory interface

NS_IMETHODIMP
nsNPAPIPlugin::CreateInstance(nsISupports *aOuter, const nsIID &aIID,
                              void **aResult)
{
  if (mShadow) {
    const char *mimedescr = NULL;
    mShadow->GetMIMEDescription(&mimedescr);
    return mShadow->CreatePluginInstance(nsnull, aIID, mimedescr, aResult);
  }

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNPAPIPlugin::LockFactory(PRBool aLock)
{
  // Not implemented in simplest case.
  return NS_OK;
}

// nsIJVMPlugin interface

NS_IMETHODIMP
nsNPAPIPlugin::AddToClassPath(const char* dirPath)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMPlugin> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->AddToClassPath(dirPath);
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNPAPIPlugin::RemoveFromClassPath(const char* dirPath)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMPlugin> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->RemoveFromClassPath(dirPath);
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNPAPIPlugin::GetClassPath(const char* *result)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMPlugin> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->GetClassPath(result);
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNPAPIPlugin::GetJavaWrapper(JNIEnv* env, jint jsobj, jobject *jobj)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMPlugin> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->GetJavaWrapper(env, jsobj, jobj);
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNPAPIPlugin::CreateSecureEnv(JNIEnv* proxyEnv, nsISecureEnv* *outSecureEnv)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMPlugin> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->CreateSecureEnv(proxyEnv, outSecureEnv);
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNPAPIPlugin::SpendTime(PRUint32 timeMillis)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMPlugin> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->SpendTime(timeMillis);
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNPAPIPlugin::UnwrapJavaWrapper(JNIEnv* jenv, jobject jobj, jint* obj)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMPlugin> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->UnwrapJavaWrapper(jenv, jobj, obj);
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

// nsIJVMConsole interface

NS_IMETHODIMP
nsNPAPIPlugin::Show(void)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMConsole> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->Show();
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNPAPIPlugin::Hide(void)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMConsole> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->Hide();
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNPAPIPlugin::IsVisible(PRBool *result)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMConsole> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->IsVisible(result);
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNPAPIPlugin::Print(const char* msg, const char* encodingName)
{
  if (mShadow) {
    nsCOMPtr<nsIJVMConsole> plugin(do_QueryInterface(mShadow));
    if (!plugin)
      return NS_ERROR_NOT_IMPLEMENTED;
    return plugin->Print(msg, encodingName);
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

// Helper methods

void
nsNPAPIPlugin::SetShadow(nsIPluginOld *shadow)
{
  NS_IF_RELEASE(mShadow);
  mShadow = shadow;
  NS_IF_ADDREF(mShadow);
}

nsIPluginOld *
nsNPAPIPlugin::GetShadow()
{
  return mShadow;
}

#endif // OJI

nsresult
nsNPAPIPlugin::Initialize(void)
{
#ifdef OJI
  if (mShadow)
    return mShadow->Initialize();
#endif
  if (!fLibrary)
    return NS_ERROR_FAILURE;
  return NS_OK;
}

nsresult
nsNPAPIPlugin::Shutdown(void)
{
#ifdef OJI
  if (mShadow)
    return mShadow->Shutdown();
#endif

  NPP_PLUGIN_LOG(PLUGIN_LOG_BASIC,
                 ("NPP Shutdown to be called: this=%p\n", this));

  NPError shutdownError;
  fLibrary->NP_Shutdown(&shutdownError);
#ifdef XP_MACOSX
  if (shutdownError == NS_OK && fPluginRefNum > 0)
    ::CloseResFile(fPluginRefNum);
#endif
  return NS_OK;
}

nsresult
nsNPAPIPlugin::GetMIMEDescription(const char* *resultingDesc)
{
#ifdef OJI
  if (mShadow)
    return mShadow->GetMIMEDescription(resultingDesc);
#endif

  nsresult gmdResult = fLibrary->NP_GetMIMEDescription(resultingDesc);
  if (gmdResult != NS_OK) {
    return gmdResult;
  }

  return NS_OK;
}

nsresult
nsNPAPIPlugin::GetValue(nsPluginVariable variable, void *value)
{
#ifdef OJI
  if (mShadow)
    return mShadow->GetValue(variable, value);
#endif

  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("nsNPAPIPlugin::GetValue called: this=%p, variable=%d\n", this, variable));

  NPError gvError;
  fLibrary->NP_GetValue(nsnull, (NPPVariable) variable, value, &gvError);

  return gvError;
}

// Create a new NPP GET or POST (given in the type argument) url
// stream that may have a notify callback
NPError
MakeNewNPAPIStreamInternal(NPP npp, const char *relativeURL, const char *target,
                          eNPPStreamTypeInternal type,
                          PRBool bDoNotify = PR_FALSE,
                          void *notifyData = nsnull, uint32_t len = 0,
                          const char *buf = nsnull, NPBool file = PR_FALSE)
{
  if (!npp)
    return NPERR_INVALID_INSTANCE_ERROR;

  PluginDestructionGuard guard(npp);

  nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *) npp->ndata;
  if (!inst || !inst->IsRunning())
    return NPERR_INVALID_INSTANCE_ERROR;

  nsCOMPtr<nsIPluginHost> pluginHost = do_GetService(MOZ_PLUGIN_HOST_CONTRACTID);
  NS_ASSERTION(pluginHost, "failed to get plugin host");
  if (!pluginHost) return NPERR_GENERIC_ERROR;

  nsCOMPtr<nsIPluginStreamListener> listener;
  if (!target)
    ((nsNPAPIPluginInstance*)inst)->NewNotifyStream(getter_AddRefs(listener),
                                                    notifyData,
                                                    bDoNotify, relativeURL);

  switch (type) {
  case eNPPStreamTypeInternal_Get:
    {
      if (NS_FAILED(pluginHost->GetURL(static_cast<nsIPluginInstance*>(inst), relativeURL, target, listener)))
        return NPERR_GENERIC_ERROR;
      break;
    }
  case eNPPStreamTypeInternal_Post:
    {
      if (NS_FAILED(pluginHost->PostURL(static_cast<nsIPluginInstance*>(inst), relativeURL, len, buf, file, target,
                                listener)))
        return NPERR_GENERIC_ERROR;
      break;
    }
  default:
    NS_ASSERTION(0, "how'd I get here");
  }

  return NPERR_NO_ERROR;
}

#if defined(MOZ_MEMORY_WINDOWS) && !defined(MOZ_MEMORY_WINCE)
extern "C" size_t malloc_usable_size(const void *ptr);
#endif

namespace {

static char *gNPPException;

// A little helper class used to wrap up plugin manager streams (that is,
// streams from the plugin to the browser).
class nsNPAPIStreamWrapper : nsISupports
{
public:
  NS_DECL_ISUPPORTS

protected:
  nsIOutputStream *fStream;
  NPStream        fNPStream;

public:
  nsNPAPIStreamWrapper(nsIOutputStream* stream);
  ~nsNPAPIStreamWrapper();

  void GetStream(nsIOutputStream* &result);
  NPStream* GetNPStream(void) { return &fNPStream; }
};

class nsPluginThreadRunnable : public nsRunnable,
                               public PRCList
{
public:
  nsPluginThreadRunnable(NPP instance, PluginThreadCallback func,
                         void *userData);
  virtual ~nsPluginThreadRunnable();

  NS_IMETHOD Run();

  PRBool IsForInstance(NPP instance)
  {
    return (mInstance == instance);
  }

  void Invalidate()
  {
    mFunc = nsnull;
  }

  PRBool IsValid()
  {
    return (mFunc != nsnull);
  }

private:  
  NPP mInstance;
  PluginThreadCallback mFunc;
  void *mUserData;
};

static nsIDocument *
GetDocumentFromNPP(NPP npp)
{
  NS_ENSURE_TRUE(npp, nsnull);

  nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *)npp->ndata;
  NS_ENSURE_TRUE(inst, nsnull);

  PluginDestructionGuard guard(inst);

  nsCOMPtr<nsIPluginInstanceOwner> owner;
  inst->GetOwner(getter_AddRefs(owner));
  NS_ENSURE_TRUE(owner, nsnull);

  nsCOMPtr<nsIDocument> doc;
  owner->GetDocument(getter_AddRefs(doc));

  return doc;
}

static JSContext *
GetJSContextFromDoc(nsIDocument *doc)
{
  nsIScriptGlobalObject *sgo = doc->GetScriptGlobalObject();
  NS_ENSURE_TRUE(sgo, nsnull);

  nsIScriptContext *scx = sgo->GetContext();
  NS_ENSURE_TRUE(scx, nsnull);

  return (JSContext *)scx->GetNativeContext();
}

static JSContext *
GetJSContextFromNPP(NPP npp)
{
  nsIDocument *doc = GetDocumentFromNPP(npp);
  NS_ENSURE_TRUE(doc, nsnull);

  return GetJSContextFromDoc(doc);
}

static NPIdentifier
doGetIdentifier(JSContext *cx, const NPUTF8* name)
{
  NS_ConvertUTF8toUTF16 utf16name(name);

  JSString *str = ::JS_InternUCStringN(cx, (jschar *)utf16name.get(),
                                       utf16name.Length());

  if (!str)
    return NULL;

  return (NPIdentifier)STRING_TO_JSVAL(str);
}

#if defined(MOZ_MEMORY_WINDOWS) && !defined(MOZ_MEMORY_WINCE)
BOOL
InHeap(HANDLE hHeap, LPVOID lpMem)
{
  BOOL success = FALSE;
  PROCESS_HEAP_ENTRY he;
  he.lpData = NULL;
  while (HeapWalk(hHeap, &he) != 0) {
    if (he.lpData == lpMem) {
      success = TRUE;
      break;
    }
  }
  HeapUnlock(hHeap);
  return success;
}
#endif

} /* anonymous namespace */

NS_IMPL_ISUPPORTS1(nsNPAPIStreamWrapper, nsISupports)

nsNPAPIStreamWrapper::nsNPAPIStreamWrapper(nsIOutputStream* stream)
: fStream(stream)
{
  NS_ASSERTION(stream, "bad stream");

  fStream = stream;
  NS_ADDREF(fStream);

  memset(&fNPStream, 0, sizeof(fNPStream));
  fNPStream.ndata = (void*) this;
}

nsNPAPIStreamWrapper::~nsNPAPIStreamWrapper(void)
{
  fStream->Close();
  NS_IF_RELEASE(fStream);
}

void
nsNPAPIStreamWrapper::GetStream(nsIOutputStream* &result)
{
  result = fStream;
  NS_IF_ADDREF(fStream);
}

NPPExceptionAutoHolder::NPPExceptionAutoHolder()
  : mOldException(gNPPException)
{
  gNPPException = nsnull;
}

NPPExceptionAutoHolder::~NPPExceptionAutoHolder()
{
  NS_ASSERTION(!gNPPException, "NPP exception not properly cleared!");

  gNPPException = mOldException;
}

nsPluginThreadRunnable::nsPluginThreadRunnable(NPP instance,
                                               PluginThreadCallback func,
                                               void *userData)
  : mInstance(instance), mFunc(func), mUserData(userData)
{
  if (!sPluginThreadAsyncCallLock) {
    // Failed to create lock, not much we can do here then...
    mFunc = nsnull;

    return;
  }

  PR_INIT_CLIST(this);

  {
    nsAutoLock lock(sPluginThreadAsyncCallLock);

    nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *)instance->ndata;
    if (!inst || !inst->IsRunning()) {
      // The plugin was stopped, ignore this async call.
      mFunc = nsnull;

      return;
    }

    PR_APPEND_LINK(this, &sPendingAsyncCalls);
  }
}

nsPluginThreadRunnable::~nsPluginThreadRunnable()
{
  if (!sPluginThreadAsyncCallLock) {
    return;
  }

  {
    nsAutoLock lock(sPluginThreadAsyncCallLock);

    PR_REMOVE_LINK(this);
  }
}

NS_IMETHODIMP
nsPluginThreadRunnable::Run()
{
  if (mFunc) {
    PluginDestructionGuard guard(mInstance);

    NS_TRY_SAFE_CALL_VOID(mFunc(mUserData), nsnull, nsnull);
  }

  return NS_OK;
}

void
OnPluginDestroy(NPP instance)
{
  if (!sPluginThreadAsyncCallLock) {
    return;
  }

  {
    nsAutoLock lock(sPluginThreadAsyncCallLock);

    if (PR_CLIST_IS_EMPTY(&sPendingAsyncCalls)) {
      return;
    }

    nsPluginThreadRunnable *r =
      (nsPluginThreadRunnable *)PR_LIST_HEAD(&sPendingAsyncCalls);

    do {
      if (r->IsForInstance(instance)) {
        r->Invalidate();
      }

      r = (nsPluginThreadRunnable *)PR_NEXT_LINK(r);
    } while (r != &sPendingAsyncCalls);
  }
}

void
OnShutdown()
{
  NS_ASSERTION(PR_CLIST_IS_EMPTY(&sPendingAsyncCalls),
               "Pending async plugin call list not cleaned up!");

  if (sPluginThreadAsyncCallLock) {
    nsAutoLock::DestroyLock(sPluginThreadAsyncCallLock);

    sPluginThreadAsyncCallLock = nsnull;
  }
}

void
EnterAsyncPluginThreadCallLock()
{
  if (sPluginThreadAsyncCallLock) {
    PR_Lock(sPluginThreadAsyncCallLock);
  }
}

void
ExitAsyncPluginThreadCallLock()
{
  if (sPluginThreadAsyncCallLock) {
    PR_Unlock(sPluginThreadAsyncCallLock);
  }
}

NPP NPPStack::sCurrentNPP = nsnull;

const char *
PeekException()
{
  return gNPPException;
}

void
PopException()
{
  NS_ASSERTION(gNPPException, "Uh, no NPP exception to pop!");

  if (gNPPException) {
    free(gNPPException);

    gNPPException = nsnull;
  }
}

//
// Static callbacks that get routed back through the new C++ API
//

namespace mozilla {
namespace plugins {
namespace parent {

NPError NP_CALLBACK
_geturl(NPP npp, const char* relativeURL, const char* target)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_geturl called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }

  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("NPN_GetURL: npp=%p, target=%s, url=%s\n", (void *)npp, target,
   relativeURL));

  PluginDestructionGuard guard(npp);

  // Block Adobe Acrobat from loading URLs that are not http:, https:,
  // or ftp: URLs if the given target is null.
  if (!target && relativeURL &&
      (strncmp(relativeURL, "http:", 5) != 0) &&
      (strncmp(relativeURL, "https:", 6) != 0) &&
      (strncmp(relativeURL, "ftp:", 4) != 0)) {
    nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *) npp->ndata;

    const char *name = nsPluginHost::GetPluginName(inst);

    if (name && strstr(name, "Adobe") && strstr(name, "Acrobat")) {
      return NPERR_NO_ERROR;
    }
  }

  return MakeNewNPAPIStreamInternal(npp, relativeURL, target,
                                    eNPPStreamTypeInternal_Get);
}

NPError NP_CALLBACK
_geturlnotify(NPP npp, const char* relativeURL, const char* target,
              void* notifyData)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_geturlnotify called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }

  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
    ("NPN_GetURLNotify: npp=%p, target=%s, notify=%p, url=%s\n", (void*)npp,
     target, notifyData, relativeURL));

  PluginDestructionGuard guard(npp);

  return MakeNewNPAPIStreamInternal(npp, relativeURL, target,
                                    eNPPStreamTypeInternal_Get, PR_TRUE,
                                    notifyData);
}

NPError NP_CALLBACK
_posturlnotify(NPP npp, const char *relativeURL, const char *target,
               uint32_t len, const char *buf, NPBool file, void *notifyData)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_posturlnotify called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  if (!buf)
    return NPERR_INVALID_PARAM;

  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_PostURLNotify: npp=%p, target=%s, len=%d, file=%d, "
                  "notify=%p, url=%s, buf=%s\n",
                  (void*)npp, target, len, file, notifyData, relativeURL,
                  buf));

  PluginDestructionGuard guard(npp);

  return MakeNewNPAPIStreamInternal(npp, relativeURL, target,
                                    eNPPStreamTypeInternal_Post, PR_TRUE,
                                    notifyData, len, buf, file);
}

NPError NP_CALLBACK
_posturl(NPP npp, const char *relativeURL, const char *target,
         uint32_t len, const char *buf, NPBool file)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_posturl called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_PostURL: npp=%p, target=%s, file=%d, len=%d, url=%s, "
                  "buf=%s\n",
                  (void*)npp, target, file, len, relativeURL, buf));

  PluginDestructionGuard guard(npp);

  return MakeNewNPAPIStreamInternal(npp, relativeURL, target,
                                    eNPPStreamTypeInternal_Post, PR_FALSE, nsnull,
                                    len, buf, file);
}

NPError NP_CALLBACK
_newstream(NPP npp, NPMIMEType type, const char* target, NPStream* *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_newstream called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("NPN_NewStream: npp=%p, type=%s, target=%s\n", (void*)npp,
   (const char *)type, target));

  NPError err = NPERR_INVALID_INSTANCE_ERROR;
  if (npp && npp->ndata) {
    nsIPluginInstance *inst = (nsIPluginInstance *) npp->ndata;

    PluginDestructionGuard guard(inst);

    nsCOMPtr<nsIOutputStream> stream;
    if (NS_SUCCEEDED(inst->NewStreamFromPlugin((const char*) type, target,
                                               getter_AddRefs(stream)))) {
      nsNPAPIStreamWrapper* wrapper = new nsNPAPIStreamWrapper(stream);
      if (wrapper) {
        (*result) = wrapper->GetNPStream();
        err = NPERR_NO_ERROR;
      } else {
        err = NPERR_OUT_OF_MEMORY_ERROR;
      }
    } else {
      err = NPERR_GENERIC_ERROR;
    }
  }
  return err;
}

int32_t NP_CALLBACK
_write(NPP npp, NPStream *pstream, int32_t len, void *buffer)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_write called from the wrong thread\n"));
    return 0;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_Write: npp=%p, url=%s, len=%d, buffer=%s\n", (void*)npp,
                  pstream->url, len, (char*)buffer));

  // negative return indicates failure to the plugin
  if (!npp)
    return -1;

  PluginDestructionGuard guard(npp);

  nsNPAPIStreamWrapper* wrapper = (nsNPAPIStreamWrapper*) pstream->ndata;
  NS_ASSERTION(wrapper, "null stream");
  if (!wrapper)
    return -1;

  nsIOutputStream* stream;
  wrapper->GetStream(stream);

  PRUint32 count = 0;
  nsresult rv = stream->Write((char *)buffer, len, &count);
  NS_RELEASE(stream);

  if (rv != NS_OK)
    return -1;

  return (int32_t)count;
}

NPError NP_CALLBACK
_destroystream(NPP npp, NPStream *pstream, NPError reason)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_write called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_DestroyStream: npp=%p, url=%s, reason=%d\n", (void*)npp,
                  pstream->url, (int)reason));

  if (!npp)
    return NPERR_INVALID_INSTANCE_ERROR;

  PluginDestructionGuard guard(npp);

  nsCOMPtr<nsIPluginStreamListener> listener =
    do_QueryInterface((nsISupports *)pstream->ndata);

  // DestroyStream can kill two kinds of streams: NPP derived and NPN derived.
  // check to see if they're trying to kill a NPP stream
  if (listener) {
    // Tell the stream listner that the stream is now gone.
    listener->OnStopBinding(nsnull, NS_BINDING_ABORTED);

    // FIXME: http://bugzilla.mozilla.org/show_bug.cgi?id=240131
    //
    // Is it ok to leave pstream->ndata set here, and who releases it
    // (or is it even properly ref counted)? And who closes the stream
    // etc?
  } else {
    nsNPAPIStreamWrapper* wrapper = (nsNPAPIStreamWrapper *)pstream->ndata;
    NS_ASSERTION(wrapper, "null wrapper");

    if (!wrapper)
      return NPERR_INVALID_PARAM;

    // This will release the wrapped nsIOutputStream.
    delete wrapper;
    pstream->ndata = nsnull;
  }

  return NPERR_NO_ERROR;
}

void NP_CALLBACK
_status(NPP npp, const char *message)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_status called from the wrong thread\n"));
    return;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_Status: npp=%p, message=%s\n",
                                     (void*)npp, message));

  if (!npp || !npp->ndata) {
    NS_WARNING("_status: npp or npp->ndata == 0");
    return;
  }

  nsIPluginInstance *inst = (nsIPluginInstance *) npp->ndata;

  PluginDestructionGuard guard(inst);

  inst->ShowStatus(message);
}

void NP_CALLBACK
_memfree (void *ptr)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_memfree called from the wrong thread\n"));
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY, ("NPN_MemFree: ptr=%p\n", ptr));

  if (ptr)
    nsMemory::Free(ptr);
}

uint32_t NP_CALLBACK
_memflush(uint32_t size)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_memflush called from the wrong thread\n"));
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY, ("NPN_MemFlush: size=%d\n", size));

  nsMemory::HeapMinimize(PR_TRUE);
  return 0;
}

void NP_CALLBACK
_reloadplugins(NPBool reloadPages)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_reloadplugins called from the wrong thread\n"));
    return;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_ReloadPlugins: reloadPages=%d\n", reloadPages));

  nsCOMPtr<nsIPluginHost> pluginHost(do_GetService(MOZ_PLUGIN_HOST_CONTRACTID));
  if (!pluginHost)
    return;

  pluginHost->ReloadPlugins(reloadPages);
}

void NP_CALLBACK
_invalidaterect(NPP npp, NPRect *invalidRect)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_invalidaterect called from the wrong thread\n"));
    return;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_InvalidateRect: npp=%p, top=%d, left=%d, bottom=%d, "
                  "right=%d\n", (void *)npp, invalidRect->top,
                  invalidRect->left, invalidRect->bottom, invalidRect->right));

  if (!npp || !npp->ndata) {
    NS_WARNING("_invalidaterect: npp or npp->ndata == 0");
    return;
  }

  nsIPluginInstance *inst = (nsIPluginInstance *) npp->ndata;

  PluginDestructionGuard guard(inst);

  inst->InvalidateRect((nsPluginRect *)invalidRect);
}

void NP_CALLBACK
_invalidateregion(NPP npp, NPRegion invalidRegion)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_invalidateregion called from the wrong thread\n"));
    return;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_InvalidateRegion: npp=%p, region=%p\n", (void*)npp,
                  (void*)invalidRegion));

  if (!npp || !npp->ndata) {
    NS_WARNING("_invalidateregion: npp or npp->ndata == 0");
    return;
  }

  nsIPluginInstance *inst = (nsIPluginInstance *)npp->ndata;

  PluginDestructionGuard guard(inst);

  inst->InvalidateRegion((nsPluginRegion)invalidRegion);
}

void NP_CALLBACK
_forceredraw(NPP npp)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_forceredraw called from the wrong thread\n"));
    return;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_ForceDraw: npp=%p\n", (void*)npp));

  if (!npp || !npp->ndata) {
    NS_WARNING("_forceredraw: npp or npp->ndata == 0");
    return;
  }

  nsIPluginInstance *inst = (nsIPluginInstance *) npp->ndata;

  PluginDestructionGuard guard(inst);

  inst->ForceRedraw();
}

NPObject* NP_CALLBACK
_getwindowobject(NPP npp)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getwindowobject called from the wrong thread\n"));
    return nsnull;
  }
  JSContext *cx = GetJSContextFromNPP(npp);
  NS_ENSURE_TRUE(cx, nsnull);

  // Using ::JS_GetGlobalObject(cx) is ok here since the window we
  // want to return here is the outer window, *not* the inner (since
  // we don't know what the plugin will do with it).
  return nsJSObjWrapper::GetNewOrUsed(npp, cx, ::JS_GetGlobalObject(cx));
}

NPObject* NP_CALLBACK
_getpluginelement(NPP npp)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getpluginelement called from the wrong thread\n"));
    return nsnull;
  }

  nsNPAPIPluginInstance* inst = static_cast<nsNPAPIPluginInstance*>(npp->ndata);
  if (!inst)
    return nsnull;

  nsCOMPtr<nsIDOMElement> element;
  inst->GetDOMElement(getter_AddRefs(element));

  if (!element)
    return nsnull;

  JSContext *cx = GetJSContextFromNPP(npp);
  NS_ENSURE_TRUE(cx, nsnull);

  nsCOMPtr<nsIXPConnect> xpc(do_GetService(nsIXPConnect::GetCID()));
  NS_ENSURE_TRUE(xpc, nsnull);

  nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
  xpc->WrapNative(cx, ::JS_GetGlobalObject(cx), element,
                  NS_GET_IID(nsIDOMElement),
                  getter_AddRefs(holder));
  NS_ENSURE_TRUE(holder, nsnull);

  JSObject* obj = nsnull;
  holder->GetJSObject(&obj);
  NS_ENSURE_TRUE(obj, nsnull);

  return nsJSObjWrapper::GetNewOrUsed(npp, cx, obj);
}

NPIdentifier NP_CALLBACK
_getstringidentifier(const NPUTF8* name)
{
  if (!name) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS, ("NPN_getstringidentifier: passed null name"));
    return NULL;
  }
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getstringidentifier called from the wrong thread\n"));
  }

  nsCOMPtr<nsIThreadJSContextStack> stack =
    do_GetService("@mozilla.org/js/xpc/ContextStack;1");
  if (!stack)
    return NULL;

  JSContext *cx = nsnull;
  stack->GetSafeJSContext(&cx);
  if (!cx)
    return NULL;

  JSAutoRequest ar(cx);
  return doGetIdentifier(cx, name);
}

void NP_CALLBACK
_getstringidentifiers(const NPUTF8** names, int32_t nameCount,
                      NPIdentifier *identifiers)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getstringidentifiers called from the wrong thread\n"));
  }
  nsCOMPtr<nsIThreadJSContextStack> stack =
    do_GetService("@mozilla.org/js/xpc/ContextStack;1");
  if (!stack)
    return;

  JSContext *cx = nsnull;
  stack->GetSafeJSContext(&cx);
  if (!cx)
    return;

  JSAutoRequest ar(cx);

  for (int32_t i = 0; i < nameCount; ++i) {
    if (names[i]) {
      identifiers[i] = doGetIdentifier(cx, names[i]);
    } else {
      NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS, ("NPN_getstringidentifiers: passed null name"));
      identifiers[i] = NULL;
    }
  }
}

NPIdentifier NP_CALLBACK
_getintidentifier(int32_t intid)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getstringidentifier called from the wrong thread\n"));
  }
  return (NPIdentifier)INT_TO_JSVAL(intid);
}

NPUTF8* NP_CALLBACK
_utf8fromidentifier(NPIdentifier identifier)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_utf8fromidentifier called from the wrong thread\n"));
  }
  if (!identifier)
    return NULL;

  jsval v = (jsval)identifier;

  if (!JSVAL_IS_STRING(v)) {
    return nsnull;
  }

  JSString *str = JSVAL_TO_STRING(v);

  return
    ToNewUTF8String(nsDependentString((PRUnichar *)::JS_GetStringChars(str),
                                      ::JS_GetStringLength(str)));
}

int32_t NP_CALLBACK
_intfromidentifier(NPIdentifier identifier)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_intfromidentifier called from the wrong thread\n"));
  }
  jsval v = (jsval)identifier;

  if (!JSVAL_IS_INT(v)) {
    return PR_INT32_MIN;
  }

  return JSVAL_TO_INT(v);
}

bool NP_CALLBACK
_identifierisstring(NPIdentifier identifier)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_identifierisstring called from the wrong thread\n"));
  }
  jsval v = (jsval)identifier;

  return JSVAL_IS_STRING(v);
}

NPObject* NP_CALLBACK
_createobject(NPP npp, NPClass* aClass)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_createobject called from the wrong thread\n"));
    return nsnull;
  }
  if (!npp) {
    NS_ERROR("Null npp passed to _createobject()!");

    return nsnull;
  }

  PluginDestructionGuard guard(npp);

  if (!aClass) {
    NS_ERROR("Null class passed to _createobject()!");

    return nsnull;
  }

  NPPAutoPusher nppPusher(npp);

  NPObject *npobj;

  if (aClass->allocate) {
    npobj = aClass->allocate(npp, aClass);
  } else {
    npobj = (NPObject *)PR_Malloc(sizeof(NPObject));
  }

  if (npobj) {
    npobj->_class = aClass;
    npobj->referenceCount = 1;
    NS_LOG_ADDREF(npobj, 1, "BrowserNPObject", sizeof(NPObject));
  }

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("Created NPObject %p, NPClass %p\n", npobj, aClass));

  return npobj;
}

NPObject* NP_CALLBACK
_retainobject(NPObject* npobj)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_retainobject called from the wrong thread\n"));
  }
  if (npobj) {
    int32_t refCnt = PR_AtomicIncrement((PRInt32*)&npobj->referenceCount);
    NS_LOG_ADDREF(npobj, refCnt, "BrowserNPObject", sizeof(NPObject));
  }

  return npobj;
}

void NP_CALLBACK
_releaseobject(NPObject* npobj)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_releaseobject called from the wrong thread\n"));
  }
  if (!npobj)
    return;

  int32_t refCnt = PR_AtomicDecrement((PRInt32*)&npobj->referenceCount);
  NS_LOG_RELEASE(npobj, refCnt, "BrowserNPObject");

  if (refCnt == 0) {
    nsNPObjWrapper::OnDestroy(npobj);

    NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                   ("Deleting NPObject %p, refcount hit 0\n", npobj));

    if (npobj->_class && npobj->_class->deallocate) {
      npobj->_class->deallocate(npobj);
    } else {
      PR_Free(npobj);
    }
  }
}

bool NP_CALLBACK
_invoke(NPP npp, NPObject* npobj, NPIdentifier method, const NPVariant *args,
        uint32_t argCount, NPVariant *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_invoke called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->invoke)
    return false;

  PluginDestructionGuard guard(npp);

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_Invoke(npp %p, npobj %p, method %p, args %d\n", npp,
                  npobj, method, argCount));

  return npobj->_class->invoke(npobj, method, args, argCount, result);
}

bool NP_CALLBACK
_invokeDefault(NPP npp, NPObject* npobj, const NPVariant *args,
               uint32_t argCount, NPVariant *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_invokedefault called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->invokeDefault)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_InvokeDefault(npp %p, npobj %p, args %d\n", npp,
                  npobj, argCount));

  return npobj->_class->invokeDefault(npobj, args, argCount, result);
}

bool NP_CALLBACK
_evaluate(NPP npp, NPObject* npobj, NPString *script, NPVariant *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_evaluate called from the wrong thread\n"));
    return false;
  }
  if (!npp)
    return false;

  NPPAutoPusher nppPusher(npp);

  nsIDocument *doc = GetDocumentFromNPP(npp);
  NS_ENSURE_TRUE(doc, false);

  JSContext *cx = GetJSContextFromDoc(doc);
  NS_ENSURE_TRUE(cx, false);

  nsCOMPtr<nsIScriptContext> scx = GetScriptContextFromJSContext(cx);
  NS_ENSURE_TRUE(scx, false);

  JSObject *obj =
    nsNPObjWrapper::GetNewOrUsed(npp, cx, npobj);

  if (!obj) {
    return false;
  }

  // Root obj and the rval (below).
  jsval vec[] = { OBJECT_TO_JSVAL(obj), JSVAL_NULL };
  JSAutoTempValueRooter tvr(cx, NS_ARRAY_LENGTH(vec), vec);
  jsval *rval = &vec[1];

  if (result) {
    // Initialize the out param to void
    VOID_TO_NPVARIANT(*result);
  }

  if (!script || !script->UTF8Length || !script->UTF8Characters) {
    // Nothing to evaluate.

    return true;
  }

  NS_ConvertUTF8toUTF16 utf16script(script->UTF8Characters,
                                    script->UTF8Length);

  nsIPrincipal *principal = doc->NodePrincipal();

  nsCAutoString specStr;
  const char *spec;

  nsCOMPtr<nsIURI> uri;
  principal->GetURI(getter_AddRefs(uri));

  if (uri) {
    uri->GetSpec(specStr);
    spec = specStr.get();
  } else {
    // No URI in a principal means it's the system principal. If the
    // document URI is a chrome:// URI, pass that in as the URI of the
    // script, else pass in null for the filename as there's no way to
    // know where this document really came from. Passing in null here
    // also means that the script gets treated by XPConnect as if it
    // needs additional protection, which is what we want for unknown
    // chrome code anyways.

    uri = doc->GetDocumentURI();
    PRBool isChrome = PR_FALSE;

    if (uri && NS_SUCCEEDED(uri->SchemeIs("chrome", &isChrome)) && isChrome) {
      uri->GetSpec(specStr);
      spec = specStr.get();
    } else {
      spec = nsnull;
    }
  }

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_Evaluate(npp %p, npobj %p, script <<<%s>>>) called\n",
                  npp, npobj, script->UTF8Characters));

  nsresult rv = scx->EvaluateStringWithValue(utf16script, obj, principal,
                                             spec, 0, 0, rval, nsnull);

  return NS_SUCCEEDED(rv) &&
         (!result || JSValToNPVariant(npp, cx, *rval, result));
}

bool NP_CALLBACK
_getproperty(NPP npp, NPObject* npobj, NPIdentifier property,
             NPVariant *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getproperty called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->getProperty)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_GetProperty(npp %p, npobj %p, property %p) called\n",
                  npp, npobj, property));

  return npobj->_class->getProperty(npobj, property, result);
}

bool NP_CALLBACK
_setproperty(NPP npp, NPObject* npobj, NPIdentifier property,
             const NPVariant *value)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_setproperty called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->setProperty)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_SetProperty(npp %p, npobj %p, property %p) called\n",
                  npp, npobj, property));

  return npobj->_class->setProperty(npobj, property, value);
}

bool NP_CALLBACK
_removeproperty(NPP npp, NPObject* npobj, NPIdentifier property)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_removeproperty called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->removeProperty)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_RemoveProperty(npp %p, npobj %p, property %p) called\n",
                  npp, npobj, property));

  return npobj->_class->removeProperty(npobj, property);
}

bool NP_CALLBACK
_hasproperty(NPP npp, NPObject* npobj, NPIdentifier propertyName)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_hasproperty called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->hasProperty)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_HasProperty(npp %p, npobj %p, property %p) called\n",
                  npp, npobj, propertyName));

  return npobj->_class->hasProperty(npobj, propertyName);
}

bool NP_CALLBACK
_hasmethod(NPP npp, NPObject* npobj, NPIdentifier methodName)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_hasmethod called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->hasMethod)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_HasMethod(npp %p, npobj %p, property %p) called\n",
                  npp, npobj, methodName));

  return npobj->_class->hasMethod(npobj, methodName);
}

bool NP_CALLBACK
_enumerate(NPP npp, NPObject *npobj, NPIdentifier **identifier,
           uint32_t *count)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_enumerate called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class)
    return false;

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_Enumerate(npp %p, npobj %p) called\n", npp, npobj));

  if (!NP_CLASS_STRUCT_VERSION_HAS_ENUM(npobj->_class) ||
      !npobj->_class->enumerate) {
    *identifier = 0;
    *count = 0;
    return true;
  }

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  return npobj->_class->enumerate(npobj, identifier, count);
}

bool NP_CALLBACK
_construct(NPP npp, NPObject* npobj, const NPVariant *args,
               uint32_t argCount, NPVariant *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_construct called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class ||
      !NP_CLASS_STRUCT_VERSION_HAS_CTOR(npobj->_class) ||
      !npobj->_class->construct) {
    return false;
  }

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  return npobj->_class->construct(npobj, args, argCount, result);
}

void NP_CALLBACK
_releasevariantvalue(NPVariant* variant)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_releasevariantvalue called from the wrong thread\n"));
  }
  switch (variant->type) {
  case NPVariantType_Void :
  case NPVariantType_Null :
  case NPVariantType_Bool :
  case NPVariantType_Int32 :
  case NPVariantType_Double :
    break;
  case NPVariantType_String :
    {
      const NPString *s = &NPVARIANT_TO_STRING(*variant);

      if (s->UTF8Characters) {
#if defined(MOZ_MEMORY_WINDOWS) && !defined(MOZ_MEMORY_WINCE)
        if (malloc_usable_size((void *)s->UTF8Characters) != 0) {
          PR_Free((void *)s->UTF8Characters);
        } else {
          void *p = (void *)s->UTF8Characters;
          DWORD nheaps = 0;
          nsAutoTArray<HANDLE, 50> heaps;
          nheaps = GetProcessHeaps(0, heaps.Elements());
          heaps.AppendElements(nheaps);
          GetProcessHeaps(nheaps, heaps.Elements());
          for (DWORD i = 0; i < nheaps; i++) {
            if (InHeap(heaps[i], p)) {
              HeapFree(heaps[i], 0, p);
              break;
            }
          }
        }
#else
        PR_Free((void *)s->UTF8Characters);
#endif
      }
      break;
    }
  case NPVariantType_Object:
    {
      NPObject *npobj = NPVARIANT_TO_OBJECT(*variant);

      if (npobj)
        _releaseobject(npobj);

      break;
    }
  default:
    NS_ERROR("Unknown NPVariant type!");
  }

  VOID_TO_NPVARIANT(*variant);
}

bool NP_CALLBACK
_tostring(NPObject* npobj, NPVariant *result)
{
  NS_ERROR("Write me!");

  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_tostring called from the wrong thread\n"));
    return false;
  }

  return false;
}

void NP_CALLBACK
_setexception(NPObject* npobj, const NPUTF8 *message)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_setexception called from the wrong thread\n"));
    return;
  }

  if (gNPPException) {
    // If a plugin throws multiple exceptions, we'll only report the
    // last one for now.
    free(gNPPException);
  }

  gNPPException = strdup(message);
}

NPError NP_CALLBACK
_getvalue(NPP npp, NPNVariable variable, void *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getvalue called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_GetValue: npp=%p, var=%d\n",
                                     (void*)npp, (int)variable));

  nsresult res;

  PluginDestructionGuard guard(npp);

  switch(variable) {
#if defined(XP_UNIX) && !defined(XP_MACOSX)
  case NPNVxDisplay : {
#ifdef MOZ_WIDGET_GTK2
    if (npp) {
      nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *) npp->ndata;
      PRBool windowless = PR_FALSE;
      inst->GetValue(nsPluginInstanceVariable_WindowlessBool, &windowless);
      NPBool needXEmbed = PR_FALSE;
      if (!windowless) {
        inst->GetValue((nsPluginInstanceVariable)NPPVpluginNeedsXEmbed, &needXEmbed);
      }
      if (windowless || needXEmbed) {
        (*(Display **)result) = GDK_DISPLAY();
        return NPERR_NO_ERROR;
      }
    }
    // adobe nppdf calls XtGetApplicationNameAndClass(display,
    // &instance, &class) we have to init Xt toolkit before get
    // XtDisplay just call gtk_xtbin_new(w,0) once
    static GtkWidget *gtkXtBinHolder = 0;
    if (!gtkXtBinHolder) {
      gtkXtBinHolder = gtk_xtbin_new(gdk_get_default_root_window(),0);
      // it crashes on destroy, let it leak
      // gtk_widget_destroy(gtkXtBinHolder);
    }
    (*(Display **)result) =  GTK_XTBIN(gtkXtBinHolder)->xtdisplay;
    return NPERR_NO_ERROR;
#endif
    return NPERR_GENERIC_ERROR;
  }

  case NPNVxtAppContext:
    return NPERR_GENERIC_ERROR;
#endif

#if defined(XP_WIN) || defined(XP_OS2) || defined(MOZ_WIDGET_GTK2)
  case NPNVnetscapeWindow: {
    if (!npp || !npp->ndata)
      return NPERR_INVALID_INSTANCE_ERROR;

    nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *) npp->ndata;

    nsCOMPtr<nsIPluginInstanceOwner> owner;
    inst->GetOwner(getter_AddRefs(owner));
    NS_ENSURE_TRUE(owner, nsnull);

    if (NS_SUCCEEDED(owner->GetNetscapeWindow(result))) {
      return NPERR_NO_ERROR;
    }
    return NPERR_GENERIC_ERROR;
  }
#endif

  case NPNVjavascriptEnabledBool: {
    *(NPBool*)result = PR_FALSE;
    nsCOMPtr<nsIPrefBranch> prefs(do_GetService(NS_PREFSERVICE_CONTRACTID));
    if (prefs) {
      PRBool js = PR_FALSE;;
      res = prefs->GetBoolPref("javascript.enabled", &js);
      if (NS_SUCCEEDED(res))
        *(NPBool*)result = js;
    }
    return NPERR_NO_ERROR;
  }

  case NPNVasdEnabledBool:
    *(NPBool*)result = PR_FALSE;
    return NPERR_NO_ERROR;

  case NPNVisOfflineBool: {
    PRBool offline = PR_FALSE;
    nsCOMPtr<nsIIOService> ioservice =
      do_GetService(NS_IOSERVICE_CONTRACTID, &res);
    if (NS_SUCCEEDED(res))
      res = ioservice->GetOffline(&offline);
    if (NS_FAILED(res))
      return NPERR_GENERIC_ERROR;

    *(NPBool*)result = offline;
    return NPERR_NO_ERROR;
  }

  case NPNVToolkit: {
#ifdef MOZ_WIDGET_GTK2
    *((NPNToolkitType*)result) = NPNVGtk2;
#endif

    if (*(NPNToolkitType*)result)
        return NPERR_NO_ERROR;

    return NPERR_GENERIC_ERROR;
  }

  case NPNVSupportsXEmbedBool: {
#ifdef MOZ_WIDGET_GTK2
    *(NPBool*)result = PR_TRUE;
#else
    *(NPBool*)result = PR_FALSE;
#endif
    return NPERR_NO_ERROR;
  }

  case NPNVWindowNPObject: {
    *(NPObject **)result = _getwindowobject(npp);

    return *(NPObject **)result ? NPERR_NO_ERROR : NPERR_GENERIC_ERROR;
  }

  case NPNVPluginElementNPObject: {
    *(NPObject **)result = _getpluginelement(npp);

    return *(NPObject **)result ? NPERR_NO_ERROR : NPERR_GENERIC_ERROR;
  }

  case NPNVSupportsWindowless: {
#if defined(XP_WIN) || defined(XP_MACOSX) || (defined(MOZ_X11) && defined(MOZ_WIDGET_GTK2))
    *(NPBool*)result = PR_TRUE;
#else
    *(NPBool*)result = PR_FALSE;
#endif
    return NPERR_NO_ERROR;
  }

  case NPNVprivateModeBool: {
    nsCOMPtr<nsIPrivateBrowsingService> pbs = do_GetService(NS_PRIVATE_BROWSING_SERVICE_CONTRACTID);
    if (pbs) {
      PRBool enabled;
      pbs->GetPrivateBrowsingEnabled(&enabled);
      *(NPBool*)result = (NPBool)enabled;
      return NPERR_NO_ERROR;
    }
    return NPERR_GENERIC_ERROR;
  }

#if (MOZ_PLATFORM_MAEMO == 5)
  case NPNVSupportsWindowlessLocal: {
    *(NPBool*)result = PR_TRUE;
    return NPERR_NO_ERROR;
  }
#endif

#ifdef XP_MACOSX
  case NPNVpluginDrawingModel: {
    if (npp) {
      nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance*)npp->ndata;
      if (inst) {
        *(NPDrawingModel*)result = inst->GetDrawingModel();
        return NPERR_NO_ERROR;
      }
    }
    else {
      return NPERR_GENERIC_ERROR;
    }
  }

#ifndef NP_NO_QUICKDRAW
  case NPNVsupportsQuickDrawBool: {
    *(NPBool*)result = PR_TRUE;
    
    return NPERR_NO_ERROR;
  }
#endif

  case NPNVsupportsCoreGraphicsBool: {
    *(NPBool*)result = PR_TRUE;
    
    return NPERR_NO_ERROR;
  }
#endif

  // we no longer hand out any XPCOM objects, except on WINCE,
  // where it's needed for the ActiveX shunt that makes Flash
  // work until we get an NPAPI plugin there.
#ifdef WINCE
  case NPNVDOMWindow: {
    nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *)npp->ndata;
    NS_ENSURE_TRUE(inst, NPERR_GENERIC_ERROR);

    nsIDOMWindow *domWindow = inst->GetDOMWindow().get();

    if (domWindow) {
      // Pass over ownership of domWindow to the caller.
      (*(nsIDOMWindow**)result) = domWindow;
      return NPERR_NO_ERROR;
    }

    return NPERR_GENERIC_ERROR;
  }

  case NPNVDOMElement: {
    nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *) npp->ndata;
    NS_ENSURE_TRUE(inst, NPERR_GENERIC_ERROR);

    nsCOMPtr<nsIDOMElement> e;
    inst->GetDOMElement(getter_AddRefs(e));
    if (e) {
      NS_ADDREF(*(nsIDOMElement**)result = e.get());
      return NPERR_NO_ERROR;
    }

    return NPERR_GENERIC_ERROR;
  }
#else
  case NPNVDOMElement:
    // fall through
  case NPNVDOMWindow:
    // fall through
#endif /* WINCE */
  case NPNVserviceManager:
    // old XPCOM objects, no longer supported, but null out the out
    // param to avoid crashing plugins that still try to use this.
    *(nsISupports**)result = nsnull;
    // fall through
  default:
    return NPERR_GENERIC_ERROR;
  }
}

NPError NP_CALLBACK
_setvalue(NPP npp, NPPVariable variable, void *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_setvalue called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_SetValue: npp=%p, var=%d\n",
                                     (void*)npp, (int)variable));

  if (!npp)
    return NPERR_INVALID_INSTANCE_ERROR;

  nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *) npp->ndata;

  NS_ASSERTION(inst, "null instance");

  if (!inst)
    return NPERR_INVALID_INSTANCE_ERROR;

  PluginDestructionGuard guard(inst);

  switch (variable) {

    // we should keep backward compatibility with NPAPI where the
    // actual pointer value is checked rather than its content
    // when passing booleans
    case NPPVpluginWindowBool: {
#ifdef XP_MACOSX
      // This setting doesn't apply to OS X (only to Windows and Unix/Linux).
      // See https://developer.mozilla.org/En/NPN_SetValue#section_5.  Return
      // NPERR_NO_ERROR here to conform to other browsers' behavior on OS X
      // (e.g. Safari and Opera).
      return NPERR_NO_ERROR;
#else
      NPBool bWindowless = (result == nsnull);
      return inst->SetWindowless(bWindowless);
#endif
    }

#if (MOZ_PLATFORM_MAEMO == 5)
    case NPPVpluginWindowlessLocalBool: {
      NPBool bWindowlessLocal = (result != nsnull);
      return inst->SetWindowlessLocal(bWindowlessLocal);
    }
#endif
    case NPPVpluginTransparentBool: {
      NPBool bTransparent = (result != nsnull);
      return inst->SetTransparent(bTransparent);
    }

    case NPPVjavascriptPushCallerBool:
      {
        nsresult rv;
        nsCOMPtr<nsIJSContextStack> contextStack =
          do_GetService("@mozilla.org/js/xpc/ContextStack;1", &rv);
        if (NS_SUCCEEDED(rv)) {
          NPBool bPushCaller = (result != nsnull);
          if (bPushCaller) {
            JSContext *cx;
            rv = inst->GetJSContext(&cx);
            if (NS_SUCCEEDED(rv))
              rv = contextStack->Push(cx);
          } else {
            rv = contextStack->Pop(nsnull);
          }
        }
        return NS_SUCCEEDED(rv) ? NPERR_NO_ERROR : NPERR_GENERIC_ERROR;
      }

    case NPPVpluginKeepLibraryInMemory: {
      NPBool bCached = (result != nsnull);
      return inst->SetCached(bCached);
    }

    case NPPVpluginWantsAllNetworkStreams: {
      PRBool bWantsAllNetworkStreams = (result != nsnull);
      return inst->SetWantsAllNetworkStreams(bWantsAllNetworkStreams);
    }

#ifdef XP_MACOSX
    case NPPVpluginDrawingModel: {
      if (inst) {
        inst->SetDrawingModel((NPDrawingModel)NS_PTR_TO_INT32(result));
        return NPERR_NO_ERROR;
      }
      else {
        return NPERR_GENERIC_ERROR;
      }
    }
#endif

    default:
      return NPERR_GENERIC_ERROR;
  }
}

NPError NP_CALLBACK
_requestread(NPStream *pstream, NPByteRange *rangeList)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_requestread called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_RequestRead: stream=%p\n",
                                     (void*)pstream));

#ifdef PLUGIN_LOGGING
  for(NPByteRange * range = rangeList; range != nsnull; range = range->next)
    PR_LOG(nsPluginLogging::gNPNLog,PLUGIN_LOG_NOISY,
    ("%i-%i", range->offset, range->offset + range->length - 1));

  PR_LOG(nsPluginLogging::gNPNLog,PLUGIN_LOG_NOISY, ("\n\n"));
  PR_LogFlush();
#endif

  if (!pstream || !rangeList || !pstream->ndata)
    return NPERR_INVALID_PARAM;

  nsNPAPIPluginStreamListener* streamlistener = (nsNPAPIPluginStreamListener*)pstream->ndata;

  nsPluginStreamType streamtype = nsPluginStreamType_Normal;

  streamlistener->GetStreamType(&streamtype);

  if (streamtype != nsPluginStreamType_Seek)
    return NPERR_STREAM_NOT_SEEKABLE;

  if (!streamlistener->mStreamInfo)
    return NPERR_GENERIC_ERROR;

  nsresult rv = streamlistener->mStreamInfo
    ->RequestRead((nsByteRange*) rangeList);
  if (NS_FAILED(rv))
    return NPERR_GENERIC_ERROR;

  return NS_OK;
}

// Deprecated, only stubbed out
void* NP_CALLBACK /* OJI type: JRIEnv* */
_getJavaEnv(void)
{
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_GetJavaEnv\n"));
  return NULL;
}

const char * NP_CALLBACK
_useragent(NPP npp)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_useragent called from the wrong thread\n"));
    return nsnull;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_UserAgent: npp=%p\n", (void*)npp));

  nsCOMPtr<nsIPluginHost> pluginHost(do_GetService(MOZ_PLUGIN_HOST_CONTRACTID));
  if (!pluginHost)
    return nsnull;

  const char *retstr;
  nsresult rv = pluginHost->UserAgent(&retstr);
  if (NS_FAILED(rv))
    return nsnull;

  return retstr;
}

void * NP_CALLBACK
_memalloc (uint32_t size)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,("NPN_memalloc called from the wrong thread\n"));
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY, ("NPN_MemAlloc: size=%d\n", size));
  return nsMemory::Alloc(size);
}

// Deprecated, only stubbed out
void* NP_CALLBACK /* OJI type: jref */
_getJavaPeer(NPP npp)
{
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_GetJavaPeer: npp=%p\n", (void*)npp));
  return NULL;
}

void NP_CALLBACK
_pushpopupsenabledstate(NPP npp, NPBool enabled)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_pushpopupsenabledstate called from the wrong thread\n"));
    return;
  }
  nsNPAPIPluginInstance *inst = npp ? (nsNPAPIPluginInstance *)npp->ndata : NULL;
  if (!inst)
    return;

  inst->PushPopupsEnabledState(enabled);
}

void NP_CALLBACK
_poppopupsenabledstate(NPP npp)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_poppopupsenabledstate called from the wrong thread\n"));
    return;
  }
  nsNPAPIPluginInstance *inst = npp ? (nsNPAPIPluginInstance *)npp->ndata : NULL;
  if (!inst)
    return;

  inst->PopPopupsEnabledState();
}

void NP_CALLBACK
_pluginthreadasynccall(NPP instance, PluginThreadCallback func, void *userData)
{
  if (NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,("NPN_pluginthreadasynccall called from the main thread\n"));
  } else {
    NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,("NPN_pluginthreadasynccall called from a non main thread\n"));
  }
  nsRefPtr<nsPluginThreadRunnable> evt =
    new nsPluginThreadRunnable(instance, func, userData);

  if (evt && evt->IsValid()) {
    NS_DispatchToMainThread(evt);
  }
}

NPError NP_CALLBACK
_getvalueforurl(NPP instance, NPNURLVariable variable, const char *url,
                char **value, uint32_t *len)
{
  if (!instance) {
    return NPERR_INVALID_PARAM;
  }

  if (!url || !*url || !len) {
    return NPERR_INVALID_URL;
  }

  *len = 0;

  switch (variable) {
  case NPNURLVProxy:
    {
      nsCOMPtr<nsIPluginHost> pluginHost(do_GetService(MOZ_PLUGIN_HOST_CONTRACTID));

      if (pluginHost && NS_SUCCEEDED(pluginHost->FindProxyForURL(url, value))) {
        *len = *value ? PL_strlen(*value) : 0;
        return NPERR_NO_ERROR;
      }
      break;
    }
  case NPNURLVCookie:
    {
      nsCOMPtr<nsICookieService> cookieService =
        do_GetService(NS_COOKIESERVICE_CONTRACTID);

      if (!cookieService)
        return NPERR_GENERIC_ERROR;

      // Make an nsURI from the url argument
      nsCOMPtr<nsIURI> uri;
      if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), nsDependentCString(url)))) {
        return NPERR_GENERIC_ERROR;
      }

      nsXPIDLCString cookieStr;
      nsresult cookieReturn = cookieService->GetCookieString(uri, nsnull,
                                                             getter_Copies(cookieStr));
      if (NS_FAILED(cookieReturn) || !cookieStr) {
        return NPERR_GENERIC_ERROR;
      }

      *value = PL_strndup(cookieStr, cookieStr.Length());

      if (*value) {
        *len = cookieStr.Length();

        return NPERR_NO_ERROR;
      }
    }

    break;
  default:
    // Fall through and return an error...
    ;
  }

  return NPERR_GENERIC_ERROR;
}

NPError NP_CALLBACK
_setvalueforurl(NPP instance, NPNURLVariable variable, const char *url,
                const char *value, uint32_t len)
{
  if (!instance) {
    return NPERR_INVALID_PARAM;
  }

  if (!url || !*url) {
    return NPERR_INVALID_URL;
  }

  switch (variable) {
  case NPNURLVCookie:
    {
      if (!url || !value || (0 >= len))
        return NPERR_INVALID_PARAM;

      nsresult rv = NS_ERROR_FAILURE;
      nsCOMPtr<nsIIOService> ioService(do_GetService(NS_IOSERVICE_CONTRACTID, &rv));
      if (NS_FAILED(rv))
        return NPERR_GENERIC_ERROR;

      nsCOMPtr<nsICookieService> cookieService = do_GetService(NS_COOKIESERVICE_CONTRACTID, &rv);
      if (NS_FAILED(rv))
        return NPERR_GENERIC_ERROR;

      nsCOMPtr<nsIURI> uriIn;
      rv = ioService->NewURI(nsDependentCString(url), nsnull, nsnull, getter_AddRefs(uriIn));
      if (NS_FAILED(rv))
        return NPERR_GENERIC_ERROR;

      nsCOMPtr<nsIPrompt> prompt;
      nsPluginHost::GetPrompt(nsnull, getter_AddRefs(prompt));

      char *cookie = (char*)value;
      char c = cookie[len];
      cookie[len] = '\0';
      rv = cookieService->SetCookieString(uriIn, prompt, cookie, nsnull);
      cookie[len] = c;
      if (NS_SUCCEEDED(rv))
        return NPERR_NO_ERROR;
    }

    break;
  case NPNURLVProxy:
    // We don't support setting proxy values, fall through...
  default:
    // Fall through and return an error...
    ;
  }

  return NPERR_GENERIC_ERROR;
}

NPError NP_CALLBACK
_getauthenticationinfo(NPP instance, const char *protocol, const char *host,
                       int32_t port, const char *scheme, const char *realm,
                       char **username, uint32_t *ulen, char **password,
                       uint32_t *plen)
{
  if (!instance || !protocol || !host || !scheme || !realm || !username ||
      !ulen || !password || !plen)
    return NPERR_INVALID_PARAM;

  *username = nsnull;
  *password = nsnull;
  *ulen = 0;
  *plen = 0;

  nsDependentCString proto(protocol);

  if (!proto.LowerCaseEqualsLiteral("http") &&
      !proto.LowerCaseEqualsLiteral("https"))
    return NPERR_GENERIC_ERROR;

  nsCOMPtr<nsIHttpAuthManager> authManager =
    do_GetService("@mozilla.org/network/http-auth-manager;1");
  if (!authManager)
    return NPERR_GENERIC_ERROR;

  nsAutoString unused, uname16, pwd16;
  if (NS_FAILED(authManager->GetAuthIdentity(proto, nsDependentCString(host),
                                             port, nsDependentCString(scheme),
                                             nsDependentCString(realm),
                                             EmptyCString(), unused, uname16,
                                             pwd16))) {
    return NPERR_GENERIC_ERROR;
  }

  NS_ConvertUTF16toUTF8 uname8(uname16);
  NS_ConvertUTF16toUTF8 pwd8(pwd16);

  *username = ToNewCString(uname8);
  *ulen = *username ? uname8.Length() : 0;

  *password = ToNewCString(pwd8);
  *plen = *password ? pwd8.Length() : 0;

  return NPERR_NO_ERROR;
}

uint32_t NP_CALLBACK
_scheduletimer(NPP instance, uint32_t interval, NPBool repeat, PluginTimerFunc timerFunc)
{
  nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *)instance->ndata;
  if (!inst)
    return 0;

  return inst->ScheduleTimer(interval, repeat, timerFunc);
}

void NP_CALLBACK
_unscheduletimer(NPP instance, uint32_t timerID)
{
  nsNPAPIPluginInstance *inst = (nsNPAPIPluginInstance *)instance->ndata;
  if (!inst)
    return;

  inst->UnscheduleTimer(timerID);
}

} /* namespace parent */
} /* namespace plugins */
} /* namespace mozilla */

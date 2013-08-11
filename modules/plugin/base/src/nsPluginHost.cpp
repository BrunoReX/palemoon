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
 *   Sean Echevarria <sean@beatnik.com>
 *   Håkan Waara <hwaara@chello.se>
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

/* nsPluginHost.cpp - top-level plugin management code */

#include "nscore.h"
#include "nsPluginHost.h"

#include <stdio.h>
#include "prio.h"
#include "prmem.h"
#include "nsNPAPIPlugin.h"
#include "nsNPAPIPluginStreamListener.h"
#include "nsIPlugin.h"
#include "nsNPAPIPluginInstance.h"
#ifdef OJI
#include "nsIJVMPlugin.h"
#include "nsIJVMPluginInstance.h"
#include "nsIJVMManager.h"
#endif
#include "nsIPluginStreamListener.h"
#include "nsIHTTPHeaderListener.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsIObserverService.h"
#include "nsIHttpProtocolHandler.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIUploadChannel.h"
#include "nsIByteRangeRequest.h"
#include "nsIStreamListener.h"
#include "nsIInputStream.h"
#include "nsIOutputStream.h"
#include "nsIURL.h"
#include "nsXPIDLString.h"
#include "nsReadableUtils.h"
#include "nsIProtocolProxyService.h"
#include "nsIStreamConverterService.h"
#include "nsIFile.h"
#include "nsIInputStream.h"
#include "nsIIOService.h"
#include "nsIURL.h"
#include "nsIChannel.h"
#include "nsISeekableStream.h"
#include "nsNetUtil.h"
#include "nsIProgressEventSink.h"
#include "nsIDocument.h"
#include "nsICachingChannel.h"
#include "nsHashtable.h"
#include "nsIProxyInfo.h"
#include "nsObsoleteModuleLoading.h"
#include "nsIComponentRegistrar.h"
#include "nsPluginLogging.h"
#include "nsIPrefBranch2.h"
#include "nsIScriptChannel.h"
#include "nsPrintfCString.h"
#include "nsIBlocklistService.h"
#include "nsVersionComparator.h"
#include "nsIPrivateBrowsingService.h"
#include "nsIObjectLoadingContent.h"
#include "nsIWritablePropertyBag2.h"

#include "nsEnumeratorUtils.h"
#include "nsXPCOM.h"
#include "nsXPCOMCID.h"
#include "nsICategoryManager.h"
#include "nsISupportsPrimitives.h"

// for the dialog
#include "nsIStringBundle.h"
#include "nsIWindowWatcher.h"
#include "nsPIDOMWindow.h"

#include "nsIScriptGlobalObject.h"
#include "nsIScriptGlobalObjectOwner.h"
#include "nsIPrincipal.h"

#include "nsNetCID.h"
#include "nsIDOMPlugin.h"
#include "nsIDOMMimeType.h"
#include "nsMimeTypes.h"
#include "prprf.h"
#include "nsThreadUtils.h"
#include "nsIInputStreamTee.h"
#include "nsIInterfaceInfoManager.h"
#include "xptinfo.h"

#include "nsIMIMEService.h"
#include "nsCExternalHandlerService.h"
#include "nsILocalFile.h"
#include "nsIFileChannel.h"

#include "nsPluginSafety.h"

#include "nsICharsetConverterManager.h"
#include "nsIPlatformCharset.h"

#include "nsIDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsXULAppAPI.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsPluginDirServiceProvider.h"
#include "nsInt64.h"
#include "nsPluginError.h"

#include "nsUnicharUtils.h"
#include "nsPluginManifestLineReader.h"

#include "nsDefaultPlugin.h"
#include "nsIWeakReferenceUtils.h"
#include "nsIDOMElement.h"
#include "nsIDOMHTMLObjectElement.h"
#include "nsIDOMHTMLEmbedElement.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "nsIWebNavigation.h"
#include "nsISupportsArray.h"
#include "nsIDocShell.h"
#include "nsPluginNativeWindow.h"
#include "nsIScriptSecurityManager.h"
#include "nsIContentPolicy.h"
#include "nsContentPolicyUtils.h"
#include "nsContentErrors.h"
#include "nsIContentUtils.h"

#include "nsIInterfaceRequestor.h"
#include "nsIChannelEventSink.h"

#if defined(XP_WIN)
#include "nsIWindowMediator.h"
#include "nsIBaseWindow.h"
#include "windows.h"
#include "winbase.h"
#endif

#if defined(XP_UNIX) && defined(MOZ_WIDGET_GTK2) & defined(MOZ_X11)
#include <gdk/gdkx.h> // for GDK_DISPLAY()
#endif

// this is the name of the directory which will be created
// to cache temporary files.
#define kPluginTmpDirName NS_LITERAL_CSTRING("plugtmp")

// Version of cached plugin info
// 0.01 first implementation
// 0.02 added caching of CanUnload to fix bug 105935
// 0.03 changed name, description and mime desc from string to bytes, bug 108246
// 0.04 added new mime entry point on Mac, bug 113464
// 0.05 added new entry point check for the default plugin, bug 132430
// 0.06 strip off suffixes in mime description strings, bug 53895
// 0.07 changed nsIRegistry to flat file support for caching plugins info
// 0.08 mime entry point on MachO, bug 137535
// 0.09 the file encoding is changed to UTF-8, bug 420285
// 0.10 added plugin versions on appropriate platforms, bug 427743
// 0.11 file name and full path fields now store expected values on all platforms, bug 488181
// The current plugin registry version (and the maximum version we know how to read)
static const char *kPluginRegistryVersion = "0.11";
// The minimum registry version we know how to read
static const char *kMinimumRegistryVersion = "0.9";

#ifdef OJI
static NS_DEFINE_CID(kPluginOldCID, NS_PLUGINOLD_CID);
static NS_DEFINE_IID(kIPluginInstanceOldIID, NS_IPLUGININSTANCEOLD_IID);
#endif
static NS_DEFINE_IID(kIPluginTagInfoIID, NS_IPLUGINTAGINFO_IID);
static const char kDirectoryServiceContractID[] = "@mozilla.org/file/directory_service;1";

// Registry keys for caching plugin info
static const char kPluginsRootKey[] = "software/plugins";
static const char kPluginsNameKey[] = "name";
static const char kPluginsDescKey[] = "description";
static const char kPluginsFilenameKey[] = "filename";
static const char kPluginsFullpathKey[] = "fullpath";
static const char kPluginsModTimeKey[] = "lastModTimeStamp";
static const char kPluginsCanUnload[] = "canUnload";
static const char kPluginsVersionKey[] = "version";
static const char kPluginsMimeTypeKey[] = "mimetype";
static const char kPluginsMimeDescKey[] = "description";
static const char kPluginsMimeExtKey[] = "extension";

#define kPluginRegistryFilename NS_LITERAL_CSTRING("pluginreg.dat")

#ifdef PLUGIN_LOGGING
PRLogModuleInfo* nsPluginLogging::gNPNLog = nsnull;
PRLogModuleInfo* nsPluginLogging::gNPPLog = nsnull;
PRLogModuleInfo* nsPluginLogging::gPluginLog = nsnull;
#endif

#define BRAND_PROPERTIES_URL "chrome://branding/locale/brand.properties"
#define PLUGIN_PROPERTIES_URL "chrome://global/locale/downloadProgress.properties"

// #defines for plugin cache and prefs
#define NS_PREF_MAX_NUM_CACHED_PLUGINS "browser.plugins.max_num_cached_plugins"
#define DEFAULT_NUMBER_OF_STOPPED_PLUGINS 10

#define MAGIC_REQUEST_CONTEXT 0x01020304

nsresult PostPluginUnloadEvent(PRLibrary * aLibrary);

static nsPluginInstanceTagList *gActivePluginList;

#ifdef CALL_SAFETY_ON
PRBool gSkipPluginSafeCalls = PR_FALSE;
#endif

nsIFile *nsPluginHost::sPluginTempDir;
nsPluginHost *nsPluginHost::sInst;

// flat file reg funcs
static
PRBool ReadSectionHeader(nsPluginManifestLineReader& reader, const char *token)
{
  do {
    if (*reader.LinePtr() == '[') {
      char* p = reader.LinePtr() + (reader.LineLength() - 1);
      if (*p != ']')
        break;
      *p = 0;

      char* values[1];
      if (1 != reader.ParseLine(values, 1))
        break;
      // ignore the leading '['
      if (PL_strcmp(values[0]+1, token)) {
        break; // it's wrong token
      }
      return PR_TRUE;
    }
  } while (reader.NextLine());
  return PR_FALSE;
}

// Little helper struct to asynchronously reframe any presentations (embedded)
// or reload any documents (full-page), that contained plugins
// which were shutdown as a result of a plugins.refresh(1)
class nsPluginDocReframeEvent: public nsRunnable {
public:
  nsPluginDocReframeEvent(nsISupportsArray* aDocs) { mDocs = aDocs; }

  NS_DECL_NSIRUNNABLE

  nsCOMPtr<nsISupportsArray> mDocs;
};

NS_IMETHODIMP nsPluginDocReframeEvent::Run() {
  NS_ENSURE_TRUE(mDocs, NS_ERROR_FAILURE);

  PRUint32 c;
  mDocs->Count(&c);

  // for each document (which previously had a running instance), tell
  // the frame constructor to rebuild
  for (PRUint32 i = 0; i < c; i++) {
    nsCOMPtr<nsIDocument> doc (do_QueryElementAt(mDocs, i));
    if (doc) {
      nsIPresShell *shell = doc->GetPrimaryShell();

      // if this document has a presentation shell, then it has frames and can be reframed
      if (shell) {
        /* A reframe will cause a fresh object frame, instance owner, and instance
         * to be created. Reframing of the entire document is necessary as we may have
         * recently found new plugins and we want a shot at trying to use them instead
         * of leaving alternate renderings.
         * We do not want to completely reload all the documents that had running plugins
         * because we could possibly trigger a script to run in the unload event handler
         * which may want to access our defunct plugin and cause us to crash.
         */

        shell->ReconstructFrames(); // causes reframe of document
      } else {  // no pres shell --> full-page plugin

        NS_NOTREACHED("all plugins should have a pres shell!");

      }
    }
  }

  return mDocs->Clear();
}

nsPluginInstanceTag::nsPluginInstanceTag(nsPluginTag* aPluginTag,
                               nsIPluginInstance* aInstance,
                               const char * url,
                               PRBool aDefaultPlugin)
{
  mNext = nsnull;
  mPluginTag = aPluginTag;

  mURL = PL_strdup(url);
  mInstance = aInstance;
  if (aInstance)
    NS_ADDREF(aInstance);
  mXPConnected = PR_FALSE;
  mDefaultPlugin = aDefaultPlugin;
  mStopped = PR_FALSE;
  mllStopTime = LL_ZERO;
}

nsPluginInstanceTag::~nsPluginInstanceTag()
{
  mPluginTag = nsnull;
  if (mInstance) {
    nsCOMPtr<nsIPluginInstanceOwner> owner;
    mInstance->GetOwner(getter_AddRefs(owner));
    if (owner)
      owner->SetInstance(nsnull);
    mInstance->InvalidateOwner();

    NS_RELEASE(mInstance);
  }
  PL_strfree(mURL);
}

void nsPluginInstanceTag::setStopped(PRBool stopped)
{
  mStopped = stopped;
  if (mStopped) // plugin instance is told to stop
    mllStopTime = PR_Now();
  else
    mllStopTime = LL_ZERO;
}

nsPluginInstanceTagList::nsPluginInstanceTagList()
{
  mFirst = nsnull;
}

nsPluginInstanceTagList::~nsPluginInstanceTagList()
{
  if (!mFirst)
    return;
  shutdown();
}

void nsPluginInstanceTagList::shutdown()
{
  if (!mFirst)
    return;

  for (nsPluginInstanceTag * plugin = mFirst; plugin != nsnull;) {
    nsPluginInstanceTag * next = plugin->mNext;
    remove(plugin);
    plugin = next;
  }
  mFirst = nsnull;
}

void nsPluginInstanceTagList::add(nsPluginInstanceTag * plugin)
{
  plugin->mNext = mFirst;
  mFirst = plugin;
}

PRBool nsPluginInstanceTagList::IsLastInstance(nsPluginInstanceTag * plugin)
{
  if (!plugin)
    return PR_FALSE;

  if (!plugin->mPluginTag)
    return PR_FALSE;

  for (nsPluginInstanceTag * p = mFirst; p != nsnull; p = p->mNext) {
    if ((p->mPluginTag == plugin->mPluginTag) && (p != plugin))
      return PR_FALSE;
  }
  return PR_TRUE;
}

void nsPluginInstanceTagList::remove(nsPluginInstanceTag * plugin)
{
  if (!mFirst)
    return;

  nsPluginInstanceTag * prev = nsnull;
  for (nsPluginInstanceTag * p = mFirst; p != nsnull; p = p->mNext) {
    if (p == plugin) {
      PRBool lastInstance = IsLastInstance(p);
      nsPluginTag *pluginTag = p->mPluginTag;

      if (p == mFirst)
        mFirst = p->mNext;
      else
        prev->mNext = p->mNext;

      delete p;

      if (lastInstance && pluginTag) {
        nsresult rv;
        nsCOMPtr<nsIPrefBranch> pref(do_GetService(NS_PREFSERVICE_CONTRACTID, &rv));
        if (NS_FAILED(rv))
          return;

        PRBool unloadPluginsASAP = PR_FALSE;
        rv = pref->GetBoolPref("plugins.unloadASAP", &unloadPluginsASAP);
        if (NS_SUCCEEDED(rv) && unloadPluginsASAP)
          pluginTag->TryUnloadPlugin();
      }

      return;
    }
    prev = p;
  }
  return;
}

// This method terminates all running instances of plugins and collects their
// documents to be returned through an array. This method is used
// when we are shutting down or when a plugins.refresh(1) happens.
// If aPluginTag is given, then only that plugin is terminated
void nsPluginInstanceTagList::stopRunning(nsISupportsArray* aReloadDocs,
                                     nsPluginTag* aPluginTag)
{
  if (!mFirst)
    return;

  PRBool doCallSetWindowAfterDestroy = PR_FALSE;

  for (nsPluginInstanceTag * p = mFirst; p != nsnull; p = p->mNext) {
    if (!p->mStopped && p->mInstance &&
       (!aPluginTag || aPluginTag == p->mPluginTag)) {
      // then determine if the plugin wants Destroy to be called after
      // Set Window.  This is for bug 50547.
      p->mInstance->GetValue(nsPluginInstanceVariable_CallSetWindowAfterDestroyBool,
                             (void *) &doCallSetWindowAfterDestroy);
      if (doCallSetWindowAfterDestroy) {
        p->mInstance->Stop();
#ifdef OJI
        nsCOMPtr<nsIPluginInstanceOld> instOld(do_QueryInterface(p->mInstance));
        if (instOld)
          instOld->Destroy();
#endif
        p->mInstance->SetWindow(nsnull);
      }
      else {
        p->mInstance->SetWindow(nsnull);
        p->mInstance->Stop();
#ifdef OJI
        nsCOMPtr<nsIPluginInstanceOld> instOld(do_QueryInterface(p->mInstance));
        if (instOld)
          instOld->Destroy();
#endif
      }
      doCallSetWindowAfterDestroy = PR_FALSE;
      p->setStopped(PR_TRUE);

      // If we've been passed an array to return, lets collect all our documents,
      // removing duplicates. These will be reframed (embedded) or reloaded (full-page) later
      // to kickstart our instances.
      if (aReloadDocs && p->mInstance) {
        nsCOMPtr<nsIPluginInstanceOwner> owner;
        p->mInstance->GetOwner(getter_AddRefs(owner));
        if (owner) {
          nsCOMPtr<nsIDocument> doc;
          owner->GetDocument(getter_AddRefs(doc));
          if (doc && aReloadDocs->IndexOf(doc) == -1)  // don't allow for duplicates
            aReloadDocs->AppendElement(doc);
        }
      }
    }
  }
}

void nsPluginInstanceTagList::removeAllStopped()
{
  if (!mFirst)
    return;

  nsPluginInstanceTag * next = nsnull;

  for (nsPluginInstanceTag * p = mFirst; p != nsnull;) {
    next = p->mNext;

    if (p->mStopped)
      remove(p);

    p = next;
  }
  return;
}

nsPluginInstanceTag * nsPluginInstanceTagList::find(nsIPluginInstance* instance)
{
  for (nsPluginInstanceTag * p = mFirst; p != nsnull; p = p->mNext) {
    if (p->mInstance == instance) {
#ifdef NS_DEBUG
      PRBool doCache = PR_TRUE;
      p->mInstance->GetValue(nsPluginInstanceVariable_DoCacheBool, (void *) &doCache);
      NS_ASSERTION(!p->mStopped || doCache, "This plugin is not supposed to be cached!");
#endif
      return p;
    }
  }
  return nsnull;
}

nsPluginInstanceTag * nsPluginInstanceTagList::find(const char * mimetype)
{
  PRBool defaultplugin = (PL_strcmp(mimetype, "*") == 0);

  for (nsPluginInstanceTag * p = mFirst; p != nsnull; p = p->mNext) {
    // give it some special treatment for the default plugin first
    // because we cannot tell the default plugin by asking instance for a mime type
    if (defaultplugin && p->mDefaultPlugin)
      return p;

    if (!p->mInstance)
      continue;

    const char* mt;
    nsresult rv = p->mInstance->GetMIMEType(&mt);
    if (NS_FAILED(rv))
      continue;

    if (PL_strcasecmp(mt, mimetype) == 0) {
#ifdef NS_DEBUG
      PRBool doCache = PR_TRUE;
      p->mInstance->GetValue(nsPluginInstanceVariable_DoCacheBool, (void *) &doCache);
      NS_ASSERTION(!p->mStopped || doCache, "This plugin is not supposed to be cached!");
#endif
      return p;
    }
  }
  return nsnull;
}

nsPluginInstanceTag * nsPluginInstanceTagList::findStopped(const char * url)
{
  for (nsPluginInstanceTag * p = mFirst; p != nsnull; p = p->mNext) {
    if (!PL_strcmp(url, p->mURL) && p->mStopped) {
#ifdef NS_DEBUG
      PRBool doCache = PR_TRUE;
      p->mInstance->GetValue(nsPluginInstanceVariable_DoCacheBool, (void *) &doCache);
      NS_ASSERTION(doCache, "This plugin is not supposed to be cached!");
#endif
       return p;
    }
  }
  return nsnull;
}

PRUint32 nsPluginInstanceTagList::getStoppedCount()
{
  PRUint32 stoppedCount = 0;
  for (nsPluginInstanceTag * p = mFirst; p != nsnull; p = p->mNext) {
    if (p->mStopped)
      stoppedCount++;
  }
  return stoppedCount;
}

nsPluginInstanceTag * nsPluginInstanceTagList::findOldestStopped()
{
  nsPluginInstanceTag * res = nsnull;
  PRInt64 llTime = LL_MAXINT;
  for (nsPluginInstanceTag * p = mFirst; p != nsnull; p = p->mNext) {
    if (!p->mStopped)
      continue;

    if (LL_CMP(p->mllStopTime, <, llTime)) {
      llTime = p->mllStopTime;
      res = p;
    }
  }

#ifdef NS_DEBUG
  if (res) {
    PRBool doCache = PR_TRUE;
    res->mInstance->GetValue(nsPluginInstanceVariable_DoCacheBool, (void *) &doCache);
    NS_ASSERTION(doCache, "This plugin is not supposed to be cached!");
  }
#endif

  return res;
}

inline char* new_str(const char* str)
{
  if (str == nsnull)
    return nsnull;

  char* result = new char[strlen(str) + 1];
  if (result != nsnull)
    return strcpy(result, str);
  return result;
}

nsPluginTag::nsPluginTag(nsPluginTag* aPluginTag)
  : mPluginHost(nsnull),
    mName(aPluginTag->mName),
    mDescription(aPluginTag->mDescription),
    mVariants(aPluginTag->mVariants),
    mMimeTypeArray(nsnull),
    mMimeDescriptionArray(aPluginTag->mMimeDescriptionArray),
    mExtensionsArray(nsnull),
    mLibrary(nsnull),
    mCanUnloadLibrary(PR_TRUE),
    mXPConnected(PR_FALSE),
    mIsJavaPlugin(aPluginTag->mIsJavaPlugin),
    mIsNPRuntimeEnabledJavaPlugin(aPluginTag->mIsNPRuntimeEnabledJavaPlugin),
    mFileName(aPluginTag->mFileName),
    mFullPath(aPluginTag->mFullPath),
    mVersion(aPluginTag->mVersion),
    mLastModifiedTime(0),
    mFlags(NS_PLUGIN_FLAG_ENABLED)
{
  if (aPluginTag->mMimeTypeArray != nsnull) {
    mMimeTypeArray = new char*[mVariants];
    for (int i = 0; i < mVariants; i++)
      mMimeTypeArray[i] = new_str(aPluginTag->mMimeTypeArray[i]);
  }

  if (aPluginTag->mExtensionsArray != nsnull) {
    mExtensionsArray = new char*[mVariants];
    for (int i = 0; i < mVariants; i++)
      mExtensionsArray[i] = new_str(aPluginTag->mExtensionsArray[i]);
  }
}

nsPluginTag::nsPluginTag(nsPluginInfo* aPluginInfo)
  : mPluginHost(nsnull),
    mName(aPluginInfo->fName),
    mDescription(aPluginInfo->fDescription),
    mVariants(aPluginInfo->fVariantCount),
    mMimeTypeArray(nsnull),
    mExtensionsArray(nsnull),
    mLibrary(nsnull),
#ifdef XP_MACOSX
    mCanUnloadLibrary(!aPluginInfo->fBundle),
#else
    mCanUnloadLibrary(PR_TRUE),
#endif
    mXPConnected(PR_FALSE),
    mIsJavaPlugin(PR_FALSE),
    mIsNPRuntimeEnabledJavaPlugin(PR_FALSE),
    mFileName(aPluginInfo->fFileName),
    mFullPath(aPluginInfo->fFullPath),
    mVersion(aPluginInfo->fVersion),
    mLastModifiedTime(0),
    mFlags(NS_PLUGIN_FLAG_ENABLED)
{
  if (aPluginInfo->fMimeTypeArray != nsnull) {
    mMimeTypeArray = new char*[mVariants];
    for (int i = 0; i < mVariants; i++) {
      if (mIsJavaPlugin && aPluginInfo->fMimeTypeArray[i] &&
          strcmp(aPluginInfo->fMimeTypeArray[i],
                 "application/x-java-vm-npruntime") == 0) {
        mIsNPRuntimeEnabledJavaPlugin = PR_TRUE;

        // Stop processing here, any mimetypes after the magic "I'm a
        // NPRuntime enabled Java plugin" mimetype will be ignored.
        mVariants = i;

        break;
      }

      mMimeTypeArray[i] = new_str(aPluginInfo->fMimeTypeArray[i]);
      if (nsPluginHost::IsJavaMIMEType(mMimeTypeArray[i]))
        mIsJavaPlugin = PR_TRUE;
    }
  }

  if (aPluginInfo->fMimeDescriptionArray != nsnull) {
    for (int i = 0; i < mVariants; i++) {
      // we should cut off the list of suffixes which the mime
      // description string may have, see bug 53895
      // it is usually in form "some description (*.sf1, *.sf2)"
      // so we can search for the opening round bracket
      char cur = '\0';
      char pre = '\0';
      char * p = PL_strrchr(aPluginInfo->fMimeDescriptionArray[i], '(');
      if (p && (p != aPluginInfo->fMimeDescriptionArray[i])) {
        if ((p - 1) && *(p - 1) == ' ') {
          pre = *(p - 1);
          *(p - 1) = '\0';
        } else {
          cur = *p;
          *p = '\0';
        }

      }
      mMimeDescriptionArray.AppendElement(
        aPluginInfo->fMimeDescriptionArray[i]);
      // restore the original string
      if (cur != '\0')
        *p = cur;
      if (pre != '\0')
        *(p - 1) = pre;
    }
  } else {
    mMimeDescriptionArray.SetLength(mVariants);
  }

  if (aPluginInfo->fExtensionArray != nsnull) {
    mExtensionsArray = new char*[mVariants];
    for (int i = 0; i < mVariants; i++)
      mExtensionsArray[i] = new_str(aPluginInfo->fExtensionArray[i]);
  }

  EnsureMembersAreUTF8();
}

nsPluginTag::nsPluginTag(const char* aName,
                         const char* aDescription,
                         const char* aFileName,
                         const char* aFullPath,
                         const char* aVersion,
                         const char* const* aMimeTypes,
                         const char* const* aMimeDescriptions,
                         const char* const* aExtensions,
                         PRInt32 aVariants,
                         PRInt64 aLastModifiedTime,
                         PRBool aCanUnload,
                         PRBool aArgsAreUTF8)
  : mPluginHost(nsnull),
    mName(aName),
    mDescription(aDescription),
    mVariants(aVariants),
    mMimeTypeArray(nsnull),
    mExtensionsArray(nsnull),
    mLibrary(nsnull),
    mCanUnloadLibrary(aCanUnload),
    mXPConnected(PR_FALSE),
    mIsJavaPlugin(PR_FALSE),
    mIsNPRuntimeEnabledJavaPlugin(PR_FALSE),
    mFileName(aFileName),
    mFullPath(aFullPath),
    mVersion(aVersion),
    mLastModifiedTime(aLastModifiedTime),
    mFlags(0) // Caller will read in our flags from cache
{
  if (aVariants) {
    mMimeTypeArray        = new char*[mVariants];
    mExtensionsArray      = new char*[mVariants];

    for (PRInt32 i = 0; i < aVariants; ++i) {
      if (mIsJavaPlugin && aMimeTypes[i] &&
          strcmp(aMimeTypes[i], "application/x-java-vm-npruntime") == 0) {
        mIsNPRuntimeEnabledJavaPlugin = PR_TRUE;

        // Stop processing here, any mimetypes after the magic "I'm a
        // NPRuntime enabled Java plugin" mimetype will be ignored.
        mVariants = i;

        break;
      }

      mMimeTypeArray[i]        = new_str(aMimeTypes[i]);
      mMimeDescriptionArray.AppendElement(aMimeDescriptions[i]);
      mExtensionsArray[i]      = new_str(aExtensions[i]);
      if (nsPluginHost::IsJavaMIMEType(mMimeTypeArray[i]))
        mIsJavaPlugin = PR_TRUE;
    }
  }

  if (!aArgsAreUTF8)
    EnsureMembersAreUTF8();
}

nsPluginTag::~nsPluginTag()
{
  if (mMimeTypeArray) {
    for (int i = 0; i < mVariants; i++)
      delete[] mMimeTypeArray[i];

    delete[] (mMimeTypeArray);
    mMimeTypeArray = nsnull;
  }

  if (mExtensionsArray) {
    for (int i = 0; i < mVariants; i++)
      delete[] mExtensionsArray[i];

    delete[] (mExtensionsArray);
    mExtensionsArray = nsnull;
  }
}

NS_IMPL_ISUPPORTS1(nsPluginTag, nsIPluginTag)

static nsresult ConvertToUTF8(nsIUnicodeDecoder *aUnicodeDecoder,
                              nsAFlatCString& aString)
{
  PRInt32 numberOfBytes = aString.Length();
  PRInt32 outUnicodeLen;
  nsAutoString buffer;
  nsresult rv = aUnicodeDecoder->GetMaxLength(aString.get(), numberOfBytes,
                                              &outUnicodeLen);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!EnsureStringLength(buffer, outUnicodeLen))
    return NS_ERROR_OUT_OF_MEMORY;
  rv = aUnicodeDecoder->Convert(aString.get(), &numberOfBytes,
                                buffer.BeginWriting(), &outUnicodeLen);
  NS_ENSURE_SUCCESS(rv, rv);
  buffer.SetLength(outUnicodeLen);
  CopyUTF16toUTF8(buffer, aString);

  return NS_OK;
}

nsresult nsPluginTag::EnsureMembersAreUTF8()
{
#ifdef XP_WIN
  return NS_OK;
#else
  nsresult rv;

  nsCOMPtr<nsIPlatformCharset> pcs =
    do_GetService(NS_PLATFORMCHARSET_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIUnicodeDecoder> decoder;
  nsCOMPtr<nsICharsetConverterManager> ccm =
    do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString charset;
  rv = pcs->GetCharset(kPlatformCharsetSel_FileName, charset);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!charset.LowerCaseEqualsLiteral("utf-8")) {
    rv = ccm->GetUnicodeDecoderRaw(charset.get(), getter_AddRefs(decoder));
    NS_ENSURE_SUCCESS(rv, rv);

    ConvertToUTF8(decoder, mFileName);
    ConvertToUTF8(decoder, mFullPath);
  }

  // The description of the plug-in and the various MIME type descriptions
  // should be encoded in the standard plain text file encoding for this system.
  // XXX should we add kPlatformCharsetSel_PluginResource?
  rv = pcs->GetCharset(kPlatformCharsetSel_PlainTextInFile, charset);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!charset.LowerCaseEqualsLiteral("utf-8")) {
    rv = ccm->GetUnicodeDecoderRaw(charset.get(), getter_AddRefs(decoder));
    NS_ENSURE_SUCCESS(rv, rv);

    ConvertToUTF8(decoder, mName);
    ConvertToUTF8(decoder, mDescription);
    for (PRUint32 i = 0; i < mMimeDescriptionArray.Length(); ++i) {
      ConvertToUTF8(decoder, mMimeDescriptionArray[i]);
    }
  }
  return NS_OK;
#endif
}

void nsPluginTag::SetHost(nsPluginHost * aHost)
{
  mPluginHost = aHost;
}

NS_IMETHODIMP
nsPluginTag::GetDescription(nsACString& aDescription)
{
  aDescription = mDescription;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginTag::GetFilename(nsACString& aFileName)
{
  aFileName = mFileName;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginTag::GetVersion(nsACString& aVersion)
{
  aVersion = mVersion;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginTag::GetName(nsACString& aName)
{
  aName = mName;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginTag::GetDisabled(PRBool* aDisabled)
{
  *aDisabled = !HasFlag(NS_PLUGIN_FLAG_ENABLED);
  return NS_OK;
}

NS_IMETHODIMP
nsPluginTag::SetDisabled(PRBool aDisabled)
{
  if (HasFlag(NS_PLUGIN_FLAG_ENABLED) == !aDisabled)
    return NS_OK;

  if (aDisabled)
    UnMark(NS_PLUGIN_FLAG_ENABLED);
  else
    Mark(NS_PLUGIN_FLAG_ENABLED);

  mPluginHost->UpdatePluginInfo(this);
  return NS_OK;
}

NS_IMETHODIMP
nsPluginTag::GetBlocklisted(PRBool* aBlocklisted)
{
  *aBlocklisted = HasFlag(NS_PLUGIN_FLAG_BLOCKLISTED);
  return NS_OK;
}

NS_IMETHODIMP
nsPluginTag::SetBlocklisted(PRBool aBlocklisted)
{
  if (HasFlag(NS_PLUGIN_FLAG_BLOCKLISTED) == aBlocklisted)
    return NS_OK;

  if (aBlocklisted)
    Mark(NS_PLUGIN_FLAG_BLOCKLISTED);
  else
    UnMark(NS_PLUGIN_FLAG_BLOCKLISTED);

  mPluginHost->UpdatePluginInfo(nsnull);
  return NS_OK;
}

// helper struct for asynchronous handling of plugin unloading
class nsPluginUnloadEvent : public nsRunnable {
public:
  nsPluginUnloadEvent(PRLibrary* aLibrary)
    : mLibrary(aLibrary)
  {}
 
  NS_DECL_NSIRUNNABLE
 
  PRLibrary* mLibrary;
};

NS_IMETHODIMP nsPluginUnloadEvent::Run()
{
  if (mLibrary) {
    // put our unload call in a safety wrapper
    NS_TRY_SAFE_CALL_VOID(PR_UnloadLibrary(mLibrary), nsnull, nsnull);
  } else {
    NS_WARNING("missing library from nsPluginUnloadEvent");
  }
  return NS_OK;
}

// unload plugin asynchronously if possible, otherwise just unload now
nsresult PostPluginUnloadEvent(PRLibrary* aLibrary)
{
  nsCOMPtr<nsIRunnable> ev = new nsPluginUnloadEvent(aLibrary);
  if (ev && NS_SUCCEEDED(NS_DispatchToCurrentThread(ev)))
    return NS_OK;

  // failure case
  NS_TRY_SAFE_CALL_VOID(PR_UnloadLibrary(aLibrary), nsnull, nsnull);

  return NS_ERROR_FAILURE;
}

void nsPluginTag::TryUnloadPlugin(PRBool aForceShutdown)
{
#ifdef OJI
  // Don't unload any XPCOM plugins.  They don't get a call to
  // nsIPlugin::Shutdown if plugins are reloaded.
  PRBool isXPCOM = PR_FALSE;
  if (!(mFlags & NS_PLUGIN_FLAG_NPAPI))
    isXPCOM = PR_TRUE;

  if (isXPCOM && !aForceShutdown) return;
#endif

  if (mEntryPoint) {
    mEntryPoint->Shutdown();
    mEntryPoint = nsnull;
  }

  // before we unload check if we are allowed to, see bug #61388
  if (mLibrary && mCanUnloadLibrary) {
    // NPAPI plugins can be unloaded now if they don't use XPConnect
    if (!mXPConnected) {
      // unload the plugin asynchronously by posting a PLEvent
      PostPluginUnloadEvent(mLibrary);
    }
    else {
      // add library to the unused library list to handle it later
      if (mPluginHost)
        mPluginHost->AddUnusedLibrary(mLibrary);
    }
  }

  // we should zero it anyway, it is going to be unloaded by
  // CleanUnsedLibraries before we need to call the library
  // again so the calling code should not be fooled and reload
  // the library fresh
  mLibrary = nsnull;

  // Remove mime types added to the category manager
  // only if we were made 'active' by setting the host
  if (mPluginHost) {
    RegisterWithCategoryManager(PR_FALSE, nsPluginTag::ePluginUnregister);
  }
}

void nsPluginTag::Mark(PRUint32 mask)
{
  PRBool wasEnabled = IsEnabled();
  mFlags |= mask;
  // Update entries in the category manager if necessary.
  if (mPluginHost && wasEnabled != IsEnabled()) {
    if (wasEnabled)
      RegisterWithCategoryManager(PR_FALSE, nsPluginTag::ePluginUnregister);
    else
      RegisterWithCategoryManager(PR_FALSE, nsPluginTag::ePluginRegister);
  }
}

void nsPluginTag::UnMark(PRUint32 mask)
{
  PRBool wasEnabled = IsEnabled();
  mFlags &= ~mask;
  // Update entries in the category manager if necessary.
  if (mPluginHost && wasEnabled != IsEnabled()) {
    if (wasEnabled)
      RegisterWithCategoryManager(PR_FALSE, nsPluginTag::ePluginUnregister);
    else
      RegisterWithCategoryManager(PR_FALSE, nsPluginTag::ePluginRegister);
  }
}

PRBool nsPluginTag::HasFlag(PRUint32 flag)
{
  return (mFlags & flag) != 0;
}

PRUint32 nsPluginTag::Flags()
{
  return mFlags;
}

PRBool nsPluginTag::IsEnabled()
{
  return HasFlag(NS_PLUGIN_FLAG_ENABLED) && !HasFlag(NS_PLUGIN_FLAG_BLOCKLISTED);
}

PRBool nsPluginTag::Equals(nsPluginTag *aPluginTag)
{
  NS_ENSURE_TRUE(aPluginTag, PR_FALSE);

  if ((!mName.Equals(aPluginTag->mName)) ||
      (!mDescription.Equals(aPluginTag->mDescription)) ||
      (mVariants != aPluginTag->mVariants))
    return PR_FALSE;

  if (mVariants && mMimeTypeArray && aPluginTag->mMimeTypeArray) {
    for (PRInt32 i = 0; i < mVariants; i++) {
      if (PL_strcmp(mMimeTypeArray[i], aPluginTag->mMimeTypeArray[i]) != 0)
        return PR_FALSE;
    }
  }
  return PR_TRUE;
}

/**
 * When a plugin requests opens multiple requests to the same URL and
 * the request must be satified by saving a file to disk, each stream
 * listener holds a reference to the backing file: the file is only removed
 * when all the listeners are done.
 */
class CachedFileHolder
{
public:
  CachedFileHolder(nsIFile* cacheFile);
  ~CachedFileHolder();

  void AddRef();
  void Release();

  nsIFile* file() const { return mFile; }

private:
  nsAutoRefCnt mRefCnt;
  nsCOMPtr<nsIFile> mFile;
};

class nsPluginStreamListenerPeer : public nsIStreamListener,
                                   public nsIProgressEventSink,
                                   public nsIHttpHeaderVisitor,
                                   public nsSupportsWeakReference,
                                   public nsINPAPIPluginStreamInfo,
                                   public nsIInterfaceRequestor,
                                   public nsIChannelEventSink
{
public:
  nsPluginStreamListenerPeer();
  virtual ~nsPluginStreamListenerPeer();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIPROGRESSEVENTSINK
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIHTTPHEADERVISITOR
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSICHANNELEVENTSINK

  // nsINPAPIPluginStreamInfo interface
  NS_DECL_NSIPLUGINSTREAMINFO

  // Called by RequestRead
  void
  MakeByteRangeString(nsByteRange* aRangeList, nsACString &string, PRInt32 *numRequests);

  PRBool UseExistingPluginCacheFile(nsPluginStreamListenerPeer* psi);

  // Called by GetURL and PostURL (via NewStream)
  nsresult Initialize(nsIURI *aURL,
                      nsIPluginInstance *aInstance,
                      nsIPluginStreamListener *aListener,
                      PRInt32 requestCount = 1);

  nsresult InitializeEmbedded(nsIURI *aURL,
                             nsIPluginInstance* aInstance,
                             nsIPluginInstanceOwner *aOwner = nsnull,
                             nsPluginHost *aHost = nsnull);

  nsresult InitializeFullPage(nsIURI* aURL, nsIPluginInstance *aInstance);

  nsresult OnFileAvailable(nsIFile* aFile);

  nsresult ServeStreamAsFile(nsIRequest *request, nsISupports *ctxt);
  
  nsIPluginInstance *GetPluginInstance() { return mInstance; }

private:
  nsresult SetUpCache(nsIURI* aURL); // todo: see about removing this...
  nsresult SetUpStreamListener(nsIRequest* request, nsIURI* aURL);
  nsresult SetupPluginCacheFile(nsIChannel* channel);
  nsresult GetInterfaceGlobal(const nsIID& aIID, void** result);

  nsCOMPtr<nsIURI> mURL;
  nsCString mURLSpec; // Have to keep this member because GetURL hands out char*
  nsCOMPtr<nsIPluginInstanceOwner> mOwner;
  nsCOMPtr<nsIPluginInstance> mInstance;
  nsCOMPtr<nsIPluginStreamListener> mPStreamListener;

  // Set to PR_TRUE if we request failed (like with a HTTP response of 404)
  PRPackedBool            mRequestFailed;

  /*
   * Set to PR_TRUE after nsIPluginStreamListener::OnStartBinding() has
   * been called.  Checked in ::OnStopRequest so we can call the
   * plugin's OnStartBinding if, for some reason, it has not already
   * been called.
   */
  PRPackedBool      mStartBinding;
  PRPackedBool      mHaveFiredOnStartRequest;
  // these get passed to the plugin stream listener
  PRUint32                mLength;
  nsPluginStreamType      mStreamType;
  nsPluginHost            *mHost;

  // local cached file, we save the content into local cache if browser cache is not available,
  // or plugin asks stream as file and it expects file extension until bug 90558 got fixed
  nsRefPtr<CachedFileHolder> mLocalCachedFileHolder;
  nsCOMPtr<nsIOutputStream> mFileCacheOutputStream;
  nsHashtable             *mDataForwardToRequest;

  nsCString mContentType;
  PRBool mSeekable;
  PRUint32 mModified;
  nsCOMPtr<nsIPluginInstance> mPluginInstance;
  PRInt32 mStreamOffset;
  PRBool mStreamComplete;

public:
  PRBool                  mAbort;
  PRInt32                 mPendingRequests;
  nsWeakPtr               mWeakPtrChannelCallbacks;
  nsWeakPtr               mWeakPtrChannelLoadGroup;
};

class nsPluginByteRangeStreamListener : public nsIStreamListener {
public:
  nsPluginByteRangeStreamListener(nsIWeakReference* aWeakPtr);
  virtual ~nsPluginByteRangeStreamListener();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

private:
  nsCOMPtr<nsIStreamListener> mStreamConverter;
  nsWeakPtr mWeakPtrPluginStreamListenerPeer;
  PRBool mRemoveMagicNumber;
};

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetContentType(char** result)
{
  *result = const_cast<char*>(mContentType.get());
  return NS_OK;
}

CachedFileHolder::CachedFileHolder(nsIFile* cacheFile)
  : mFile(cacheFile)
{
  NS_ASSERTION(mFile, "Empty CachedFileHolder");
}

CachedFileHolder::~CachedFileHolder()
{
  mFile->Remove(PR_FALSE);
}

void
CachedFileHolder::AddRef()
{
  ++mRefCnt;
  NS_LOG_ADDREF(this, mRefCnt, "CachedFileHolder", sizeof(*this));
}

void
CachedFileHolder::Release()
{
  --mRefCnt;
  NS_LOG_RELEASE(this, mRefCnt, "CachedFileHolder");
  if (0 == mRefCnt)
    delete this;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::IsSeekable(PRBool* result)
{
  *result = mSeekable;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetLength(PRUint32* result)
{
  *result = mLength;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetLastModified(PRUint32* result)
{
  *result = mModified;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetURL(const char** result)
{
  *result = mURLSpec.get();
  return NS_OK;
}

void
nsPluginStreamListenerPeer::MakeByteRangeString(nsByteRange* aRangeList, nsACString &rangeRequest,
                                                PRInt32 *numRequests)
{
  rangeRequest.Truncate();
  *numRequests  = 0;
  //the string should look like this: bytes=500-700,601-999
  if (!aRangeList)
    return;

  PRInt32 requestCnt = 0;
  nsCAutoString string("bytes=");

  for (nsByteRange * range = aRangeList; range != nsnull; range = range->next) {
    // XXX zero length?
    if (!range->length)
      continue;

    // XXX needs to be fixed for negative offsets
    string.AppendInt(range->offset);
    string.Append("-");
    string.AppendInt(range->offset + range->length - 1);
    if (range->next)
      string += ",";

    requestCnt++;
  }

  // get rid of possible trailing comma
  string.Trim(",", PR_FALSE);

  rangeRequest = string;
  *numRequests  = requestCnt;
  return;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::RequestRead(nsByteRange* rangeList)
{
  nsCAutoString rangeString;
  PRInt32 numRequests;

  MakeByteRangeString(rangeList, rangeString, &numRequests);

  if (numRequests == 0)
    return NS_ERROR_FAILURE;

  nsresult rv = NS_OK;

  nsCOMPtr<nsIInterfaceRequestor> callbacks = do_QueryReferent(mWeakPtrChannelCallbacks);
  nsCOMPtr<nsILoadGroup> loadGroup = do_QueryReferent(mWeakPtrChannelLoadGroup);
  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannel(getter_AddRefs(channel), mURL, nsnull, loadGroup, callbacks);
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel));
  if (!httpChannel)
    return NS_ERROR_FAILURE;

  httpChannel->SetRequestHeader(NS_LITERAL_CSTRING("Range"), rangeString, PR_FALSE);

  mAbort = PR_TRUE; // instruct old stream listener to cancel
                    // the request on the next ODA.

  nsCOMPtr<nsIStreamListener> converter;

  if (numRequests == 1) {
    converter = this;

    // set current stream offset equal to the first offset in the range list
    // it will work for single byte range request
    // for multy range we'll reset it in ODA
    SetStreamOffset(rangeList->offset);
  } else {
    nsWeakPtr weakpeer =
      do_GetWeakReference(static_cast<nsISupportsWeakReference*>(this));
    nsPluginByteRangeStreamListener *brrListener =
      new nsPluginByteRangeStreamListener(weakpeer);
    if (brrListener)
      converter = brrListener;
    else
      return NS_ERROR_OUT_OF_MEMORY;
  }

  mPendingRequests += numRequests;

  nsCOMPtr<nsISupportsPRUint32> container = do_CreateInstance(NS_SUPPORTS_PRUINT32_CONTRACTID, &rv);
  if (NS_FAILED(rv))
    return rv;
  rv = container->SetData(MAGIC_REQUEST_CONTEXT);
  if (NS_FAILED(rv))
    return rv;

  return channel->AsyncOpen(converter, container);
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetStreamOffset(PRInt32 *result)
{
  *result = mStreamOffset;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::SetStreamOffset(PRInt32 value)
{
  mStreamOffset = value;
  return NS_OK;
}

class nsPluginCacheListener : public nsIStreamListener
{
public:
  nsPluginCacheListener(nsPluginStreamListenerPeer* aListener);
  virtual ~nsPluginCacheListener();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

private:
  nsPluginStreamListenerPeer* mListener;
};

nsPluginCacheListener::nsPluginCacheListener(nsPluginStreamListenerPeer* aListener)
{
  mListener = aListener;
  NS_ADDREF(mListener);
}

nsPluginCacheListener::~nsPluginCacheListener()
{
  NS_IF_RELEASE(mListener);
}

NS_IMPL_ISUPPORTS1(nsPluginCacheListener, nsIStreamListener)

NS_IMETHODIMP
nsPluginCacheListener::OnStartRequest(nsIRequest *request, nsISupports* ctxt)
{
  return NS_OK;
}

NS_IMETHODIMP
nsPluginCacheListener::OnDataAvailable(nsIRequest *request, nsISupports* ctxt,
                                       nsIInputStream* aIStream,
                                       PRUint32 sourceOffset,
                                       PRUint32 aLength)
{

  PRUint32 readlen;
  char* buffer = (char*) PR_Malloc(aLength);

  // if we don't read from the stream, OnStopRequest will never be called
  if (!buffer)
    return NS_ERROR_OUT_OF_MEMORY;

  nsresult rv = aIStream->Read(buffer, aLength, &readlen);

  NS_ASSERTION(aLength == readlen, "nsCacheListener->OnDataAvailable: "
               "readlen != aLength");

  PR_Free(buffer);
  return rv;
}

NS_IMETHODIMP
nsPluginCacheListener::OnStopRequest(nsIRequest *request,
                                     nsISupports* aContext,
                                     nsresult aStatus)
{
  return NS_OK;
}

nsPluginStreamListenerPeer::nsPluginStreamListenerPeer()
{
  mHost = nsnull;
  mStreamType = nsPluginStreamType_Normal;
  mStartBinding = PR_FALSE;
  mAbort = PR_FALSE;
  mRequestFailed = PR_FALSE;

  mPendingRequests = 0;
  mHaveFiredOnStartRequest = PR_FALSE;
  mDataForwardToRequest = nsnull;

  mSeekable = PR_FALSE;
  mModified = 0;
  mStreamOffset = 0;
  mStreamComplete = 0;
}

nsPluginStreamListenerPeer::~nsPluginStreamListenerPeer()
{
#ifdef PLUGIN_LOGGING
  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL,
    ("nsPluginStreamListenerPeer::dtor this=%p, url=%s\n",this, mURLSpec.get()));
#endif

  NS_IF_RELEASE(mHost);

  // close FD of mFileCacheOutputStream if it's still open
  // or we won't be able to remove the cache file
  if (mFileCacheOutputStream)
    mFileCacheOutputStream = nsnull;

  delete mDataForwardToRequest;
}

NS_IMPL_ISUPPORTS8(nsPluginStreamListenerPeer,
                   nsIStreamListener,
                   nsIRequestObserver,
                   nsIHttpHeaderVisitor,
                   nsISupportsWeakReference,
                   nsIPluginStreamInfo,
                   nsINPAPIPluginStreamInfo,
                   nsIInterfaceRequestor,
                   nsIChannelEventSink)

// Called as a result of GetURL and PostURL
nsresult nsPluginStreamListenerPeer::Initialize(nsIURI *aURL,
                                                nsIPluginInstance *aInstance,
                                                nsIPluginStreamListener* aListener,
                                                PRInt32 requestCount)
{
#ifdef PLUGIN_LOGGING
  nsCAutoString urlSpec;
  if (aURL != nsnull) aURL->GetAsciiSpec(urlSpec);

  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL,
        ("nsPluginStreamListenerPeer::Initialize instance=%p, url=%s\n", aInstance, urlSpec.get()));

  PR_LogFlush();
#endif

  mURL = aURL;

  mInstance = aInstance;
  mPStreamListener = aListener;

  mPendingRequests = requestCount;

  mDataForwardToRequest = new nsHashtable(16, PR_FALSE);
  if (!mDataForwardToRequest)
      return NS_ERROR_FAILURE;

  return NS_OK;
}


/* Called by NewEmbeddedPluginStream() - if this is called, we weren't
 * able to load the plugin, so we need to load it later once we figure
 * out the mimetype.  In order to load it later, we need the plugin
 * host and instance owner.
 */
nsresult nsPluginStreamListenerPeer::InitializeEmbedded(nsIURI *aURL,
                                                        nsIPluginInstance* aInstance,
                                                        nsIPluginInstanceOwner *aOwner,
                                                        nsPluginHost *aHost)
{
#ifdef PLUGIN_LOGGING
  nsCAutoString urlSpec;
  aURL->GetSpec(urlSpec);

  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL,
        ("nsPluginStreamListenerPeer::InitializeEmbedded url=%s\n", urlSpec.get()));

  PR_LogFlush();
#endif

  mURL = aURL;

  if (aInstance) {
    NS_ASSERTION(mInstance == nsnull, "nsPluginStreamListenerPeer::InitializeEmbedded mInstance != nsnull");
    mInstance = aInstance;

  } else {
    mOwner = aOwner;

    mHost = aHost;
    NS_IF_ADDREF(mHost);
  }

  mPendingRequests = 1;

  mDataForwardToRequest = new nsHashtable(16, PR_FALSE);
  if (!mDataForwardToRequest)
      return NS_ERROR_FAILURE;

  return NS_OK;
}


// Called by NewFullPagePluginStream()
nsresult nsPluginStreamListenerPeer::InitializeFullPage(nsIURI* aURL, nsIPluginInstance *aInstance)
{
  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("nsPluginStreamListenerPeer::InitializeFullPage instance=%p\n",aInstance));

  NS_ASSERTION(mInstance == nsnull, "nsPluginStreamListenerPeer::InitializeFullPage mInstance != nsnull");
  mInstance = aInstance;

  mURL = aURL;

  mDataForwardToRequest = new nsHashtable(16, PR_FALSE);
  if (!mDataForwardToRequest)
      return NS_ERROR_FAILURE;

  mPendingRequests = 1;

  return NS_OK;
}

// SetupPluginCacheFile is called if we have to save the stream to disk.
// the most likely cause for this is either there is no disk cache available
// or the stream is coming from a https server.
//
// These files will be deleted when the host is destroyed.
//
// TODO? What if we fill up the the dest dir?
nsresult
nsPluginStreamListenerPeer::SetupPluginCacheFile(nsIChannel* channel)
{
  nsresult rv = NS_OK;
  // lets try to reused a file if we already have in the local plugin cache
  // we loop through all of active plugins
  // and call |nsPluginStreamInfo::UseExistingPluginCacheFile()| on opened stream
  // will return RP_TRUE if file exisrs
  // and some conditions are matched, in this case that file will be use
  // in |::OnFileAvailable()| calls w/o rewriting the file again.
  // The file will be deleted in |nsPluginStreamListenerPeer::~nsPluginStreamListenerPeer|
  PRBool useExistingCacheFile = PR_FALSE;
  nsPluginInstanceTag *pActivePlugins = gActivePluginList->mFirst;
  while (pActivePlugins && !useExistingCacheFile) {
    // most recent streams are at the end of list
    for (PRInt32 i = pActivePlugins->mStreams.Count() - 1; i >= 0; --i) {
      nsPluginStreamListenerPeer *lp =
        static_cast<nsPluginStreamListenerPeer*>(pActivePlugins->mStreams[i]);
      if (lp && lp->mLocalCachedFileHolder) {
        useExistingCacheFile = lp->UseExistingPluginCacheFile(this);
        if (useExistingCacheFile) {
          mLocalCachedFileHolder = lp->mLocalCachedFileHolder;
          break;
        }
      }
      if (useExistingCacheFile)
        break;
    }

    pActivePlugins = pActivePlugins->mNext;
  }

  if (!useExistingCacheFile) {
    nsCOMPtr<nsIFile> pluginTmp;
    rv = nsPluginHost::GetPluginTempDir(getter_AddRefs(pluginTmp));
    if (NS_FAILED(rv)) {
      return rv;
    }

    // Get the filename from the channel
    nsCOMPtr<nsIURI> uri;
    rv = channel->GetURI(getter_AddRefs(uri));
    if (NS_FAILED(rv)) return rv;

    nsCOMPtr<nsIURL> url(do_QueryInterface(uri));
    if (!url)
      return NS_ERROR_FAILURE;

    nsCAutoString filename;
    url->GetFileName(filename);
    if (NS_FAILED(rv))
      return rv;

    // Create a file to save our stream into. Should we scramble the name?
    filename.Insert(NS_LITERAL_CSTRING("plugin-"), 0);
    rv = pluginTmp->AppendNative(filename);
    if (NS_FAILED(rv))
      return rv;

    // Yes, make it unique.
    rv = pluginTmp->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
    if (NS_FAILED(rv))
      return rv;

    // create a file output stream to write to...
    nsCOMPtr<nsIOutputStream> outstream;
    rv = NS_NewLocalFileOutputStream(getter_AddRefs(mFileCacheOutputStream), pluginTmp, -1, 00600);
    if (NS_FAILED(rv))
      return rv;

    // save the file.
    
    mLocalCachedFileHolder = new CachedFileHolder(pluginTmp);
  }

  // add this listenerPeer to list of stream peers for this instance
  // it'll delay release of listenerPeer until nsPluginInstanceTag::~nsPluginInstanceTag
  // and the temp file is going to stay alive until then
  pActivePlugins = gActivePluginList->find(mInstance);
  if (pActivePlugins)
    pActivePlugins->mStreams.AppendObject(this);

  return rv;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::OnStartRequest(nsIRequest *request,
                                           nsISupports* aContext)
{
  nsresult  rv = NS_OK;

  if (mHaveFiredOnStartRequest) {
      return NS_OK;
  }

  mHaveFiredOnStartRequest = PR_TRUE;

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  NS_ENSURE_TRUE(channel, NS_ERROR_FAILURE);

  // deal with 404 (Not Found) HTTP response,
  // just return, this causes the request to be ignored.
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel));
  if (httpChannel) {
    PRUint32 responseCode = 0;
    rv = httpChannel->GetResponseStatus(&responseCode);
    if (NS_FAILED(rv)) {
      // NPP_Notify() will be called from OnStopRequest
      // in nsNPAPIPluginStreamListener::CleanUpStream
      // return error will cancel this request
      // ...and we also need to tell the plugin that
      mRequestFailed = PR_TRUE;
      return NS_ERROR_FAILURE;
    }

    if (responseCode > 206) { // not normal
      PRBool bWantsAllNetworkStreams = PR_FALSE;
      mInstance->GetValue(nsPluginInstanceVariable_WantsAllNetworkStreams,
                          (void *)&bWantsAllNetworkStreams);
      if (!bWantsAllNetworkStreams) {
        mRequestFailed = PR_TRUE;
        return NS_ERROR_FAILURE;
      }
    }
  }

  // do a little sanity check to make sure our frame isn't gone
  // by getting the tag type and checking for an error, we can determine if
  // the frame is gone
  if (mOwner) {
    nsCOMPtr<nsIPluginTagInfo> pti = do_QueryInterface(mOwner);
    NS_ENSURE_TRUE(pti, NS_ERROR_FAILURE);
    nsPluginTagType tagType;
    if (NS_FAILED(pti->GetTagType(&tagType)))
      return NS_ERROR_FAILURE;  // something happened to our object frame, so bail!
  }

  // Get the notification callbacks from the channel and save it as
  // week ref we'll use it in nsPluginStreamInfo::RequestRead() when
  // we'll create channel for byte range request.
  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  channel->GetNotificationCallbacks(getter_AddRefs(callbacks));
  if (callbacks)
    mWeakPtrChannelCallbacks = do_GetWeakReference(callbacks);

  nsCOMPtr<nsILoadGroup> loadGroup;
  channel->GetLoadGroup(getter_AddRefs(loadGroup));
  if (loadGroup)
    mWeakPtrChannelLoadGroup = do_GetWeakReference(loadGroup);

  PRInt32 length;
  rv = channel->GetContentLength(&length);

  // it's possible for the server to not send a Content-Length.
  // we should still work in this case.
  if (NS_FAILED(rv) || length == -1) {
    // check out if this is file channel
    nsCOMPtr<nsIFileChannel> fileChannel = do_QueryInterface(channel);
    if (fileChannel) {
      // file does not exist
      mRequestFailed = PR_TRUE;
      return NS_ERROR_FAILURE;
    }
    mLength = 0;
  }
  else {
    mLength = length;
  }

  mRequest = request;

  nsCAutoString aContentType; // XXX but we already got the type above!
  rv = channel->GetContentType(aContentType);
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsIURI> aURL;
  rv = channel->GetURI(getter_AddRefs(aURL));
  if (NS_FAILED(rv))
    return rv;

  aURL->GetSpec(mURLSpec);

  if (!aContentType.IsEmpty())
    mContentType = aContentType;

#ifdef PLUGIN_LOGGING
  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NOISY,
  ("nsPluginStreamListenerPeer::OnStartRequest this=%p request=%p mime=%s, url=%s\n",
  this, request, aContentType.get(), mURLSpec.get()));

  PR_LogFlush();
#endif

  nsPluginWindow    *window = nsnull;

  // if we don't have an nsIPluginInstance (mInstance), it means
  // we weren't able to load a plugin previously because we
  // didn't have the mimetype.  Now that we do (aContentType),
  // we'll try again with SetUpPluginInstance()
  // which is called by InstantiateEmbeddedPlugin()
  // NOTE: we don't want to try again if we didn't get the MIME type this time

  if (!mInstance && mOwner && !aContentType.IsEmpty()) {
    mOwner->GetInstance(getter_AddRefs(mInstance));

    mOwner->GetWindow(window);
    if (!mInstance && mHost && window) {
      // determine if we need to try embedded again. FullPage takes a different code path
      nsPluginMode mode;
      mOwner->GetMode(&mode);
      if (mode == nsPluginMode_Embedded) {
        // Make sure to not allow new streams to be opened here; we've
        // already got a stream for this data; we just need a properly
        // set up plugin instance.
        rv = mHost->DoInstantiateEmbeddedPlugin(aContentType.get(), aURL,
                                                mOwner, PR_FALSE);
      }
      else {
        rv = mHost->SetUpPluginInstance(aContentType.get(), aURL, mOwner);
      }

      if (NS_OK == rv) {
        mOwner->GetInstance(getter_AddRefs(mInstance));
        if (mInstance) {
          mInstance->Start();
          mOwner->CreateWidget();
          // If we've got a native window, the let the plugin know about it.
          if (window->window) {
            nsCOMPtr<nsIPluginInstance> inst = mInstance;
            ((nsPluginNativeWindow*)window)->CallSetWindow(inst);
          }
        }
      }
    }
  }

  // Set up the stream listener...
  rv = SetUpStreamListener(request, aURL);
  if (NS_FAILED(rv)) return rv;

  return rv;
}

NS_IMETHODIMP nsPluginStreamListenerPeer::OnProgress(nsIRequest *request,
                                                     nsISupports* aContext,
                                                     PRUint64 aProgress,
                                                     PRUint64 aProgressMax)
{
  nsresult rv = NS_OK;
  return rv;
}

NS_IMETHODIMP nsPluginStreamListenerPeer::OnStatus(nsIRequest *request,
                                                   nsISupports* aContext,
                                                   nsresult aStatus,
                                                   const PRUnichar* aStatusArg)
{
  return NS_OK;
}

class nsPRUintKey : public nsHashKey {
protected:
  PRUint32 mKey;
public:
  nsPRUintKey(PRUint32 key) : mKey(key) {}

  PRUint32 HashCode() const {
    return mKey;
  }

  PRBool Equals(const nsHashKey *aKey) const {
    return mKey == ((const nsPRUintKey*)aKey)->mKey;
  }
  nsHashKey *Clone() const {
    return new nsPRUintKey(mKey);
  }
  PRUint32 GetValue() { return mKey; }
};

NS_IMETHODIMP nsPluginStreamListenerPeer::OnDataAvailable(nsIRequest *request,
                                                          nsISupports* aContext,
                                                          nsIInputStream *aIStream,
                                                          PRUint32 sourceOffset,
                                                          PRUint32 aLength)
{
  if (mRequestFailed)
    return NS_ERROR_FAILURE;

  if (mAbort) {
      PRUint32 magicNumber = 0;  // set it to something that is not the magic number.
      nsCOMPtr<nsISupportsPRUint32> container = do_QueryInterface(aContext);
      if (container)
        container->GetData(&magicNumber);

      if (magicNumber != MAGIC_REQUEST_CONTEXT) {
        // this is not one of our range requests
        mAbort = PR_FALSE;
        return NS_BINDING_ABORTED;
      }
  }

  nsresult rv = NS_OK;

  if (!mPStreamListener)
    return NS_ERROR_FAILURE;

  mRequest = request;

  const char * url = nsnull;
  GetURL(&url);

  PLUGIN_LOG(PLUGIN_LOG_NOISY,
  ("nsPluginStreamListenerPeer::OnDataAvailable this=%p request=%p, offset=%d, length=%d, url=%s\n",
  this, request, sourceOffset, aLength, url ? url : "no url set"));

  // if the plugin has requested an AsFileOnly stream, then don't
  // call OnDataAvailable
  if (mStreamType != nsPluginStreamType_AsFileOnly) {
    // get the absolute offset of the request, if one exists.
    nsCOMPtr<nsIByteRangeRequest> brr = do_QueryInterface(request);
    if (brr) {
      if (!mDataForwardToRequest)
        return NS_ERROR_FAILURE;

      PRInt64 absoluteOffset64 = LL_ZERO;
      brr->GetStartRange(&absoluteOffset64);

      // XXX handle 64-bit for real
      PRInt32 absoluteOffset = (PRInt32)nsInt64(absoluteOffset64);

      // we need to track how much data we have forwarded to the
      // plugin.

      // FIXME: http://bugzilla.mozilla.org/show_bug.cgi?id=240130
      //
      // Why couldn't this be tracked on the plugin info, and not in a
      // *hash table*?
      nsPRUintKey key(absoluteOffset);
      PRInt32 amtForwardToPlugin =
        NS_PTR_TO_INT32(mDataForwardToRequest->Get(&key));
      mDataForwardToRequest->Put(&key, NS_INT32_TO_PTR(amtForwardToPlugin + aLength));

      SetStreamOffset(absoluteOffset + amtForwardToPlugin);
    }

    nsCOMPtr<nsIInputStream> stream = aIStream;

    // if we are caching the file ourselves to disk, we want to 'tee' off
    // the data as the plugin read from the stream.  We do this by the magic
    // of an input stream tee.

    if (mFileCacheOutputStream) {
        rv = NS_NewInputStreamTee(getter_AddRefs(stream), aIStream, mFileCacheOutputStream);
        if (NS_FAILED(rv))
            return rv;
    }

    rv =  mPStreamListener->OnDataAvailable(this,
                                            stream,
                                            aLength);

    // if a plugin returns an error, the peer must kill the stream
    //   else the stream and PluginStreamListener leak
    if (NS_FAILED(rv))
      request->Cancel(rv);
  }
  else
  {
    // if we don't read from the stream, OnStopRequest will never be called
    char* buffer = new char[aLength];
    PRUint32 amountRead, amountWrote = 0;
    rv = aIStream->Read(buffer, aLength, &amountRead);

    // if we are caching this to disk ourselves, lets write the bytes out.
    if (mFileCacheOutputStream) {
      while (amountWrote < amountRead && NS_SUCCEEDED(rv)) {
        rv = mFileCacheOutputStream->Write(buffer, amountRead, &amountWrote);
      }
    }
    delete [] buffer;
  }
  return rv;
}

NS_IMETHODIMP nsPluginStreamListenerPeer::OnStopRequest(nsIRequest *request,
                                                        nsISupports* aContext,
                                                        nsresult aStatus)
{
  nsresult rv = NS_OK;

  PLUGIN_LOG(PLUGIN_LOG_NOISY,
  ("nsPluginStreamListenerPeer::OnStopRequest this=%p aStatus=%d request=%p\n",
  this, aStatus, request));

  // for ByteRangeRequest we're just updating the mDataForwardToRequest hash and return.
  nsCOMPtr<nsIByteRangeRequest> brr = do_QueryInterface(request);
  if (brr) {
    PRInt64 absoluteOffset64 = LL_ZERO;
    brr->GetStartRange(&absoluteOffset64);
    // XXX support 64-bit offsets
    PRInt32 absoluteOffset = (PRInt32)nsInt64(absoluteOffset64);

    nsPRUintKey key(absoluteOffset);

    // remove the request from our data forwarding count hash.
    mDataForwardToRequest->Remove(&key);


    PLUGIN_LOG(PLUGIN_LOG_NOISY,
    ("                          ::OnStopRequest for ByteRangeRequest Started=%d\n",
    absoluteOffset));
  } else {
    // if this is not byte range request and
    // if we are writting the stream to disk ourselves,
    // close & tear it down here
    mFileCacheOutputStream = nsnull;
  }

  // if we still have pending stuff to do, lets not close the plugin socket.
  if (--mPendingRequests > 0)
      return NS_OK;

  // we keep our connections around...
  nsCOMPtr<nsISupportsPRUint32> container = do_QueryInterface(aContext);
  if (container) {
    PRUint32 magicNumber = 0;  // set it to something that is not the magic number.
    container->GetData(&magicNumber);
    if (magicNumber == MAGIC_REQUEST_CONTEXT) {
      // this is one of our range requests
      return NS_OK;
    }
  }

  if (!mPStreamListener)
      return NS_ERROR_FAILURE;

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  if (!channel)
    return NS_ERROR_FAILURE;
  // Set the content type to ensure we don't pass null to the plugin
  nsCAutoString aContentType;
  rv = channel->GetContentType(aContentType);
  if (NS_FAILED(rv) && !mRequestFailed)
    return rv;

  if (!aContentType.IsEmpty())
    mContentType = aContentType;

  // set error status if stream failed so we notify the plugin
  if (mRequestFailed)
    aStatus = NS_ERROR_FAILURE;

  if (NS_FAILED(aStatus)) {
    // on error status cleanup the stream
    // and return w/o OnFileAvailable()
    mPStreamListener->OnStopBinding(this, aStatus);
    return NS_OK;
  }

  // call OnFileAvailable if plugin requests stream type StreamType_AsFile or StreamType_AsFileOnly
  if (mStreamType >= nsPluginStreamType_AsFile) {
    nsCOMPtr<nsIFile> localFile;
    if (mLocalCachedFileHolder)
      localFile = mLocalCachedFileHolder->file();
    else {
      nsCOMPtr<nsICachingChannel> cacheChannel = do_QueryInterface(request);
      if (cacheChannel) {
        cacheChannel->GetCacheFile(getter_AddRefs(localFile));
      } else {
        // see if it is a file channel.
        nsCOMPtr<nsIFileChannel> fileChannel = do_QueryInterface(request);
        if (fileChannel) {
          fileChannel->GetFile(getter_AddRefs(localFile));
        }
      }
    }

    if (localFile) {
      OnFileAvailable(localFile);
    }
  }

  if (mStartBinding) {
    // On start binding has been called
    mPStreamListener->OnStopBinding(this, aStatus);
  } else {
    // OnStartBinding hasn't been called, so complete the action.
    mPStreamListener->OnStartBinding(this);
    mPStreamListener->OnStopBinding(this, aStatus);
  }

  if (NS_SUCCEEDED(aStatus)) {
    mStreamComplete = PR_TRUE;
  }
  mRequest = NULL;

  return NS_OK;
}

// private methods for nsPluginStreamListenerPeer
nsresult nsPluginStreamListenerPeer::SetUpCache(nsIURI* aURL)
{
  nsPluginCacheListener* cacheListener = new nsPluginCacheListener(this);
  // XXX: Null LoadGroup?
  return NS_OpenURI(cacheListener, nsnull, aURL, nsnull);
}

nsresult nsPluginStreamListenerPeer::SetUpStreamListener(nsIRequest *request,
                                                         nsIURI* aURL)
{
  nsresult rv = NS_OK;

  // If we don't yet have a stream listener, we need to get
  // one from the plugin.
  // NOTE: this should only happen when a stream was NOT created
  // with GetURL or PostURL (i.e. it's the initial stream we
  // send to the plugin as determined by the SRC or DATA attribute)
  if (!mPStreamListener && mInstance)
    rv = mInstance->NewStreamToPlugin(getter_AddRefs(mPStreamListener));

  if (NS_FAILED(rv))
    return rv;

  if (!mPStreamListener)
    return NS_ERROR_NULL_POINTER;

  PRBool useLocalCache = PR_FALSE;

  // get httpChannel to retrieve some info we need for nsIPluginStreamInfo setup
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(channel);

  /*
   * Assumption
   * By the time nsPluginStreamListenerPeer::OnDataAvailable() gets
   * called, all the headers have been read.
   */
  if (httpChannel) {
    // Reassemble the HTTP response status line and provide it to our
    // listener.  Would be nice if we could get the raw status line,
    // but nsIHttpChannel doesn't currently provide that.
    nsCOMPtr<nsIHTTPHeaderListener> listener =
      do_QueryInterface(mPStreamListener);
    if (listener) {
      // Status code: required; the status line isn't useful without it.
      PRUint32 statusNum;
      if (NS_SUCCEEDED(httpChannel->GetResponseStatus(&statusNum)) &&
          statusNum < 1000) {
        // HTTP version: provide if available.  Defaults to empty string.
        nsCString ver;
        nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal =
          do_QueryInterface(channel);
        if (httpChannelInternal) {
          PRUint32 major, minor;
          if (NS_SUCCEEDED(httpChannelInternal->GetResponseVersion(&major,
                                                                   &minor))) {
            ver = nsPrintfCString("/%lu.%lu", major, minor);
          }
        }

        // Status text: provide if available.  Defaults to "OK".
        nsCString statusText;
        if (NS_FAILED(httpChannel->GetResponseStatusText(statusText))) {
          statusText = "OK";
        }

        // Assemble everything and pass to listener.
        nsPrintfCString status(100, "HTTP%s %lu %s", ver.get(), statusNum,
                               statusText.get());
        listener->StatusLine(status.get());
      }
    }

    // Also provide all HTTP response headers to our listener.
    httpChannel->VisitResponseHeaders(this);

    mSeekable = PR_FALSE;
    // first we look for a content-encoding header. If we find one, we tell the
    // plugin that stream is not seekable, because the plugin always sees
    // uncompressed data, so it can't make meaningful range requests on a
    // compressed entity.  Also, we force the plugin to use
    // nsPluginStreamType_AsFile stream type and we have to save decompressed
    // file into local plugin cache, because necko cache contains original
    // compressed file.
    nsCAutoString contentEncoding;
    if (NS_SUCCEEDED(httpChannel->GetResponseHeader(NS_LITERAL_CSTRING("Content-Encoding"),
                                                    contentEncoding))) {
      useLocalCache = PR_TRUE;
    } else {
      // set seekability (seekable if the stream has a known length and if the
      // http server accepts byte ranges).
      PRUint32 length;
      GetLength(&length);
      if (length) {
        nsCAutoString range;
        if (NS_SUCCEEDED(httpChannel->GetResponseHeader(NS_LITERAL_CSTRING("accept-ranges"), range)) &&
          range.Equals(NS_LITERAL_CSTRING("bytes"), nsCaseInsensitiveCStringComparator())) {
          mSeekable = PR_TRUE;
        }
      }
    }

    // we require a content len
    // get Last-Modified header for plugin info
    nsCAutoString lastModified;
    if (NS_SUCCEEDED(httpChannel->GetResponseHeader(NS_LITERAL_CSTRING("last-modified"), lastModified)) &&
        !lastModified.IsEmpty()) {
      PRTime time64;
      PR_ParseTimeString(lastModified.get(), PR_TRUE, &time64);  //convert string time to integer time

      // Convert PRTime to unix-style time_t, i.e. seconds since the epoch
      double fpTime;
      LL_L2D(fpTime, time64);
      mModified = (PRUint32)(fpTime * 1e-6 + 0.5);
    }
  }

  rv = mPStreamListener->OnStartBinding(this);

  mStartBinding = PR_TRUE;

  if (NS_FAILED(rv))
    return rv;

  mPStreamListener->GetStreamType(&mStreamType);

  if (!useLocalCache && mStreamType >= nsPluginStreamType_AsFile) {
    // check it out if this is not a file channel.
    nsCOMPtr<nsIFileChannel> fileChannel = do_QueryInterface(request);
    if (!fileChannel) {
      // and browser cache is not available
      nsCOMPtr<nsICachingChannel> cacheChannel = do_QueryInterface(request);
      if (!(cacheChannel && (NS_SUCCEEDED(cacheChannel->SetCacheAsFile(PR_TRUE))))) {
        useLocalCache = PR_TRUE;
      }
    }
  }

  if (useLocalCache) {
    SetupPluginCacheFile(channel);
  }

  return NS_OK;
}

nsresult
nsPluginStreamListenerPeer::OnFileAvailable(nsIFile* aFile)
{
  nsresult rv;
  if (!mPStreamListener)
    return NS_ERROR_FAILURE;

  nsCAutoString path;
  rv = aFile->GetNativePath(path);
  if (NS_FAILED(rv)) return rv;

  if (path.IsEmpty()) {
    NS_WARNING("empty path");
    return NS_OK;
  }

  rv = mPStreamListener->OnFileAvailable(this, path.get());
  return rv;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::VisitHeader(const nsACString &header, const nsACString &value)
{
  nsCOMPtr<nsIHTTPHeaderListener> listener = do_QueryInterface(mPStreamListener);
  if (!listener)
    return NS_ERROR_FAILURE;

  return listener->NewResponseHeader(PromiseFlatCString(header).get(),
                                     PromiseFlatCString(value).get());
}

nsresult
nsPluginStreamListenerPeer::GetInterfaceGlobal(const nsIID& aIID, void** result)
{
  if (!mInstance) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPluginInstanceOwner> owner;
  mInstance->GetOwner(getter_AddRefs(owner));
  if (owner) {
    nsCOMPtr<nsIDocument> doc;
    nsresult rv = owner->GetDocument(getter_AddRefs(doc));
    if (NS_SUCCEEDED(rv) && doc) {
      nsPIDOMWindow *window = doc->GetWindow();
      if (window) {
        nsCOMPtr<nsIWebNavigation> webNav = do_GetInterface(window);
        nsCOMPtr<nsIInterfaceRequestor> ir = do_QueryInterface(webNav);
        return ir->GetInterface(aIID, result);
      }
    }
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetInterface(const nsIID& aIID, void** result)
{
  // Provide nsIChannelEventSink ourselves, otherwise let our document's
  // window provide the interface.

  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    return QueryInterface(aIID, result);
  }

  return GetInterfaceGlobal(aIID, result);
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::OnChannelRedirect(nsIChannel *oldChannel, nsIChannel *newChannel, PRUint32 flags)
{
  // Don't allow cross-origin 307 POST redirects. Fall back to channel event sink for window.

  nsCOMPtr<nsIHttpChannel> oldHttpChannel(do_QueryInterface(oldChannel));
  if (oldHttpChannel) {
    PRUint32 responseStatus;
    nsresult rv = oldHttpChannel->GetResponseStatus(&responseStatus);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (responseStatus == 307) {
      nsCAutoString method;
      rv = oldHttpChannel->GetRequestMethod(method);
      if (NS_FAILED(rv)) {
        return rv;
      }
      if (method.EqualsLiteral("POST")) {
        nsCOMPtr<nsIContentUtils2> contentUtils2 = do_GetService("@mozilla.org/content/contentutils2;1");
        NS_ENSURE_TRUE(contentUtils2, NS_ERROR_FAILURE);
        rv = contentUtils2->CheckSameOrigin(oldChannel, newChannel);
        if (NS_FAILED(rv)) {
          return rv;
        }
      }
    }
  }

  nsCOMPtr<nsIChannelEventSink> channelEventSink;
  nsresult rv = GetInterfaceGlobal(NS_GET_IID(nsIChannelEventSink), getter_AddRefs(channelEventSink));
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  return channelEventSink->OnChannelRedirect(oldChannel, newChannel, flags);
}

nsPluginHost::nsPluginHost()
{
  mPluginsLoaded = PR_FALSE;
  mDontShowBadPluginMessage = PR_FALSE;
  mIsDestroyed = PR_FALSE;
  mOverrideInternalTypes = PR_FALSE;
  mAllowAlienStarHandler = PR_FALSE;
  mDefaultPluginDisabled = PR_FALSE;

  gActivePluginList = &mPluginInstanceTagList;

  // check to see if pref is set at startup to let plugins take over in
  // full page mode for certain image mime types that we handle internally
  mPrefService = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (mPrefService) {
    PRBool tmp;
    nsresult rv = mPrefService->GetBoolPref("plugin.override_internal_types",
                                            &tmp);
    if (NS_SUCCEEDED(rv)) {
      mOverrideInternalTypes = tmp;
    }

    rv = mPrefService->GetBoolPref("plugin.allow_alien_star_handler", &tmp);
    if (NS_SUCCEEDED(rv)) {
      mAllowAlienStarHandler = tmp;
    }

    rv = mPrefService->GetBoolPref("plugin.default_plugin_disabled", &tmp);
    if (NS_SUCCEEDED(rv)) {
      mDefaultPluginDisabled = tmp;
    }

    rv = mPrefService->GetBoolPref("plugin.disable", &tmp);
    if (NS_SUCCEEDED(rv)) {
      mPluginsDisabled = tmp;
    }

#ifdef WINCE
    mDefaultPluginDisabled = PR_TRUE;
#endif
  }

  nsCOMPtr<nsIObserverService> obsService = do_GetService("@mozilla.org/observer-service;1");
  if (obsService) {
    obsService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, PR_FALSE);
    obsService->AddObserver(this, NS_PRIVATE_BROWSING_SWITCH_TOPIC, PR_FALSE);
  }

#ifdef PLUGIN_LOGGING
  nsPluginLogging::gNPNLog = PR_NewLogModule(NPN_LOG_NAME);
  nsPluginLogging::gNPPLog = PR_NewLogModule(NPP_LOG_NAME);
  nsPluginLogging::gPluginLog = PR_NewLogModule(PLUGIN_LOG_NAME);

  PR_LOG(nsPluginLogging::gNPNLog, PLUGIN_LOG_ALWAYS,("NPN Logging Active!\n"));
  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_ALWAYS,("General Plugin Logging Active! (nsPluginHost::ctor)\n"));
  PR_LOG(nsPluginLogging::gNPPLog, PLUGIN_LOG_ALWAYS,("NPP Logging Active!\n"));

  PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("nsPluginHost::ctor\n"));
  PR_LogFlush();
#endif
  mCachedPlugins = nsnull;
}

nsPluginHost::~nsPluginHost()
{
  PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("nsPluginHost::dtor\n"));

  Destroy();
  sInst = nsnull;
}

#ifdef OJI
NS_IMPL_ISUPPORTS8(nsPluginHost,
                   nsIPluginHost,
                   nsIPluginHostOld,
                   nsIPluginManager,
                   nsIPluginManager2,
                   nsIFileUtilities,
                   nsICookieStorage,
                   nsIObserver,
                   nsISupportsWeakReference)
#else
NS_IMPL_ISUPPORTS3(nsPluginHost,
                   nsIPluginHost,
                   nsIObserver,
                   nsISupportsWeakReference)
#endif

nsPluginHost*
nsPluginHost::GetInst()
{
  if (!sInst) {
    sInst = new nsPluginHost();
    if (!sInst)
      return nsnull;
    NS_ADDREF(sInst);
  }

  NS_ADDREF(sInst);
  return sInst;
}

// static
const char *
nsPluginHost::GetPluginName(nsIPluginInstance *aPluginInstance)
{
  nsPluginInstanceTag *plugin =
    gActivePluginList ? gActivePluginList->find(aPluginInstance) : nsnull;

  if (plugin && plugin->mPluginTag)
    return plugin->mPluginTag->mName.get();

  return nsnull;
}

PRBool nsPluginHost::IsRunningPlugin(nsPluginTag * plugin)
{
  if (!plugin || !plugin->mEntryPoint || !plugin->mLibrary) {
    return PR_FALSE;
  }

  for (nsPluginInstanceTag* ap = mPluginInstanceTagList.mFirst; ap; ap = ap->mNext) {
    if (ap->mPluginTag == plugin && !ap->mStopped) {
      return PR_TRUE;
    }
  }

  return PR_FALSE;
}

nsresult nsPluginHost::ReloadPlugins(PRBool reloadPages)
{
  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("nsPluginHost::ReloadPlugins Begin reloadPages=%d\n",
  reloadPages));

  nsresult rv = NS_OK;

  // this will create the initial plugin list out of cache
  // if it was not created yet
  if (!mPluginsLoaded)
    return LoadPlugins();

  // we are re-scanning plugins. New plugins may have been added, also some
  // plugins may have been removed, so we should probably shut everything down
  // but don't touch running (active and  not stopped) plugins

  // check if plugins changed, no need to do anything else
  // if no changes to plugins have been made
  // PR_FALSE instructs not to touch the plugin list, just to
  // look for possible changes
  PRBool pluginschanged = PR_TRUE;
  FindPlugins(PR_FALSE, &pluginschanged);

  // if no changed detected, return an appropriate error code
  if (!pluginschanged)
    return NS_ERROR_PLUGINS_PLUGINSNOTCHANGED;

  nsCOMPtr<nsISupportsArray> instsToReload;

  if (reloadPages) {
    NS_NewISupportsArray(getter_AddRefs(instsToReload));

    // Then stop any running plugin instances but hold on to the documents in the array
    // We are going to need to restart the instances in these documents later
    mPluginInstanceTagList.stopRunning(instsToReload, nsnull);
  }

  // clean active plugin list
  mPluginInstanceTagList.removeAllStopped();

  // shutdown plugins and kill the list if there are no running plugins
  nsRefPtr<nsPluginTag> prev;
  nsRefPtr<nsPluginTag> next;

  for (nsRefPtr<nsPluginTag> p = mPlugins; p != nsnull;) {
    next = p->mNext;

    // only remove our plugin from the list if it's not running.
#ifdef OJI
    // Don't remove any XPCOM plugins.  They don't get a call to
    // nsIPlugin::Shutdown if plugins are reloaded.
    if (!IsRunningPlugin(p) && (!p->mEntryPoint || p->HasFlag(NS_PLUGIN_FLAG_NPAPI)))
#else
    if (!IsRunningPlugin(p))
#endif
    {
      if (p == mPlugins)
        mPlugins = next;
      else
        prev->mNext = next;

      p->mNext = nsnull;

      // attempt to unload plugins whenever they are removed from the list
      p->TryUnloadPlugin(PR_TRUE);

      p = next;
      continue;
    }

    prev = p;
    p = next;
  }

  // set flags
  mPluginsLoaded = PR_FALSE;

  // load them again
  rv = LoadPlugins();

  // If we have shut down any plugin instances, we've now got to restart them.
  // Post an event to do the rest as we are going to be destroying the frame tree and we also want
  // any posted unload events to finish
  PRUint32 c;
  if (reloadPages &&
      instsToReload &&
      NS_SUCCEEDED(instsToReload->Count(&c)) &&
      c > 0) {
    nsCOMPtr<nsIRunnable> ev = new nsPluginDocReframeEvent(instsToReload);
    if (ev)
      NS_DispatchToCurrentThread(ev);
  }

  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("nsPluginHost::ReloadPlugins End\n"));

  return rv;
}

#define NS_RETURN_UASTRING_SIZE 128

nsresult nsPluginHost::UserAgent(const char **retstring)
{
  static char resultString[NS_RETURN_UASTRING_SIZE];
  nsresult res;

  nsCOMPtr<nsIHttpProtocolHandler> http = do_GetService(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "http", &res);
  if (NS_FAILED(res))
    return res;

  nsCAutoString uaString;
  res = http->GetUserAgent(uaString);

  if (NS_SUCCEEDED(res)) {
    if (NS_RETURN_UASTRING_SIZE > uaString.Length()) {
      PL_strcpy(resultString, uaString.get());
    } else {
      // Copy as much of UA string as we can (terminate at right-most space).
      PL_strncpy(resultString, uaString.get(), NS_RETURN_UASTRING_SIZE);
      for (int i = NS_RETURN_UASTRING_SIZE - 1; i >= 0; i--) {
        if (i == 0) {
          resultString[NS_RETURN_UASTRING_SIZE - 1] = '\0';
        }
        else if (resultString[i] == ' ') {
          resultString[i] = '\0';
          break;
        }
      }
    }
    *retstring = resultString;
  }
  else {
    *retstring = nsnull;
  }

  PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("nsPluginHost::UserAgent return=%s\n", *retstring));

  return res;
}

nsresult nsPluginHost::GetPrompt(nsIPluginInstanceOwner *aOwner, nsIPrompt **aPrompt)
{
  nsresult rv;
  nsCOMPtr<nsIPrompt> prompt;
  nsCOMPtr<nsIWindowWatcher> wwatch = do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);

  if (wwatch) {
    nsCOMPtr<nsIDOMWindow> domWindow;
    if (aOwner) {
      nsCOMPtr<nsIDocument> document;
      aOwner->GetDocument(getter_AddRefs(document));
      if (document) {
        domWindow = document->GetWindow();
      }
    }

    if (!domWindow) {
      wwatch->GetWindowByName(NS_LITERAL_STRING("_content").get(), nsnull, getter_AddRefs(domWindow));
    }
    rv = wwatch->GetNewPrompter(domWindow, getter_AddRefs(prompt));
  }

  NS_IF_ADDREF(*aPrompt = prompt);
  return rv;
}

NS_IMETHODIMP nsPluginHost::GetURL(nsISupports* pluginInst,
                                   const char* url,
                                   const char* target,
                                   nsIPluginStreamListener* streamListener,
                                   const char* altHost,
                                   const char* referrer,
                                   PRBool forceJSEnabled)
{
  return GetURLWithHeaders(pluginInst, url, target, streamListener,
                           altHost, referrer, forceJSEnabled, nsnull, nsnull);
}

nsresult nsPluginHost::GetURLWithHeaders(nsISupports* pluginInst,
                                         const char* url,
                                         const char* target,
                                         nsIPluginStreamListener* streamListener,
                                         const char* altHost,
                                         const char* referrer,
                                         PRBool forceJSEnabled,
                                         PRUint32 getHeadersLength,
                                         const char* getHeaders)
{
  nsAutoString string;
  string.AssignWithConversion(url);

  // we can only send a stream back to the plugin (as specified by a
  // null target) if we also have a nsIPluginStreamListener to talk to
  if (!target && !streamListener)
   return NS_ERROR_ILLEGAL_VALUE;

  nsresult rv;
  nsCOMPtr<nsIPluginInstance> instance = do_QueryInterface(pluginInst, &rv);
#ifdef OJI
  if (NS_FAILED(rv)) {
    nsCOMPtr<nsIPluginInstanceOld> instanceOld(do_QueryInterface(pluginInst));
    rv = NewForOldPluginInstance(instanceOld, getter_AddRefs(instance));
  }
#endif
  if (NS_SUCCEEDED(rv))
    rv = DoURLLoadSecurityCheck(instance, url);

  if (NS_SUCCEEDED(rv)) {
    if (target) {
      nsCOMPtr<nsIPluginInstanceOwner> owner;
      rv = instance->GetOwner(getter_AddRefs(owner));
      if (owner) {
        if ((0 == PL_strcmp(target, "newwindow")) ||
            (0 == PL_strcmp(target, "_new")))
          target = "_blank";
        else if (0 == PL_strcmp(target, "_current"))
          target = "_self";

        rv = owner->GetURL(url, target, nsnull, 0, (void *) getHeaders, getHeadersLength);
      }
    }

    if (streamListener) {
      rv = NewPluginURLStream(string, instance, streamListener, nsnull,
                              PR_FALSE, nsnull, getHeaders, getHeadersLength);
    }
  }

  return rv;
}

NS_IMETHODIMP nsPluginHost::PostURL(nsISupports* pluginInst,
                                    const char* url,
                                    PRUint32 postDataLen,
                                    const char* postData,
                                    PRBool isFile,
                                    const char* target,
                                    nsIPluginStreamListener* streamListener,
                                    const char* altHost,
                                    const char* referrer,
                                    PRBool forceJSEnabled,
                                    PRUint32 postHeadersLength,
                                    const char* postHeaders)
{
  nsAutoString string; string.AssignWithConversion(url);
  nsresult rv;

  // we can only send a stream back to the plugin (as specified
  // by a null target) if we also have a nsIPluginStreamListener
  // to talk to also
  if (!target && !streamListener)
   return NS_ERROR_ILLEGAL_VALUE;

  nsCOMPtr<nsIPluginInstance> instance = do_QueryInterface(pluginInst, &rv);
#ifdef OJI
  if (NS_FAILED(rv)) {
    nsCOMPtr<nsIPluginInstanceOld> instanceOld(do_QueryInterface(pluginInst));
    rv = NewForOldPluginInstance(instanceOld, getter_AddRefs(instance));
  }
#endif
  if (NS_SUCCEEDED(rv))
    rv = DoURLLoadSecurityCheck(instance, url);

  if (NS_SUCCEEDED(rv)) {
    char *dataToPost;
    if (isFile) {
      rv = CreateTmpFileToPost(postData, &dataToPost);
      if (NS_FAILED(rv) || !dataToPost)
        return rv;
    } else {
      PRUint32 newDataToPostLen;
      ParsePostBufferToFixHeaders(postData, postDataLen, &dataToPost, &newDataToPostLen);
      if (!dataToPost)
        return NS_ERROR_UNEXPECTED;

      // we use nsIStringInputStream::adoptDataa()
      // in NS_NewPluginPostDataStream to set the stream
      // all new data alloced in  ParsePostBufferToFixHeaders()
      // well be nsMemory::Free()d on destroy the stream
      postDataLen = newDataToPostLen;
    }

    if (target) {
      nsCOMPtr<nsIPluginInstanceOwner> owner;
      rv = instance->GetOwner(getter_AddRefs(owner));
      if (owner) {
        if (!target) {
          target = "_self";
        } else {
          if ((0 == PL_strcmp(target, "newwindow")) ||
              (0 == PL_strcmp(target, "_new"))) {
            target = "_blank";
          } else if (0 == PL_strcmp(target, "_current")) {
            target = "_self";
          }
        }
        rv = owner->GetURL(url, target, (void*)dataToPost, postDataLen,
                           (void*)postHeaders, postHeadersLength, isFile);
      }
    }

    // if we don't have a target, just create a stream.  This does
    // NS_OpenURI()!
    if (streamListener)
      rv = NewPluginURLStream(string, instance, streamListener,
                              (const char*)dataToPost, isFile, postDataLen,
                              postHeaders, postHeadersLength);
    if (isFile)
      NS_Free(dataToPost);
  }

  return rv;
}

/* This method queries the prefs for proxy information.
 * It has been tested and is known to work in the following three cases
 * when no proxy host or port is specified
 * when only the proxy host is specified
 * when only the proxy port is specified
 * This method conforms to the return code specified in
 * http://developer.netscape.com/docs/manuals/proxy/adminnt/autoconf.htm#1020923
 * with the exception that multiple values are not implemented.
 */

NS_IMETHODIMP nsPluginHost::FindProxyForURL(const char* url, char* *result)
{
  if (!url || !result) {
    return NS_ERROR_INVALID_ARG;
  }
  nsresult res;

  nsCOMPtr<nsIURI> uriIn;
  nsCOMPtr<nsIProtocolProxyService> proxyService;
  nsCOMPtr<nsIIOService> ioService;

  proxyService = do_GetService(NS_PROTOCOLPROXYSERVICE_CONTRACTID, &res);
  if (NS_FAILED(res) || !proxyService)
    return res;

  ioService = do_GetService(NS_IOSERVICE_CONTRACTID, &res);
  if (NS_FAILED(res) || !ioService)
    return res;

  // make an nsURI from the argument url
  res = ioService->NewURI(nsDependentCString(url), nsnull, nsnull, getter_AddRefs(uriIn));
  if (NS_FAILED(res))
    return res;

  nsCOMPtr<nsIProxyInfo> pi;

  res = proxyService->Resolve(uriIn, 0, getter_AddRefs(pi));
  if (NS_FAILED(res))
    return res;

  nsCAutoString host, type;
  PRInt32 port = -1;

  // These won't fail, and even if they do... we'll be ok.
  if (pi) {
    pi->GetType(type);
    pi->GetHost(host);
    pi->GetPort(&port);
  }

  if (!pi || host.IsEmpty() || port <= 0 || host.EqualsLiteral("direct")) {
    *result = PL_strdup("DIRECT");
  } else if (type.EqualsLiteral("http")) {
    *result = PR_smprintf("PROXY %s:%d", host.get(), port);
  } else if (type.EqualsLiteral("socks4")) {
    *result = PR_smprintf("SOCKS %s:%d", host.get(), port);
  } else if (type.EqualsLiteral("socks")) {
    // XXX - this is socks5, but there is no API for us to tell the
    // plugin that fact. SOCKS for now, in case the proxy server
    // speaks SOCKS4 as well. See bug 78176
    // For a long time this was returning an http proxy type, so
    // very little is probably broken by this
    *result = PR_smprintf("SOCKS %s:%d", host.get(), port);
  } else {
    NS_ASSERTION(PR_FALSE, "Unknown proxy type!");
    *result = PL_strdup("DIRECT");
  }

  if (nsnull == *result)
    res = NS_ERROR_OUT_OF_MEMORY;

  return res;
}

NS_IMETHODIMP nsPluginHost::Init()
{
  return NS_OK;
}

NS_IMETHODIMP nsPluginHost::Destroy()
{
  PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("nsPluginHost::Destroy Called\n"));

  if (mIsDestroyed)
    return NS_OK;

  mIsDestroyed = PR_TRUE;

  // we should call nsIPluginInstance::Stop and nsIPluginInstance::SetWindow
  // for those plugins who want it
  mPluginInstanceTagList.stopRunning(nsnull, nsnull);

  // at this point nsIPlugin::Shutdown calls will be performed if needed
  mPluginInstanceTagList.shutdown();

  nsPluginTag *pluginTag;
  for (pluginTag = mPlugins; pluginTag; pluginTag = pluginTag->mNext) {
    pluginTag->TryUnloadPlugin(PR_TRUE);
  }

  while (mPlugins) {
    nsRefPtr<nsPluginTag> temp = mPlugins->mNext;
    // while walking through the list of the plugins see if we still have anything
    // to shutdown some plugins may have never created an instance but still expect
    // the shutdown call see bugzilla bug 73071
    // with current logic, no need to do anything special as nsIPlugin::Shutdown
    // will be performed in the destructor
    mPlugins->mNext = nsnull;
    mPlugins = temp;
  }

  // Delete any remaining cached plugins list
  mCachedPlugins = nsnull;

  // Lets remove any of the temporary files that we created.
  if (sPluginTempDir) {
    sPluginTempDir->Remove(PR_TRUE);
    NS_RELEASE(sPluginTempDir);
  }

#ifdef XP_WIN
  if (mPrivateDirServiceProvider) {
    nsCOMPtr<nsIDirectoryService> dirService =
      do_GetService(kDirectoryServiceContractID);
    if (dirService)
      dirService->UnregisterProvider(mPrivateDirServiceProvider);
    mPrivateDirServiceProvider = nsnull;
  }
#endif /* XP_WIN */

  mPrefService = nsnull; // release prefs service to avoid leaks!

  return NS_OK;
}

void nsPluginHost::UnloadUnusedLibraries()
{
  // unload any remaining plugin libraries from memory
  for (PRUint32 i = 0; i < mUnusedLibraries.Length(); i++) {
    PRLibrary * library = mUnusedLibraries[i];
    if (library)
      PostPluginUnloadEvent(library);
  }
  mUnusedLibraries.Clear();
}

nsresult
nsPluginHost::GetPluginTempDir(nsIFile **aDir)
{
  if (!sPluginTempDir) {
    nsCOMPtr<nsIFile> tmpDir;
    nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR,
                                         getter_AddRefs(tmpDir));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = tmpDir->AppendNative(kPluginTmpDirName);

    // make it unique, and mode == 0700, not world-readable
    rv = tmpDir->CreateUnique(nsIFile::DIRECTORY_TYPE, 0700);
    NS_ENSURE_SUCCESS(rv, rv);

    tmpDir.swap(sPluginTempDir);
  }

  return sPluginTempDir->Clone(aDir);
}

NS_IMETHODIMP nsPluginHost::InstantiatePluginForChannel(nsIChannel* aChannel,
                                                        nsIPluginInstanceOwner* aOwner,
                                                        nsIStreamListener** aListener)
{
  NS_PRECONDITION(aChannel && aOwner,
                  "Invalid arguments to InstantiatePluginForChannel");
  nsCOMPtr<nsIURI> uri;
  nsresult rv = aChannel->GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv))
    return rv;

#ifdef PLUGIN_LOGGING
  if (PR_LOG_TEST(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL)) {
    nsCAutoString urlSpec;
    uri->GetAsciiSpec(urlSpec);

    PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL,
           ("nsPluginHost::InstantiatePluginForChannel Begin owner=%p, url=%s\n",
           aOwner, urlSpec.get()));

    PR_LogFlush();
  }
#endif

  // Note that we're not setting up a plugin instance here; the stream
  // listener's OnStartRequest will handle doing that, looking for
  // stopped plugins, etc, etc.

  return NewEmbeddedPluginStreamListener(uri, aOwner, nsnull, aListener);
}

// Called by nsPluginInstanceOwner (nsObjectFrame.cpp - embedded case)
NS_IMETHODIMP nsPluginHost::InstantiateEmbeddedPlugin(const char *aMimeType,
                                                          nsIURI* aURL,
                                                          nsIPluginInstanceOwner *aOwner)
{
  return DoInstantiateEmbeddedPlugin(aMimeType, aURL, aOwner, PR_TRUE);
}

nsresult
nsPluginHost::DoInstantiateEmbeddedPlugin(const char *aMimeType, nsIURI* aURL,
                                          nsIPluginInstanceOwner* aOwner,
                                          PRBool aAllowOpeningStreams)
{
  NS_ENSURE_ARG_POINTER(aOwner);

#ifdef PLUGIN_LOGGING
  nsCAutoString urlSpec;
  if (aURL)
    aURL->GetAsciiSpec(urlSpec);

  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL,
        ("nsPluginHost::InstantiateEmbeddedPlugin Begin mime=%s, owner=%p, url=%s\n",
        aMimeType, aOwner, urlSpec.get()));

  PR_LogFlush();
#endif

  nsresult  rv;
  nsIPluginInstance *instance = nsnull;
  nsCOMPtr<nsIPluginTagInfo> pti;
  nsPluginTagType tagType;

  rv = aOwner->QueryInterface(kIPluginTagInfoIID, getter_AddRefs(pti));

  if (rv != NS_OK)
    return rv;

  rv = pti->GetTagType(&tagType);

  if ((rv != NS_OK) || !((tagType == nsPluginTagType_Embed)
                        || (tagType == nsPluginTagType_Applet)
                        || (tagType == nsPluginTagType_Object))) {
    return rv;
  }

  // Security checks
  // Can't do security checks without a URI - hopefully the plugin will take
  // care of that
  // No need to do the security check if aAllowOpeningStreams is
  // false; we don't plan to do any network access in that case.
  // Furthermore, doing it could reenter plugin instantiation, which
  // would be Bad.
  if (aURL && aAllowOpeningStreams) {
    nsCOMPtr<nsIScriptSecurityManager> secMan =
                    do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID, &rv);
    if (NS_FAILED(rv))
      return rv; // Better fail if we can't do security checks

    nsCOMPtr<nsIDocument> doc;
    aOwner->GetDocument(getter_AddRefs(doc));
    if (!doc)
      return NS_ERROR_NULL_POINTER;

    rv = secMan->CheckLoadURIWithPrincipal(doc->NodePrincipal(), aURL, 0);
    if (NS_FAILED(rv))
      return rv;

    nsCOMPtr<nsIDOMElement> elem;
    pti->GetDOMElement(getter_AddRefs(elem));

    PRInt16 shouldLoad = nsIContentPolicy::ACCEPT; // default permit
    nsresult rv =
      NS_CheckContentLoadPolicy(nsIContentPolicy::TYPE_OBJECT,
                                aURL,
                                doc->NodePrincipal(),
                                elem,
                                nsDependentCString(aMimeType ? aMimeType : ""),
                                nsnull, //extra
                                &shouldLoad);
    if (NS_FAILED(rv) || NS_CP_REJECTED(shouldLoad))
      return NS_ERROR_CONTENT_BLOCKED_SHOW_ALT;
  }

  // Look for even disabled plugins, because if the plugin for this type is
  // disabled, we don't want to go on and end up in SetUpDefaultPluginInstance.
  nsPluginTag* pluginTag = FindPluginForType(aMimeType, PR_FALSE);
  if (pluginTag) {
    if (!pluginTag->IsEnabled())
      return NS_ERROR_NOT_AVAILABLE;
  }

  PRBool isJava = pluginTag && pluginTag->mIsJavaPlugin;

  // Determine if the scheme of this URL is one we can handle internaly because we should
  // only open the initial stream if it's one that we can handle internally. Otherwise
  // |NS_OpenURI| in |InstantiateEmbeddedPlugin| may open up a OS protocal registered helper app
  // Also set bCanHandleInternally to true if aAllowOpeningStreams is
  // false; we don't want to do any network traffic in that case.
  PRBool bCanHandleInternally = PR_FALSE;
  nsCAutoString scheme;
  if (aURL && aAllowOpeningStreams && NS_SUCCEEDED(aURL->GetScheme(scheme))) {
      nsCAutoString contractID(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX);
      contractID += scheme;
      ToLowerCase(contractID);
      nsCOMPtr<nsIProtocolHandler> handler = do_GetService(contractID.get());
      if (handler)
        bCanHandleInternally = PR_TRUE;
  }

  if (FindStoppedPluginForURL(aURL, aOwner) == NS_OK) {

    PLUGIN_LOG(PLUGIN_LOG_NOISY,
    ("nsPluginHost::InstantiateEmbeddedPlugin FoundStopped mime=%s\n", aMimeType));

    aOwner->GetInstance(instance);
    if (!isJava && bCanHandleInternally)
      rv = NewEmbeddedPluginStream(aURL, aOwner, instance);

    // notify Java DOM component
    nsresult res;
    nsCOMPtr<nsIPluginInstanceOwner> javaDOM =
             do_GetService("@mozilla.org/blackwood/java-dom;1", &res);
    if (NS_SUCCEEDED(res) && javaDOM)
      javaDOM->SetInstance(instance);

    NS_IF_RELEASE(instance);
    return NS_OK;
  }

  // if we don't have a MIME type at this point, we still have one more chance by
  // opening the stream and seeing if the server hands one back
  if (!aMimeType)
    return bCanHandleInternally ? NewEmbeddedPluginStream(aURL, aOwner, nsnull) : NS_ERROR_FAILURE;

  rv = SetUpPluginInstance(aMimeType, aURL, aOwner);

  if (rv == NS_OK) {
    rv = aOwner->GetInstance(instance);
  } else {
   /* If we are here, it's time to either show the default plugin
    * or return failure so layout will replace us.
    *
    * Currently, the default plugin is shown for all EMBED and APPLET
    * tags and also any OBJECT tag that has a PLUGINURL PARAM tag name.
    */

    PRBool bHasPluginURL = PR_FALSE;
    nsCOMPtr<nsIPluginTagInfo> pti(do_QueryInterface(aOwner));

    if (pti) {
      const char *value;
      bHasPluginURL = NS_SUCCEEDED(pti->GetParameter("PLUGINURL", &value));
    }

    // if we didn't find a pluginURL param on the object tag,
    // there's nothing more to do here
    if (nsPluginTagType_Object == tagType && !bHasPluginURL)
      return rv;

    if (NS_FAILED(SetUpDefaultPluginInstance(aMimeType, aURL, aOwner)))
      return NS_ERROR_FAILURE;

    if (NS_FAILED(aOwner->GetInstance(instance)))
      return NS_ERROR_FAILURE;

    rv = NS_OK;
  }

  // if we have a failure error, it means we found a plugin for the mimetype,
  // but we had a problem with the entry point
  if (rv == NS_ERROR_FAILURE)
    return rv;

  // if we are here then we have loaded a plugin for this mimetype
  // and it could be the Default plugin

  nsPluginWindow    *window = nsnull;

  //we got a plugin built, now stream
  aOwner->GetWindow(window);

  if (instance) {
    instance->Start();
    aOwner->CreateWidget();

    // If we've got a native window, the let the plugin know about it.
    if (window->window) {
      nsCOMPtr<nsIPluginInstance> inst = instance;
      ((nsPluginNativeWindow*)window)->CallSetWindow(inst);
    }

    // create an initial stream with data
    // don't make the stream if it's a java applet or we don't have SRC or DATA attribute
    PRBool havedata = PR_FALSE;

    nsCOMPtr<nsIPluginTagInfo> pti(do_QueryInterface(aOwner, &rv));

    if (pti) {
      const char *value;
      havedata = NS_SUCCEEDED(pti->GetAttribute("SRC", &value));
      // no need to check for "data" as it would have been converted to "src"
    }

    if (havedata && !isJava && bCanHandleInternally)
      rv = NewEmbeddedPluginStream(aURL, aOwner, instance);

    // notify Java DOM component
    nsresult res;
    nsCOMPtr<nsIPluginInstanceOwner> javaDOM =
             do_GetService("@mozilla.org/blackwood/java-dom;1", &res);
    if (NS_SUCCEEDED(res) && javaDOM)
      javaDOM->SetInstance(instance);

    NS_RELEASE(instance);
  }

#ifdef PLUGIN_LOGGING
  nsCAutoString urlSpec2;
  if (aURL != nsnull) aURL->GetAsciiSpec(urlSpec2);

  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL,
        ("nsPluginHost::InstantiateEmbeddedPlugin Finished mime=%s, rv=%d, owner=%p, url=%s\n",
        aMimeType, rv, aOwner, urlSpec2.get()));

  PR_LogFlush();
#endif

  return rv;
}

// Called by full-page case
NS_IMETHODIMP nsPluginHost::InstantiateFullPagePlugin(const char *aMimeType,
                                                          nsIURI* aURI,
                                                          nsIStreamListener *&aStreamListener,
                                                          nsIPluginInstanceOwner *aOwner)
{
#ifdef PLUGIN_LOGGING
  nsCAutoString urlSpec;
  aURI->GetSpec(urlSpec);
  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("nsPluginHost::InstantiateFullPagePlugin Begin mime=%s, owner=%p, url=%s\n",
  aMimeType, aOwner, urlSpec.get()));
#endif

  if (FindStoppedPluginForURL(aURI, aOwner) == NS_OK) {
    PLUGIN_LOG(PLUGIN_LOG_NOISY,
    ("nsPluginHost::InstantiateFullPagePlugin FoundStopped mime=%s\n",aMimeType));

    nsIPluginInstance* instance;
    aOwner->GetInstance(instance);
    nsPluginTag* pluginTag = FindPluginForType(aMimeType, PR_TRUE);
    if (!pluginTag || !pluginTag->mIsJavaPlugin)
      NewFullPagePluginStream(aStreamListener, aURI, instance);
    NS_IF_RELEASE(instance);
    return NS_OK;
  }

  nsresult rv = SetUpPluginInstance(aMimeType, aURI, aOwner);

  if (NS_OK == rv) {
    nsCOMPtr<nsIPluginInstance> instance;
    nsPluginWindow * win = nsnull;

    aOwner->GetInstance(*getter_AddRefs(instance));
    aOwner->GetWindow(win);

    if (win && instance) {
      instance->Start();
      aOwner->CreateWidget();

      // If we've got a native window, the let the plugin know about it.
      nsPluginNativeWindow * window = (nsPluginNativeWindow *)win;
      if (window->window)
        window->CallSetWindow(instance);

      rv = NewFullPagePluginStream(aStreamListener, aURI, instance);

      // If we've got a native window, the let the plugin know about it.
      if (window->window)
        window->CallSetWindow(instance);
    }
  }

  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("nsPluginHost::InstantiateFullPagePlugin End mime=%s, rv=%d, owner=%p, url=%s\n",
  aMimeType, rv, aOwner, urlSpec.get()));

  return rv;
}

nsPluginTag*
nsPluginHost::FindTagForPlugin(nsIPlugin* aPlugin)
{
  nsPluginTag* pluginTag;
  for (pluginTag = mPlugins; pluginTag; pluginTag = pluginTag->mNext) {
    if (pluginTag->mEntryPoint == aPlugin) {
      return pluginTag;
    }
  }
  return nsnull;
}

nsresult nsPluginHost::FindStoppedPluginForURL(nsIURI* aURL,
                                                   nsIPluginInstanceOwner *aOwner)
{
  nsCAutoString url;
  if (!aURL)
    return NS_ERROR_FAILURE;

  aURL->GetAsciiSpec(url);

  nsPluginInstanceTag * plugin = mPluginInstanceTagList.findStopped(url.get());

  if (plugin && plugin->mStopped) {
    nsIPluginInstance* instance = plugin->mInstance;
    nsPluginWindow    *window = nsnull;
    aOwner->GetWindow(window);

    aOwner->SetInstance(instance);
    instance->SetOwner(aOwner);

    instance->Start();
    aOwner->CreateWidget();

    // If we've got a native window, the let the plugin know about it.
    if (window->window) {
      nsCOMPtr<nsIPluginInstance> inst = instance;
      ((nsPluginNativeWindow*)window)->CallSetWindow(inst);
    }

    plugin->setStopped(PR_FALSE);
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

nsresult nsPluginHost::AddInstanceToActiveList(nsCOMPtr<nsIPlugin> aPlugin,
                                               nsIPluginInstance* aInstance,
                                               nsIURI* aURL,
                                               PRBool aDefaultPlugin)

{
  nsCAutoString url;
  // It's OK to not have a URL here, as is the case with the dummy
  // Java plugin. In that case simply use an empty string...
  if (aURL)
    aURL->GetSpec(url);

  // Let's find the corresponding plugin tag by matching nsIPlugin pointer.
  // It is going to be used later when we decide whether or not we should delay
  // unloading NPAPI dll from memory.
  nsPluginTag * pluginTag = nsnull;
  if (aPlugin) {
    pluginTag = FindTagForPlugin(aPlugin);
    NS_ASSERTION(pluginTag, "Plugin tag not found");
  }

  nsPluginInstanceTag * plugin = new nsPluginInstanceTag(pluginTag, aInstance, url.get(), aDefaultPlugin);

  if (!plugin)
    return NS_ERROR_OUT_OF_MEMORY;

  mPluginInstanceTagList.add(plugin);
  return NS_OK;
}

void
nsPluginTag::RegisterWithCategoryManager(PRBool aOverrideInternalTypes,
                                         nsPluginTag::nsRegisterType aType)
{
  if (!mMimeTypeArray)
    return;

  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("nsPluginTag::RegisterWithCategoryManager plugin=%s, removing = %s\n",
  mFileName.get(), aType == ePluginUnregister ? "yes" : "no"));

  nsCOMPtr<nsICategoryManager> catMan = do_GetService(NS_CATEGORYMANAGER_CONTRACTID);
  if (!catMan)
    return;

  const char *contractId = "@mozilla.org/content/plugin/document-loader-factory;1";

  nsCOMPtr<nsIPrefBranch> psvc(do_GetService(NS_PREFSERVICE_CONTRACTID));
  if (!psvc)
    return; // NS_ERROR_OUT_OF_MEMORY

  // A preference controls whether or not the full page plugin is disabled for
  // a particular type. The string must be in the form:
  //   type1,type2,type3,type4
  // Note: need an actual interface to control this and subsequent disabling 
  // (and other plugin host settings) so applications can reliably disable 
  // plugins - without relying on implementation details such as prefs/category
  // manager entries.
  nsXPIDLCString overrideTypes;
  psvc->GetCharPref("plugin.disable_full_page_plugin_for_types", getter_Copies(overrideTypes));
  nsCAutoString overrideTypesFormatted;
  overrideTypesFormatted.Assign(',');
  overrideTypesFormatted += overrideTypes;
  overrideTypesFormatted.Append(',');

  nsACString::const_iterator start, end;
  for (int i = 0; i < mVariants; i++) {
    if (aType == ePluginUnregister) {
      nsXPIDLCString value;
      if (NS_SUCCEEDED(catMan->GetCategoryEntry("Gecko-Content-Viewers",
                                                mMimeTypeArray[i],
                                                getter_Copies(value)))) {
        // Only delete the entry if a plugin registered for it
        if (strcmp(value, contractId) == 0) {
          catMan->DeleteCategoryEntry("Gecko-Content-Viewers",
                                      mMimeTypeArray[i],
                                      PR_TRUE);
        }
      }
    } else {
      overrideTypesFormatted.BeginReading(start);
      overrideTypesFormatted.EndReading(end);
      
      nsDependentCString mimeType(mMimeTypeArray[i]);
      nsCAutoString commaSeparated; 
      commaSeparated.Assign(',');
      commaSeparated += mimeType;
      commaSeparated.Append(',');
      if (!FindInReadable(commaSeparated, start, end)) {
        catMan->AddCategoryEntry("Gecko-Content-Viewers",
                                 mMimeTypeArray[i],
                                 contractId,
                                 PR_FALSE, /* persist: broken by bug 193031 */
                                 aOverrideInternalTypes, /* replace if we're told to */
                                 nsnull);
      }
    }

    PLUGIN_LOG(PLUGIN_LOG_NOISY,
    ("nsPluginTag::RegisterWithCategoryManager mime=%s, plugin=%s\n",
    mMimeTypeArray[i], mFileName.get()));
  }
}

NS_IMETHODIMP nsPluginHost::SetUpPluginInstance(const char *aMimeType,
                                                nsIURI *aURL,
                                                nsIPluginInstanceOwner *aOwner)
{
  nsresult rv = NS_OK;

  rv = TrySetUpPluginInstance(aMimeType, aURL, aOwner);

  // if we fail, refresh plugin list just in case the plugin has been
  // just added and try to instantiate plugin instance again, see bug 143178
  if (NS_FAILED(rv)) {
    // we should also make sure not to do this more than once per page
    // so if there are a few embed tags with unknown plugins,
    // we don't get unnecessary overhead
    // let's cache document to decide whether this is the same page or not
    nsCOMPtr<nsIDocument> document;
    if (aOwner)
      aOwner->GetDocument(getter_AddRefs(document));

    nsCOMPtr<nsIDocument> currentdocument = do_QueryReferent(mCurrentDocument);
    if (document == currentdocument)
      return rv;

    mCurrentDocument = do_GetWeakReference(document);

    // ReloadPlugins will do the job smartly: nothing will be done
    // if no changes detected, in such a case just return
    if (NS_ERROR_PLUGINS_PLUGINSNOTCHANGED == ReloadPlugins(PR_FALSE))
      return rv;

    // other failure return codes may be not fatal, so we can still try
    rv = TrySetUpPluginInstance(aMimeType, aURL, aOwner);
  }

  return rv;
}

nsresult
nsPluginHost::TrySetUpPluginInstance(const char *aMimeType,
                                     nsIURI *aURL,
                                     nsIPluginInstanceOwner *aOwner)
{
#ifdef PLUGIN_LOGGING
  nsCAutoString urlSpec;
  if (aURL != nsnull) aURL->GetSpec(urlSpec);

  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL,
        ("nsPluginHost::TrySetupPluginInstance Begin mime=%s, owner=%p, url=%s\n",
        aMimeType, aOwner, urlSpec.get()));

  PR_LogFlush();
#endif


  nsresult result = NS_ERROR_FAILURE;
  nsCOMPtr<nsIPluginInstance> instance;
  nsCOMPtr<nsIPlugin> plugin;
  const char* mimetype = nsnull;

  // if don't have a mimetype or no plugin can handle this mimetype
  // check by file extension
  nsPluginTag* pluginTag = FindPluginForType(aMimeType, PR_TRUE);
  if (!pluginTag) {
    nsCOMPtr<nsIURL> url = do_QueryInterface(aURL);
    if (!url) return NS_ERROR_FAILURE;

    nsCAutoString fileExtension;
    url->GetFileExtension(fileExtension);

    // if we don't have an extension or no plugin for this extension,
    // return failure as there is nothing more we can do
    if (fileExtension.IsEmpty() ||
        !(pluginTag = FindPluginEnabledForExtension(fileExtension.get(),
                                                    mimetype))) {
      return NS_ERROR_FAILURE;
    }
  }
  else {
    mimetype = aMimeType;
  }

  NS_ASSERTION(pluginTag, "Must have plugin tag here!");

#ifdef OJI
  PRBool isJavaPlugin = pluginTag->mIsJavaPlugin;
  if (isJavaPlugin && !pluginTag->mIsNPRuntimeEnabledJavaPlugin) {
    // We must make sure LiveConnect is started, if needed.
    nsCOMPtr<nsIDocument> document;
    aOwner->GetDocument(getter_AddRefs(document));
    if (document) {
      nsCOMPtr<nsPIDOMWindow> window =
        do_QueryInterface(document->GetScriptGlobalObject());

      if (window) {
        window->InitJavaProperties();
      }
    }
  }
#endif

  GetPlugin(mimetype, getter_AddRefs(plugin));

  if (plugin) {
#if defined(XP_WIN) && !defined(WINCE)
    static BOOL firstJavaPlugin = FALSE;
    BOOL restoreOrigDir = FALSE;
    WCHAR origDir[_MAX_PATH];
    if (pluginTag->mIsJavaPlugin && !firstJavaPlugin) {
      DWORD dw = GetCurrentDirectoryW(_MAX_PATH, origDir);
      NS_ASSERTION(dw <= _MAX_PATH, "Falied to obtain the current directory, which may leads to incorrect class laoding");
      nsCOMPtr<nsIFile> binDirectory;
      result = NS_GetSpecialDirectory(NS_XPCOM_CURRENT_PROCESS_DIR,
                                      getter_AddRefs(binDirectory));

      if (NS_SUCCEEDED(result)) {
        nsAutoString path;
        binDirectory->GetPath(path);
        restoreOrigDir = SetCurrentDirectoryW(path.get());
      }
    }
#endif
#ifdef OJI
    nsNPAPIPlugin *pluginPriv = static_cast<nsNPAPIPlugin*>(plugin.get());
    nsIPluginOld *shadowPlugin = pluginPriv->GetShadow();
    if (shadowPlugin) {
      nsIPluginInstanceOld *shadowInstance = nsnull;
      result = shadowPlugin->CreatePluginInstance(NULL,
                                                  kIPluginInstanceOldIID,
                                                  aMimeType,
                                                  (void **)&shadowInstance);
      if (NS_SUCCEEDED(result) && shadowInstance) {
        instance =
          static_cast<nsIPluginInstance*>(new nsNPAPIPluginInstance(shadowInstance));
        if (!instance)
          return NS_ERROR_OUT_OF_MEMORY;
      }
    } else {
      result = plugin->CreatePluginInstance(getter_AddRefs(instance));
    }
#else
    result = plugin->CreatePluginInstance(getter_AddRefs(instance));
#endif

#if defined(XP_WIN) && !defined(WINCE)
    if (!firstJavaPlugin && restoreOrigDir) {
      BOOL bCheck = SetCurrentDirectoryW(origDir);
      NS_ASSERTION(bCheck, " Error restoring driectoy");
      firstJavaPlugin = TRUE;
    }
#endif
  }

  if (NS_FAILED(result))
    return result;

  // it is adreffed here
  aOwner->SetInstance(instance);

  // this should not addref the instance or owner
  // except in some cases not Java, see bug 140931
  // our COM pointer will free the peer
  result = instance->Initialize(aOwner, mimetype);
  if (NS_FAILED(result)) {
    aOwner->SetInstance(nsnull);
    return result;
  }

  // instance and peer will be addreffed here
  result = AddInstanceToActiveList(plugin, instance, aURL, PR_FALSE);

#ifdef PLUGIN_LOGGING
  nsCAutoString urlSpec2;
  if (aURL)
    aURL->GetSpec(urlSpec2);

  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_BASIC,
        ("nsPluginHost::TrySetupPluginInstance Finished mime=%s, rv=%d, owner=%p, url=%s\n",
        aMimeType, result, aOwner, urlSpec2.get()));

  PR_LogFlush();
#endif

  return result;
}

nsresult
nsPluginHost::SetUpDefaultPluginInstance(const char *aMimeType,
                                         nsIURI *aURL,
                                         nsIPluginInstanceOwner *aOwner)
{
  if (mDefaultPluginDisabled) {
    // The default plugin is disabled, don't load it.
    return NS_OK;
  }

  nsCOMPtr<nsIPluginInstance> instance;
  nsCOMPtr<nsIPlugin> plugin = NULL;
  const char* mimetype = aMimeType;

  if (!aURL)
    return NS_ERROR_FAILURE;

  GetPlugin("*", getter_AddRefs(plugin));

  nsresult result = NS_ERROR_OUT_OF_MEMORY;
  if (plugin)
    result = plugin->CreatePluginInstance(getter_AddRefs(instance));
  if (NS_FAILED(result))
    return result;

  // it is adreffed here
  aOwner->SetInstance(instance);

  // if we don't have a mimetype, check by file extension
  nsXPIDLCString mt;
  if (!mimetype || !*mimetype) {
    nsresult res = NS_OK;
    nsCOMPtr<nsIMIMEService> ms (do_GetService(NS_MIMESERVICE_CONTRACTID, &res));
    if (NS_SUCCEEDED(res)) {
      res = ms->GetTypeFromURI(aURL, mt);
      if (NS_SUCCEEDED(res))
        mimetype = mt.get();
    }
  }

  // this should not addref the instance or owner
  result = instance->Initialize(aOwner, mimetype);
  if (NS_FAILED(result)) {
    aOwner->SetInstance(nsnull);
    return result;
  }

  // instance will be addreffed here
  result = AddInstanceToActiveList(plugin, instance, aURL, PR_TRUE);

  return result;
}

NS_IMETHODIMP
nsPluginHost::IsPluginEnabledForType(const char* aMimeType)
{
  // Pass PR_FALSE as the second arg so we can return NS_ERROR_PLUGIN_DISABLED
  // for disabled plug-ins.
  nsPluginTag *plugin = FindPluginForType(aMimeType, PR_FALSE);
  if (!plugin)
    return NS_ERROR_FAILURE;

  if (!plugin->IsEnabled()) {
    if (plugin->HasFlag(NS_PLUGIN_FLAG_BLOCKLISTED))
      return NS_ERROR_PLUGIN_BLOCKLISTED;
    else
      return NS_ERROR_PLUGIN_DISABLED;
  }

  return NS_OK;
}

// check comma delimitered extensions
static int CompareExtensions(const char *aExtensionList, const char *aExtension)
{
  if (!aExtensionList || !aExtension)
    return -1;

  const char *pExt = aExtensionList;
  const char *pComma = strchr(pExt, ',');
  if (!pComma)
    return PL_strcasecmp(pExt, aExtension);

  int extlen = strlen(aExtension);
  while (pComma) {
    int length = pComma - pExt;
    if (length == extlen && 0 == PL_strncasecmp(aExtension, pExt, length))
      return 0;
    pComma++;
    pExt = pComma;
    pComma = strchr(pExt, ',');
  }

  // the last one
  return PL_strcasecmp(pExt, aExtension);
}

NS_IMETHODIMP
nsPluginHost::IsPluginEnabledForExtension(const char* aExtension,
                                          const char* &aMimeType)
{
  nsPluginTag *plugin = FindPluginEnabledForExtension(aExtension, aMimeType);
  return plugin ? NS_OK : NS_ERROR_FAILURE;
}

class DOMMimeTypeImpl : public nsIDOMMimeType {
public:
  NS_DECL_ISUPPORTS

  DOMMimeTypeImpl(nsPluginTag* aTag, PRUint32 aMimeTypeIndex)
  {
    if (!aTag)
      return;
    CopyUTF8toUTF16(aTag->mMimeDescriptionArray[aMimeTypeIndex], mDescription);
    if (aTag->mExtensionsArray)
      CopyUTF8toUTF16(aTag->mExtensionsArray[aMimeTypeIndex], mSuffixes);
    if (aTag->mMimeTypeArray)
      CopyUTF8toUTF16(aTag->mMimeTypeArray[aMimeTypeIndex], mType);
  }

  virtual ~DOMMimeTypeImpl() {
  }

  NS_METHOD GetDescription(nsAString& aDescription)
  {
    aDescription.Assign(mDescription);
    return NS_OK;
  }

  NS_METHOD GetEnabledPlugin(nsIDOMPlugin** aEnabledPlugin)
  {
    // this has to be implemented by the DOM version.
    *aEnabledPlugin = nsnull;
    return NS_OK;
  }

  NS_METHOD GetSuffixes(nsAString& aSuffixes)
  {
    aSuffixes.Assign(mSuffixes);
    return NS_OK;
  }

  NS_METHOD GetType(nsAString& aType)
  {
    aType.Assign(mType);
    return NS_OK;
  }

private:
  nsString mDescription;
  nsString mSuffixes;
  nsString mType;
};

NS_IMPL_ISUPPORTS1(DOMMimeTypeImpl, nsIDOMMimeType)

class DOMPluginImpl : public nsIDOMPlugin {
public:
  NS_DECL_ISUPPORTS

  DOMPluginImpl(nsPluginTag* aPluginTag) : mPluginTag(aPluginTag)
  {
  }

  virtual ~DOMPluginImpl() {
  }

  NS_METHOD GetDescription(nsAString& aDescription)
  {
    CopyUTF8toUTF16(mPluginTag.mDescription, aDescription);
    return NS_OK;
  }

  NS_METHOD GetFilename(nsAString& aFilename)
  {
    PRBool bShowPath;
    nsCOMPtr<nsIPrefBranch> prefService = do_GetService(NS_PREFSERVICE_CONTRACTID);
    if (prefService &&
        NS_SUCCEEDED(prefService->GetBoolPref("plugin.expose_full_path", &bShowPath)) &&
        bShowPath) {
      CopyUTF8toUTF16(mPluginTag.mFullPath, aFilename);
    } else {
      CopyUTF8toUTF16(mPluginTag.mFileName, aFilename);
    }

    return NS_OK;
  }

  NS_METHOD GetVersion(nsAString& aVersion)
  {
    CopyUTF8toUTF16(mPluginTag.mVersion, aVersion);
    return NS_OK;
  }

  NS_METHOD GetName(nsAString& aName)
  {
    CopyUTF8toUTF16(mPluginTag.mName, aName);
    return NS_OK;
  }

  NS_METHOD GetLength(PRUint32* aLength)
  {
    *aLength = mPluginTag.mVariants;
    return NS_OK;
  }

  NS_METHOD Item(PRUint32 aIndex, nsIDOMMimeType** aReturn)
  {
    nsIDOMMimeType* mimeType = new DOMMimeTypeImpl(&mPluginTag, aIndex);
    NS_IF_ADDREF(mimeType);
    *aReturn = mimeType;
    return NS_OK;
  }

  NS_METHOD NamedItem(const nsAString& aName, nsIDOMMimeType** aReturn)
  {
    for (int i = mPluginTag.mVariants - 1; i >= 0; --i) {
      if (aName.Equals(NS_ConvertUTF8toUTF16(mPluginTag.mMimeTypeArray[i])))
        return Item(i, aReturn);
    }
    return NS_OK;
  }

private:
  nsPluginTag mPluginTag;
};

NS_IMPL_ISUPPORTS1(DOMPluginImpl, nsIDOMPlugin)

NS_IMETHODIMP
nsPluginHost::GetPluginCount(PRUint32* aPluginCount)
{
  LoadPlugins();

  PRUint32 count = 0;

  nsPluginTag* plugin = mPlugins;
  while (plugin != nsnull) {
    if (plugin->IsEnabled()) {
      ++count;
    }
    plugin = plugin->mNext;
  }

  *aPluginCount = count;

  return NS_OK;
}

NS_IMETHODIMP
nsPluginHost::GetPlugins(PRUint32 aPluginCount, nsIDOMPlugin** aPluginArray)
{
  LoadPlugins();

  nsPluginTag* plugin = mPlugins;
  for (PRUint32 i = 0; i < aPluginCount && plugin; plugin = plugin->mNext) {
    if (plugin->IsEnabled()) {
      nsIDOMPlugin* domPlugin = new DOMPluginImpl(plugin);
      NS_IF_ADDREF(domPlugin);
      aPluginArray[i++] = domPlugin;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsPluginHost::GetPluginTags(PRUint32* aPluginCount, nsIPluginTag*** aResults)
{
  LoadPlugins();

  PRUint32 count = 0;
  nsRefPtr<nsPluginTag> plugin = mPlugins;
  while (plugin != nsnull) {
    count++;
    plugin = plugin->mNext;
  }

  *aResults = static_cast<nsIPluginTag**>
                         (nsMemory::Alloc(count * sizeof(**aResults)));
  if (!*aResults)
    return NS_ERROR_OUT_OF_MEMORY;

  *aPluginCount = count;

  plugin = mPlugins;
  PRUint32 i;
  for (i = 0; i < count; i++) {
    (*aResults)[i] = plugin;
    NS_ADDREF((*aResults)[i]);
    plugin = plugin->mNext;
  }

  return NS_OK;
}

nsPluginTag*
nsPluginHost::FindPluginForType(const char* aMimeType,
                                PRBool aCheckEnabled)
{
  nsPluginTag *plugins = nsnull;
  PRInt32     variants, cnt;

  LoadPlugins();

  // if we have a mimetype passed in, search the mPlugins
  // linked list for a match
  if (nsnull != aMimeType) {
    plugins = mPlugins;

    while (nsnull != plugins) {
      variants = plugins->mVariants;
      for (cnt = 0; cnt < variants; cnt++) {
        if ((!aCheckEnabled || plugins->IsEnabled()) &&
            plugins->mMimeTypeArray[cnt] &&
            (0 == PL_strcasecmp(plugins->mMimeTypeArray[cnt], aMimeType))) {
          return plugins;
        }
      }

      plugins = plugins->mNext;
    }
  }

  return nsnull;
}

nsPluginTag*
nsPluginHost::FindPluginEnabledForExtension(const char* aExtension,
                                            const char*& aMimeType)
{
  nsPluginTag *plugins = nsnull;
  PRInt32     variants, cnt;

  LoadPlugins();

  // if we have a mimetype passed in, search the mPlugins linked
  // list for a match
  if (aExtension) {
    plugins = mPlugins;
    while (plugins) {
      variants = plugins->mVariants;
      if (plugins->mExtensionsArray) {
        for (cnt = 0; cnt < variants; cnt++) {
          // mExtensionsArray[cnt] is a list of extensions separated
          // by commas
          if (plugins->IsEnabled() &&
              0 == CompareExtensions(plugins->mExtensionsArray[cnt], aExtension)) {
            aMimeType = plugins->mMimeTypeArray[cnt];
            return plugins;
          }
        }
      }

      plugins = plugins->mNext;
    }
  }

  return nsnull;
}

static nsresult ConvertToNative(nsIUnicodeEncoder *aEncoder,
                                const nsACString& aUTF8String,
                                nsACString& aNativeString)
{
  NS_ConvertUTF8toUTF16 utf16(aUTF8String);
  PRInt32 len = utf16.Length();
  PRInt32 outLen;
  nsresult rv = aEncoder->GetMaxLength(utf16.get(), len, &outLen);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!EnsureStringLength(aNativeString, outLen))
    return NS_ERROR_OUT_OF_MEMORY;
  rv = aEncoder->Convert(utf16.get(), &len,
                         aNativeString.BeginWriting(), &outLen);
  NS_ENSURE_SUCCESS(rv, rv);
  aNativeString.SetLength(outLen);
  return NS_OK;
}

static nsresult CreateNPAPIPlugin(const nsPluginTag *aPluginTag,
                                  nsIPlugin **aOutNPAPIPlugin)
{
  nsresult rv;
  nsCOMPtr <nsIPlatformCharset> pcs =
    do_GetService(NS_PLATFORMCHARSET_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString charset;
  rv = pcs->GetCharset(kPlatformCharsetSel_FileName, charset);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString fullPath;
  if (!charset.LowerCaseEqualsLiteral("utf-8")) {
    nsCOMPtr<nsIUnicodeEncoder> encoder;
    nsCOMPtr<nsICharsetConverterManager> ccm =
      do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = ccm->GetUnicodeEncoderRaw(charset.get(), getter_AddRefs(encoder));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = ConvertToNative(encoder, aPluginTag->mFullPath, fullPath);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    fullPath = aPluginTag->mFullPath;
  }

  return nsNPAPIPlugin::CreatePlugin(fullPath.get(),
                                     aPluginTag->mLibrary,
                                     aOutNPAPIPlugin);
}

NS_IMETHODIMP nsPluginHost::GetPlugin(const char *aMimeType, nsIPlugin** aPlugin)
{
  nsresult rv = NS_ERROR_FAILURE;
  *aPlugin = NULL;

  if (!aMimeType)
    return NS_ERROR_ILLEGAL_VALUE;

  // If plugins haven't been scanned yet, do so now
  LoadPlugins();

  nsPluginTag* pluginTag = FindPluginForType(aMimeType, PR_TRUE);
  if (pluginTag) {
    rv = NS_OK;
    PLUGIN_LOG(PLUGIN_LOG_BASIC,
    ("nsPluginHost::GetPlugin Begin mime=%s, plugin=%s\n",
    aMimeType, pluginTag->mFileName.get()));

#ifdef NS_DEBUG
    if (aMimeType && !pluginTag->mFileName.IsEmpty())
      printf("For %s found plugin %s\n", aMimeType, pluginTag->mFileName.get());
#endif

    if (!pluginTag->mLibrary) { // if we haven't done this yet
      if (pluginTag->mFullPath.IsEmpty())
        return NS_ERROR_FAILURE;
      nsCOMPtr<nsILocalFile> file = do_CreateInstance("@mozilla.org/file/local;1");
      file->InitWithPath(NS_ConvertUTF8toUTF16(pluginTag->mFullPath));
      nsPluginFile pluginFile(file);
      PRLibrary* pluginLibrary = NULL;

      if (pluginFile.LoadPlugin(pluginLibrary) != NS_OK || pluginLibrary == NULL)
        return NS_ERROR_FAILURE;

      // remove from unused lib list, if it is there
      if (mUnusedLibraries.Contains(pluginLibrary))
        mUnusedLibraries.RemoveElement(pluginLibrary);

      pluginTag->mLibrary = pluginLibrary;
    }

    nsCOMPtr<nsIPlugin> plugin = pluginTag->mEntryPoint;
#ifdef OJI
    if (!plugin) {
      // First try to get the entry point from an NPAPI plugin.
      rv = CreateNPAPIPlugin(pluginTag, getter_AddRefs(plugin));
      if (NS_SUCCEEDED(rv)) {
        pluginTag->mEntryPoint = plugin;
        pluginTag->Mark(NS_PLUGIN_FLAG_NPAPI);
      // And if that fails, try to get an OJI plugin's entry point
      } else if (pluginTag->mIsJavaPlugin &&
                 pluginTag->mName.Find("Java Embedding Plugin") != kNotFound) {
        // Refuse to load any other OJI plugin than the JEP.
        nsFactoryProc nsGetFactory = nsnull;
        nsGetFactory = (nsFactoryProc) PR_FindFunctionSymbol(pluginTag->mLibrary, "NSGetFactory");

        // No, this is not a leak. GetGlobalServiceManager() doesn't
        // addref the pointer on the way out. It probably should.
        nsIServiceManagerObsolete* serviceManager;
        nsServiceManager::GetGlobalServiceManager((nsIServiceManager**)&serviceManager);

        nsIPluginOld *shadow = nsnull;
        rv = nsGetFactory(serviceManager, kPluginOldCID, nsnull, nsnull,
                          (nsIFactory**)&shadow);
        if (NS_SUCCEEDED(rv) && shadow) {
          plugin = static_cast<nsIPlugin*>(new nsNPAPIPlugin(shadow));
          if (!plugin)
            return NS_ERROR_OUT_OF_MEMORY;
          plugin->Initialize();
          pluginTag->mEntryPoint = plugin;
        }
        else {
          return NS_ERROR_FAILURE;
        }
      }
      else {
        return NS_ERROR_FAILURE;
      }
    }
#else
    if (!plugin) {
      // Now lets try to get the entry point from an NPAPI plugin
      rv = CreateNPAPIPlugin(pluginTag, getter_AddRefs(plugin));
      if (NS_FAILED(rv))
        return rv;

      NS_ASSERTION(plugin, "CreateNPAPIPlugin succeeded without setting 'plugin'");
      pluginTag->mEntryPoint = plugin;
    }
#endif

    if (!plugin)
      return NS_ERROR_FAILURE;


    NS_ADDREF(*aPlugin = plugin);
    return NS_OK;
  }

  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("nsPluginHost::GetPlugin End mime=%s, rv=%d, plugin=%p name=%s\n",
  aMimeType, rv, *aPlugin,
  (pluginTag ? pluginTag->mFileName.get() : "(not found)")));

  return rv;
}

#ifdef OJI
// nsIPluginHostOld methods not implemented elsewhere

NS_IMETHODIMP nsPluginHost::GetPluginFactory(const char *aMimeType, nsIPlugin** aPlugin)
{
	return GetPlugin(aMimeType, aPlugin);
}

// nsIFactory interface

NS_IMETHODIMP nsPluginHost::CreateInstance(nsISupports *aOuter,
                                           REFNSIID aIID,
                                           void **aResult)
{
  NS_NOTREACHED("how'd I get here?");
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP nsPluginHost::LockFactory(PRBool aLock)
{
  NS_NOTREACHED("how'd I get here?");
  return NS_ERROR_UNEXPECTED;
}

// nsIFileUtilties interface

NS_IMETHODIMP nsPluginHost::GetProgramPath(const char* *result)
{
  nsresult rv;
  NS_ENSURE_ARG_POINTER(result);
  *result = nsnull;

  nsCOMPtr<nsIProperties> dirService(do_GetService(kDirectoryServiceContractID, &rv));
  if (NS_FAILED(rv))
    return rv;
  nsCOMPtr<nsILocalFile> programDir;
  rv = dirService->Get(NS_XPCOM_CURRENT_PROCESS_DIR, NS_GET_IID(nsILocalFile), getter_AddRefs(programDir));
  if (NS_FAILED(rv))
    return rv;

  nsCAutoString temp;
  rv = programDir->GetNativePath(temp);
  *result = ToNewCString(temp);
  return rv;
}

NS_IMETHODIMP nsPluginHost::GetTempDirPath(const char* *result)
{
  nsresult rv;
  NS_ENSURE_ARG_POINTER(result);
  *result = nsnull;

  nsCOMPtr<nsIProperties> dirService(do_GetService(kDirectoryServiceContractID, &rv));
  if (NS_FAILED(rv))
    return rv;
  nsCOMPtr<nsILocalFile> tempDir;
  rv = dirService->Get(NS_OS_TEMP_DIR, NS_GET_IID(nsILocalFile), getter_AddRefs(tempDir));
  if (NS_FAILED(rv))
    return rv;

  nsCAutoString temp;
  rv = tempDir->GetNativePath(temp);
  *result = ToNewCString(temp);
  return rv;
}

NS_IMETHODIMP nsPluginHost::NewTempFileName(const char* prefix, PRUint32 bufLen, char* resultBuf)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

// nsIPluginManager methods not implemented elsewhere

NS_IMETHODIMP nsPluginHost::GetValue(nsPluginManagerVariable aVariable, void *aValue)
{
  NS_ENSURE_ARG_POINTER(aValue);
  if (nsPluginManagerVariable_SupportsXEmbed == aVariable)
    *(NPBool*)aValue = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP nsPluginHost::RegisterPlugin(REFNSIID aCID,
                                           const char* aPluginName,
                                           const char* aDescription,
                                           const char** aMimeTypes,
                                           const char** aMimeDescriptions,
                                           const char** aFileExtensions,
                                           PRInt32 aCount)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsPluginHost::UnregisterPlugin(REFNSIID aCID)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

// nsIPluginManager2 methods not implemented elsewhere

NS_IMETHODIMP nsPluginHost::BeginWaitCursor(void)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsPluginHost::EndWaitCursor(void)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsPluginHost::SupportsURLProtocol(const char* protocol, PRBool *result)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsPluginHost::NotifyStatusChange(nsIPlugin* plugin, nsresult errorStatus)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsPluginHost::RegisterWindow(nsIEventHandler* handler, nsPluginPlatformWindowRef window)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsPluginHost::UnregisterWindow(nsIEventHandler* handler, nsPluginPlatformWindowRef window)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsPluginHost::AllocateMenuID(nsIEventHandler* handler, PRBool isSubmenu, PRInt16 *result)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsPluginHost::DeallocateMenuID(nsIEventHandler* handler, PRInt16 menuID)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsPluginHost::HasAllocatedMenuID(nsIEventHandler* handler, PRInt16 menuID, PRBool *result)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

// nsICookieStorage interface

NS_IMETHODIMP nsPluginHost::GetCookie(const char* inCookieURL, void* inOutCookieBuffer, PRUint32& inOutCookieSize)
{
  nsresult rv = NS_ERROR_NOT_IMPLEMENTED;
  nsXPIDLCString cookieString;
  PRUint32 cookieStringLen = 0;
  nsCOMPtr<nsIURI> uriIn;

  if (!inCookieURL || (0 >= inOutCookieSize)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIIOService> ioService(do_GetService(NS_IOSERVICE_CONTRACTID, &rv));

  if (NS_FAILED(rv) || !ioService)
    return rv;

  nsCOMPtr<nsICookieService> cookieService =
           do_GetService(NS_COOKIESERVICE_CONTRACTID, &rv);

  if (NS_FAILED(rv) || !cookieService)
    return NS_ERROR_INVALID_ARG;

  // make an nsURI from the argument url
  rv = ioService->NewURI(nsDependentCString(inCookieURL), nsnull, nsnull, getter_AddRefs(uriIn));
  if (NS_FAILED(rv))
    return rv;

  rv = cookieService->GetCookieString(uriIn, nsnull, getter_Copies(cookieString));

  if (NS_FAILED(rv) || !cookieString ||
      (inOutCookieSize <= (cookieStringLen = PL_strlen(cookieString.get())))) {
    return NS_ERROR_FAILURE;
  }

  PL_strcpy((char *) inOutCookieBuffer, cookieString.get());
  inOutCookieSize = cookieStringLen;
  rv = NS_OK;

  return rv;
}

NS_IMETHODIMP nsPluginHost::SetCookie(const char* inCookieURL, const void* inCookieBuffer, PRUint32 inCookieSize)
{
  nsresult rv = NS_ERROR_NOT_IMPLEMENTED;
  nsCOMPtr<nsIURI> uriIn;

  if (!inCookieURL || !inCookieBuffer ||
      (0 >= inCookieSize)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIIOService> ioService(do_GetService(NS_IOSERVICE_CONTRACTID, &rv));

  if (NS_FAILED(rv) || !ioService)
    return rv;

  nsCOMPtr<nsICookieService> cookieService =
           do_GetService(NS_COOKIESERVICE_CONTRACTID, &rv);

  if (NS_FAILED(rv) || !cookieService)
    return NS_ERROR_FAILURE;

  // make an nsURI from the argument url
  rv = ioService->NewURI(nsDependentCString(inCookieURL), nsnull, nsnull, getter_AddRefs(uriIn));
  if (NS_FAILED(rv))
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIPrompt> prompt;
  GetPrompt(nsnull, getter_AddRefs(prompt));

  char * cookie = (char *)inCookieBuffer;
  char c = cookie[inCookieSize];
  cookie[inCookieSize] = '\0';
  rv = cookieService->SetCookieString(uriIn, prompt, cookie, nsnull);
  cookie[inCookieSize] = c;

  return rv;
}

// Helper method

// Given an nsIPluginInstanceOld pointer, return a pointer to its
// corresponding nsIPluginInstance object (the one whose mShadow member is
// the nsIPluginInstanceOld pointer).
//
// Here we assume that the nsPIPluginInstancePeer::GetOwner() and
// nsIPluginInstanceOwner::GetInstance() methods will never be called
// from an "old" plugin instance (an instance of nsIPluginInstanceOld).
// If this were to happen, the plugin would get an instance pointer
// that it couldn't use (it would need and expect an nsIPluginInstanceOld
// pointer, but get an nsIPluginInstance pointer instead).  The JEP's
// MRJPlugin.plugin doesn't use either of these two methods.  And given
// that the nsPIPluginInstancePeer interface is "private", I don't expect
// that other XPCOM/OJI plugins use it, either.  But if we ever load one
// that does, we'll need to change the nsIPluginInstanceOwner::GetInstance()
// method to return a different result depending on whether or not its
// mInstance member has an mShadow member.  And we'll also need to change
// this code.
//
// This method is (of course) always called from the browser.
nsresult
nsPluginHost::NewForOldPluginInstance(nsIPluginInstanceOld* aInstanceOld, nsIPluginInstance** aInstance)
{
  if (!aInstance)
    return NS_ERROR_NULL_POINTER;

  *aInstance = nsnull;

  if (!aInstanceOld)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIPluginInstancePeer> peer;
  nsresult rv = aInstanceOld->GetPeer(getter_AddRefs(peer));
  if (NS_SUCCEEDED(rv) && !peer)
    rv = NS_ERROR_FAILURE;
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsPIPluginInstancePeer> privpeer = do_QueryInterface(peer, &rv);
  if (NS_SUCCEEDED(rv) && !privpeer)
    rv = NS_ERROR_FAILURE;
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsIPluginInstanceOwner> owner;
  rv = privpeer->GetOwner(getter_AddRefs(owner));
  if (NS_SUCCEEDED(rv) && !owner)
    rv = NS_ERROR_FAILURE;
  if (NS_FAILED(rv))
    return rv;

  nsIPluginInstance* instance = nsnull;
  rv = owner->GetInstance(instance);
  if (NS_SUCCEEDED(rv) && !instance)
    rv = NS_ERROR_FAILURE;

  if (NS_SUCCEEDED(rv))
    *aInstance = instance;

  return rv;
}

#endif // OJI

// XXX called from ScanPluginsDirectory only when told to filter
// currently 'unwanted' plugins are Java, and all other plugins except
// Acrobat, Flash, Quicktime and Shockwave
static PRBool isUnwantedPlugin(nsPluginTag * tag)
{
  if (tag->mFileName.IsEmpty())
    return PR_TRUE;

  for (PRInt32 i = 0; i < tag->mVariants; ++i) {
    if (nsnull == PL_strcasecmp(tag->mMimeTypeArray[i], "application/pdf"))
      return PR_FALSE;

    if (nsnull == PL_strcasecmp(tag->mMimeTypeArray[i], "application/x-shockwave-flash"))
      return PR_FALSE;

    if (nsnull == PL_strcasecmp(tag->mMimeTypeArray[i],"application/x-director"))
      return PR_FALSE;
  }

  // On Windows, we also want to include the Quicktime plugin from the 4.x directory
  // But because it spans several DLL's, the best check for now is by filename
  if (tag->mFileName.Find("npqtplugin", PR_TRUE, 0, -1) != kNotFound)
    return PR_FALSE;

  return PR_TRUE;
}

PRBool nsPluginHost::IsJavaMIMEType(const char* aType)
{
  return aType &&
    ((0 == PL_strncasecmp(aType, "application/x-java-vm",
                          sizeof("application/x-java-vm") - 1)) ||
     (0 == PL_strncasecmp(aType, "application/x-java-applet",
                          sizeof("application/x-java-applet") - 1)) ||
     (0 == PL_strncasecmp(aType, "application/x-java-bean",
                          sizeof("application/x-java-bean") - 1)));
}

nsPluginTag * nsPluginHost::HaveSamePlugin(nsPluginTag * aPluginTag)
{
  for (nsPluginTag* tag = mPlugins; tag; tag = tag->mNext) {
    if (tag->Equals(aPluginTag))
      return tag;
  }
  return nsnull;
}

PRBool nsPluginHost::IsDuplicatePlugin(nsPluginTag * aPluginTag)
{
  nsPluginTag * tag = HaveSamePlugin(aPluginTag);
  if (tag) {
    // if we got the same plugin, check the full path to see if this is a dup;

    // mFileName contains full path on Windows and Unix and leaf name on Mac
    // if those are not equal, we have the same plugin with  different path,
    // i.e. duplicate, return true
    if (!tag->mFileName.Equals(aPluginTag->mFileName))
      return PR_TRUE;

    // if they are equal, compare mFullPath fields just in case
    // mFileName contained leaf name only, and if not equal, return true
    if (!tag->mFullPath.Equals(aPluginTag->mFullPath))
      return PR_TRUE;
  }

  // we do not have it at all, return false
  return PR_FALSE;
}

// Structure for collecting plugin files found during directory scanning
struct pluginFileinDirectory
{
  nsString mFilePath;
  nsString mFileName; // Used for plugins in NS_APP_PLUGINS_DIR.
  PRInt64  mModTime;

  pluginFileinDirectory()
  {
    mModTime = LL_ZERO;
  }
};

// QuickSort callback for comparing the modification time of two files
// if the times are the same, compare the filenames

NS_SPECIALIZE_TEMPLATE
class nsDefaultComparator<pluginFileinDirectory, pluginFileinDirectory>
{
  public:
  PRBool Equals(const pluginFileinDirectory& aA,
                const pluginFileinDirectory& aB) const {
    if (aA.mModTime == aB.mModTime &&
        Compare(aA.mFilePath, aB.mFilePath,
                nsCaseInsensitiveStringComparator()) == 0)
      return PR_TRUE;
    else
      return PR_FALSE;
  }
  PRBool LessThan(const pluginFileinDirectory& aA,
                  const pluginFileinDirectory& aB) const {
    if (aA.mModTime < aB.mModTime)
      return PR_TRUE;
    else if(aA.mModTime == aB.mModTime)
      return Compare(aA.mFilePath, aB.mFilePath,
                     nsCaseInsensitiveStringComparator()) < 0;
    else
      return PR_FALSE;
  }
};

typedef NS_NPAPIPLUGIN_CALLBACK(char *, NP_GETMIMEDESCRIPTION)(void);

static nsresult FixUpPluginInfo(nsPluginInfo &aInfo, nsPluginFile &aPluginFile)
{
#ifndef XP_WIN
  return NS_OK;
#endif

  for (PRUint32 i = 0; i < aInfo.fVariantCount; i++) {
    if (PL_strcmp(aInfo.fMimeTypeArray[i], "*"))
      continue;

    // we got "*" type
    // check if this is an alien plugin (not our default plugin)
    // by trying to find a special entry point
    PRLibrary *library = nsnull;
    if (NS_FAILED(aPluginFile.LoadPlugin(library)) || !library)
      return NS_ERROR_FAILURE;

    NP_GETMIMEDESCRIPTION pf = (NP_GETMIMEDESCRIPTION)PR_FindFunctionSymbol(library, "NP_GetMIMEDescription");

    if (pf) {
      // if we found it, this is the default plugin, return
      char * mimedescription = pf();
      if (!PL_strncmp(mimedescription, NS_PLUGIN_DEFAULT_MIME_DESCRIPTION, 1))
        return NS_OK;
    }

    // if we are here that means we have an alien plugin
    // which wants to take over "*" type

    // change its "*" mime type to "[*]"
    PL_strfree(aInfo.fMimeTypeArray[i]);
    aInfo.fMimeTypeArray[i] = PL_strdup("[*]");

    // continue the loop?
  }
  return NS_OK;
}

nsresult nsPluginHost::ScanPluginsDirectory(nsIFile * pluginsDir,
                                            nsIComponentManager * compManager,
                                            PRBool aCreatePluginList,
                                            PRBool * aPluginsChanged,
                                            PRBool checkForUnwantedPlugins)
{
  NS_ENSURE_ARG_POINTER(aPluginsChanged);
  nsresult rv;

  *aPluginsChanged = PR_FALSE;

#ifdef PLUGIN_LOGGING
  nsCAutoString dirPath;
  pluginsDir->GetNativePath(dirPath);
  PLUGIN_LOG(PLUGIN_LOG_BASIC,
  ("nsPluginHost::ScanPluginsDirectory dir=%s\n", dirPath.get()));
#endif

  // If pluginsDir is NS_APP_PLUGINS_DIR (inside the app bundle) we need to
  // handle the call to RemoveCachedPluginsInfo() (below) differently -- we
  // need to make it search by file name and not file path.  Otherwise it
  // won't find settings changes for bundled plugins made by other copies of
  // the browser -- otherwise (for example) disabling the JEP in one FF app
  // bundle (say FF 3.5.X) won't disable it in another FF app bundle (say
  // FF 3.6).
  PRBool isAppPluginsDir = PR_FALSE;
  nsCOMPtr<nsIFile> appPluginsDir;
  rv = NS_GetSpecialDirectory(NS_APP_PLUGINS_DIR, getter_AddRefs(appPluginsDir));
  if (NS_SUCCEEDED(rv)) {
    rv = appPluginsDir->Equals(pluginsDir, &isAppPluginsDir);
    if (NS_FAILED(rv))
      isAppPluginsDir = PR_FALSE;
  }

  nsCOMPtr<nsISimpleEnumerator> iter;
  rv = pluginsDir->GetDirectoryEntries(getter_AddRefs(iter));
  if (NS_FAILED(rv))
    return rv;

  // Collect all the files in this directory in an array we can sort later
  nsAutoTArray<pluginFileinDirectory, 8> pluginFilesArray;

  PRBool hasMore;
  while (NS_SUCCEEDED(iter->HasMoreElements(&hasMore)) && hasMore) {
    nsCOMPtr<nsISupports> supports;
    rv = iter->GetNext(getter_AddRefs(supports));
    if (NS_FAILED(rv))
      continue;
    nsCOMPtr<nsILocalFile> dirEntry(do_QueryInterface(supports, &rv));
    if (NS_FAILED(rv))
      continue;

    // Sun's JRE 1.3.1 plugin must have symbolic links resolved or else it'll crash.
    // See bug 197855.
    dirEntry->Normalize();

    nsAutoString filePath;
    rv = dirEntry->GetPath(filePath);
    if (NS_FAILED(rv))
      continue;

    nsAutoString fileName;
    rv = dirEntry->GetLeafName(fileName);
    if (NS_FAILED(rv))
      continue;

    if (nsPluginsDir::IsPluginFile(dirEntry)) {
      pluginFileinDirectory * item = pluginFilesArray.AppendElement();
      if (!item)
        return NS_ERROR_OUT_OF_MEMORY;

      // Get file mod time
      PRInt64 fileModTime = LL_ZERO;
      dirEntry->GetLastModifiedTime(&fileModTime);

      item->mModTime = fileModTime;
      item->mFilePath = filePath;
      item->mFileName = fileName;
    }
  } // end round of up of plugin files

  // now sort the array by file modification time or by filename, if equal
  // put newer plugins first to weed out dups and catch upgrades, see bug 119966
  pluginFilesArray.Sort();

  PRBool warnOutdated = PR_FALSE;

  // finally, go through the array, looking at each entry and continue processing it
  for (PRUint32 i = 0; i < pluginFilesArray.Length(); i++) {
    pluginFileinDirectory &pfd = pluginFilesArray[i];
    nsCOMPtr <nsIFile> file = do_CreateInstance("@mozilla.org/file/local;1");
    nsCOMPtr <nsILocalFile> localfile = do_QueryInterface(file);
    localfile->InitWithPath(pfd.mFilePath);
    PRInt64 fileModTime = pfd.mModTime;

    // Look for it in our cache
    nsRefPtr<nsPluginTag> pluginTag;
    nsCAutoString fileSpec;
    if (isAppPluginsDir) {
      AppendUTF16toUTF8(pfd.mFileName, fileSpec);
    } else {
      AppendUTF16toUTF8(pfd.mFilePath, fileSpec);
    }
    RemoveCachedPluginsInfo(fileSpec.get(), isAppPluginsDir,
                            getter_AddRefs(pluginTag));

    PRBool enabled = PR_TRUE;
    PRBool seenBefore = PR_FALSE;
    if (pluginTag) {
      seenBefore = PR_TRUE;
      // If plugin changed, delete cachedPluginTag and don't use cache
      if (LL_NE(fileModTime, pluginTag->mLastModifiedTime)) {
        // Plugins has changed. Don't use cached plugin info.
        enabled = (pluginTag->Flags() & NS_PLUGIN_FLAG_ENABLED) != 0;
        pluginTag = nsnull;

        // plugin file changed, flag this fact
        *aPluginsChanged = PR_TRUE;
      }
      else {
        // if it is unwanted plugin we are checking for, get it back to the cache info list
        // if this is a duplicate plugin, too place it back in the cache info list marking unwantedness
        if ((checkForUnwantedPlugins && isUnwantedPlugin(pluginTag)) ||
           IsDuplicatePlugin(pluginTag)) {
          if (!pluginTag->HasFlag(NS_PLUGIN_FLAG_UNWANTED)) {
            // Plugin switched from wanted to unwanted
            *aPluginsChanged = PR_TRUE;
          }
          pluginTag->Mark(NS_PLUGIN_FLAG_UNWANTED);
          pluginTag->mNext = mCachedPlugins;
          mCachedPlugins = pluginTag;
        } else if (pluginTag->HasFlag(NS_PLUGIN_FLAG_UNWANTED)) {
          pluginTag->UnMark(NS_PLUGIN_FLAG_UNWANTED);
          // Plugin switched from unwanted to wanted
          *aPluginsChanged = PR_TRUE;
        }
      }
      if (!aCreatePluginList) {
        if (*aPluginsChanged)
          return NS_OK;
        continue;
      }
    }
    
    // if it is not found in cache info list or has been changed, create a new one
    if (!pluginTag) {
      nsPluginFile pluginFile(file);
      PRLibrary* pluginLibrary = nsnull;

      // load the plugin's library so we can ask it some questions, but not for Windows
#ifndef XP_WIN
      if (pluginFile.LoadPlugin(pluginLibrary) != NS_OK || pluginLibrary == nsnull)
        continue;
#endif

      // create a tag describing this plugin.
      nsPluginInfo info;
      memset(&info, 0, sizeof(info));
      nsresult res = pluginFile.GetPluginInfo(info);
      // if we don't have mime type don't proceed, this is not a plugin
      if (NS_FAILED(res) || !info.fMimeTypeArray) {
        pluginFile.FreePluginInfo(info);
        continue;
      }

      // Check for any potential '*' mime type handlers which are not our
      // own default plugin and disable them as they will break the plugin
      // finder service, see Bugzilla bug 132430
      if (!mAllowAlienStarHandler)
        FixUpPluginInfo(info, pluginFile);

      pluginTag = new nsPluginTag(&info);
      pluginFile.FreePluginInfo(info);

      if (pluginTag == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;

      pluginTag->mLibrary = pluginLibrary;
      pluginTag->mLastModifiedTime = fileModTime;

      nsCOMPtr<nsIBlocklistService> blocklist = do_GetService("@mozilla.org/extensions/blocklist;1");
      if (blocklist) {
        PRUint32 state;
        rv = blocklist->GetPluginBlocklistState(pluginTag, EmptyString(),
                                                EmptyString(), &state);

        if (NS_SUCCEEDED(rv)) {
          // If the blocklist says so then block the plugin. If the blocklist says
          // it is risky and we have never seen this plugin before then disable it
          if (state == nsIBlocklistService::STATE_BLOCKED)
            pluginTag->Mark(NS_PLUGIN_FLAG_BLOCKLISTED);
          else if (state == nsIBlocklistService::STATE_SOFTBLOCKED && !seenBefore)
            enabled = PR_FALSE;
          else if (state == nsIBlocklistService::STATE_OUTDATED && !seenBefore)
            warnOutdated = PR_TRUE;
        }
      }

      if (!enabled)
        pluginTag->UnMark(NS_PLUGIN_FLAG_ENABLED);

      // if this is unwanted plugin we are checkin for, or this is a duplicate plugin,
      // add it to our cache info list so we can cache the unwantedness of this plugin
      // when we sync cached plugins to registry
      NS_ASSERTION(!pluginTag->HasFlag(NS_PLUGIN_FLAG_UNWANTED),
                   "Brand-new tags should not be unwanted");
      if ((checkForUnwantedPlugins && isUnwantedPlugin(pluginTag)) ||
         IsDuplicatePlugin(pluginTag)) {
        pluginTag->Mark(NS_PLUGIN_FLAG_UNWANTED);
        pluginTag->mNext = mCachedPlugins;
        mCachedPlugins = pluginTag;
      }
    }

    // set the flag that we want to add this plugin to the list for now
    // and see if it remains after we check several reasons not to do so
    PRBool bAddIt = PR_TRUE;

    // check if this is a specific plugin we don't want
    if (checkForUnwantedPlugins && isUnwantedPlugin(pluginTag))
      bAddIt = PR_FALSE;

    // check if we already have this plugin in the list which
    // is possible if we do refresh
    if (bAddIt) {
      if (HaveSamePlugin(pluginTag)) {
        // we cannot get here if the plugin has just been added
        // and thus |pluginTag| is not from cache, because otherwise
        // it would not be present in the list;
        // so there is no need to delete |pluginTag| -- it _is_ from the cache info list.
        bAddIt = PR_FALSE;
      }
    }

    // do it if we still want it
    if (bAddIt) {
      // We have a valid new plugin so report that plugins have changed.
      *aPluginsChanged = PR_TRUE;
      
      // If we're not creating a plugin list, simply looking for changes,
      // then we're done.
      if (!aCreatePluginList) {
        return NS_OK;
      }
      pluginTag->SetHost(this);
      pluginTag->mNext = mPlugins;
      mPlugins = pluginTag;

      if (pluginTag->IsEnabled())
        pluginTag->RegisterWithCategoryManager(mOverrideInternalTypes);
    }
    else if (!pluginTag->HasFlag(NS_PLUGIN_FLAG_UNWANTED)) {
      // we don't need it, delete it;
      // but don't delete unwanted plugins since they are cached
      // in the cache info list and will be deleted later
      pluginTag = nsnull;
    }
  }
  
  if (warnOutdated)
    mPrefService->SetBoolPref("plugins.update.notifyUser", PR_TRUE);

  return NS_OK;
}

nsresult nsPluginHost::ScanPluginsDirectoryList(nsISimpleEnumerator * dirEnum,
                                                nsIComponentManager * compManager,
                                                PRBool aCreatePluginList,
                                                PRBool * aPluginsChanged,
                                                PRBool checkForUnwantedPlugins)
{
    PRBool hasMore;
    while (NS_SUCCEEDED(dirEnum->HasMoreElements(&hasMore)) && hasMore) {
      nsCOMPtr<nsISupports> supports;
      nsresult rv = dirEnum->GetNext(getter_AddRefs(supports));
      if (NS_FAILED(rv))
        continue;
      nsCOMPtr<nsIFile> nextDir(do_QueryInterface(supports, &rv));
      if (NS_FAILED(rv))
        continue;

      // don't pass aPluginsChanged directly to prevent it from been reset
      PRBool pluginschanged = PR_FALSE;
      ScanPluginsDirectory(nextDir, compManager, aCreatePluginList, &pluginschanged, checkForUnwantedPlugins);

      if (pluginschanged)
        *aPluginsChanged = PR_TRUE;

      // if changes are detected and we are not creating the list, do not proceed
      if (!aCreatePluginList && *aPluginsChanged)
        break;
    }
    return NS_OK;
}

NS_IMETHODIMP nsPluginHost::LoadPlugins()
{
  // do not do anything if it is already done
  // use ReloadPlugins() to enforce loading
  if (mPluginsLoaded)
    return NS_OK;

  if (mPluginsDisabled)
    return NS_OK;

  PRBool pluginschanged;
  nsresult rv = FindPlugins(PR_TRUE, &pluginschanged);
  if (NS_FAILED(rv))
    return rv;

  // only if plugins have changed will we notify plugin-change observers
  if (pluginschanged) {
    nsCOMPtr<nsIObserverService>
      obsService(do_GetService("@mozilla.org/observer-service;1"));
    if (obsService)
      obsService->NotifyObservers(nsnull, "plugins-list-updated", nsnull);
  }

  return NS_OK;
}

#include "nsITimelineService.h"

// if aCreatePluginList is false we will just scan for plugins
// and see if any changes have been made to the plugins.
// This is needed in ReloadPlugins to prevent possible recursive reloads
nsresult nsPluginHost::FindPlugins(PRBool aCreatePluginList, PRBool * aPluginsChanged)
{
  // let's start timing if we are only really creating the plugin list
  if (aCreatePluginList) {
    NS_TIMELINE_START_TIMER("LoadPlugins");
  }

#ifdef CALL_SAFETY_ON
  // check preferences on whether or not we want to try safe calls to plugins
  NS_INIT_PLUGIN_SAFE_CALLS;
#endif

  NS_ENSURE_ARG_POINTER(aPluginsChanged);

  *aPluginsChanged = PR_FALSE;
  nsresult rv;

  // Read cached plugins info. If the profile isn't yet available then don't
  // scan for plugins
  if (ReadPluginInfo() == NS_ERROR_NOT_AVAILABLE)
    return NS_OK;

  nsCOMPtr<nsIComponentManager> compManager;
  NS_GetComponentManager(getter_AddRefs(compManager));

#ifdef XP_WIN
  // Failure here is not a show-stopper so just warn.
  rv = EnsurePrivateDirServiceProvider();
  NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to register dir service provider.");
#endif /* XP_WIN */

  nsCOMPtr<nsIProperties> dirService(do_GetService(kDirectoryServiceContractID, &rv));
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsISimpleEnumerator> dirList;

  // Scan plugins directories;
  // don't pass aPluginsChanged directly, to prevent its
  // possible reset in subsequent ScanPluginsDirectory calls
  PRBool pluginschanged = PR_FALSE;

  // Scan the app-defined list of plugin dirs.
  rv = dirService->Get(NS_APP_PLUGINS_DIR_LIST, NS_GET_IID(nsISimpleEnumerator), getter_AddRefs(dirList));
  if (NS_SUCCEEDED(rv)) {
    ScanPluginsDirectoryList(dirList, compManager, aCreatePluginList, &pluginschanged);

    if (pluginschanged)
      *aPluginsChanged = PR_TRUE;

    // if we are just looking for possible changes,
    // no need to proceed if changes are detected
    if (!aCreatePluginList && *aPluginsChanged) {
      mCachedPlugins = nsnull;
      return NS_OK;
    }
  }

  mPluginsLoaded = PR_TRUE; // at this point 'some' plugins have been loaded,
                            // the rest is optional

#ifdef XP_WIN
  PRBool bScanPLIDs = PR_FALSE;

  if (mPrefService)
    mPrefService->GetBoolPref("plugin.scan.plid.all", &bScanPLIDs);

    // Now lets scan any PLID directories
  if (bScanPLIDs && mPrivateDirServiceProvider) {
    rv = mPrivateDirServiceProvider->GetPLIDDirectories(getter_AddRefs(dirList));
    if (NS_SUCCEEDED(rv)) {
      ScanPluginsDirectoryList(dirList, compManager, aCreatePluginList, &pluginschanged);

      if (pluginschanged)
        *aPluginsChanged = PR_TRUE;

      // if we are just looking for possible changes,
      // no need to proceed if changes are detected
      if (!aCreatePluginList && *aPluginsChanged) {
        mCachedPlugins = nsnull;
        return NS_OK;
      }
    }
  }


  // Scan the installation paths of our popular plugins if the prefs are enabled

  // This table controls the order of scanning
  const char* const prefs[] = {NS_WIN_JRE_SCAN_KEY,         nsnull,
                               NS_WIN_ACROBAT_SCAN_KEY,     nsnull,
                               NS_WIN_QUICKTIME_SCAN_KEY,   nsnull,
                               NS_WIN_WMP_SCAN_KEY,         nsnull,
                               NS_WIN_4DOTX_SCAN_KEY,       "1"  /*  second column is flag for 4.x folder */ };

  PRUint32 size = sizeof(prefs) / sizeof(prefs[0]);

  for (PRUint32 i = 0; i < size; i+=2) {
    nsCOMPtr<nsIFile> dirToScan;
    PRBool bExists;
    if (NS_SUCCEEDED(dirService->Get(prefs[i], NS_GET_IID(nsIFile), getter_AddRefs(dirToScan))) &&
        dirToScan &&
        NS_SUCCEEDED(dirToScan->Exists(&bExists)) &&
        bExists) {

      PRBool bFilterUnwanted = PR_FALSE;

      // 4.x plugins folder stuff:
      // Normally we "filter" the 4.x folder through |IsUnwantedPlugin|
      // Check for a pref to see if we want to scan the entire 4.x plugins folder
      if (prefs[i+1]) {
        PRBool bScanEverything;
        bFilterUnwanted = PR_TRUE;  // default to filter 4.x folder
        if (mPrefService &&
            NS_SUCCEEDED(mPrefService->GetBoolPref(prefs[i], &bScanEverything)) &&
            bScanEverything)
          bFilterUnwanted = PR_FALSE;

      }
      ScanPluginsDirectory(dirToScan, compManager, aCreatePluginList, &pluginschanged, bFilterUnwanted);

      if (pluginschanged)
        *aPluginsChanged = PR_TRUE;

      // if we are just looking for possible changes,
      // no need to proceed if changes are detected
      if (!aCreatePluginList && *aPluginsChanged) {
        mCachedPlugins = nsnull;
        return NS_OK;
      }
    }
  }
#endif

  // if get to this point and did not detect changes in plugins
  // that means no plugins got updated or added
  // let's see if plugins have been removed
  if (!*aPluginsChanged) {
    // count plugins remained in cache, if there are some, that means some plugins were removed;
    // while counting, we should ignore unwanted plugins which are also present in cache
    PRUint32 cachecount = 0;
    for (nsPluginTag * cachetag = mCachedPlugins; cachetag; cachetag = cachetag->mNext) {
      if (!cachetag->HasFlag(NS_PLUGIN_FLAG_UNWANTED))
        cachecount++;
    }
    // if there is something left in cache, some plugins got removed from the directory
    // and therefor their info did not get removed from the cache info list during directory scan;
    // flag this fact
    if (cachecount > 0)
      *aPluginsChanged = PR_TRUE;
  }

  // if we are not creating the list, there is no need to proceed
  if (!aCreatePluginList) {
    mCachedPlugins = nsnull;
    return NS_OK;
  }

  // if we are creating the list, it is already done;
  // update the plugins info cache if changes are detected
  if (*aPluginsChanged)
    WritePluginInfo();

  // No more need for cached plugins. Clear it up.
  mCachedPlugins = nsnull;

  // reverse our list of plugins
  nsRefPtr<nsPluginTag> next;
  nsRefPtr<nsPluginTag> prev;
  for (nsRefPtr<nsPluginTag> cur = mPlugins; cur; cur = next) {
    next = cur->mNext;
    cur->mNext = prev;
    prev = cur;
  }

  mPlugins = prev;

  NS_TIMELINE_STOP_TIMER("LoadPlugins");
  NS_TIMELINE_MARK_TIMER("LoadPlugins");

  return NS_OK;
}

nsresult
nsPluginHost::UpdatePluginInfo(nsPluginTag* aPluginTag)
{
  ReadPluginInfo();
  WritePluginInfo();
  mCachedPlugins = nsnull;

  if (!aPluginTag || aPluginTag->IsEnabled())
    return NS_OK;

  nsCOMPtr<nsISupportsArray> instsToReload;
  NS_NewISupportsArray(getter_AddRefs(instsToReload));
  mPluginInstanceTagList.stopRunning(instsToReload, aPluginTag);
  mPluginInstanceTagList.removeAllStopped();
  
  PRUint32 c;
  if (instsToReload && NS_SUCCEEDED(instsToReload->Count(&c)) && c > 0) {
    nsCOMPtr<nsIRunnable> ev = new nsPluginDocReframeEvent(instsToReload);
    if (ev)
      NS_DispatchToCurrentThread(ev);
  }

  return NS_OK;
}

nsresult
nsPluginHost::WritePluginInfo()
{

  nsresult rv = NS_OK;
  nsCOMPtr<nsIProperties> directoryService(do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID,&rv));
  if (NS_FAILED(rv))
    return rv;

  directoryService->Get(NS_APP_USER_PROFILE_50_DIR, NS_GET_IID(nsIFile),
                        getter_AddRefs(mPluginRegFile));

  if (!mPluginRegFile)
    return NS_ERROR_FAILURE;

  PRFileDesc* fd = nsnull;

  nsCOMPtr<nsIFile> pluginReg;

  rv = mPluginRegFile->Clone(getter_AddRefs(pluginReg));
  if (NS_FAILED(rv))
    return rv;

  rv = pluginReg->AppendNative(kPluginRegistryFilename);
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsILocalFile> localFile = do_QueryInterface(pluginReg, &rv);
  if (NS_FAILED(rv))
    return rv;

  rv = localFile->OpenNSPRFileDesc(PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE, 0600, &fd);
  if (NS_FAILED(rv))
    return rv;

  PR_fprintf(fd, "Generated File. Do not edit.\n");

  PR_fprintf(fd, "\n[HEADER]\nVersion%c%s%c%c\n",
             PLUGIN_REGISTRY_FIELD_DELIMITER,
             kPluginRegistryVersion,
             PLUGIN_REGISTRY_FIELD_DELIMITER,
             PLUGIN_REGISTRY_END_OF_LINE_MARKER);

  // Store all plugins in the mPlugins list - all plugins currently in use.
  PR_fprintf(fd, "\n[PLUGINS]\n");

  nsPluginTag *taglist[] = {mPlugins, mCachedPlugins};
  for (int i=0; i<(int)(sizeof(taglist)/sizeof(nsPluginTag *)); i++) {
    for (nsPluginTag *tag = taglist[i]; tag; tag=tag->mNext) {
      // from mCachedPlugins list write down only unwanted plugins
      if ((taglist[i] == mCachedPlugins) && !tag->HasFlag(NS_PLUGIN_FLAG_UNWANTED))
        continue;
      // store each plugin info into the registry
      // filename & fullpath are on separate line
      // because they can contain field delimiter char
      PR_fprintf(fd, "%s%c%c\n%s%c%c\n%s%c%c\n",
        (!tag->mFileName.IsEmpty() ? tag->mFileName.get() : ""),
        PLUGIN_REGISTRY_FIELD_DELIMITER,
        PLUGIN_REGISTRY_END_OF_LINE_MARKER,
        (!tag->mFullPath.IsEmpty() ? tag->mFullPath.get() : ""),
        PLUGIN_REGISTRY_FIELD_DELIMITER,
        PLUGIN_REGISTRY_END_OF_LINE_MARKER,
        (!tag->mVersion.IsEmpty() ? tag->mVersion.get() : ""),
        PLUGIN_REGISTRY_FIELD_DELIMITER,
        PLUGIN_REGISTRY_END_OF_LINE_MARKER);

      // lastModifiedTimeStamp|canUnload|tag->mFlags
      PR_fprintf(fd, "%lld%c%d%c%lu%c%c\n",
        tag->mLastModifiedTime,
        PLUGIN_REGISTRY_FIELD_DELIMITER,
        tag->mCanUnloadLibrary,
        PLUGIN_REGISTRY_FIELD_DELIMITER,
        tag->Flags(),
        PLUGIN_REGISTRY_FIELD_DELIMITER,
        PLUGIN_REGISTRY_END_OF_LINE_MARKER);

      //description, name & mtypecount are on separate line
      PR_fprintf(fd, "%s%c%c\n%s%c%c\n%d\n",
        (!tag->mDescription.IsEmpty() ? tag->mDescription.get() : ""),
        PLUGIN_REGISTRY_FIELD_DELIMITER,
        PLUGIN_REGISTRY_END_OF_LINE_MARKER,
        (!tag->mName.IsEmpty() ? tag->mName.get() : ""),
        PLUGIN_REGISTRY_FIELD_DELIMITER,
        PLUGIN_REGISTRY_END_OF_LINE_MARKER,
        tag->mVariants + (tag->mIsNPRuntimeEnabledJavaPlugin ? 1 : 0));

      // Add in each mimetype this plugin supports
      for (int i=0; i<tag->mVariants; i++) {
        PR_fprintf(fd, "%d%c%s%c%s%c%s%c%c\n",
          i,PLUGIN_REGISTRY_FIELD_DELIMITER,
          (tag->mMimeTypeArray && tag->mMimeTypeArray[i] ? tag->mMimeTypeArray[i] : ""),
          PLUGIN_REGISTRY_FIELD_DELIMITER,
          (!tag->mMimeDescriptionArray[i].IsEmpty() ? tag->mMimeDescriptionArray[i].get() : ""),
          PLUGIN_REGISTRY_FIELD_DELIMITER,
          (tag->mExtensionsArray && tag->mExtensionsArray[i] ? tag->mExtensionsArray[i] : ""),
          PLUGIN_REGISTRY_FIELD_DELIMITER,
          PLUGIN_REGISTRY_END_OF_LINE_MARKER);
      }

      if (tag->mIsNPRuntimeEnabledJavaPlugin) {
        PR_fprintf(fd, "%d%c%s%c%s%c%s%c%c\n",
          tag->mVariants, PLUGIN_REGISTRY_FIELD_DELIMITER,
          "application/x-java-vm-npruntime",
          PLUGIN_REGISTRY_FIELD_DELIMITER,
          "",
          PLUGIN_REGISTRY_FIELD_DELIMITER,
          "",
          PLUGIN_REGISTRY_FIELD_DELIMITER,
          PLUGIN_REGISTRY_END_OF_LINE_MARKER);
      }
    }
  }

  if (fd)
    PR_Close(fd);
  return NS_OK;
}

#define PLUGIN_REG_MIMETYPES_ARRAY_SIZE 12
nsresult
nsPluginHost::ReadPluginInfo()
{
  nsresult rv;

  nsCOMPtr<nsIProperties> directoryService(do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID,&rv));
  if (NS_FAILED(rv))
    return rv;

  directoryService->Get(NS_APP_USER_PROFILE_50_DIR, NS_GET_IID(nsIFile),
                        getter_AddRefs(mPluginRegFile));

  if (!mPluginRegFile) {
    // There is no profile yet, this will tell us if there is going to be one
    // in the future.
    directoryService->Get(NS_APP_PROFILE_DIR_STARTUP, NS_GET_IID(nsIFile),
                          getter_AddRefs(mPluginRegFile));
    if (!mPluginRegFile)
      return NS_ERROR_FAILURE;
    else
      return NS_ERROR_NOT_AVAILABLE;
  }

  PRFileDesc* fd = nsnull;

  nsCOMPtr<nsIFile> pluginReg;

  rv = mPluginRegFile->Clone(getter_AddRefs(pluginReg));
  if (NS_FAILED(rv))
    return rv;

  rv = pluginReg->AppendNative(kPluginRegistryFilename);
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsILocalFile> localFile = do_QueryInterface(pluginReg, &rv);
  if (NS_FAILED(rv))
    return rv;

  PRInt64 fileSize;
  rv = localFile->GetFileSize(&fileSize);
  if (NS_FAILED(rv))
    return rv;

  PRInt32 flen = nsInt64(fileSize);
  if (flen == 0) {
    NS_WARNING("Plugins Registry Empty!");
    return NS_OK; // ERROR CONDITION
  }

  nsPluginManifestLineReader reader;
  char* registry = reader.Init(flen);
  if (!registry)
    return NS_ERROR_OUT_OF_MEMORY;

  rv = localFile->OpenNSPRFileDesc(PR_RDONLY, 0444, &fd);
  if (NS_FAILED(rv))
    return rv;

  // set rv to return an error on goto out
  rv = NS_ERROR_FAILURE;

  PRInt32 bread = PR_Read(fd, registry, flen);
  PR_Close(fd);

  if (flen > bread)
    return rv;

  if (!ReadSectionHeader(reader, "HEADER"))
    return rv;;

  if (!reader.NextLine())
    return rv;

  char* values[6];

  // VersionLiteral, kPluginRegistryVersion
  if (2 != reader.ParseLine(values, 2))
    return rv;

  // VersionLiteral
  if (PL_strcmp(values[0], "Version"))
    return rv;

  // kPluginRegistryVersion
  PRInt32 vdiff = NS_CompareVersions(values[1], kPluginRegistryVersion);
  // If this is a registry from some future version then don't attempt to read it
  if (vdiff > 0)
    return rv;
  // If this is a registry from before the minimum then don't attempt to read it
  if (NS_CompareVersions(values[1], kMinimumRegistryVersion) < 0)
    return rv;

  // Registry v0.10 and upwards includes the plugin version field
  PRBool regHasVersion = NS_CompareVersions(values[1], "0.10") >= 0;

  if (!ReadSectionHeader(reader, "PLUGINS"))
    return rv;

#if defined(XP_MACOSX)
  PRBool hasFullPathInFileNameField = PR_FALSE;
#else
  PRBool hasFullPathInFileNameField = (NS_CompareVersions(values[1], "0.11") < 0);
#endif

  while (reader.NextLine()) {
    const char *filename;
    const char *fullpath;
    nsCAutoString derivedFileName;
    
    if (hasFullPathInFileNameField) {
      fullpath = reader.LinePtr();
      if (!reader.NextLine())
        return rv;
      // try to derive a file name from the full path
      if (fullpath) {
        nsCOMPtr<nsILocalFile> file = do_CreateInstance("@mozilla.org/file/local;1");
        file->InitWithNativePath(nsDependentCString(fullpath));
        file->GetNativeLeafName(derivedFileName);
        filename = derivedFileName.get();
      } else {
        filename = NULL;
      }

      // skip the next line, useless in this version
      if (!reader.NextLine())
        return rv;
    } else {
      filename = reader.LinePtr();
      if (!reader.NextLine())
        return rv;

      fullpath = reader.LinePtr();
      if (!reader.NextLine())
        return rv;
    }

    const char *version;
    if (regHasVersion) {
      version = reader.LinePtr();
      if (!reader.NextLine())
        return rv;
    } else {
      version = "0";
    }

    // lastModifiedTimeStamp|canUnload|tag.mFlag
    if (reader.ParseLine(values, 3) != 3)
      return rv;

    // If this is an old plugin registry mark this plugin tag to be refreshed
    PRInt64 lastmod = (vdiff == 0) ? nsCRT::atoll(values[0]) : -1;
    PRBool canunload = atoi(values[1]);
    PRUint32 tagflag = atoi(values[2]);
    if (!reader.NextLine())
      return rv;

    const char *description = reader.LinePtr();
    if (!reader.NextLine())
      return rv;

    const char *name = reader.LinePtr();
    if (!reader.NextLine())
      return rv;

    int mimetypecount = atoi(reader.LinePtr());

    char *stackalloced[PLUGIN_REG_MIMETYPES_ARRAY_SIZE * 3];
    char **mimetypes;
    char **mimedescriptions;
    char **extensions;
    char **heapalloced = 0;
    if (mimetypecount > PLUGIN_REG_MIMETYPES_ARRAY_SIZE - 1) {
      heapalloced = new char *[mimetypecount * 3];
      mimetypes = heapalloced;
    } else {
      mimetypes = stackalloced;
    }
    mimedescriptions = mimetypes + mimetypecount;
    extensions = mimedescriptions + mimetypecount;

    int mtr = 0; //mimetype read
    for (; mtr < mimetypecount; mtr++) {
      if (!reader.NextLine())
        break;

      //line number|mimetype|description|extension
      if (4 != reader.ParseLine(values, 4))
        break;
      int line = atoi(values[0]);
      if (line != mtr)
        break;
      mimetypes[mtr] = values[1];
      mimedescriptions[mtr] = values[2];
      extensions[mtr] = values[3];
    }

    if (mtr != mimetypecount) {
      if (heapalloced) {
        delete [] heapalloced;
      }
      return rv;
    }

    nsRefPtr<nsPluginTag> tag = new nsPluginTag(name,
      description,
      filename,
      fullpath,
      version,
      (const char* const*)mimetypes,
      (const char* const*)mimedescriptions,
      (const char* const*)extensions,
      mimetypecount, lastmod, canunload, PR_TRUE);
    if (heapalloced)
      delete [] heapalloced;

    if (!tag)
      continue;

    // Mark plugin as loaded from cache
    tag->Mark(tagflag | NS_PLUGIN_FLAG_FROMCACHE);
    PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_BASIC,
      ("LoadCachedPluginsInfo : Loading Cached plugininfo for %s\n", tag->mFileName.get()));
    tag->mNext = mCachedPlugins;
    mCachedPlugins = tag;

  }
  return NS_OK;
}

void
nsPluginHost::RemoveCachedPluginsInfo(const char *fileSpec, PRBool byFileName,
                                      nsPluginTag **result)
{
  nsRefPtr<nsPluginTag> prev;
  nsRefPtr<nsPluginTag> tag = mCachedPlugins;
  while (tag)
  {
    PRBool found;
    if (byFileName) {
      found = tag->mFileName.Equals(fileSpec);
    } else {
      found = tag->mFullPath.Equals(fileSpec);
    }
    if (found) {
      // Found it. Remove it from our list
      if (prev)
        prev->mNext = tag->mNext;
      else
        mCachedPlugins = tag->mNext;
      tag->mNext = nsnull;
      *result = tag;
      NS_ADDREF(*result);
      break;
    }
    prev = tag;
    tag = tag->mNext;
  }
}

#ifdef XP_WIN
nsresult
nsPluginHost::EnsurePrivateDirServiceProvider()
{
  if (!mPrivateDirServiceProvider) {
    nsresult rv;
    mPrivateDirServiceProvider = new nsPluginDirServiceProvider();
    if (!mPrivateDirServiceProvider)
      return NS_ERROR_OUT_OF_MEMORY;
    nsCOMPtr<nsIDirectoryService> dirService(do_GetService(kDirectoryServiceContractID, &rv));
    if (NS_FAILED(rv))
      return rv;
    rv = dirService->RegisterProvider(mPrivateDirServiceProvider);
    if (NS_FAILED(rv))
      return rv;
  }
  return NS_OK;
}
#endif /* XP_WIN */

nsresult nsPluginHost::NewPluginURLStream(const nsString& aURL,
                                          nsIPluginInstance *aInstance,
                                          nsIPluginStreamListener* aListener,
                                          const char *aPostData,
                                          PRBool aIsFile,
                                          PRUint32 aPostDataLen,
                                          const char *aHeadersData,
                                          PRUint32 aHeadersDataLen)
{
  nsCOMPtr<nsIURI> url;
  nsAutoString absUrl;
  nsresult rv;

  if (aURL.Length() <= 0)
    return NS_OK;

  // get the full URL of the document that the plugin is embedded
  //   in to create an absolute url in case aURL is relative
  nsCOMPtr<nsIDocument> doc;
  nsCOMPtr<nsIPluginInstanceOwner> owner;
  aInstance->GetOwner(getter_AddRefs(owner));
  if (owner) {
    rv = owner->GetDocument(getter_AddRefs(doc));
    if (NS_SUCCEEDED(rv) && doc) {
      // Create an absolute URL
      rv = NS_MakeAbsoluteURI(absUrl, aURL, doc->GetBaseURI());
    }
  }

  if (absUrl.IsEmpty())
    absUrl.Assign(aURL);

  rv = NS_NewURI(getter_AddRefs(url), absUrl);

  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIPluginTagInfo> pti = do_QueryInterface(owner);
    nsCOMPtr<nsIDOMElement> element;
    if (pti)
      pti->GetDOMElement(getter_AddRefs(element));

    PRInt16 shouldLoad = nsIContentPolicy::ACCEPT;
    rv = NS_CheckContentLoadPolicy(nsIContentPolicy::TYPE_OBJECT_SUBREQUEST,
                                   url,
                                   (doc ? doc->NodePrincipal() : nsnull),
                                   element,
                                   EmptyCString(), //mime guess
                                   nsnull,         //extra
                                   &shouldLoad);
    if (NS_FAILED(rv)) return rv;
    if (NS_CP_REJECTED(shouldLoad)) {
      // Disallowed by content policy
      return NS_ERROR_CONTENT_BLOCKED;
    }

    nsRefPtr<nsPluginStreamListenerPeer> listenerPeer = new nsPluginStreamListenerPeer();
    if (listenerPeer == NULL)
      return NS_ERROR_OUT_OF_MEMORY;

    rv = listenerPeer->Initialize(url, aInstance, aListener);

    if (NS_SUCCEEDED(rv)) {
      nsCOMPtr<nsIChannel> channel;
      rv = NS_NewChannel(getter_AddRefs(channel), url, nsnull,
        nsnull, /* do not add this internal plugin's channel
                on the load group otherwise this channel could be canceled
                form |nsDocShell::OnLinkClickSync| bug 166613 */
        listenerPeer);
      if (NS_FAILED(rv))
        return rv;

      if (doc) {
        // Set the owner of channel to the document principal...
        channel->SetOwner(doc->NodePrincipal());

        // And if it's a script allow it to execute against the
        // document's script context.
        nsCOMPtr<nsIScriptChannel> scriptChannel(do_QueryInterface(channel));
        if (scriptChannel) {
          scriptChannel->SetExecutionPolicy(nsIScriptChannel::EXECUTE_NORMAL);
          // Plug-ins seem to depend on javascript: URIs running synchronously
          scriptChannel->SetExecuteAsync(PR_FALSE);
        }
      }

      // deal with headers and post data
      nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel));
      if (httpChannel) {
        if (aPostData) {

          nsCOMPtr<nsIInputStream> postDataStream;
          rv = NS_NewPluginPostDataStream(getter_AddRefs(postDataStream), (const char*)aPostData,
                                          aPostDataLen, aIsFile);

          if (!postDataStream) {
            NS_RELEASE(aInstance);
            return NS_ERROR_UNEXPECTED;
          }

          // XXX it's a bit of a hack to rewind the postdata stream
          // here but it has to be done in case the post data is
          // being reused multiple times.
          nsCOMPtr<nsISeekableStream>
          postDataSeekable(do_QueryInterface(postDataStream));
          if (postDataSeekable)
            postDataSeekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);

          nsCOMPtr<nsIUploadChannel> uploadChannel(do_QueryInterface(httpChannel));
          NS_ASSERTION(uploadChannel, "http must support nsIUploadChannel");

          uploadChannel->SetUploadStream(postDataStream, EmptyCString(), -1);
        }

        if (aHeadersData)
          rv = AddHeadersToChannel(aHeadersData, aHeadersDataLen, httpChannel);
      }
      rv = channel->AsyncOpen(listenerPeer, nsnull);
    }
  }
  return rv;
}

// Called by GetURL and PostURL
nsresult
nsPluginHost::DoURLLoadSecurityCheck(nsIPluginInstance *aInstance,
                                         const char* aURL)
{
  if (!aURL || *aURL == '\0')
    return NS_OK;

  // get the URL of the document that loaded the plugin
  nsCOMPtr<nsIPluginInstanceOwner> owner;
  aInstance->GetOwner(getter_AddRefs(owner));
  if (!owner)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIDocument> doc;
  owner->GetDocument(getter_AddRefs(doc));
  if (!doc)
    return NS_ERROR_FAILURE;

  // Create an absolute URL for the target in case the target is relative
  nsCOMPtr<nsIURI> targetURL;
  NS_NewURI(getter_AddRefs(targetURL), aURL, doc->GetBaseURI());
  if (!targetURL)
    return NS_ERROR_FAILURE;

  nsresult rv;
  nsCOMPtr<nsIScriptSecurityManager> secMan(
    do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID, &rv));
  if (NS_FAILED(rv))
    return rv;

  return secMan->CheckLoadURIWithPrincipal(doc->NodePrincipal(), targetURL,
                                           nsIScriptSecurityManager::STANDARD);

}

nsresult
nsPluginHost::AddHeadersToChannel(const char *aHeadersData,
                                  PRUint32 aHeadersDataLen,
                                  nsIChannel *aGenericChannel)
{
  nsresult rv = NS_OK;

  nsCOMPtr<nsIHttpChannel> aChannel = do_QueryInterface(aGenericChannel);
  if (!aChannel) {
    return NS_ERROR_NULL_POINTER;
  }

  // used during the manipulation of the String from the aHeadersData
  nsCAutoString headersString;
  nsCAutoString oneHeader;
  nsCAutoString headerName;
  nsCAutoString headerValue;
  PRInt32 crlf = 0;
  PRInt32 colon = 0;

  // Turn the char * buffer into an nsString.
  headersString = aHeadersData;

  // Iterate over the nsString: for each "\r\n" delimited chunk,
  // add the value as a header to the nsIHTTPChannel
  while (PR_TRUE) {
    crlf = headersString.Find("\r\n", PR_TRUE);
    if (-1 == crlf) {
      rv = NS_OK;
      return rv;
    }
    headersString.Mid(oneHeader, 0, crlf);
    headersString.Cut(0, crlf + 2);
    oneHeader.StripWhitespace();
    colon = oneHeader.Find(":");
    if (-1 == colon) {
      rv = NS_ERROR_NULL_POINTER;
      return rv;
    }
    oneHeader.Left(headerName, colon);
    colon++;
    oneHeader.Mid(headerValue, colon, oneHeader.Length() - colon);

    // FINALLY: we can set the header!

    rv = aChannel->SetRequestHeader(headerName, headerValue, PR_TRUE);
    if (NS_FAILED(rv)) {
      rv = NS_ERROR_NULL_POINTER;
      return rv;
    }
  }
  return rv;
}

NS_IMETHODIMP
nsPluginHost::StopPluginInstance(nsIPluginInstance* aInstance)
{
  if (PluginDestructionGuard::DelayDestroy(aInstance)) {
    return NS_OK;
  }

  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("nsPluginHost::StopPluginInstance called instance=%p\n",aInstance));

  nsPluginInstanceTag * plugin = mPluginInstanceTagList.find(aInstance);

  if (plugin) {
    plugin->setStopped(PR_TRUE);  // be sure we set the "stop" bit

    // if the plugin does not want to be 'cached' just remove it
    PRBool doCache = PR_TRUE;
    aInstance->GetValue(nsPluginInstanceVariable_DoCacheBool, (void *) &doCache);

    if (!doCache) {
      PRLibrary * library = nsnull;
      if (plugin->mPluginTag)
        library = plugin->mPluginTag->mLibrary;

      mPluginInstanceTagList.remove(plugin);
    } else {
      // if it is allowed to be cached simply stop it, but first we should check
      // if we haven't exceeded the maximum allowed number of cached instances

      // try to get the max cached plugins from a pref or use default
      PRUint32 max_num;
      nsresult rv = NS_ERROR_FAILURE;
      if (mPrefService)
        rv = mPrefService->GetIntPref(NS_PREF_MAX_NUM_CACHED_PLUGINS, (int*)&max_num);
      if (NS_FAILED(rv))
        max_num = DEFAULT_NUMBER_OF_STOPPED_PLUGINS;

      if (mPluginInstanceTagList.getStoppedCount() >= max_num) {
        nsPluginInstanceTag * oldest = mPluginInstanceTagList.findOldestStopped();
        if (oldest != nsnull)
          mPluginInstanceTagList.remove(oldest);
      }
    }
  }
  return NS_OK;
}

nsresult nsPluginHost::NewEmbeddedPluginStreamListener(nsIURI* aURL,
                                                       nsIPluginInstanceOwner *aOwner,
                                                       nsIPluginInstance* aInstance,
                                                       nsIStreamListener** aListener)
{
  if (!aURL)
    return NS_OK;

  nsRefPtr<nsPluginStreamListenerPeer> listener =
      new nsPluginStreamListenerPeer();
  if (listener == nsnull)
    return NS_ERROR_OUT_OF_MEMORY;

  nsresult rv;

  // if we have an instance, everything has been set up
  // if we only have an owner, then we need to pass it in
  // so the listener can set up the instance later after
  // we've determined the mimetype of the stream
  if (aInstance != nsnull)
    rv = listener->InitializeEmbedded(aURL, aInstance);
  else if (aOwner != nsnull)
    rv = listener->InitializeEmbedded(aURL, nsnull, aOwner, this);
  else
    rv = NS_ERROR_ILLEGAL_VALUE;
  if (NS_SUCCEEDED(rv))
    NS_ADDREF(*aListener = listener);

  return rv;
}

// Called by InstantiateEmbeddedPlugin()
nsresult nsPluginHost::NewEmbeddedPluginStream(nsIURI* aURL,
                                               nsIPluginInstanceOwner *aOwner,
                                               nsIPluginInstance* aInstance)
{
  nsCOMPtr<nsIStreamListener> listener;
  nsresult rv = NewEmbeddedPluginStreamListener(aURL, aOwner, aInstance,
                                                getter_AddRefs(listener));
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIDocument> doc;
    nsCOMPtr<nsILoadGroup> loadGroup;
    if (aOwner) {
      rv = aOwner->GetDocument(getter_AddRefs(doc));
      if (NS_SUCCEEDED(rv) && doc) {
        loadGroup = doc->GetDocumentLoadGroup();
      }
    }
    nsCOMPtr<nsIChannel> channel;
    rv = NS_NewChannel(getter_AddRefs(channel), aURL, nsnull, loadGroup, nsnull);
    if (NS_SUCCEEDED(rv)) {
      // if this is http channel, set referrer, some servers are configured
      // to reject requests without referrer set, see bug 157796
      nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel));
      if (httpChannel && doc)
        httpChannel->SetReferrer(doc->GetDocumentURI());

      rv = channel->AsyncOpen(listener, nsnull);
      if (NS_SUCCEEDED(rv))
        return NS_OK;
    }
  }

  return rv;
}

// Called by InstantiateFullPagePlugin()
nsresult nsPluginHost::NewFullPagePluginStream(nsIStreamListener *&aStreamListener,
                                               nsIURI* aURI,
                                               nsIPluginInstance *aInstance)
{
  nsPluginStreamListenerPeer  *listener = new nsPluginStreamListenerPeer();
  if (!listener)
    return NS_ERROR_OUT_OF_MEMORY;

  nsresult rv;

  rv = listener->InitializeFullPage(aURI, aInstance);

  aStreamListener = listener;
  NS_ADDREF(listener);

  // add peer to list of stream peers for this instance
  nsPluginInstanceTag * p = mPluginInstanceTagList.find(aInstance);
  if (p)
    p->mStreams.AppendObject(listener);

  return rv;
}

NS_IMETHODIMP nsPluginHost::Observe(nsISupports *aSubject,
                                    const char *aTopic,
                                    const PRUnichar *someData)
{
  if (!nsCRT::strcmp(NS_XPCOM_SHUTDOWN_OBSERVER_ID, aTopic)) {
    OnShutdown();
    Destroy();
    UnloadUnusedLibraries();
    sInst->Release();
  }
  if (!nsCRT::strcmp(NS_PRIVATE_BROWSING_SWITCH_TOPIC, aTopic)) {
    // inform all active plugins of changed private mode state
    for (nsPluginInstanceTag* ap = mPluginInstanceTagList.mFirst; ap; ap = ap->mNext) {
      nsNPAPIPluginInstance* pi = static_cast<nsNPAPIPluginInstance*>(ap->mInstance);
#ifdef OJI
      nsPluginTag* pt = ap->mPluginTag;
      if (pt->HasFlag(NS_PLUGIN_FLAG_NPAPI))
#endif
      pi->PrivateModeStateChanged();
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPluginHost::HandleBadPlugin(PRLibrary* aLibrary, nsIPluginInstance *aInstance)
{
  // the |aLibrary| parameter is not needed anymore, after we added |aInstance| which
  // can also be used to look up the plugin name, but we cannot get rid of it because
  // the |nsIPluginHost| interface is deprecated which in fact means 'frozen'

  nsresult rv = NS_OK;

  NS_ASSERTION(PR_FALSE, "Plugin performed illegal operation");

  if (mDontShowBadPluginMessage)
    return rv;

  nsCOMPtr<nsIPluginInstanceOwner> owner;
  if (aInstance)
    aInstance->GetOwner(getter_AddRefs(owner));

  nsCOMPtr<nsIPrompt> prompt;
  GetPrompt(owner, getter_AddRefs(prompt));
  if (prompt) {
    nsCOMPtr<nsIStringBundleService> strings(do_GetService(NS_STRINGBUNDLE_CONTRACTID, &rv));
    if (NS_FAILED(rv))
      return rv;

    nsCOMPtr<nsIStringBundle> bundle;
    rv = strings->CreateBundle(BRAND_PROPERTIES_URL, getter_AddRefs(bundle));
    if (NS_FAILED(rv))
      return rv;

    nsXPIDLString brandName;
    if (NS_FAILED(rv = bundle->GetStringFromName(NS_LITERAL_STRING("brandShortName").get(),
                                 getter_Copies(brandName))))
      return rv;

    rv = strings->CreateBundle(PLUGIN_PROPERTIES_URL, getter_AddRefs(bundle));
    if (NS_FAILED(rv))
      return rv;

    nsXPIDLString title, message, checkboxMessage;
    if (NS_FAILED(rv = bundle->GetStringFromName(NS_LITERAL_STRING("BadPluginTitle").get(),
                                 getter_Copies(title))))
      return rv;

    const PRUnichar *formatStrings[] = { brandName.get() };
    if (NS_FAILED(rv = bundle->FormatStringFromName(NS_LITERAL_STRING("BadPluginMessage").get(),
                                 formatStrings, 1, getter_Copies(message))))
      return rv;

    if (NS_FAILED(rv = bundle->GetStringFromName(NS_LITERAL_STRING("BadPluginCheckboxMessage").get(),
                                 getter_Copies(checkboxMessage))))
      return rv;

    // add plugin name to the message
    nsCString pluginname;
    nsPluginInstanceTag * p = mPluginInstanceTagList.find(aInstance);
    if (p) {
      nsPluginTag * tag = p->mPluginTag;
      if (tag) {
        if (!tag->mName.IsEmpty())
          pluginname = tag->mName;
        else
          pluginname = tag->mFileName;
      }
    }

    NS_ConvertUTF8toUTF16 msg(pluginname);
    msg.AppendLiteral("\n\n");
    msg.Append(message);

    PRInt32 buttonPressed;
    PRBool checkboxState = PR_FALSE;
    rv = prompt->ConfirmEx(title, msg.get(),
                         nsIPrompt::BUTTON_TITLE_OK * nsIPrompt::BUTTON_POS_0,
                         nsnull, nsnull, nsnull,
                         checkboxMessage, &checkboxState, &buttonPressed);


    if (NS_SUCCEEDED(rv) && checkboxState)
      mDontShowBadPluginMessage = PR_TRUE;
  }

  return rv;
}

NS_IMETHODIMP
nsPluginHost::SetIsScriptableInstance(nsIPluginInstance * aPluginInstance, PRBool aScriptable)
{
  nsPluginInstanceTag * p = mPluginInstanceTagList.find(aPluginInstance);
  if (p == nsnull)
    return NS_ERROR_FAILURE;

  p->mXPConnected = aScriptable;
  if (p->mPluginTag)
    p->mPluginTag->mXPConnected = aScriptable;

  return NS_OK;
}

NS_IMETHODIMP
nsPluginHost::ParsePostBufferToFixHeaders(const char *inPostData, PRUint32 inPostDataLen,
                                          char **outPostData, PRUint32 *outPostDataLen)
{
  if (!inPostData || !outPostData || !outPostDataLen)
    return NS_ERROR_NULL_POINTER;

  *outPostData = 0;
  *outPostDataLen = 0;

  const char CR = '\r';
  const char LF = '\n';
  const char CRLFCRLF[] = {CR,LF,CR,LF,'\0'}; // C string"\r\n\r\n"
  const char ContentLenHeader[] = "Content-length";

  nsAutoTArray<const char*, 8> singleLF;
  const char *pSCntlh = 0;// pointer to start of ContentLenHeader in inPostData
  const char *pSod = 0;   // pointer to start of data in inPostData
  const char *pEoh = 0;   // pointer to end of headers in inPostData
  const char *pEod = inPostData + inPostDataLen; // pointer to end of inPostData
  if (*inPostData == LF) {
    // If no custom headers are required, simply add a blank
    // line ('\n') to the beginning of the file or buffer.
    // so *inPostData == '\n' is valid
    pSod = inPostData + 1;
  } else {
    const char *s = inPostData; //tmp pointer to sourse inPostData
    while (s < pEod) {
      if (!pSCntlh &&
          (*s == 'C' || *s == 'c') &&
          (s + sizeof(ContentLenHeader) - 1 < pEod) &&
          (!PL_strncasecmp(s, ContentLenHeader, sizeof(ContentLenHeader) - 1)))
      {
        // lets assume this is ContentLenHeader for now
        const char *p = pSCntlh = s;
        p += sizeof(ContentLenHeader) - 1;
        // search for first CR or LF == end of ContentLenHeader
        for (; p < pEod; p++) {
          if (*p == CR || *p == LF) {
            // got delimiter,
            // one more check; if previous char is a digit
            // most likely pSCntlh points to the start of ContentLenHeader
            if (*(p-1) >= '0' && *(p-1) <= '9') {
              s = p;
            }
            break; //for loop
          }
        }
        if (pSCntlh == s) { // curret ptr is the same
          pSCntlh = 0; // that was not ContentLenHeader
          break; // there is nothing to parse, break *WHILE LOOP* here
        }
      }

      if (*s == CR) {
        if (pSCntlh && // only if ContentLenHeader is found we are looking for end of headers
            ((s + sizeof(CRLFCRLF)-1) <= pEod) &&
            !memcmp(s, CRLFCRLF, sizeof(CRLFCRLF)-1))
        {
          s += sizeof(CRLFCRLF)-1;
          pEoh = pSod = s; // data stars here
          break;
        }
      } else if (*s == LF) {
        if (*(s-1) != CR) {
          singleLF.AppendElement(s);
        }
        if (pSCntlh && (s+1 < pEod) && (*(s+1) == LF)) {
          s++;
          singleLF.AppendElement(s);
          s++;
          pEoh = pSod = s; // data stars here
          break;
        }
      }
      s++;
    }
  }

  // deal with output buffer
  if (!pSod) { // lets assume whole buffer is a data
    pSod = inPostData;
  }

  PRUint32 newBufferLen = 0;
  PRUint32 dataLen = pEod - pSod;
  PRUint32 headersLen = pEoh ? pSod - inPostData : 0;

  char *p; // tmp ptr into new output buf
  if (headersLen) { // we got a headers
    // this function does not make any assumption on correctness
    // of ContentLenHeader value in this case.

    newBufferLen = dataLen + headersLen;
    // in case there were single LFs in headers
    // reserve an extra space for CR will be added before each single LF
    int cntSingleLF = singleLF.Length();
    newBufferLen += cntSingleLF;

    if (!(*outPostData = p = (char*)nsMemory::Alloc(newBufferLen)))
      return NS_ERROR_OUT_OF_MEMORY;

    // deal with single LF
    const char *s = inPostData;
    if (cntSingleLF) {
      for (int i=0; i<cntSingleLF; i++) {
        const char *plf = singleLF.ElementAt(i); // ptr to single LF in headers
        int n = plf - s; // bytes to copy
        if (n) { // for '\n\n' there is nothing to memcpy
          memcpy(p, s, n);
          p += n;
        }
        *p++ = CR;
        s = plf;
        *p++ = *s++;
      }
    }
    // are we done with headers?
    headersLen = pEoh - s;
    if (headersLen) { // not yet
      memcpy(p, s, headersLen); // copy the rest
      p += headersLen;
    }
  } else  if (dataLen) { // no ContentLenHeader is found but there is a data
    // make new output buffer big enough
    // to keep ContentLenHeader+value followed by data
    PRUint32 l = sizeof(ContentLenHeader) + sizeof(CRLFCRLF) + 32;
    newBufferLen = dataLen + l;
    if (!(*outPostData = p = (char*)nsMemory::Alloc(newBufferLen)))
      return NS_ERROR_OUT_OF_MEMORY;
    headersLen = PR_snprintf(p, l,"%s: %ld%s", ContentLenHeader, dataLen, CRLFCRLF);
    if (headersLen == l) { // if PR_snprintf has ate all extra space consider this as an error
      nsMemory::Free(p);
      *outPostData = 0;
      return NS_ERROR_FAILURE;
    }
    p += headersLen;
    newBufferLen = headersLen + dataLen;
  }
  // at this point we've done with headers.
  // there is a possibility that input buffer has only headers info in it
  // which already parsed and copied into output buffer.
  // copy the data
  if (dataLen) {
    memcpy(p, pSod, dataLen);
  }

  *outPostDataLen = newBufferLen;

  return NS_OK;
}

NS_IMETHODIMP
nsPluginHost::CreateTmpFileToPost(const char *postDataURL, char **pTmpFileName)
{
  *pTmpFileName = 0;
  nsresult rv;
  PRInt64 fileSize;
  nsCAutoString filename;

  // stat file == get size & convert file:///c:/ to c: if needed
  nsCOMPtr<nsIFile> inFile;
  rv = NS_GetFileFromURLSpec(nsDependentCString(postDataURL),
                             getter_AddRefs(inFile));
  if (NS_FAILED(rv)) {
    nsCOMPtr<nsILocalFile> localFile;
    rv = NS_NewNativeLocalFile(nsDependentCString(postDataURL), PR_FALSE,
                               getter_AddRefs(localFile));
    if (NS_FAILED(rv)) return rv;
    inFile = localFile;
  }
  rv = inFile->GetFileSize(&fileSize);
  if (NS_FAILED(rv)) return rv;
  rv = inFile->GetNativePath(filename);
  if (NS_FAILED(rv)) return rv;

  if (!LL_IS_ZERO(fileSize)) {
    nsCOMPtr<nsIInputStream> inStream;
    rv = NS_NewLocalFileInputStream(getter_AddRefs(inStream), inFile);
    if (NS_FAILED(rv)) return rv;

    // Create a temporary file to write the http Content-length:
    // %ld\r\n\" header and "\r\n" == end of headers for post data to

    nsCOMPtr<nsIFile> tempFile;
    rv = GetPluginTempDir(getter_AddRefs(tempFile));
    if (NS_FAILED(rv))
      return rv;

    nsCAutoString inFileName;
    inFile->GetNativeLeafName(inFileName);
    // XXX hack around bug 70083
    inFileName.Insert(NS_LITERAL_CSTRING("post-"), 0);
    rv = tempFile->AppendNative(inFileName);

    if (NS_FAILED(rv))
      return rv;

    // make it unique, and mode == 0600, not world-readable
    rv = tempFile->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
    if (NS_FAILED(rv))
      return rv;

    nsCOMPtr<nsIOutputStream> outStream;
    if (NS_SUCCEEDED(rv)) {
      rv = NS_NewLocalFileOutputStream(getter_AddRefs(outStream),
        tempFile,
        (PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE),
        0600); // 600 so others can't read our form data
    }
    NS_ASSERTION(NS_SUCCEEDED(rv), "Post data file couldn't be created!");
    if (NS_FAILED(rv))
      return rv;

    char buf[1024];
    PRUint32 br, bw;
    PRBool firstRead = PR_TRUE;
    while (1) {
      // Read() mallocs if buffer is null
      rv = inStream->Read(buf, 1024, &br);
      if (NS_FAILED(rv) || (PRInt32)br <= 0)
        break;
      if (firstRead) {
        //"For protocols in which the headers must be distinguished from the body,
        // such as HTTP, the buffer or file should contain the headers, followed by
        // a blank line, then the body. If no custom headers are required, simply
        // add a blank line ('\n') to the beginning of the file or buffer.

        char *parsedBuf;
        // assuming first 1K (or what we got) has all headers in,
        // lets parse it through nsPluginHost::ParsePostBufferToFixHeaders()
        ParsePostBufferToFixHeaders((const char *)buf, br, &parsedBuf, &bw);
        rv = outStream->Write(parsedBuf, bw, &br);
        nsMemory::Free(parsedBuf);
        if (NS_FAILED(rv) || (bw != br))
          break;

        firstRead = PR_FALSE;
        continue;
      }
      bw = br;
      rv = outStream->Write(buf, bw, &br);
      if (NS_FAILED(rv) || (bw != br))
        break;
    }

    inStream->Close();
    outStream->Close();
    if (NS_SUCCEEDED(rv)) {
      nsCAutoString path;
      if (NS_SUCCEEDED(tempFile->GetNativePath(path)))
        *pTmpFileName = ToNewCString(path);
    }
  }
  return rv;
}

NS_IMETHODIMP
nsPluginHost::NewPluginNativeWindow(nsPluginNativeWindow ** aPluginNativeWindow)
{
  return PLUG_NewPluginNativeWindow(aPluginNativeWindow);
}

NS_IMETHODIMP
nsPluginHost::DeletePluginNativeWindow(nsPluginNativeWindow * aPluginNativeWindow)
{
  return PLUG_DeletePluginNativeWindow(aPluginNativeWindow);
}

NS_IMETHODIMP
nsPluginHost::InstantiateDummyJavaPlugin(nsIPluginInstanceOwner *aOwner)
{
  // Pass PR_FALSE as the second arg, we want the answer to be the
  // same here whether the Java plugin is enabled or not.
  nsPluginTag *plugin = FindPluginForType("application/x-java-vm", PR_FALSE);

  if (!plugin || !plugin->mIsNPRuntimeEnabledJavaPlugin) {
    // No NPRuntime enabled Java plugin found, no point in
    // instantiating a dummy plugin then.

    return NS_OK;
  }

  nsresult rv = SetUpPluginInstance("application/x-java-vm", nsnull, aOwner);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPluginInstance> instance;
  aOwner->GetInstance(*getter_AddRefs(instance));
  if (!instance)
    return NS_OK;

  instance->DefineJavaProperties();

  return NS_OK;
}

NS_IMETHODIMP
nsPluginHost::GetPluginName(nsIPluginInstance *aPluginInstance,
                            const char** aPluginName)
{
  *aPluginName = GetPluginName(aPluginInstance);
  return NS_OK;
}

NS_IMETHODIMP
nsPluginHost::GetPluginTagForInstance(nsIPluginInstance *aPluginInstance,
                                      nsIPluginTag **aPluginTag)
{
  NS_ENSURE_ARG_POINTER(aPluginInstance);
  NS_ENSURE_ARG_POINTER(aPluginTag);
  
  nsPluginInstanceTag *plugin =
    gActivePluginList ? gActivePluginList->find(aPluginInstance) : nsnull;

  NS_ENSURE_TRUE(plugin && plugin->mPluginTag, NS_ERROR_FAILURE);
  
  *aPluginTag = plugin->mPluginTag;
  NS_ADDREF(*aPluginTag);
  return NS_OK;
}

nsresult nsPluginHost::AddUnusedLibrary(PRLibrary * aLibrary)
{
  if (!mUnusedLibraries.Contains(aLibrary)) // don't add duplicates
    mUnusedLibraries.AppendElement(aLibrary);

  return NS_OK;
}

#ifdef MOZ_IPC
#ifdef XP_WIN
// Re-enable any top level browser windows that were disabled by modal dialogs
// displayed by the crashed plugin.
static void
CheckForDisabledWindows()
{
  nsCOMPtr<nsIWindowMediator> wm(do_GetService(NS_WINDOWMEDIATOR_CONTRACTID));
  if (!wm)
    return;

  nsCOMPtr<nsISimpleEnumerator> windowList;
  wm->GetXULWindowEnumerator(nsnull, getter_AddRefs(windowList));
  if (!windowList)
    return;

  PRBool haveWindows;
  do {
    windowList->HasMoreElements(&haveWindows);
    if (!haveWindows)
      return;

    nsCOMPtr<nsISupports> supportsWindow;
    windowList->GetNext(getter_AddRefs(supportsWindow));
    nsCOMPtr<nsIBaseWindow> baseWin(do_QueryInterface(supportsWindow));
    if (baseWin) {
      PRBool aFlag;
      nsCOMPtr<nsIWidget> widget;
      baseWin->GetMainWidget(getter_AddRefs(widget));
      if (widget && !widget->GetParent() &&
          NS_SUCCEEDED(widget->IsVisible(aFlag)) && aFlag == PR_TRUE &&
          NS_SUCCEEDED(widget->IsEnabled(&aFlag)) && aFlag == PR_FALSE) {
        nsIWidget * child = widget->GetFirstChild();
        PRBool enable = PR_TRUE;
        while (child)  {
          nsWindowType aType;
          if (NS_SUCCEEDED(child->GetWindowType(aType)) &&
              aType == eWindowType_dialog) {
            enable = PR_FALSE;
            break;
          }
          child = child->GetNextSibling();
        }
        if (enable) {
          widget->Enable(PR_TRUE);
        }
      }
    }
  } while (haveWindows);
}
#endif

void
nsPluginHost::PluginCrashed(nsNPAPIPlugin* aPlugin,
                            const nsAString& pluginDumpID,
                            const nsAString& browserDumpID)
{
  nsPluginTag* plugin = FindTagForPlugin(aPlugin);
  if (!plugin) {
    NS_WARNING("nsPluginTag not found in nsPluginHost::PluginCrashed");
    return;
  }

  // Notify the app's observer that a plugin crashed so it can submit
  // a crashreport.
  PRBool submittedCrashReport = PR_FALSE;
  nsCOMPtr<nsIObserverService> obsService =
    do_GetService("@mozilla.org/observer-service;1");
  nsCOMPtr<nsIWritablePropertyBag2> propbag =
    do_CreateInstance("@mozilla.org/hash-property-bag;1");
  if (obsService && propbag) {
    propbag->SetPropertyAsAString(NS_LITERAL_STRING("pluginDumpID"),
                                  pluginDumpID);
    propbag->SetPropertyAsAString(NS_LITERAL_STRING("browserDumpID"),
                                  browserDumpID);
    propbag->SetPropertyAsBool(NS_LITERAL_STRING("submittedCrashReport"),
                               submittedCrashReport);
    obsService->NotifyObservers(propbag, "plugin-crashed", nsnull);
    // see if an observer submitted a crash report.
    propbag->GetPropertyAsBool(NS_LITERAL_STRING("submittedCrashReport"),
                               &submittedCrashReport);
  }

  // Invalidate each nsPluginInstanceTag for the crashed plugin

  nsPluginInstanceTag** pinstancetag = &mPluginInstanceTagList.mFirst;
  while (*pinstancetag) {
    nsPluginInstanceTag* instancetag = *pinstancetag;
    if (instancetag->mPluginTag == plugin) {
      // notify the content node (nsIObjectLoadingContent) that the
      // plugin has crashed
      nsCOMPtr<nsIDOMElement> domElement;
      static_cast<nsNPAPIPluginInstance*>(instancetag->mInstance)
        ->GetDOMElement(getter_AddRefs(domElement));
      nsCOMPtr<nsIObjectLoadingContent_MOZILLA_1_9_2_BRANCH> objectContent(do_QueryInterface(domElement));
      if (objectContent) {
        objectContent->PluginCrashed(plugin, pluginDumpID, browserDumpID,
                                     submittedCrashReport);
      }

      instancetag->mInstance->Stop();
      *pinstancetag = (*pinstancetag)->mNext;
      delete instancetag;
    }
    else {
      pinstancetag = &(*pinstancetag)->mNext;
    }
  }

  // Only after all instances have been invalidated is it safe to null
  // out nsPluginTag.mEntryPoint. The next time we try to create an
  // instance of this plugin we reload it (launch a new plugin process).

  plugin->mEntryPoint = nsnull;

#ifdef XP_WIN
  CheckForDisabledWindows();
#endif
}
#endif

nsresult nsPluginStreamListenerPeer::ServeStreamAsFile(nsIRequest *request,
                                                       nsISupports* aContext)
{
  if (!mInstance)
    return NS_ERROR_FAILURE;

  // mInstance->Stop calls mPStreamListener->CleanUpStream(), so stream will be properly clean up
  mInstance->Stop();
  mInstance->Start();
  nsCOMPtr<nsIPluginInstanceOwner> owner;
  mInstance->GetOwner(getter_AddRefs(owner));
  if (owner) {
    nsPluginWindow    *window = nsnull;
    owner->GetWindow(window);
#if defined (MOZ_WIDGET_GTK2)
    // Should call GetPluginPort() here.
    // This part is copied from nsPluginInstanceOwner::GetPluginPort(). 
    nsCOMPtr<nsIWidget> widget;
    ((nsPluginNativeWindow*)window)->GetPluginWidget(getter_AddRefs(widget));
    if (widget) {
      window->window = (nsPluginPort*) widget->GetNativeData(NS_NATIVE_PLUGIN_PORT);
    }
#endif
    if (window->window) {
      nsCOMPtr<nsIPluginInstance> inst = mInstance;
      ((nsPluginNativeWindow*)window)->CallSetWindow(inst);
    }
  }

  mSeekable = PR_FALSE;
  mPStreamListener->OnStartBinding(this);
  mStreamOffset = 0;

  // force the plugin use stream as file
  mStreamType = nsPluginStreamType_AsFile;

  // then check it out if browser cache is not available
  nsCOMPtr<nsICachingChannel> cacheChannel = do_QueryInterface(request);
  if (!(cacheChannel && (NS_SUCCEEDED(cacheChannel->SetCacheAsFile(PR_TRUE))))) {
      nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
      if (channel) {
        SetupPluginCacheFile(channel);
      }
  }

  // unset mPendingRequests
  mPendingRequests = 0;

  return NS_OK;
}

NS_IMPL_ISUPPORTS1(nsPluginByteRangeStreamListener, nsIStreamListener)
nsPluginByteRangeStreamListener::nsPluginByteRangeStreamListener(nsIWeakReference* aWeakPtr)
{
  mWeakPtrPluginStreamListenerPeer = aWeakPtr;
  mRemoveMagicNumber = PR_FALSE;
}

nsPluginByteRangeStreamListener::~nsPluginByteRangeStreamListener()
{
  mStreamConverter = 0;
  mWeakPtrPluginStreamListenerPeer = 0;
}

NS_IMETHODIMP
nsPluginByteRangeStreamListener::OnStartRequest(nsIRequest *request, nsISupports *ctxt)
{
  nsresult rv;

  nsCOMPtr<nsIStreamListener> finalStreamListener = do_QueryReferent(mWeakPtrPluginStreamListenerPeer);
  if (!finalStreamListener)
     return NS_ERROR_FAILURE;

  nsCOMPtr<nsIStreamConverterService> serv = do_GetService(NS_STREAMCONVERTERSERVICE_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = serv->AsyncConvertData(MULTIPART_BYTERANGES,
                                "*/*",
                                finalStreamListener,
                                nsnull,
                                getter_AddRefs(mStreamConverter));
    if (NS_SUCCEEDED(rv)) {
      rv = mStreamConverter->OnStartRequest(request, ctxt);
      if (NS_SUCCEEDED(rv))
        return rv;
    }
  }
  mStreamConverter = 0;

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(request));
  if (!httpChannel) {
    return NS_ERROR_FAILURE;
  }

  PRUint32 responseCode = 0;
  rv = httpChannel->GetResponseStatus(&responseCode);
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }
  
  // get nsPluginStreamListenerPeer* ptr from finalStreamListener
  nsPluginStreamListenerPeer *pslp =
    reinterpret_cast<nsPluginStreamListenerPeer*>(finalStreamListener.get());

  if (responseCode != 200) {
    PRBool bWantsAllNetworkStreams = PR_FALSE;
    pslp->GetPluginInstance()->
      GetValue(nsPluginInstanceVariable_WantsAllNetworkStreams,
               (void *)&bWantsAllNetworkStreams);
    if (!bWantsAllNetworkStreams){
      return NS_ERROR_FAILURE;
    }
  }

  // if server cannot continue with byte range (206 status) and sending us whole object (200 status)
  // reset this seekable stream & try serve it to plugin instance as a file
  mStreamConverter = finalStreamListener;
  mRemoveMagicNumber = PR_TRUE;

  rv = pslp->ServeStreamAsFile(request, ctxt);
  return rv;
}

NS_IMETHODIMP
nsPluginByteRangeStreamListener::OnStopRequest(nsIRequest *request, nsISupports *ctxt,
                              nsresult status)
{
  if (!mStreamConverter)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIStreamListener> finalStreamListener = do_QueryReferent(mWeakPtrPluginStreamListenerPeer);
  if (!finalStreamListener)
    return NS_ERROR_FAILURE;

  if (mRemoveMagicNumber) {
    // remove magic number from container
    nsCOMPtr<nsISupportsPRUint32> container = do_QueryInterface(ctxt);
    if (container) {
      PRUint32 magicNumber = 0;
      container->GetData(&magicNumber);
      if (magicNumber == MAGIC_REQUEST_CONTEXT) {
        // to allow properly finish nsPluginStreamListenerPeer->OnStopRequest()
        // set it to something that is not the magic number.
        container->SetData(0);
      }
    } else {
      NS_WARNING("Bad state of nsPluginByteRangeStreamListener");
    }
  }

  return mStreamConverter->OnStopRequest(request, ctxt, status);
}

NS_IMETHODIMP
nsPluginByteRangeStreamListener::OnDataAvailable(nsIRequest *request, nsISupports *ctxt,
                                nsIInputStream *inStr, PRUint32 sourceOffset, PRUint32 count)
{
  if (!mStreamConverter)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIStreamListener> finalStreamListener = do_QueryReferent(mWeakPtrPluginStreamListenerPeer);
  if (!finalStreamListener)
    return NS_ERROR_FAILURE;

  return mStreamConverter->OnDataAvailable(request, ctxt, inStr, sourceOffset, count);
}

PRBool
nsPluginStreamListenerPeer::UseExistingPluginCacheFile(nsPluginStreamListenerPeer* psi)
{

  NS_ENSURE_ARG_POINTER(psi);

 if ( psi->mLength == mLength &&
      psi->mModified == mModified &&
      mStreamComplete &&
      mURLSpec.Equals(psi->mURLSpec))
  {
    return PR_TRUE;
  }
  return PR_FALSE;
}

// Runnable that does an async destroy of a plugin.

class nsPluginDestroyRunnable : public nsRunnable,
                                public PRCList
{
public:
  nsPluginDestroyRunnable(nsIPluginInstance *aInstance)
    : mInstance(aInstance)
  {
    PR_INIT_CLIST(this);
    PR_APPEND_LINK(this, &sRunnableListHead);
  }

  virtual ~nsPluginDestroyRunnable()
  {
    PR_REMOVE_LINK(this);
  }

  NS_IMETHOD Run()
  {
    nsCOMPtr<nsIPluginInstance> instance;

    // Null out mInstance to make sure this code in another runnable
    // will do the right thing even if someone was holding on to this
    // runnable longer than we expect.
    instance.swap(mInstance);

    if (PluginDestructionGuard::DelayDestroy(instance)) {
      // It's still not safe to destroy the plugin, it's now up to the
      // outermost guard on the stack to take care of the destruction.
      return NS_OK;
    }

    nsPluginDestroyRunnable *r =
      static_cast<nsPluginDestroyRunnable*>(PR_NEXT_LINK(&sRunnableListHead));

    while (r != &sRunnableListHead) {
      if (r != this && r->mInstance == instance) {
        // There's another runnable scheduled to tear down
        // instance. Let it do the job.
        return NS_OK;
      }
      r = static_cast<nsPluginDestroyRunnable*>(PR_NEXT_LINK(r));
    }

    PLUGIN_LOG(PLUGIN_LOG_NORMAL,
               ("Doing delayed destroy of instance %p\n", instance.get()));

    instance->Stop();
#ifdef OJI
    nsCOMPtr<nsIPluginInstanceOld> instOld(do_QueryInterface(instance));
    if (instOld)
      instOld->Destroy();
#endif

    nsRefPtr<nsPluginHost> host = nsPluginHost::GetInst();

    if (host)
      host->StopPluginInstance(instance);

    PLUGIN_LOG(PLUGIN_LOG_NORMAL,
               ("Done with delayed destroy of instance %p\n", instance.get()));

    return NS_OK;
  }

protected:
  nsCOMPtr<nsIPluginInstance> mInstance;

  static PRCList sRunnableListHead;
};

PRCList nsPluginDestroyRunnable::sRunnableListHead =
  PR_INIT_STATIC_CLIST(&nsPluginDestroyRunnable::sRunnableListHead);

PRCList PluginDestructionGuard::sListHead =
  PR_INIT_STATIC_CLIST(&PluginDestructionGuard::sListHead);

PluginDestructionGuard::~PluginDestructionGuard()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on the main thread");

  PR_REMOVE_LINK(this);

  if (mDelayedDestroy) {
    // We've attempted to destroy the plugin instance we're holding on
    // to while we were guarding it. Do the actual destroy now, off of
    // a runnable.
    nsRefPtr<nsPluginDestroyRunnable> evt =
      new nsPluginDestroyRunnable(mInstance);

    NS_DispatchToMainThread(evt);
  }
}

// static
PRBool
PluginDestructionGuard::DelayDestroy(nsIPluginInstance *aInstance)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on the main thread");
  NS_ASSERTION(aInstance, "Uh, I need an instance!");

  // Find the first guard on the stack and make it do a delayed
  // destroy upon destruction.

  PluginDestructionGuard *g =
    static_cast<PluginDestructionGuard*>(PR_LIST_HEAD(&sListHead));

  while (g != &sListHead) {
    if (g->mInstance == aInstance) {
      g->mDelayedDestroy = PR_TRUE;

      return PR_TRUE;
    }
    g = static_cast<PluginDestructionGuard*>(PR_NEXT_LINK(g));    
  }

  return PR_FALSE;
}

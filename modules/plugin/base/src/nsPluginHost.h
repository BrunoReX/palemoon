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
 *  Josh Aas <josh@mozilla.com>
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

#ifndef nsPluginHost_h_
#define nsPluginHost_h_

#include "nsIPluginHost.h"
#ifdef OJI
#include "nsIPluginManager.h"
#include "nsIPluginManager2.h"
#include "nsIPluginHostOld.h"
#include "nsIFileUtilities.h"
#include "nsICookieStorage.h"
#include "nsICookieService.h"
#endif
#include "nsIObserver.h"
#include "nsCOMPtr.h"
#include "nsCOMArray.h"
#include "prlink.h"
#include "prclist.h"
#include "npapi.h"
#include "nsNPAPIPluginInstance.h"
#include "nsIPlugin.h"
#include "nsIPluginTag.h"
#include "nsPluginsDir.h"
#include "nsPluginDirServiceProvider.h"
#include "nsAutoPtr.h"
#include "nsWeakPtr.h"
#include "nsIPrompt.h"
#include "nsISupportsArray.h"
#include "nsIPrefBranch.h"
#include "nsWeakReference.h"
#include "nsThreadUtils.h"
#include "nsTArray.h"

class nsNPAPIPlugin;
class nsIComponentManager;
class nsIFile;
class nsIChannel;
class nsPluginHost;

// Remember that flags are written out to pluginreg.dat, be careful
// changing their meaning.
#define NS_PLUGIN_FLAG_ENABLED      0x0001    // is this plugin enabled?
#ifdef OJI
#define NS_PLUGIN_FLAG_NPAPI        0x0002    // is this an NPAPI plugin?
#endif
// no longer used (if no OJI)       0x0002    // reuse only if regenerating pluginreg.dat
#define NS_PLUGIN_FLAG_FROMCACHE    0x0004    // this plugintag info was loaded from cache
#define NS_PLUGIN_FLAG_UNWANTED     0x0008    // this is an unwanted plugin
#define NS_PLUGIN_FLAG_BLOCKLISTED  0x0010    // this is a blocklisted plugin

// A linked-list of plugin information that is used for instantiating plugins
// and reflecting plugin information into JavaScript.
class nsPluginTag : public nsIPluginTag
{
public:
  enum nsRegisterType {
    ePluginRegister,
    ePluginUnregister
  };

  NS_DECL_ISUPPORTS
  NS_DECL_NSIPLUGINTAG

  nsPluginTag(nsPluginTag* aPluginTag);
  nsPluginTag(nsPluginInfo* aPluginInfo);
  nsPluginTag(const char* aName,
              const char* aDescription,
              const char* aFileName,
              const char* aFullPath,
              const char* aVersion,
              const char* const* aMimeTypes,
              const char* const* aMimeDescriptions,
              const char* const* aExtensions,
              PRInt32 aVariants,
              PRInt64 aLastModifiedTime = 0,
              PRBool aCanUnload = PR_TRUE,
              PRBool aArgsAreUTF8 = PR_FALSE);
  ~nsPluginTag();

  void SetHost(nsPluginHost * aHost);
  void TryUnloadPlugin(PRBool aForceShutdown = PR_FALSE);
  void Mark(PRUint32 mask);
  void UnMark(PRUint32 mask);
  PRBool HasFlag(PRUint32 flag);
  PRUint32 Flags();
  PRBool Equals(nsPluginTag* aPluginTag);
  PRBool IsEnabled();
  void RegisterWithCategoryManager(PRBool aOverrideInternalTypes,
                                   nsRegisterType aType = ePluginRegister);

  nsRefPtr<nsPluginTag> mNext;
  nsPluginHost *mPluginHost;
  nsCString     mName; // UTF-8
  nsCString     mDescription; // UTF-8
  PRInt32       mVariants;
  char          **mMimeTypeArray;
  nsTArray<nsCString> mMimeDescriptionArray; // UTF-8
  char          **mExtensionsArray;
  PRLibrary     *mLibrary;
  nsCOMPtr<nsIPlugin> mEntryPoint;
  PRPackedBool  mCanUnloadLibrary;
  PRPackedBool  mXPConnected;
  PRPackedBool  mIsJavaPlugin;
  PRPackedBool  mIsNPRuntimeEnabledJavaPlugin;
  nsCString     mFileName; // UTF-8
  nsCString     mFullPath; // UTF-8
  nsCString     mVersion;  // UTF-8
  PRInt64       mLastModifiedTime;
private:
  PRUint32      mFlags;

  nsresult EnsureMembersAreUTF8();
};

struct nsPluginInstanceTag
{
  nsPluginInstanceTag*   mNext;
  char*                  mURL;
  nsRefPtr<nsPluginTag>  mPluginTag;
  nsIPluginInstance*     mInstance;
  PRTime                 mllStopTime;
  PRPackedBool           mStopped;
  PRPackedBool           mDefaultPlugin;
  PRPackedBool           mXPConnected;
  // Array holding all opened stream listeners for this entry
  nsCOMArray<nsIPluginStreamInfo> mStreams;

  nsPluginInstanceTag(nsPluginTag* aPluginTag,
                      nsIPluginInstance* aInstance, 
                      const char * url,
                      PRBool aDefaultPlugin);
  ~nsPluginInstanceTag();

  void setStopped(PRBool stopped);
};

class nsPluginInstanceTagList
{
public:
  nsPluginInstanceTag *mFirst;

  nsPluginInstanceTagList();
  ~nsPluginInstanceTagList();

  void shutdown();
  void add(nsPluginInstanceTag *plugin);
  void remove(nsPluginInstanceTag *plugin);
  nsPluginInstanceTag *find(nsIPluginInstance *instance);
  nsPluginInstanceTag *find(const char *mimetype);
  nsPluginInstanceTag *findStopped(const char *url);
  PRUint32 getStoppedCount();
  nsPluginInstanceTag *findOldestStopped();
  void removeAllStopped();
  void stopRunning(nsISupportsArray *aReloadDocs, nsPluginTag *aPluginTag);
  PRBool IsLastInstance(nsPluginInstanceTag *plugin);
};

class nsPluginHost : public nsIPluginHost,
#ifdef OJI
                     public nsIPluginManager2,
                     public nsIPluginHostOld,
                     public nsIFileUtilities,
                     public nsICookieStorage,
#endif
                     public nsIObserver,
                     public nsSupportsWeakReference
{
public:
  nsPluginHost();
  virtual ~nsPluginHost();

  static nsPluginHost* GetInst();
  static const char *GetPluginName(nsIPluginInstance *aPluginInstance);

  NS_DECL_AND_IMPL_ZEROING_OPERATOR_NEW

  NS_DECL_ISUPPORTS
  NS_DECL_NSIPLUGINHOST
  NS_DECL_NSIOBSERVER

#ifdef OJI
  NS_DECL_NSIFILEUTILITIES
  NS_DECL_NSICOOKIESTORAGE
  NS_DECL_NSIFACTORY

  // nsIPluginHostOld methods not declared elsewhere
  NS_IMETHOD GetPluginFactory(const char *aMimeType, nsIPlugin** aPlugin);

  // nsIPluginManager methods not declared elsewhere
  NS_IMETHOD GetValue(nsPluginManagerVariable aVariable, void *aValue);

  NS_IMETHOD RegisterPlugin(REFNSIID aCID,
                            const char* aPluginName,
                            const char* aDescription,
                            const char** aMimeTypes,
                            const char** aMimeDescriptions,
                            const char** aFileExtensions,
                            PRInt32 aCount);

  NS_IMETHOD UnregisterPlugin(REFNSIID aCID);

  // nsIPluginManager2 methods not declared elsewhere

  NS_IMETHOD BeginWaitCursor(void);
  NS_IMETHOD EndWaitCursor(void);
  NS_IMETHOD SupportsURLProtocol(const char* protocol, PRBool *result);
  NS_IMETHOD NotifyStatusChange(nsIPlugin* plugin, nsresult errorStatus);
  NS_IMETHOD RegisterWindow(nsIEventHandler* handler, nsPluginPlatformWindowRef window);
  NS_IMETHOD UnregisterWindow(nsIEventHandler* handler, nsPluginPlatformWindowRef window);
  NS_IMETHOD AllocateMenuID(nsIEventHandler* handler, PRBool isSubmenu, PRInt16 *result);
  NS_IMETHOD DeallocateMenuID(nsIEventHandler* handler, PRInt16 menuID);
  NS_IMETHOD HasAllocatedMenuID(nsIEventHandler* handler, PRInt16 menuID, PRBool *result);

  // Helper method
  static nsresult NewForOldPluginInstance(nsIPluginInstanceOld* aInstanceOld, nsIPluginInstance** aInstance);

#endif

  NS_IMETHOD
  GetURL(nsISupports* pluginInst, 
         const char* url, 
         const char* target = NULL,
         nsIPluginStreamListener* streamListener = NULL,
         const char* altHost = NULL,
         const char* referrer = NULL,
         PRBool forceJSEnabled = PR_FALSE);
  
  NS_IMETHOD
  PostURL(nsISupports* pluginInst,
          const char* url,
          PRUint32 postDataLen, 
          const char* postData,
          PRBool isFile = PR_FALSE,
          const char* target = NULL,
          nsIPluginStreamListener* streamListener = NULL,
          const char* altHost = NULL, 
          const char* referrer = NULL,
          PRBool forceJSEnabled = PR_FALSE,
          PRUint32 postHeadersLength = 0, 
          const char* postHeaders = NULL);

  nsresult
  NewPluginURLStream(const nsString& aURL, 
                     nsIPluginInstance *aInstance, 
                     nsIPluginStreamListener *aListener,
                     const char *aPostData = nsnull, 
                     PRBool isFile = PR_FALSE,
                     PRUint32 aPostDataLen = 0, 
                     const char *aHeadersData = nsnull, 
                     PRUint32 aHeadersDataLen = 0);

  nsresult
  GetURLWithHeaders(nsISupports* pluginInst, 
                    const char* url, 
                    const char* target = NULL,
                    nsIPluginStreamListener* streamListener = NULL,
                    const char* altHost = NULL,
                    const char* referrer = NULL,
                    PRBool forceJSEnabled = PR_FALSE,
                    PRUint32 getHeadersLength = 0, 
                    const char* getHeaders = NULL);

  nsresult
  DoURLLoadSecurityCheck(nsIPluginInstance *aInstance,
                         const char* aURL);

  nsresult
  AddHeadersToChannel(const char *aHeadersData, PRUint32 aHeadersDataLen, 
                      nsIChannel *aGenericChannel);

  nsresult
  AddUnusedLibrary(PRLibrary * aLibrary);

  static nsresult GetPluginTempDir(nsIFile **aDir);

  // Writes updated plugins settings to disk and unloads the plugin
  // if it is now disabled
  nsresult UpdatePluginInfo(nsPluginTag* aPluginTag);

  // checks whether aTag is a "java" plugin tag (a tag for a plugin
  // that does Java)
  static PRBool IsJavaMIMEType(const char *aType);

  static nsresult GetPrompt(nsIPluginInstanceOwner *aOwner, nsIPrompt **aPrompt);

#ifdef MOZ_IPC
  void PluginCrashed(nsNPAPIPlugin* plugin,
                     const nsAString& pluginDumpID,
                     const nsAString& browserDumpID);
#endif

  // The guts of InstantiateEmbeddedPlugin.  The last argument should
  // be false if we already have an in-flight stream and don't need to
  // set up a new stream.
  nsresult DoInstantiateEmbeddedPlugin(const char *aMimeType, nsIURI* aURL,
                                       nsIPluginInstanceOwner* aOwner,
                                       PRBool aAllowOpeningStreams);

private:
  nsresult
  TrySetUpPluginInstance(const char *aMimeType, nsIURI *aURL, nsIPluginInstanceOwner *aOwner);

  nsresult
  NewEmbeddedPluginStreamListener(nsIURI* aURL, nsIPluginInstanceOwner *aOwner,
                                  nsIPluginInstance* aInstance,
                                  nsIStreamListener** aListener);

  nsresult
  NewEmbeddedPluginStream(nsIURI* aURL, nsIPluginInstanceOwner *aOwner, nsIPluginInstance* aInstance);

  nsresult
  NewFullPagePluginStream(nsIStreamListener *&aStreamListener, nsIURI* aURI, nsIPluginInstance *aInstance);

  // Return an nsPluginTag for this type, if any.  If aCheckEnabled is
  // true, only enabled plugins will be returned.
  nsPluginTag*
  FindPluginForType(const char* aMimeType, PRBool aCheckEnabled);

  nsPluginTag*
  FindPluginEnabledForExtension(const char* aExtension, const char* &aMimeType);

  // Return the tag for |plugin| if found, nsnull if not.
  nsPluginTag*
  FindTagForPlugin(nsIPlugin* aPlugin);

  nsresult
  FindStoppedPluginForURL(nsIURI* aURL, nsIPluginInstanceOwner *aOwner);

  nsresult
  SetUpDefaultPluginInstance(const char *aMimeType, nsIURI *aURL, nsIPluginInstanceOwner *aOwner);

  nsresult
  AddInstanceToActiveList(nsCOMPtr<nsIPlugin> aPlugin,
                          nsIPluginInstance* aInstance,
                          nsIURI* aURL, PRBool aDefaultPlugin);

  nsresult
  FindPlugins(PRBool aCreatePluginList, PRBool * aPluginsChanged);

  nsresult
  ScanPluginsDirectory(nsIFile * pluginsDir, 
                       nsIComponentManager * compManager, 
                       PRBool aCreatePluginList,
                       PRBool * aPluginsChanged,
                       PRBool checkForUnwantedPlugins = PR_FALSE);
                       
  nsresult
  ScanPluginsDirectoryList(nsISimpleEnumerator * dirEnum,
                           nsIComponentManager * compManager, 
                           PRBool aCreatePluginList,
                           PRBool * aPluginsChanged,
                           PRBool checkForUnwantedPlugins = PR_FALSE);

  PRBool IsRunningPlugin(nsPluginTag * plugin);

  // Stores all plugins info into the registry
  nsresult WritePluginInfo();

  // Loads all cached plugins info into mCachedPlugins
  nsresult ReadPluginInfo();

  // Given a file name or path, returns the plugins info from our
  // cache and removes it from the cache.  If byFileName is
  // PR_TRUE, fileSpec is a file name corresponding to a plugin in
  // NS_APP_PLUGINS_DIR.  Otherwise it's a file path.
  void RemoveCachedPluginsInfo(const char *fileSpec, PRBool byFileName,
                               nsPluginTag **result);

  //checks if the list already have the same plugin as given
  nsPluginTag* HaveSamePlugin(nsPluginTag * aPluginTag);

  // checks if given plugin is a duplicate of what we already have
  // in the plugin list but found in some different place
  PRBool IsDuplicatePlugin(nsPluginTag * aPluginTag);

  nsresult EnsurePrivateDirServiceProvider();

  // calls PostPluginUnloadEvent for each library in mUnusedLibraries
  void UnloadUnusedLibraries();
  
  nsRefPtr<nsPluginTag> mPlugins;
  nsRefPtr<nsPluginTag> mCachedPlugins;
  PRPackedBool mPluginsLoaded;
  PRPackedBool mDontShowBadPluginMessage;
  PRPackedBool mIsDestroyed;

  // set by pref plugin.override_internal_types
  PRPackedBool mOverrideInternalTypes;

  // set by pref plugin.allow_alien_star_handler
  PRPackedBool mAllowAlienStarHandler;

  // set by pref plugin.default_plugin_disabled
  PRPackedBool mDefaultPluginDisabled;

  // set by pref plugin.disable
  PRPackedBool mPluginsDisabled;

  nsPluginInstanceTagList mPluginInstanceTagList;
  nsTArray<PRLibrary*> mUnusedLibraries;

  nsCOMPtr<nsIFile> mPluginRegFile;
  nsCOMPtr<nsIPrefBranch> mPrefService;
#ifdef XP_WIN
  nsRefPtr<nsPluginDirServiceProvider> mPrivateDirServiceProvider;
#endif

  nsWeakPtr mCurrentDocument; // weak reference, we use it to id document only

  static nsIFile *sPluginTempDir;

  // We need to hold a global ptr to ourselves because we register for
  // two different CIDs for some reason...
  static nsPluginHost* sInst;
};

class NS_STACK_CLASS PluginDestructionGuard : protected PRCList
{
public:
  PluginDestructionGuard(nsIPluginInstance *aInstance)
    : mInstance(aInstance)
  {
    Init();
  }

  PluginDestructionGuard(NPP npp)
    : mInstance(npp ? static_cast<nsNPAPIPluginInstance*>(npp->ndata) : nsnull)
  {
    Init();
  }

  ~PluginDestructionGuard();

  static PRBool DelayDestroy(nsIPluginInstance *aInstance);

protected:
  void Init()
  {
    NS_ASSERTION(NS_IsMainThread(), "Should be on the main thread");

    mDelayedDestroy = PR_FALSE;

    PR_INIT_CLIST(this);
    PR_INSERT_BEFORE(this, &sListHead);
  }

  nsCOMPtr<nsIPluginInstance> mInstance;
  PRBool mDelayedDestroy;

  static PRCList sListHead;
};

#endif // nsPluginHost_h_

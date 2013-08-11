/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 *   Tim Copperfield <timecop@network.email.ne.jp>
 *   Roland Mainz <roland.mainz@informatik.med.uni-giessen.de>
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

#ifndef nsNPAPIPluginInstance_h_
#define nsNPAPIPluginInstance_h_

#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "nsIPlugin.h"
#include "nsIPluginInstance.h"
#include "nsIPluginTagInfo.h"
#include "nsPIDOMWindow.h"
#include "nsIPluginInstanceOwner.h"
#include "nsITimer.h"
#ifdef OJI
#include "nsIPluginInstanceOld.h"
#include "nsIPluginInstancePeer2.h"
#include "nsPIPluginInstancePeer.h"
#include "nsIScriptablePlugin.h"
#include "nsIPluginInstanceInternal.h"
#include "nsIJVMPluginInstance.h"
#endif

#include "npfunctions.h"
#include "mozilla/PluginLibrary.h"

class nsNPAPIPluginStreamListener;
class nsPIDOMWindow;

struct nsInstanceStream
{
  nsInstanceStream *mNext;
  nsNPAPIPluginStreamListener *mPluginStreamListener;

  nsInstanceStream();
  ~nsInstanceStream();
};

class nsNPAPITimer
{
public:
  NPP npp;
  uint32_t id;
  nsCOMPtr<nsITimer> timer;
  void (*callback)(NPP npp, uint32_t timerID);
};

class nsNPAPIPluginInstance : public nsIPluginInstance
#ifdef OJI
                             ,public nsIPluginInstanceOld,
                              public nsIScriptablePlugin,
                              public nsIPluginInstanceInternal,
                              public nsIJVMPluginInstance
#endif
{
private:
  typedef mozilla::PluginLibrary PluginLibrary;

public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPLUGININSTANCE

#ifdef OJI
  NS_DECL_NSISCRIPTABLEPLUGIN
  NS_DECL_NSIJVMPLUGININSTANCE

  // nsIPluginInstanceOld methods not declared elsewhere
  NS_IMETHOD Initialize(nsIPluginInstancePeer* peer);

  NS_IMETHOD GetPeer(nsIPluginInstancePeer* *resultingPeer);

  NS_IMETHOD Destroy(void);

  NS_IMETHOD NewStream(nsIPluginStreamListener** listener);

  // nsIPluginInstanceInternal methods not declared elsewhere
  virtual JSObject *GetJSObject(JSContext *cx);

  virtual PRUint16 GetPluginAPIVersion();

  // Helper methods
  void SetShadow(nsIPluginInstanceOld *shadow);
  nsIPluginInstanceOld *GetShadow();
#endif

  nsresult GetNPP(NPP * aNPP);

  // Return the callbacks for the plugin instance.
  nsresult GetCallbacks(const NPPluginFuncs ** aCallbacks);

  NPError SetWindowless(PRBool aWindowless);

  NPError SetWindowlessLocal(PRBool aWindowlessLocal);

  NPError SetTransparent(PRBool aTransparent);

  NPError SetWantsAllNetworkStreams(PRBool aWantsAllNetworkStreams);

#ifdef XP_MACOSX
  void SetDrawingModel(NPDrawingModel aModel);
  NPDrawingModel GetDrawingModel();
#endif

  nsresult NewNotifyStream(nsIPluginStreamListener** listener, 
                           void* notifyData, 
                           PRBool aCallNotify,
                           const char * aURL);

#ifdef OJI
  nsNPAPIPluginInstance(nsIPluginInstanceOld *aShadow);
#endif

  nsNPAPIPluginInstance(NPPluginFuncs* callbacks, PluginLibrary* aLibrary);

  // Use Release() to destroy this
  virtual ~nsNPAPIPluginInstance();

  // returns the state of mStarted
  bool IsRunning() {
    return RUNNING == mRunning;
  }

  // Indicates whether the plugin is running normally or being shut down
  bool CanFireNotifications() {
    return mRunning == RUNNING || mRunning == DESTROYING;
  }

  // cache this NPAPI plugin
  nsresult SetCached(PRBool aCache);

  already_AddRefed<nsPIDOMWindow> GetDOMWindow();

  nsresult PrivateModeStateChanged();

  nsresult GetDOMElement(nsIDOMElement* *result);

  nsNPAPITimer* TimerWithID(uint32_t id, PRUint32* index);
  uint32_t      ScheduleTimer(uint32_t interval, NPBool repeat, void (*timerFunc)(NPP npp, uint32_t timerID));
  void          UnscheduleTimer(uint32_t timerID);
protected:
  nsresult InitializePlugin();

  // Calls NPP_GetValue
  nsresult GetValueInternal(NPPVariable variable, void* value);

  nsresult GetTagType(nsPluginTagType *result);
  nsresult GetAttributes(PRUint16& n, const char*const*& names,
                         const char*const*& values);
  nsresult GetParameters(PRUint16& n, const char*const*& names,
                         const char*const*& values);
  nsresult GetMode(nsPluginMode *result);

  // A pointer to the plugin's callback functions. This information
  // is actually stored in the plugin class (<b>nsPluginClass</b>),
  // and is common for all plugins of the class.
  NPPluginFuncs* mCallbacks;

  // The structure used to communicate between the plugin instance and
  // the browser.
  NPP_t mNPP;

#ifdef XP_MACOSX
  NPDrawingModel mDrawingModel;
#endif

  enum {
    NOT_STARTED,
    RUNNING,
    DESTROYING,
    DESTROYED
  } mRunning;

  // these are used to store the windowless properties
  // which the browser will later query
  PRPackedBool mWindowless;
  PRPackedBool mWindowlessLocal;
  PRPackedBool mTransparent;
  PRPackedBool mCached;
  PRPackedBool mWantsAllNetworkStreams;

public:
  // True while creating the plugin, or calling NPP_SetWindow() on it.
  PRPackedBool mInPluginInitCall;
  PluginLibrary* mLibrary;
  nsInstanceStream *mStreams;

private:
  nsTArray<PopupControlState> mPopupStates;

  char* mMIMEType;

  // Weak pointer to the owner. The owner nulls this out (by calling
  // InvalidateOwner()) when it's no longer our owner.
  nsIPluginInstanceOwner *mOwner;

  nsTArray<nsNPAPITimer*> mTimers;

#ifdef OJI
  nsIPluginInstanceOld *mShadow; // Strong
#endif
};

#endif // nsNPAPIPluginInstance_h_

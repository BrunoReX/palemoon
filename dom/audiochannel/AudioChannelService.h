/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_audiochannelservice_h__
#define mozilla_dom_audiochannelservice_h__

#include "nsAutoPtr.h"
#include "nsIObserver.h"
#include "nsTArray.h"
#include "nsITimer.h"

#include "AudioChannelCommon.h"
#include "AudioChannelAgent.h"
#include "nsClassHashtable.h"

namespace mozilla {
namespace dom {

class AudioChannelService
: public nsIObserver
, public nsITimerCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSITIMERCALLBACK

  /**
   * Returns the AudioChannelServce singleton. Only to be called from main thread.
   * @return NS_OK on proper assignment, NS_ERROR_FAILURE otherwise.
   */
  static AudioChannelService*
  GetAudioChannelService();

  /**
   * Shutdown the singleton.
   */
  static void Shutdown();

  /**
   * Any audio channel agent that starts playing should register itself to
   * this service, sharing the AudioChannelType.
   */
  virtual void RegisterAudioChannelAgent(AudioChannelAgent* aAgent,
                                         AudioChannelType aType);

  /**
   * Any  audio channel agent that stops playing should unregister itself to
   * this service.
   */
  virtual void UnregisterAudioChannelAgent(AudioChannelAgent* aAgent);

  /**
   * Return true if this type should be muted.
   */
  virtual bool GetMuted(AudioChannelAgent* aAgent, bool aElementHidden);

  /**
   * Return true if there is a content channel active in this process
   * or one of its subprocesses.
   */
  virtual bool ContentOrNormalChannelIsActive();

  /**
   * Return true iff a normal or content channel is active for the given process
   * ID.
   */
  virtual bool ProcessContentOrNormalChannelIsActive(uint64_t aChildID);

protected:
  void Notify();

  /**
   * Send the audio-channel-changed notification for the given process ID if
   * needed.
   */
  void SendAudioChannelChangedNotification(uint64_t aChildID);

  /* Register/Unregister IPC types: */
  void RegisterType(AudioChannelType aType, uint64_t aChildID);
  void UnregisterType(AudioChannelType aType, bool aElementHidden,
                      uint64_t aChildID);
  void UnregisterTypeInternal(AudioChannelType aType, bool aElementHidden,
                              uint64_t aChildID);

  bool GetMutedInternal(AudioChannelType aType, uint64_t aChildID,
                        bool aElementHidden, bool aElementWasHidden);

  /* Update the internal type value following the visibility changes */
  void UpdateChannelType(AudioChannelType aType, uint64_t aChildID,
                         bool aElementHidden, bool aElementWasHidden);

  AudioChannelService();
  virtual ~AudioChannelService();

  enum AudioChannelInternalType {
    AUDIO_CHANNEL_INT_NORMAL = 0,
    AUDIO_CHANNEL_INT_NORMAL_HIDDEN,
    AUDIO_CHANNEL_INT_CONTENT,
    AUDIO_CHANNEL_INT_CONTENT_HIDDEN,
    AUDIO_CHANNEL_INT_NOTIFICATION,
    AUDIO_CHANNEL_INT_NOTIFICATION_HIDDEN,
    AUDIO_CHANNEL_INT_ALARM,
    AUDIO_CHANNEL_INT_ALARM_HIDDEN,
    AUDIO_CHANNEL_INT_TELEPHONY,
    AUDIO_CHANNEL_INT_TELEPHONY_HIDDEN,
    AUDIO_CHANNEL_INT_RINGER,
    AUDIO_CHANNEL_INT_RINGER_HIDDEN,
    AUDIO_CHANNEL_INT_PUBLICNOTIFICATION,
    AUDIO_CHANNEL_INT_PUBLICNOTIFICATION_HIDDEN,
    AUDIO_CHANNEL_INT_LAST
  };

  bool ChannelsActiveWithHigherPriorityThan(AudioChannelInternalType aType);

  const char* ChannelName(AudioChannelType aType);

  AudioChannelInternalType GetInternalType(AudioChannelType aType,
                                           bool aElementHidden);

  class AudioChannelAgentData {
  public:
    AudioChannelAgentData(AudioChannelType aType,
                          bool aElementHidden,
                          bool aMuted)
    : mType(aType)
    , mElementHidden(aElementHidden)
    , mMuted(aMuted)
    {}

    AudioChannelType mType;
    bool mElementHidden;
    bool mMuted;
  };

  static PLDHashOperator
  NotifyEnumerator(AudioChannelAgent* aAgent,
                   AudioChannelAgentData* aData, void *aUnused);

  nsClassHashtable< nsPtrHashKey<AudioChannelAgent>, AudioChannelAgentData > mAgents;

  nsTArray<uint64_t> mChannelCounters[AUDIO_CHANNEL_INT_LAST];

  AudioChannelType mCurrentHigherChannel;
  AudioChannelType mCurrentVisibleHigherChannel;

  nsTArray<uint64_t> mActiveContentChildIDs;
  bool mActiveContentChildIDsFrozen;

  nsCOMPtr<nsITimer> mDeferTelChannelTimer;
  bool mTimerElementHidden;
  uint64_t mTimerChildID;

  // This is needed for IPC comunication between
  // AudioChannelServiceChild and this class.
  friend class ContentParent;
  friend class ContentChild;
};

} // namespace dom
} // namespace mozilla

#endif

/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDecoder.h"
#include <limits>
#include "nsNetUtil.h"
#include "AudioStream.h"
#include "nsHTMLVideoElement.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsTArray.h"
#include "VideoUtils.h"
#include "MediaDecoderStateMachine.h"
#include "nsTimeRanges.h"
#include "nsContentUtils.h"
#include "ImageContainer.h"
#include "MediaResource.h"
#include "nsError.h"
#include "mozilla/Preferences.h"

using namespace mozilla::layers;
using namespace mozilla::dom;

namespace mozilla {

// Number of milliseconds between progress events as defined by spec
static const uint32_t PROGRESS_MS = 350;

// Number of milliseconds of no data before a stall event is fired as defined by spec
static const uint32_t STALL_MS = 3000;

// Number of estimated seconds worth of data we need to have buffered
// ahead of the current playback position before we allow the media decoder
// to report that it can play through the entire media without the decode
// catching up with the download. Having this margin make the
// MediaDecoder::CanPlayThrough() calculation more stable in the case of
// fluctuating bitrates.
static const int64_t CAN_PLAY_THROUGH_MARGIN = 10;

#ifdef PR_LOGGING
PRLogModuleInfo* gMediaDecoderLog;
#define LOG(type, msg) PR_LOG(gMediaDecoderLog, type, msg)
#else
#define LOG(type, msg)
#endif

class MediaMemoryReporter
{
  MediaMemoryReporter();
  ~MediaMemoryReporter();
  static MediaMemoryReporter* sUniqueInstance;

  static MediaMemoryReporter* UniqueInstance() {
    if (!sUniqueInstance) {
      sUniqueInstance = new MediaMemoryReporter;
    }
    return sUniqueInstance;
  }

  typedef nsTArray<MediaDecoder*> DecodersArray;
  static DecodersArray& Decoders() {
    return UniqueInstance()->mDecoders;
  }

  DecodersArray mDecoders;

  nsCOMPtr<nsIMemoryReporter> mMediaDecodedVideoMemory;
  nsCOMPtr<nsIMemoryReporter> mMediaDecodedAudioMemory;

public:
  static void AddMediaDecoder(MediaDecoder* aDecoder) {
    Decoders().AppendElement(aDecoder);
  }

  static void RemoveMediaDecoder(MediaDecoder* aDecoder) {
    DecodersArray& decoders = Decoders();
    decoders.RemoveElement(aDecoder);
    if (decoders.IsEmpty()) {
      delete sUniqueInstance;
      sUniqueInstance = nullptr;
    }
  }

  static int64_t GetDecodedVideoMemory() {
    DecodersArray& decoders = Decoders();
    int64_t result = 0;
    for (size_t i = 0; i < decoders.Length(); ++i) {
      result += decoders[i]->VideoQueueMemoryInUse();
    }
    return result;
  }

  static int64_t GetDecodedAudioMemory() {
    DecodersArray& decoders = Decoders();
    int64_t result = 0;
    for (size_t i = 0; i < decoders.Length(); ++i) {
      result += decoders[i]->AudioQueueMemoryInUse();
    }
    return result;
  }
};

NS_IMPL_THREADSAFE_ISUPPORTS1(MediaDecoder, nsIObserver)

void MediaDecoder::Pause()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
  if (mPlayState == PLAY_STATE_SEEKING || mPlayState == PLAY_STATE_ENDED) {
    mNextState = PLAY_STATE_PAUSED;
    return;
  }

  ChangeState(PLAY_STATE_PAUSED);
}

void MediaDecoder::SetVolume(double aVolume)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mInitialVolume = aVolume;
  if (mDecoderStateMachine) {
    mDecoderStateMachine->SetVolume(aVolume);
  }
}

void MediaDecoder::SetAudioCaptured(bool aCaptured)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mInitialAudioCaptured = aCaptured;
  if (mDecoderStateMachine) {
    mDecoderStateMachine->SetAudioCaptured(aCaptured);
  }
}

void MediaDecoder::ConnectDecodedStreamToOutputStream(OutputStreamData* aStream)
{
  NS_ASSERTION(!aStream->mPort, "Already connected?");

  // The output stream must stay in sync with the decoded stream, so if
  // either stream is blocked, we block the other.
  aStream->mPort = aStream->mStream->AllocateInputPort(mDecodedStream->mStream,
      MediaInputPort::FLAG_BLOCK_INPUT | MediaInputPort::FLAG_BLOCK_OUTPUT);
  // Unblock the output stream now. While it's connected to mDecodedStream,
  // mDecodedStream is responsible for controlling blocking.
  aStream->mStream->ChangeExplicitBlockerCount(-1);
}

MediaDecoder::DecodedStreamData::DecodedStreamData(MediaDecoder* aDecoder,
                                                       int64_t aInitialTime,
                                                       SourceMediaStream* aStream)
  : mLastAudioPacketTime(-1),
    mLastAudioPacketEndTime(-1),
    mAudioFramesWritten(0),
    mInitialTime(aInitialTime),
    mNextVideoTime(aInitialTime),
    mStreamInitialized(false),
    mHaveSentFinish(false),
    mHaveSentFinishAudio(false),
    mHaveSentFinishVideo(false),
    mStream(aStream),
    mMainThreadListener(new DecodedStreamMainThreadListener(aDecoder)),
    mHaveBlockedForPlayState(false)
{
  mStream->AddMainThreadListener(mMainThreadListener);
}

MediaDecoder::DecodedStreamData::~DecodedStreamData()
{
  mStream->RemoveMainThreadListener(mMainThreadListener);
  mStream->Destroy();
}

void MediaDecoder::DestroyDecodedStream()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  GetReentrantMonitor().AssertCurrentThreadIn();

  // All streams are having their SourceMediaStream disconnected, so they
  // need to be explicitly blocked again.
  for (uint32_t i = 0; i < mOutputStreams.Length(); ++i) {
    OutputStreamData& os = mOutputStreams[i];
    // During cycle collection, nsDOMMediaStream can be destroyed and send
    // its Destroy message before this decoder is destroyed. So we have to
    // be careful not to send any messages after the Destroy().
    if (!os.mStream->IsDestroyed()) {
      os.mStream->ChangeExplicitBlockerCount(1);
    }
    // Explicitly remove all existing ports. This is not strictly necessary but it's
    // good form.
    os.mPort->Destroy();
    os.mPort = nullptr;
  }

  mDecodedStream = nullptr;
}

void MediaDecoder::RecreateDecodedStream(int64_t aStartTimeUSecs)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  GetReentrantMonitor().AssertCurrentThreadIn();
  LOG(PR_LOG_DEBUG, ("MediaDecoder::RecreateDecodedStream this=%p aStartTimeUSecs=%lld!",
                     this, (long long)aStartTimeUSecs));

  DestroyDecodedStream();

  mDecodedStream = new DecodedStreamData(this, aStartTimeUSecs,
    MediaStreamGraph::GetInstance()->CreateSourceStream(nullptr));

  // Note that the delay between removing ports in DestroyDecodedStream
  // and adding new ones won't cause a glitch since all graph operations
  // between main-thread stable states take effect atomically.
  for (uint32_t i = 0; i < mOutputStreams.Length(); ++i) {
    ConnectDecodedStreamToOutputStream(&mOutputStreams[i]);
  }

  mDecodedStream->mHaveBlockedForPlayState = mPlayState != PLAY_STATE_PLAYING;
  if (mDecodedStream->mHaveBlockedForPlayState) {
    mDecodedStream->mStream->ChangeExplicitBlockerCount(1);
  }
}

void MediaDecoder::NotifyDecodedStreamMainThreadStateChanged()
{
  if (mTriggerPlaybackEndedWhenSourceStreamFinishes && mDecodedStream &&
      mDecodedStream->mStream->IsFinished()) {
    mTriggerPlaybackEndedWhenSourceStreamFinishes = false;
    if (GetState() == PLAY_STATE_PLAYING) {
      nsCOMPtr<nsIRunnable> event =
        NS_NewRunnableMethod(this, &MediaDecoder::PlaybackEnded);
      NS_DispatchToCurrentThread(event);
    }
  }
}

void MediaDecoder::AddOutputStream(ProcessedMediaStream* aStream,
                                       bool aFinishWhenEnded)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  LOG(PR_LOG_DEBUG, ("MediaDecoder::AddOutputStream this=%p aStream=%p!",
                     this, aStream));

  {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    if (!mDecodedStream) {
      RecreateDecodedStream(mDecoderStateMachine ?
          int64_t(mDecoderStateMachine->GetCurrentTime()*USECS_PER_S) : 0);
    }
    OutputStreamData* os = mOutputStreams.AppendElement();
    os->Init(aStream, aFinishWhenEnded);
    ConnectDecodedStreamToOutputStream(os);
    if (aFinishWhenEnded) {
      // Ensure that aStream finishes the moment mDecodedStream does.
      aStream->SetAutofinish(true);
    }
  }

  // This can be called before Load(), in which case our mDecoderStateMachine
  // won't have been created yet and we can rely on Load() to schedule it
  // once it is created.
  if (mDecoderStateMachine) {
    // Make sure the state machine thread runs so that any buffered data
    // is fed into our stream.
    ScheduleStateMachineThread();
  }
}

double MediaDecoder::GetDuration()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mInfiniteStream) {
    return std::numeric_limits<double>::infinity();
  }
  if (mDuration >= 0) {
     return static_cast<double>(mDuration) / static_cast<double>(USECS_PER_S);
  }
  return std::numeric_limits<double>::quiet_NaN();
}

void MediaDecoder::SetInfinite(bool aInfinite)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mInfiniteStream = aInfinite;
}

bool MediaDecoder::IsInfinite()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  return mInfiniteStream;
}

MediaDecoder::MediaDecoder() :
  mDecoderPosition(0),
  mPlaybackPosition(0),
  mCurrentTime(0.0),
  mInitialVolume(0.0),
  mRequestedSeekTime(-1.0),
  mDuration(-1),
  mSeekable(true),
  mReentrantMonitor("media.decoder"),
  mPlayState(PLAY_STATE_PAUSED),
  mNextState(PLAY_STATE_PAUSED),
  mResourceLoaded(false),
  mIgnoreProgressData(false),
  mInfiniteStream(false),
  mTriggerPlaybackEndedWhenSourceStreamFinishes(false),
  mOwner(nullptr),
  mFrameBufferLength(0),
  mPinnedForSeek(false),
  mShuttingDown(false),
  mAudioChannelType(AUDIO_CHANNEL_NORMAL)
{
  MOZ_COUNT_CTOR(MediaDecoder);
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  MediaMemoryReporter::AddMediaDecoder(this);
#ifdef PR_LOGGING
  if (!gMediaDecoderLog) {
    gMediaDecoderLog = PR_NewLogModule("MediaDecoder");
  }
#endif
}

bool MediaDecoder::Init(MediaDecoderOwner* aOwner)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mOwner = aOwner;
  mVideoFrameContainer = aOwner->GetVideoFrameContainer();
  nsContentUtils::RegisterShutdownObserver(this);
  return true;
}

void MediaDecoder::Shutdown()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  if (mShuttingDown)
    return;

  mShuttingDown = true;

  {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    DestroyDecodedStream();
  }

  // This changes the decoder state to SHUTDOWN and does other things
  // necessary to unblock the state machine thread if it's blocked, so
  // the asynchronous shutdown in nsDestroyStateMachine won't deadlock.
  if (mDecoderStateMachine) {
    mDecoderStateMachine->Shutdown();
  }

  // Force any outstanding seek and byterange requests to complete
  // to prevent shutdown from deadlocking.
  if (mResource) {
    mResource->Close();
  }

  ChangeState(PLAY_STATE_SHUTDOWN);

  StopProgress();
  mOwner = nullptr;

  nsContentUtils::UnregisterShutdownObserver(this);
}

MediaDecoder::~MediaDecoder()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  MediaMemoryReporter::RemoveMediaDecoder(this);
  UnpinForSeek();
  MOZ_COUNT_DTOR(MediaDecoder);
}

nsresult MediaDecoder::OpenResource(MediaResource* aResource,
                                        nsIStreamListener** aStreamListener)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (aStreamListener) {
    *aStreamListener = nullptr;
  }

  {
    // Hold the lock while we do this to set proper lock ordering
    // expectations for dynamic deadlock detectors: decoder lock(s)
    // should be grabbed before the cache lock
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());

    nsresult rv = aResource->Open(aStreamListener);
    if (NS_FAILED(rv)) {
      LOG(PR_LOG_DEBUG, ("%p Failed to open stream!", this));
      delete aResource;
      return rv;
    }

    mResource = aResource;
  }
  return NS_OK;
}

nsresult MediaDecoder::Load(MediaResource* aResource,
                                nsIStreamListener** aStreamListener,
                                MediaDecoder* aCloneDonor)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  nsresult rv = OpenResource(aResource, aStreamListener);
  NS_ENSURE_SUCCESS(rv, rv);

  mDecoderStateMachine = CreateStateMachine();
  if (!mDecoderStateMachine) {
    LOG(PR_LOG_DEBUG, ("%p Failed to create state machine!", this));
    return NS_ERROR_FAILURE;
  }

  return InitializeStateMachine(aCloneDonor);
}

nsresult MediaDecoder::InitializeStateMachine(MediaDecoder* aCloneDonor)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  MediaDecoder* cloneDonor = static_cast<MediaDecoder*>(aCloneDonor);
  if (NS_FAILED(mDecoderStateMachine->Init(cloneDonor ?
                                           cloneDonor->mDecoderStateMachine : nullptr))) {
    LOG(PR_LOG_DEBUG, ("%p Failed to init state machine!", this));
    return NS_ERROR_FAILURE;
  }
  {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    mDecoderStateMachine->SetSeekable(mSeekable);
    mDecoderStateMachine->SetDuration(mDuration);
    mDecoderStateMachine->SetVolume(mInitialVolume);
    mDecoderStateMachine->SetAudioCaptured(mInitialAudioCaptured);

    if (mFrameBufferLength > 0) {
      // The valid mFrameBufferLength value was specified earlier
      mDecoderStateMachine->SetFrameBufferLength(mFrameBufferLength);
    }
  }

  ChangeState(PLAY_STATE_LOADING);

  return ScheduleStateMachineThread();
}

nsresult MediaDecoder::RequestFrameBufferLength(uint32_t aLength)
{
  if (aLength < FRAMEBUFFER_LENGTH_MIN || aLength > FRAMEBUFFER_LENGTH_MAX) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }
  mFrameBufferLength = aLength;

  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
  if (mDecoderStateMachine) {
      mDecoderStateMachine->SetFrameBufferLength(aLength);
  }
  return NS_OK;
}

nsresult MediaDecoder::ScheduleStateMachineThread()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  NS_ASSERTION(mDecoderStateMachine,
               "Must have state machine to start state machine thread");
  NS_ENSURE_STATE(mDecoderStateMachine);

  if (mShuttingDown)
    return NS_OK;
  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
  MediaDecoderStateMachine* m =
    static_cast<MediaDecoderStateMachine*>(mDecoderStateMachine.get());
  return m->ScheduleStateMachine();
}

nsresult MediaDecoder::Play()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
  NS_ASSERTION(mDecoderStateMachine != nullptr, "Should have state machine.");
  nsresult res = ScheduleStateMachineThread();
  NS_ENSURE_SUCCESS(res,res);
  if (mPlayState == PLAY_STATE_SEEKING) {
    mNextState = PLAY_STATE_PLAYING;
    return NS_OK;
  }
  if (mPlayState == PLAY_STATE_ENDED)
    return Seek(0);

  ChangeState(PLAY_STATE_PLAYING);
  return NS_OK;
}

/**
 * Returns true if aValue is inside a range of aRanges, and put the range
 * index in aIntervalIndex if it is not null.
 * If aValue is not inside a range, false is returned, and aIntervalIndex, if
 * not null, is set to the index of the range which ends immediately before aValue
 * (and can be -1 if aValue is before aRanges.Start(0)).
 */
static bool IsInRanges(nsTimeRanges& aRanges, double aValue, int32_t& aIntervalIndex) {
  uint32_t length;
  aRanges.GetLength(&length);
  for (uint32_t i = 0; i < length; i++) {
    double start, end;
    aRanges.Start(i, &start);
    if (start > aValue) {
      aIntervalIndex = i - 1;
      return false;
    }
    aRanges.End(i, &end);
    if (aValue <= end) {
      aIntervalIndex = i;
      return true;
    }
  }
  aIntervalIndex = length - 1;
  return false;
}

nsresult MediaDecoder::Seek(double aTime)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());

  NS_ABORT_IF_FALSE(aTime >= 0.0, "Cannot seek to a negative value.");

  nsTimeRanges seekable;
  nsresult res;
  uint32_t length = 0;
  res = GetSeekable(&seekable);
  NS_ENSURE_SUCCESS(res, NS_OK);

  seekable.GetLength(&length);
  if (!length) {
    return NS_OK;
  }

  // If the position we want to seek to is not in a seekable range, we seek
  // to the closest position in the seekable ranges instead. If two positions
  // are equally close, we seek to the closest position from the currentTime.
  // See seeking spec, point 7 :
  // http://www.whatwg.org/specs/web-apps/current-work/multipage/the-video-element.html#seeking
  int32_t range = 0;
  if (!IsInRanges(seekable, aTime, range)) {
    if (range != -1) {
      // |range + 1| can't be negative, because the only possible negative value
      // for |range| is -1.
      if (uint32_t(range + 1) < length) {
        double leftBound, rightBound;
        res = seekable.End(range, &leftBound);
        NS_ENSURE_SUCCESS(res, NS_OK);
        res = seekable.Start(range + 1, &rightBound);
        NS_ENSURE_SUCCESS(res, NS_OK);
        double distanceLeft = NS_ABS(leftBound - aTime);
        double distanceRight = NS_ABS(rightBound - aTime);
        if (distanceLeft == distanceRight) {
          distanceLeft = NS_ABS(leftBound - mCurrentTime);
          distanceRight = NS_ABS(rightBound - mCurrentTime);
        } 
        aTime = (distanceLeft < distanceRight) ? leftBound : rightBound;
      } else {
        // Seek target is after the end last range in seekable data.
        // Clamp the seek target to the end of the last seekable range.
        res = seekable.End(length - 1, &aTime);
        NS_ENSURE_SUCCESS(res, NS_OK);
      }
    } else {
      // aTime is before the first range in |seekable|, the closest point we can
      // seek to is the start of the first range.
      seekable.Start(0, &aTime);
    }
  }

  mRequestedSeekTime = aTime;
  mCurrentTime = aTime;

  // If we are already in the seeking state, then setting mRequestedSeekTime
  // above will result in the new seek occurring when the current seek
  // completes.
  if (mPlayState != PLAY_STATE_SEEKING) {
    bool paused = false;
    if (mOwner) {
      paused = mOwner->GetPaused();
    }
    mNextState = paused ? PLAY_STATE_PAUSED : PLAY_STATE_PLAYING;
    PinForSeek();
    ChangeState(PLAY_STATE_SEEKING);
  }

  return ScheduleStateMachineThread();
}

nsresult MediaDecoder::PlaybackRateChanged()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  return NS_ERROR_NOT_IMPLEMENTED;
}

double MediaDecoder::GetCurrentTime()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  return mCurrentTime;
}

already_AddRefed<nsIPrincipal> MediaDecoder::GetCurrentPrincipal()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  return mResource ? mResource->GetCurrentPrincipal() : nullptr;
}

void MediaDecoder::AudioAvailable(float* aFrameBuffer,
                                      uint32_t aFrameBufferLength,
                                      float aTime)
{
  // Auto manage the frame buffer's memory. If we return due to an error
  // here, this ensures we free the memory. Otherwise, we pass off ownership
  // to HTMLMediaElement::NotifyAudioAvailable().
  nsAutoArrayPtr<float> frameBuffer(aFrameBuffer);
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mShuttingDown || !mOwner) {
    return;
  }
  mOwner->NotifyAudioAvailable(frameBuffer.forget(), aFrameBufferLength, aTime);
}

void MediaDecoder::MetadataLoaded(uint32_t aChannels,
                                      uint32_t aRate,
                                      bool aHasAudio,
                                      const MetadataTags* aTags)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mShuttingDown) {
    return;
  }

  {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    mDuration = mDecoderStateMachine ? mDecoderStateMachine->GetDuration() : -1;
    // Duration has changed so we should recompute playback rate
    UpdatePlaybackRate();
  }

  if (mDuration == -1) {
    SetInfinite(true);
  }

  if (mOwner) {
    // Make sure the element and the frame (if any) are told about
    // our new size.
    Invalidate();
    mOwner->MetadataLoaded(aChannels, aRate, aHasAudio, aTags);
  }

  if (!mResourceLoaded) {
    StartProgress();
  } else if (mOwner) {
    // Resource was loaded during metadata loading, when progress
    // events are being ignored. Fire the final progress event.
    mOwner->DispatchAsyncEvent(NS_LITERAL_STRING("progress"));
  }

  // Only inform the element of FirstFrameLoaded if not doing a load() in order
  // to fulfill a seek, otherwise we'll get multiple loadedfirstframe events.
  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
  bool resourceIsLoaded = !mResourceLoaded && mResource &&
    mResource->IsDataCachedToEndOfResource(mDecoderPosition);
  if (mOwner) {
    mOwner->FirstFrameLoaded(resourceIsLoaded);
  }

  // This can run cache callbacks.
  mResource->EnsureCacheUpToDate();

  // The element can run javascript via events
  // before reaching here, so only change the
  // state if we're still set to the original
  // loading state.
  if (mPlayState == PLAY_STATE_LOADING) {
    if (mRequestedSeekTime >= 0.0) {
      ChangeState(PLAY_STATE_SEEKING);
    }
    else {
      ChangeState(mNextState);
    }
  }

  if (resourceIsLoaded) {
    ResourceLoaded();
  }

  // Run NotifySuspendedStatusChanged now to give us a chance to notice
  // that autoplay should run.
  NotifySuspendedStatusChanged();
}

void MediaDecoder::ResourceLoaded()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  // Don't handle ResourceLoaded if we are shutting down, or if
  // we need to ignore progress data due to seeking (in the case
  // that the seek results in reaching end of file, we get a bogus call
  // to ResourceLoaded).
  if (mShuttingDown)
    return;

  {
    // If we are seeking or loading then the resource loaded notification we get
    // should be ignored, since it represents the end of the seek request.
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    if (mIgnoreProgressData || mResourceLoaded || mPlayState == PLAY_STATE_LOADING)
      return;

    Progress(false);

    mResourceLoaded = true;
    StopProgress();
  }

  // Ensure the final progress event gets fired
  if (mOwner) {
    mOwner->ResourceLoaded();
  }
}

void MediaDecoder::NetworkError()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mShuttingDown)
    return;

  if (mOwner)
    mOwner->NetworkError();

  Shutdown();
}

void MediaDecoder::DecodeError()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mShuttingDown)
    return;

  if (mOwner)
    mOwner->DecodeError();

  Shutdown();
}

bool MediaDecoder::IsSeeking() const
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  return mPlayState == PLAY_STATE_SEEKING;
}

bool MediaDecoder::IsEnded() const
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  return mPlayState == PLAY_STATE_ENDED || mPlayState == PLAY_STATE_SHUTDOWN;
}

void MediaDecoder::PlaybackEnded()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  if (mShuttingDown || mPlayState == MediaDecoder::PLAY_STATE_SEEKING)
    return;

  {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());

    if (mDecodedStream && !mDecodedStream->mStream->IsFinished()) {
      // Wait for it to finish before firing PlaybackEnded()
      mTriggerPlaybackEndedWhenSourceStreamFinishes = true;
      return;
    }

    for (int32_t i = mOutputStreams.Length() - 1; i >= 0; --i) {
      OutputStreamData& os = mOutputStreams[i];
      if (os.mFinishWhenEnded) {
        // Shouldn't really be needed since mDecodedStream should already have
        // finished, but doesn't hurt.
        os.mStream->Finish();
        os.mPort->Destroy();
        os.mPort = nullptr;
        // Not really needed but it keeps the invariant that a stream not
        // connected to mDecodedStream is explicity blocked.
        os.mStream->ChangeExplicitBlockerCount(1);
        mOutputStreams.RemoveElementAt(i);
      }
    }
  }

  PlaybackPositionChanged();
  ChangeState(PLAY_STATE_ENDED);

  UpdateReadyStateForData();
  if (mOwner)  {
    mOwner->PlaybackEnded();
  }

  // This must be called after |mOwner->PlaybackEnded()| call above, in order
  // to fire the required durationchange.
  if (IsInfinite()) {
    SetInfinite(false);
  }
}

NS_IMETHODIMP MediaDecoder::Observe(nsISupports *aSubjet,
                                        const char *aTopic,
                                        const PRUnichar *someData)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0) {
    Shutdown();
  }

  return NS_OK;
}

MediaDecoder::Statistics
MediaDecoder::GetStatistics()
{
  NS_ASSERTION(NS_IsMainThread() || OnStateMachineThread(),
               "Should be on main or state machine thread.");
  Statistics result;

  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
  if (mResource) {
    result.mDownloadRate =
      mResource->GetDownloadRate(&result.mDownloadRateReliable);
    result.mDownloadPosition =
      mResource->GetCachedDataEnd(mDecoderPosition);
    result.mTotalBytes = mResource->GetLength();
    result.mPlaybackRate = ComputePlaybackRate(&result.mPlaybackRateReliable);
    result.mDecoderPosition = mDecoderPosition;
    result.mPlaybackPosition = mPlaybackPosition;
  }
  else {
    result.mDownloadRate = 0;
    result.mDownloadRateReliable = true;
    result.mPlaybackRate = 0;
    result.mPlaybackRateReliable = true;
    result.mDecoderPosition = 0;
    result.mPlaybackPosition = 0;
    result.mDownloadPosition = 0;
    result.mTotalBytes = 0;
  }

  return result;
}

double MediaDecoder::ComputePlaybackRate(bool* aReliable)
{
  GetReentrantMonitor().AssertCurrentThreadIn();
  NS_ASSERTION(NS_IsMainThread() || OnStateMachineThread(),
               "Should be on main or state machine thread.");

  int64_t length = mResource ? mResource->GetLength() : -1;
  if (mDuration >= 0 && length >= 0) {
    *aReliable = true;
    return length * static_cast<double>(USECS_PER_S) / mDuration;
  }
  return mPlaybackStatistics.GetRateAtLastStop(aReliable);
}

void MediaDecoder::UpdatePlaybackRate()
{
  NS_ASSERTION(NS_IsMainThread() || OnStateMachineThread(),
               "Should be on main or state machine thread.");
  GetReentrantMonitor().AssertCurrentThreadIn();
  if (!mResource)
    return;
  bool reliable;
  uint32_t rate = uint32_t(ComputePlaybackRate(&reliable));
  if (reliable) {
    // Avoid passing a zero rate
    rate = NS_MAX(rate, 1u);
  }
  else {
    // Set a minimum rate of 10,000 bytes per second ... sometimes we just
    // don't have good data
    rate = NS_MAX(rate, 10000u);
  }
  mResource->SetPlaybackRate(rate);
}

void MediaDecoder::NotifySuspendedStatusChanged()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (!mResource)
    return;
  MediaResource* activeStream;
  bool suspended = mResource->IsSuspendedByCache(&activeStream);

  if (mOwner) {
    if (suspended) {
      // If this is an autoplay element, we need to kick off its autoplaying
      // now so we consume data and hopefully free up cache space.
      mOwner->NotifyAutoplayDataReady();
    }
    mOwner->NotifySuspendedByCache(suspended);
    UpdateReadyStateForData();
  }
}

void MediaDecoder::NotifyBytesDownloaded()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  UpdateReadyStateForData();
  Progress(false);
}

void MediaDecoder::NotifyDownloadEnded(nsresult aStatus)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  if (aStatus == NS_BINDING_ABORTED) {
    // Download has been cancelled by user.
    if (mOwner) {
      mOwner->LoadAborted();
    }
    return;
  }

  {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    UpdatePlaybackRate();
  }

  if (NS_SUCCEEDED(aStatus)) {
    ResourceLoaded();
  }
  else if (aStatus != NS_BASE_STREAM_CLOSED) {
    NetworkError();
  }
  UpdateReadyStateForData();
}

void MediaDecoder::NotifyPrincipalChanged()
{
  if (mOwner) {
    mOwner->NotifyDecoderPrincipalChanged();
  }
}

void MediaDecoder::NotifyBytesConsumed(int64_t aBytes)
{
  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
  NS_ASSERTION(OnStateMachineThread() || mDecoderStateMachine->OnDecodeThread(),
               "Should be on play state machine or decode thread.");
  if (!mIgnoreProgressData) {
    mDecoderPosition += aBytes;
    mPlaybackStatistics.AddBytes(aBytes);
  }
}

void MediaDecoder::NextFrameUnavailableBuffering()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be called on main thread");
  if (!mOwner || mShuttingDown || !mDecoderStateMachine)
    return;

  mOwner->UpdateReadyStateForData(MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE_BUFFERING);
}

void MediaDecoder::NextFrameAvailable()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be called on main thread");
  if (!mOwner || mShuttingDown || !mDecoderStateMachine)
    return;

  mOwner->UpdateReadyStateForData(MediaDecoderOwner::NEXT_FRAME_AVAILABLE);
}

void MediaDecoder::NextFrameUnavailable()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be called on main thread");
  if (!mOwner || mShuttingDown || !mDecoderStateMachine)
    return;
  mOwner->UpdateReadyStateForData(MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE);
}

void MediaDecoder::UpdateReadyStateForData()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be called on main thread");
  if (!mOwner || mShuttingDown || !mDecoderStateMachine)
    return;
  MediaDecoderOwner::NextFrameStatus frameStatus =
    mDecoderStateMachine->GetNextFrameStatus();
  mOwner->UpdateReadyStateForData(frameStatus);
}

void MediaDecoder::SeekingStopped()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  if (mShuttingDown)
    return;

  bool seekWasAborted = false;
  {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());

    // An additional seek was requested while the current seek was
    // in operation.
    if (mRequestedSeekTime >= 0.0) {
      ChangeState(PLAY_STATE_SEEKING);
      seekWasAborted = true;
    } else {
      UnpinForSeek();
      ChangeState(mNextState);
    }
  }

  if (mOwner) {
    UpdateReadyStateForData();
    if (!seekWasAborted) {
      mOwner->SeekCompleted();
    }
  }
}

// This is called when seeking stopped *and* we're at the end of the
// media.
void MediaDecoder::SeekingStoppedAtEnd()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  if (mShuttingDown)
    return;

  bool fireEnded = false;
  bool seekWasAborted = false;
  {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());

    // An additional seek was requested while the current seek was
    // in operation.
    if (mRequestedSeekTime >= 0.0) {
      ChangeState(PLAY_STATE_SEEKING);
      seekWasAborted = true;
    } else {
      UnpinForSeek();
      fireEnded = true;
      ChangeState(PLAY_STATE_ENDED);
    }
  }

  if (mOwner) {
    UpdateReadyStateForData();
    if (!seekWasAborted) {
      mOwner->SeekCompleted();
      if (fireEnded) {
        mOwner->PlaybackEnded();
      }
    }
  }
}

void MediaDecoder::SeekingStarted()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mShuttingDown)
    return;

  if (mOwner) {
    UpdateReadyStateForData();
    mOwner->SeekStarted();
  }
}

void MediaDecoder::ChangeState(PlayState aState)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());

  if (mNextState == aState) {
    mNextState = PLAY_STATE_PAUSED;
  }

  if (mPlayState == PLAY_STATE_SHUTDOWN) {
    GetReentrantMonitor().NotifyAll();
    return;
  }

  if (mDecodedStream) {
    bool blockForPlayState = aState != PLAY_STATE_PLAYING;
    if (mDecodedStream->mHaveBlockedForPlayState != blockForPlayState) {
      mDecodedStream->mStream->ChangeExplicitBlockerCount(blockForPlayState ? 1 : -1);
      mDecodedStream->mHaveBlockedForPlayState = blockForPlayState;
    }
  }
  mPlayState = aState;
  if (mDecoderStateMachine) {
    switch (aState) {
    case PLAY_STATE_PLAYING:
      mDecoderStateMachine->Play();
      break;
    case PLAY_STATE_SEEKING:
      mDecoderStateMachine->Seek(mRequestedSeekTime);
      mRequestedSeekTime = -1.0;
      break;
    default:
      /* No action needed */
      break;
    }
  }
  GetReentrantMonitor().NotifyAll();
}

void MediaDecoder::PlaybackPositionChanged()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mShuttingDown)
    return;

  double lastTime = mCurrentTime;

  // Control the scope of the monitor so it is not
  // held while the timeupdate and the invalidate is run.
  {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    if (mDecoderStateMachine) {
      if (!IsSeeking()) {
        // Only update the current playback position if we're not seeking.
        // If we are seeking, the update could have been scheduled on the
        // state machine thread while we were playing but after the seek
        // algorithm set the current playback position on the main thread,
        // and we don't want to override the seek algorithm and change the
        // current time after the seek has started but before it has
        // completed.
        mCurrentTime = mDecoderStateMachine->GetCurrentTime();
      }
      mDecoderStateMachine->ClearPositionChangeFlag();
    }
  }

  // Invalidate the frame so any video data is displayed.
  // Do this before the timeupdate event so that if that
  // event runs JavaScript that queries the media size, the
  // frame has reflowed and the size updated beforehand.
  Invalidate();

  if (mOwner && lastTime != mCurrentTime) {
    FireTimeUpdate();
  }
}

void MediaDecoder::DurationChanged()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
  int64_t oldDuration = mDuration;
  mDuration = mDecoderStateMachine ? mDecoderStateMachine->GetDuration() : -1;
  // Duration has changed so we should recompute playback rate
  UpdatePlaybackRate();

  if (mOwner && oldDuration != mDuration && !IsInfinite()) {
    LOG(PR_LOG_DEBUG, ("%p duration changed to %lld", this, mDuration));
    mOwner->DispatchEvent(NS_LITERAL_STRING("durationchange"));
  }
}

void MediaDecoder::SetDuration(double aDuration)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mDuration = static_cast<int64_t>(NS_round(aDuration * static_cast<double>(USECS_PER_S)));

  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
  if (mDecoderStateMachine) {
    mDecoderStateMachine->SetDuration(mDuration);
  }

  // Duration has changed so we should recompute playback rate
  UpdatePlaybackRate();
}

void MediaDecoder::SetSeekable(bool aSeekable)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mSeekable = aSeekable;
  if (mDecoderStateMachine) {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    mDecoderStateMachine->SetSeekable(aSeekable);
  }
}

bool MediaDecoder::IsSeekable()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  return mSeekable;
}

nsresult MediaDecoder::GetSeekable(nsTimeRanges* aSeekable)
{
  //TODO : change 0.0 to GetInitialTime() when available
  double initialTime = 0.0;

  if (IsSeekable()) {
    double end = IsInfinite() ? std::numeric_limits<double>::infinity()
                              : initialTime + GetDuration();
    aSeekable->Add(initialTime, end);
    return NS_OK;
  }

  if (mDecoderStateMachine && mDecoderStateMachine->IsSeekableInBufferedRanges()) {
    return GetBuffered(aSeekable);
  } else {
    // The stream is not seekable using only buffered ranges, and is not
    // seekable. Don't allow seeking (return no ranges in |seekable|).
    return NS_OK;
  }
}

void MediaDecoder::SetEndTime(double aTime)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mDecoderStateMachine) {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    mDecoderStateMachine->SetFragmentEndTime(static_cast<int64_t>(aTime * USECS_PER_S));
  }
}

void MediaDecoder::Suspend()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mResource) {
    mResource->Suspend(true);
  }
}

void MediaDecoder::Resume(bool aForceBuffering)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mResource) {
    mResource->Resume();
  }
  if (aForceBuffering) {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    if (mDecoderStateMachine) {
      mDecoderStateMachine->StartBuffering();
    }
  }
}

void MediaDecoder::StopProgressUpdates()
{
  NS_ASSERTION(OnStateMachineThread() || OnDecodeThread(),
               "Should be on state machine or decode thread.");
  GetReentrantMonitor().AssertCurrentThreadIn();
  mIgnoreProgressData = true;
  if (mResource) {
    mResource->SetReadMode(MediaCacheStream::MODE_METADATA);
  }
}

void MediaDecoder::StartProgressUpdates()
{
  NS_ASSERTION(OnStateMachineThread() || OnDecodeThread(),
               "Should be on state machine or decode thread.");
  GetReentrantMonitor().AssertCurrentThreadIn();
  mIgnoreProgressData = false;
  if (mResource) {
    mResource->SetReadMode(MediaCacheStream::MODE_PLAYBACK);
    mDecoderPosition = mPlaybackPosition = mResource->Tell();
  }
}

void MediaDecoder::MoveLoadsToBackground()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mResource) {
    mResource->MoveLoadsToBackground();
  }
}

void MediaDecoder::UpdatePlaybackOffset(int64_t aOffset)
{
  ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
  mPlaybackPosition = NS_MAX(aOffset, mPlaybackPosition);
}

bool MediaDecoder::OnStateMachineThread() const
{
  return IsCurrentThread(MediaDecoderStateMachine::GetStateMachineThread());
}

void MediaDecoder::NotifyAudioAvailableListener()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mDecoderStateMachine) {
    ReentrantMonitorAutoEnter mon(GetReentrantMonitor());
    mDecoderStateMachine->NotifyAudioAvailableListener();
  }
}

bool MediaDecoder::OnDecodeThread() const {
  return mDecoderStateMachine->OnDecodeThread();
}

ReentrantMonitor& MediaDecoder::GetReentrantMonitor() {
  return mReentrantMonitor.GetReentrantMonitor();
}

ImageContainer* MediaDecoder::GetImageContainer()
{
  return mVideoFrameContainer ? mVideoFrameContainer->GetImageContainer() : nullptr;
}

void MediaDecoder::Invalidate()
{
  if (mVideoFrameContainer) {
    mVideoFrameContainer->Invalidate();
  }
}

// Constructs the time ranges representing what segments of the media
// are buffered and playable.
nsresult MediaDecoder::GetBuffered(nsTimeRanges* aBuffered) {
  if (mDecoderStateMachine) {
    return mDecoderStateMachine->GetBuffered(aBuffered);
  }
  return NS_ERROR_FAILURE;
}

int64_t MediaDecoder::VideoQueueMemoryInUse() {
  if (mDecoderStateMachine) {
    return mDecoderStateMachine->VideoQueueMemoryInUse();
  }
  return 0;
}

int64_t MediaDecoder::AudioQueueMemoryInUse() {
  if (mDecoderStateMachine) {
    return mDecoderStateMachine->AudioQueueMemoryInUse();
  }
  return 0;
}

void MediaDecoder::NotifyDataArrived(const char* aBuffer, uint32_t aLength, int64_t aOffset) {
  if (mDecoderStateMachine) {
    mDecoderStateMachine->NotifyDataArrived(aBuffer, aLength, aOffset);
  }
}

void MediaDecoder::UpdatePlaybackPosition(int64_t aTime)
{
  mDecoderStateMachine->UpdatePlaybackPosition(aTime);
}

// Provide access to the state machine object
MediaDecoderStateMachine* MediaDecoder::GetStateMachine() {
  return mDecoderStateMachine;
}

// Drop reference to state machine.  Only called during shutdown dance.
void MediaDecoder::ReleaseStateMachine() {
  mDecoderStateMachine = nullptr;
}

MediaDecoderOwner* MediaDecoder::GetMediaOwner() const
{
  return mOwner;
}

static void ProgressCallback(nsITimer* aTimer, void* aClosure)
{
  MediaDecoder* decoder = static_cast<MediaDecoder*>(aClosure);
  decoder->Progress(true);
}

void MediaDecoder::Progress(bool aTimer)
{
  if (!mOwner)
    return;

  TimeStamp now = TimeStamp::Now();

  if (!aTimer) {
    mDataTime = now;
  }

  // If PROGRESS_MS has passed since the last progress event fired and more
  // data has arrived since then, fire another progress event.
  if ((mProgressTime.IsNull() ||
       now - mProgressTime >= TimeDuration::FromMilliseconds(PROGRESS_MS)) &&
      !mDataTime.IsNull() &&
      now - mDataTime <= TimeDuration::FromMilliseconds(PROGRESS_MS)) {
    mOwner->DispatchAsyncEvent(NS_LITERAL_STRING("progress"));
    mProgressTime = now;
  }

  if (!mDataTime.IsNull() &&
      now - mDataTime >= TimeDuration::FromMilliseconds(STALL_MS)) {
    mOwner->DownloadStalled();
    // Null it out
    mDataTime = TimeStamp();
  }
}

nsresult MediaDecoder::StartProgress()
{
  if (mProgressTimer)
    return NS_OK;

  mProgressTimer = do_CreateInstance("@mozilla.org/timer;1");
  return mProgressTimer->InitWithFuncCallback(ProgressCallback,
                                              this,
                                              PROGRESS_MS,
                                              nsITimer::TYPE_REPEATING_SLACK);
}

nsresult MediaDecoder::StopProgress()
{
  if (!mProgressTimer)
    return NS_OK;

  nsresult rv = mProgressTimer->Cancel();
  mProgressTimer = nullptr;

  return rv;
}

void MediaDecoder::FireTimeUpdate()
{
  if (!mOwner)
    return;
  mOwner->FireTimeUpdate(true);
}

void MediaDecoder::PinForSeek()
{
  MediaResource* resource = GetResource();
  if (!resource || mPinnedForSeek) {
    return;
  }
  mPinnedForSeek = true;
  resource->Pin();
}

void MediaDecoder::UnpinForSeek()
{
  MediaResource* resource = GetResource();
  if (!resource || !mPinnedForSeek) {
    return;
  }
  mPinnedForSeek = false;
  resource->Unpin();
}

bool MediaDecoder::CanPlayThrough()
{
  Statistics stats = GetStatistics();
  if (!stats.mDownloadRateReliable || !stats.mPlaybackRateReliable) {
    return false;
  }
  int64_t bytesToDownload = stats.mTotalBytes - stats.mDownloadPosition;
  int64_t bytesToPlayback = stats.mTotalBytes - stats.mPlaybackPosition;
  double timeToDownload = bytesToDownload / stats.mDownloadRate;
  double timeToPlay = bytesToPlayback / stats.mPlaybackRate;

  if (timeToDownload > timeToPlay) {
    // Estimated time to download is greater than the estimated time to play.
    // We probably can't play through without having to stop to buffer.
    return false;
  }

  // Estimated time to download is less than the estimated time to play.
  // We can probably play through without having to buffer, but ensure that
  // we've got a reasonable amount of data buffered after the current
  // playback position, so that if the bitrate of the media fluctuates, or if
  // our download rate or decode rate estimation is otherwise inaccurate,
  // we don't suddenly discover that we need to buffer. This is particularly
  // required near the start of the media, when not much data is downloaded.
  int64_t readAheadMargin =
    static_cast<int64_t>(stats.mPlaybackRate * CAN_PLAY_THROUGH_MARGIN);
  return stats.mTotalBytes == stats.mDownloadPosition ||
         stats.mDownloadPosition > stats.mPlaybackPosition + readAheadMargin;
}

#ifdef MOZ_RAW
bool
MediaDecoder::IsRawEnabled()
{
  return Preferences::GetBool("media.raw.enabled");
}
#endif

#ifdef MOZ_OGG
bool
MediaDecoder::IsOpusEnabled()
{
#ifdef MOZ_OPUS
  return Preferences::GetBool("media.opus.enabled");
#else
  return false;
#endif
}

bool
MediaDecoder::IsOggEnabled()
{
  return Preferences::GetBool("media.ogg.enabled");
}
#endif

#ifdef MOZ_WAVE
bool
MediaDecoder::IsWaveEnabled()
{
  return Preferences::GetBool("media.wave.enabled");
}
#endif

#ifdef MOZ_WEBM
bool
MediaDecoder::IsWebMEnabled()
{
  return Preferences::GetBool("media.webm.enabled");
}
#endif

#ifdef MOZ_GSTREAMER
bool
MediaDecoder::IsGStreamerEnabled()
{
  return Preferences::GetBool("media.gstreamer.enabled");
}
#endif

#ifdef MOZ_WIDGET_GONK
bool
MediaDecoder::IsOmxEnabled()
{
  return Preferences::GetBool("media.omx.enabled", false);
}
#endif

#ifdef MOZ_MEDIA_PLUGINS
bool
MediaDecoder::IsMediaPluginsEnabled()
{
  return Preferences::GetBool("media.plugins.enabled");
}
#endif

#ifdef MOZ_DASH
bool
MediaDecoder::IsDASHEnabled()
{
  return Preferences::GetBool("media.dash.enabled");
}
#endif

MediaMemoryReporter* MediaMemoryReporter::sUniqueInstance;

NS_MEMORY_REPORTER_IMPLEMENT(MediaDecodedVideoMemory,
  "explicit/media/decoded-video",
  KIND_HEAP,
  UNITS_BYTES,
  MediaMemoryReporter::GetDecodedVideoMemory,
  "Memory used by decoded video frames.")

NS_MEMORY_REPORTER_IMPLEMENT(MediaDecodedAudioMemory,
  "explicit/media/decoded-audio",
  KIND_HEAP,
  UNITS_BYTES,
  MediaMemoryReporter::GetDecodedAudioMemory,
  "Memory used by decoded audio chunks.")

MediaMemoryReporter::MediaMemoryReporter()
  : mMediaDecodedVideoMemory(new NS_MEMORY_REPORTER_NAME(MediaDecodedVideoMemory))
  , mMediaDecodedAudioMemory(new NS_MEMORY_REPORTER_NAME(MediaDecodedAudioMemory))
{
  NS_RegisterMemoryReporter(mMediaDecodedVideoMemory);
  NS_RegisterMemoryReporter(mMediaDecodedAudioMemory);
}

MediaMemoryReporter::~MediaMemoryReporter()
{
  NS_UnregisterMemoryReporter(mMediaDecodedVideoMemory);
  NS_UnregisterMemoryReporter(mMediaDecodedAudioMemory);
}

} // namespace mozilla


/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaEngineDefault.h"

#include "nsCOMPtr.h"
#include "nsDOMFile.h"
#include "nsILocalFile.h"
#include "Layers.h"
#include "ImageContainer.h"
#include "ImageTypes.h"

#ifdef MOZ_WIDGET_ANDROID
#include "AndroidBridge.h"
#include "nsISupportsUtils.h"
#endif

#define CHANNELS 1
#define VIDEO_RATE USECS_PER_S
#define AUDIO_RATE 16000

namespace mozilla {

NS_IMPL_THREADSAFE_ISUPPORTS1(MediaEngineDefaultVideoSource, nsITimerCallback)
/**
 * Default video source.
 */

// Cannot be initialized in the class definition
const MediaEngineVideoOptions MediaEngineDefaultVideoSource::mOpts = {
  DEFAULT_WIDTH,
  DEFAULT_HEIGHT,
  DEFAULT_FPS,
  kVideoCodecI420
};

MediaEngineDefaultVideoSource::MediaEngineDefaultVideoSource()
  : mTimer(nullptr)
{
  mState = kReleased;
}

MediaEngineDefaultVideoSource::~MediaEngineDefaultVideoSource()
{}

void
MediaEngineDefaultVideoSource::GetName(nsAString& aName)
{
  aName.Assign(NS_LITERAL_STRING("Default Video Device"));
  return;
}

void
MediaEngineDefaultVideoSource::GetUUID(nsAString& aUUID)
{
  aUUID.Assign(NS_LITERAL_STRING("1041FCBD-3F12-4F7B-9E9B-1EC556DD5676"));
  return;
}

nsresult
MediaEngineDefaultVideoSource::Allocate()
{
  if (mState != kReleased) {
    return NS_ERROR_FAILURE;
  }

  mState = kAllocated;
  return NS_OK;
}

nsresult
MediaEngineDefaultVideoSource::Deallocate()
{
  if (mState != kStopped && mState != kAllocated) {
    return NS_ERROR_FAILURE;
  }
  mState = kReleased;
  return NS_OK;
}

const MediaEngineVideoOptions *
MediaEngineDefaultVideoSource::GetOptions()
{
  return &mOpts;
}

nsresult
MediaEngineDefaultVideoSource::Start(SourceMediaStream* aStream, TrackID aID)
{
  if (mState != kAllocated) {
    return NS_ERROR_FAILURE;
  }

  mTimer = do_CreateInstance(NS_TIMER_CONTRACTID);
  if (!mTimer) {
    return NS_ERROR_FAILURE;
  }

  mSource = aStream;

  // Allocate a single blank Image
  ImageFormat format = PLANAR_YCBCR;
  mImageContainer = layers::LayerManager::CreateImageContainer();

  nsRefPtr<layers::Image> image = mImageContainer->CreateImage(&format, 1);

  int len = ((DEFAULT_WIDTH * DEFAULT_HEIGHT) * 3 / 2);
  mImage = static_cast<layers::PlanarYCbCrImage*>(image.get());
  uint8_t* frame = (uint8_t*) PR_Malloc(len);
  memset(frame, 0x80, len); // Gray

  const uint8_t lumaBpp = 8;
  const uint8_t chromaBpp = 4;

  layers::PlanarYCbCrImage::Data data;
  data.mYChannel = frame;
  data.mYSize = gfxIntSize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
  data.mYStride = DEFAULT_WIDTH * lumaBpp / 8.0;
  data.mCbCrStride = DEFAULT_WIDTH * chromaBpp / 8.0;
  data.mCbChannel = frame + DEFAULT_HEIGHT * data.mYStride;
  data.mCrChannel = data.mCbChannel + DEFAULT_HEIGHT * data.mCbCrStride / 2;
  data.mCbCrSize = gfxIntSize(DEFAULT_WIDTH / 2, DEFAULT_HEIGHT / 2);
  data.mPicX = 0;
  data.mPicY = 0;
  data.mPicSize = gfxIntSize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
  data.mStereoMode = STEREO_MODE_MONO;

  // SetData copies data, so we can free the frame
  mImage->SetData(data);
  PR_Free(frame);

  // AddTrack takes ownership of segment
  VideoSegment *segment = new VideoSegment();
  segment->AppendFrame(image.forget(), USECS_PER_S / DEFAULT_FPS, gfxIntSize(DEFAULT_WIDTH, DEFAULT_HEIGHT));
  mSource->AddTrack(aID, VIDEO_RATE, 0, segment);

  // We aren't going to add any more tracks
  mSource->AdvanceKnownTracksTime(STREAM_TIME_MAX);

  // Remember TrackID so we can end it later
  mTrackID = aID;

  // Start timer for subsequent frames
  mTimer->InitWithCallback(this, 1000 / DEFAULT_FPS, nsITimer::TYPE_REPEATING_SLACK);
  mState = kStarted;

  return NS_OK;
}

nsresult
MediaEngineDefaultVideoSource::Stop()
{
  if (mState != kStarted) {
    return NS_ERROR_FAILURE;
  }
  if (!mTimer) {
    return NS_ERROR_FAILURE;
  }

  mTimer->Cancel();
  mTimer = NULL;

  mSource->EndTrack(mTrackID);
  mSource->Finish();

  mState = kStopped;
  return NS_OK;
}

nsresult
MediaEngineDefaultVideoSource::Snapshot(uint32_t aDuration, nsIDOMFile** aFile)
{
  *aFile = nullptr;

#ifndef MOZ_WIDGET_ANDROID
  return NS_ERROR_NOT_IMPLEMENTED;
#else
  if (!AndroidBridge::Bridge()) {
    return NS_ERROR_UNEXPECTED;
  }

  nsAutoString filePath;
  AndroidBridge::Bridge()->ShowFilePickerForMimeType(filePath, NS_LITERAL_STRING("image/*"));

  nsCOMPtr<nsIFile> file;
  nsresult rv = NS_NewLocalFile(filePath, false, getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*aFile = new nsDOMFileFile(file));
  return NS_OK;
#endif
}

NS_IMETHODIMP
MediaEngineDefaultVideoSource::Notify(nsITimer* aTimer)
{
  VideoSegment segment;

  nsRefPtr<layers::PlanarYCbCrImage> image = mImage;
  segment.AppendFrame(image.forget(), USECS_PER_S / DEFAULT_FPS, gfxIntSize(DEFAULT_WIDTH, DEFAULT_HEIGHT));
  mSource->AppendToTrack(mTrackID, &segment);

  return NS_OK;
}

void
MediaEngineDefaultVideoSource::NotifyPull(MediaStreamGraph* aGraph,
                                          StreamTime aDesiredTime)
{
  // Ignore - we push video data
}


/**
 * Default audio source.
 */
NS_IMPL_THREADSAFE_ISUPPORTS1(MediaEngineDefaultAudioSource, nsITimerCallback)

MediaEngineDefaultAudioSource::MediaEngineDefaultAudioSource()
  : mTimer(nullptr)
{
  mState = kReleased;
}

MediaEngineDefaultAudioSource::~MediaEngineDefaultAudioSource()
{}

void
MediaEngineDefaultAudioSource::NotifyPull(MediaStreamGraph* aGraph,
                                          StreamTime aDesiredTime)
{
  // Ignore - we push audio data
}

void
MediaEngineDefaultAudioSource::GetName(nsAString& aName)
{
  aName.Assign(NS_LITERAL_STRING("Default Audio Device"));
  return;
}

void
MediaEngineDefaultAudioSource::GetUUID(nsAString& aUUID)
{
  aUUID.Assign(NS_LITERAL_STRING("B7CBD7C1-53EF-42F9-8353-73F61C70C092"));
  return;
}

nsresult
MediaEngineDefaultAudioSource::Allocate()
{
  if (mState != kReleased) {
    return NS_ERROR_FAILURE;
  }

  mState = kAllocated;
  return NS_OK;
}

nsresult
MediaEngineDefaultAudioSource::Deallocate()
{
  if (mState != kStopped && mState != kAllocated) {
    return NS_ERROR_FAILURE;
  }
  mState = kReleased;
  return NS_OK;
}

nsresult
MediaEngineDefaultAudioSource::Start(SourceMediaStream* aStream, TrackID aID)
{
  if (mState != kAllocated) {
    return NS_ERROR_FAILURE;
  }

  mTimer = do_CreateInstance(NS_TIMER_CONTRACTID);
  if (!mTimer) {
    return NS_ERROR_FAILURE;
  }

  mSource = aStream;

  // AddTrack will take ownership of segment
  AudioSegment* segment = new AudioSegment();
  segment->Init(CHANNELS);
  mSource->AddTrack(aID, AUDIO_RATE, 0, segment);

  // We aren't going to add any more tracks
  mSource->AdvanceKnownTracksTime(STREAM_TIME_MAX);

  // Remember TrackID so we can finish later
  mTrackID = aID;

  // 1 Audio frame per Video frame
  mTimer->InitWithCallback(this, 1000 / MediaEngineDefaultVideoSource::DEFAULT_FPS, nsITimer::TYPE_REPEATING_SLACK);
  mState = kStarted;

  return NS_OK;
}

nsresult
MediaEngineDefaultAudioSource::Stop()
{
  if (mState != kStarted) {
    return NS_ERROR_FAILURE;
  }
  if (!mTimer) {
    return NS_ERROR_FAILURE;
  }

  mTimer->Cancel();
  mTimer = NULL;

  mSource->EndTrack(mTrackID);
  mSource->Finish();

  mState = kStopped;
  return NS_OK;
}

nsresult
MediaEngineDefaultAudioSource::Snapshot(uint32_t aDuration, nsIDOMFile** aFile)
{
   return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MediaEngineDefaultAudioSource::Notify(nsITimer* aTimer)
{
  AudioSegment segment;
  segment.Init(CHANNELS);
  segment.InsertNullDataAtStart(AUDIO_RATE/100); // 10ms of fake data

  mSource->AppendToTrack(mTrackID, &segment);

  return NS_OK;
}

void
MediaEngineDefault::EnumerateVideoDevices(nsTArray<nsRefPtr<MediaEngineVideoSource> >* aVSources) {
  MutexAutoLock lock(mMutex);
  int32_t found = false;
  int32_t len = mVSources.Length();

  for (int32_t i = 0; i < len; i++) {
    nsRefPtr<MediaEngineVideoSource> source = mVSources.ElementAt(i);
    aVSources->AppendElement(source);
    if (source->IsAvailable()) {
      found = true;
    }
  }

  // All streams are currently busy, just make a new one.
  if (!found) {
    nsRefPtr<MediaEngineVideoSource> newSource =
      new MediaEngineDefaultVideoSource();
    mVSources.AppendElement(newSource);
    aVSources->AppendElement(newSource);
  }
  return;
}

void
MediaEngineDefault::EnumerateAudioDevices(nsTArray<nsRefPtr<MediaEngineAudioSource> >* aASources) {
  MutexAutoLock lock(mMutex);
  int32_t len = mASources.Length();

  for (int32_t i = 0; i < len; i++) {
    nsRefPtr<MediaEngineAudioSource> source = mASources.ElementAt(i);
    if (source->IsAvailable()) {
      aASources->AppendElement(source);
    }
  }

  // All streams are currently busy, just make a new one.
  if (aASources->Length() == 0) {
    nsRefPtr<MediaEngineAudioSource> newSource =
      new MediaEngineDefaultAudioSource();
    mASources.AppendElement(newSource);
    aASources->AppendElement(newSource);
  }
  return;
}

} // namespace mozilla

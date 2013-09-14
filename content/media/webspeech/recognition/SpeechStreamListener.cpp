/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SpeechStreamListener.h"

#include "SpeechRecognition.h"

namespace mozilla {
namespace dom {

SpeechStreamListener::SpeechStreamListener(SpeechRecognition* aRecognition)
  : mRecognition(aRecognition)
{
}

SpeechStreamListener::~SpeechStreamListener()
{
  nsCOMPtr<nsIThread> mainThread;
  NS_GetMainThread(getter_AddRefs(mainThread));

  SpeechRecognition* forgottenRecognition = nullptr;
  mRecognition.swap(forgottenRecognition);
  NS_ProxyRelease(mainThread,
                  static_cast<nsDOMEventTargetHelper*>(forgottenRecognition));
}

void
SpeechStreamListener::NotifyQueuedTrackChanges(MediaStreamGraph* aGraph,
                                               TrackID aID,
                                               TrackRate aTrackRate,
                                               TrackTicks aTrackOffset,
                                               uint32_t aTrackEvents,
                                               const MediaSegment& aQueuedMedia)
{
  AudioSegment* audio = const_cast<AudioSegment*>(
    static_cast<const AudioSegment*>(&aQueuedMedia));

  AudioSegment::ChunkIterator iterator(*audio);
  while (!iterator.IsEnded()) {
    AudioSampleFormat format = iterator->mBufferFormat;

    MOZ_ASSERT(format == AUDIO_FORMAT_S16 || format == AUDIO_FORMAT_FLOAT32);

    if (format == AUDIO_FORMAT_S16) {
      ConvertAndDispatchAudioChunk<int16_t>(*iterator);
    } else if (format == AUDIO_FORMAT_FLOAT32) {
      ConvertAndDispatchAudioChunk<float>(*iterator);
    }

    iterator.Next();
  }
}

template<typename SampleFormatType> void
SpeechStreamListener::ConvertAndDispatchAudioChunk(AudioChunk& aChunk)
{
  nsRefPtr<SharedBuffer> samples(SharedBuffer::Create(aChunk.mDuration *
                                                      1 * // channel
                                                      sizeof(int16_t)));

  const SampleFormatType* from =
    static_cast<const SampleFormatType*>(aChunk.mChannelData[0]);

  int16_t* to = static_cast<int16_t*>(samples->Data());
  ConvertAudioSamplesWithScale(from, to, aChunk.mDuration, aChunk.mVolume);

  mRecognition->FeedAudioData(samples.forget(), aChunk.mDuration, this);
  return;
}

void
SpeechStreamListener::NotifyFinished(MediaStreamGraph* aGraph)
{
  // TODO dispatch SpeechEnd event so services can be informed
}

} // namespace dom
} // namespace mozilla

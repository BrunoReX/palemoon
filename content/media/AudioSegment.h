/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_AUDIOSEGMENT_H_
#define MOZILLA_AUDIOSEGMENT_H_

#include "MediaSegment.h"
#include "nsISupportsImpl.h"
#include "AudioSampleFormat.h"
#include "SharedBuffer.h"

namespace mozilla {

class AudioStream;

/**
 * For auto-arrays etc, guess this as the common number of channels.
 */
const int GUESS_AUDIO_CHANNELS = 2;

// We ensure that the graph advances in steps that are multiples of the Web
// Audio block size
const uint32_t WEBAUDIO_BLOCK_SIZE_BITS = 7;
const uint32_t WEBAUDIO_BLOCK_SIZE = 1 << WEBAUDIO_BLOCK_SIZE_BITS;

void InterleaveAndConvertBuffer(const void** aSourceChannels,
                                AudioSampleFormat aSourceFormat,
                                int32_t aLength, float aVolume,
                                int32_t aChannels,
                                AudioDataValue* aOutput);

/**
 * Given an array of input channels (aChannelData), downmix to aOutputChannels,
 * interleave the channel data. A total of aOutputChannels*aDuration
 * interleaved samples will be copied to a channel buffer in aOutput.
 */
void DownmixAndInterleave(const nsTArray<const void*>& aChannelData,
                          AudioSampleFormat aSourceFormat, int32_t aDuration,
                          float aVolume, uint32_t aOutputChannels,
                          AudioDataValue* aOutput);

/**
 * An AudioChunk represents a multi-channel buffer of audio samples.
 * It references an underlying ThreadSharedObject which manages the lifetime
 * of the buffer. An AudioChunk maintains its own duration and channel data
 * pointers so it can represent a subinterval of a buffer without copying.
 * An AudioChunk can store its individual channels anywhere; it maintains
 * separate pointers to each channel's buffer.
 */
struct AudioChunk {
  typedef mozilla::AudioSampleFormat SampleFormat;

  // Generic methods
  void SliceTo(TrackTicks aStart, TrackTicks aEnd)
  {
    NS_ASSERTION(aStart >= 0 && aStart < aEnd && aEnd <= mDuration,
                 "Slice out of bounds");
    if (mBuffer) {
      MOZ_ASSERT(aStart < INT32_MAX, "Can't slice beyond 32-bit sample lengths");
      for (uint32_t channel = 0; channel < mChannelData.Length(); ++channel) {
        mChannelData[channel] = AddAudioSampleOffset(mChannelData[channel],
            mBufferFormat, int32_t(aStart));
      }
    }
    mDuration = aEnd - aStart;
  }
  TrackTicks GetDuration() const { return mDuration; }
  bool CanCombineWithFollowing(const AudioChunk& aOther) const
  {
    if (aOther.mBuffer != mBuffer) {
      return false;
    }
    if (mBuffer) {
      NS_ASSERTION(aOther.mBufferFormat == mBufferFormat,
                   "Wrong metadata about buffer");
      NS_ASSERTION(aOther.mChannelData.Length() == mChannelData.Length(),
                   "Mismatched channel count");
      if (mDuration > INT32_MAX) {
        return false;
      }
      for (uint32_t channel = 0; channel < mChannelData.Length(); ++channel) {
        if (aOther.mChannelData[channel] != AddAudioSampleOffset(mChannelData[channel],
            mBufferFormat, int32_t(mDuration))) {
          return false;
        }
      }
    }
    return true;
  }
  bool IsNull() const { return mBuffer == nullptr; }
  void SetNull(TrackTicks aDuration)
  {
    mBuffer = nullptr;
    mChannelData.Clear();
    mDuration = aDuration;
    mVolume = 1.0f;
  }

  TrackTicks mDuration; // in frames within the buffer
  nsRefPtr<ThreadSharedObject> mBuffer; // the buffer object whose lifetime is managed; null means data is all zeroes
  nsTArray<const void*> mChannelData; // one pointer per channel; empty if and only if mBuffer is null
  float mVolume; // volume multiplier to apply (1.0f if mBuffer is nonnull)
  SampleFormat mBufferFormat; // format of frames in mBuffer (only meaningful if mBuffer is nonnull)
};

/**
 * A list of audio samples consisting of a sequence of slices of SharedBuffers.
 * The audio rate is determined by the track, not stored in this class.
 */
class AudioSegment : public MediaSegmentBase<AudioSegment, AudioChunk> {
public:
  typedef mozilla::AudioSampleFormat SampleFormat;

  AudioSegment() : MediaSegmentBase<AudioSegment, AudioChunk>(AUDIO) {}

  void AppendFrames(already_AddRefed<ThreadSharedObject> aBuffer,
                    const nsTArray<const float*>& aChannelData,
                    int32_t aDuration)
  {
    AudioChunk* chunk = AppendChunk(aDuration);
    chunk->mBuffer = aBuffer;
    for (uint32_t channel = 0; channel < aChannelData.Length(); ++channel) {
      chunk->mChannelData.AppendElement(aChannelData[channel]);
    }
    chunk->mVolume = 1.0f;
    chunk->mBufferFormat = AUDIO_FORMAT_FLOAT32;
  }
  void AppendFrames(already_AddRefed<ThreadSharedObject> aBuffer,
                    const nsTArray<const int16_t*>& aChannelData,
                    int32_t aDuration)
  {
    AudioChunk* chunk = AppendChunk(aDuration);
    chunk->mBuffer = aBuffer;
    for (uint32_t channel = 0; channel < aChannelData.Length(); ++channel) {
      chunk->mChannelData.AppendElement(aChannelData[channel]);
    }
    chunk->mVolume = 1.0f;
    chunk->mBufferFormat = AUDIO_FORMAT_S16;
  }
  // Consumes aChunk, and returns a pointer to the persistent copy of aChunk
  // in the segment.
  AudioChunk* AppendAndConsumeChunk(AudioChunk* aChunk)
  {
    AudioChunk* chunk = AppendChunk(aChunk->mDuration);
    chunk->mBuffer = aChunk->mBuffer.forget();
    chunk->mChannelData.SwapElements(aChunk->mChannelData);
    chunk->mVolume = aChunk->mVolume;
    chunk->mBufferFormat = aChunk->mBufferFormat;
    return chunk;
  }
  void ApplyVolume(float aVolume);
  void WriteTo(AudioStream* aOutput);

  static Type StaticType() { return AUDIO; }
};

}

#endif /* MOZILLA_AUDIOSEGMENT_H_ */

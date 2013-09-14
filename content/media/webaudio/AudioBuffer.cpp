/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioBuffer.h"
#include "mozilla/dom/AudioBufferBinding.h"
#include "nsContentUtils.h"
#include "AudioContext.h"
#include "jsfriendapi.h"
#include "mozilla/ErrorResult.h"
#include "AudioSegment.h"
#include "nsIScriptError.h"
#include "nsPIDOMWindow.h"
#include "AudioChannelFormat.h"
#include "mozilla/PodOperations.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(AudioBuffer)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mContext)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mJSChannels)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  tmp->ClearJSChannels();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(AudioBuffer)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(AudioBuffer)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
  for (uint32_t i = 0; i < tmp->mJSChannels.Length(); ++i) {
    NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mJSChannels[i])
  }
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(AudioBuffer, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(AudioBuffer, Release)

AudioBuffer::AudioBuffer(AudioContext* aContext, uint32_t aLength,
                         float aSampleRate)
  : mContext(aContext),
    mLength(aLength),
    mSampleRate(aSampleRate)
{
  SetIsDOMBinding();

  nsContentUtils::HoldJSObjects(this, NS_CYCLE_COLLECTION_PARTICIPANT(AudioBuffer));
}

AudioBuffer::~AudioBuffer()
{
  ClearJSChannels();
}

void
AudioBuffer::ClearJSChannels()
{
  mJSChannels.Clear();
  nsContentUtils::DropJSObjects(this);
}

bool
AudioBuffer::InitializeBuffers(uint32_t aNumberOfChannels, JSContext* aJSContext)
{
  if (!mJSChannels.SetCapacity(aNumberOfChannels)) {
    return false;
  }
  for (uint32_t i = 0; i < aNumberOfChannels; ++i) {
    JS::RootedObject array(aJSContext, JS_NewFloat32Array(aJSContext, mLength));
    if (!array) {
      return false;
    }
    mJSChannels.AppendElement(array.get());
  }

  return true;
}

JSObject*
AudioBuffer::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aScope)
{
  return AudioBufferBinding::Wrap(aCx, aScope, this);
}

bool
AudioBuffer::RestoreJSChannelData(JSContext* aJSContext)
{
  if (mSharedChannels) {
    for (uint32_t i = 0; i < mJSChannels.Length(); ++i) {
      const float* data = mSharedChannels->GetData(i);
      // The following code first zeroes the array and then copies our data
      // into it. We could avoid this with additional JS APIs to construct
      // an array (or ArrayBuffer) containing initial data.
      JS::RootedObject array(aJSContext, JS_NewFloat32Array(aJSContext, mLength));
      if (!array) {
        return false;
      }
      memcpy(JS_GetFloat32ArrayData(array), data, sizeof(float)*mLength);
      mJSChannels[i] = array;
    }

    mSharedChannels = nullptr;
  }

  return true;
}

void
AudioBuffer::SetRawChannelContents(JSContext* aJSContext, uint32_t aChannel,
                                   float* aContents)
{
  PodCopy(JS_GetFloat32ArrayData(mJSChannels[aChannel]), aContents, mLength);
}

JSObject*
AudioBuffer::GetChannelData(JSContext* aJSContext, uint32_t aChannel,
                            ErrorResult& aRv)
{
  if (aChannel >= NumberOfChannels()) {
    aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return nullptr;
  }

  if (!RestoreJSChannelData(aJSContext)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }

  return mJSChannels[aChannel];
}

static already_AddRefed<ThreadSharedFloatArrayBufferList>
StealJSArrayDataIntoThreadSharedFloatArrayBufferList(JSContext* aJSContext,
                                                     const nsTArray<JSObject*>& aJSArrays)
{
  nsRefPtr<ThreadSharedFloatArrayBufferList> result =
    new ThreadSharedFloatArrayBufferList(aJSArrays.Length());
  for (uint32_t i = 0; i < aJSArrays.Length(); ++i) {
    JS::RootedObject arrayBuffer(aJSContext, JS_GetArrayBufferViewBuffer(aJSArrays[i]));
    void* dataToFree = nullptr;
    uint8_t* stolenData = nullptr;
    if (arrayBuffer &&
        JS_StealArrayBufferContents(aJSContext, arrayBuffer, &dataToFree,
                                    &stolenData)) {
      result->SetData(i, dataToFree, reinterpret_cast<float*>(stolenData));
    } else {
      return nullptr;
    }
  }
  return result.forget();
}

ThreadSharedFloatArrayBufferList*
AudioBuffer::GetThreadSharedChannelsForRate(JSContext* aJSContext)
{
  if (!mSharedChannels) {
    for (uint32_t i = 0; i < mJSChannels.Length(); ++i) {
      if (mLength != JS_GetTypedArrayLength(mJSChannels[i])) {
        // Probably one of the arrays was neutered
        return nullptr;
      }
    }

    mSharedChannels =
      StealJSArrayDataIntoThreadSharedFloatArrayBufferList(aJSContext, mJSChannels);
  }

  return mSharedChannels;
}

void
AudioBuffer::MixToMono(JSContext* aJSContext)
{
  if (mJSChannels.Length() == 1) {
    // The buffer is already mono
    return;
  }

  // Prepare the input channels
  nsAutoTArray<const void*, GUESS_AUDIO_CHANNELS> channels;
  channels.SetLength(mJSChannels.Length());
  for (uint32_t i = 0; i < mJSChannels.Length(); ++i) {
    channels[i] = JS_GetFloat32ArrayData(mJSChannels[i]);
  }

  // Prepare the output channels
  float* downmixBuffer = new float[mLength];

  // Perform the down-mix
  AudioChannelsDownMix(channels, &downmixBuffer, 1, mLength);

  // Truncate the shared channels and copy the downmixed data over
  mJSChannels.SetLength(1);
  SetRawChannelContents(aJSContext, 0, downmixBuffer);
  delete[] downmixBuffer;
}

}
}

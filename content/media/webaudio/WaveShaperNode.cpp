/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WaveShaperNode.h"
#include "mozilla/dom/WaveShaperNodeBinding.h"
#include "AudioNode.h"
#include "AudioNodeEngine.h"
#include "AudioNodeStream.h"
#include "mozilla/PodOperations.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(WaveShaperNode, AudioNode)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  tmp->ClearCurve();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(WaveShaperNode, AudioNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(WaveShaperNode)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mCurve)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(WaveShaperNode)
NS_INTERFACE_MAP_END_INHERITING(AudioNode)

NS_IMPL_ADDREF_INHERITED(WaveShaperNode, AudioNode)
NS_IMPL_RELEASE_INHERITED(WaveShaperNode, AudioNode)

class WaveShaperNodeEngine : public AudioNodeEngine
{
public:
  explicit WaveShaperNodeEngine(AudioNode* aNode)
    : AudioNodeEngine(aNode)
  {
  }

  virtual void SetRawArrayData(nsTArray<float>& aCurve) MOZ_OVERRIDE
  {
    mCurve.SwapElements(aCurve);
  }

  virtual void ProduceAudioBlock(AudioNodeStream* aStream,
                                 const AudioChunk& aInput,
                                 AudioChunk* aOutput,
                                 bool* aFinished)
  {
    uint32_t channelCount = aInput.mChannelData.Length();
    if (!mCurve.Length() || !channelCount) {
      // Optimize the case where we don't have a curve buffer,
      // or the input is null.
      *aOutput = aInput;
      return;
    }

    AllocateAudioBlock(channelCount, aOutput);
    for (uint32_t i = 0; i < channelCount; ++i) {
      const float* inputBuffer = static_cast<const float*>(aInput.mChannelData[i]);
      float* outputBuffer = const_cast<float*> (static_cast<const float*>(aOutput->mChannelData[i]));
      for (uint32_t j = 0; j < WEBAUDIO_BLOCK_SIZE; ++j) {
        // Index into the curve array based on the amplitude of the
        // incoming signal by clamping the amplitude to [-1, 1] and
        // performing a linear interpolation of the neighbor values.
        float index = std::max(0.0f, std::min(float(mCurve.Length() - 1),
                                              mCurve.Length() * (inputBuffer[j] + 1) / 2));
        uint32_t indexLower = uint32_t(index);
        uint32_t indexHigher = uint32_t(index + 1.0f);
        if (indexHigher == mCurve.Length()) {
          outputBuffer[j] = mCurve[indexLower];
        } else {
          float interpolationFactor = index - indexLower;
          outputBuffer[j] = (1.0f - interpolationFactor) * mCurve[indexLower] +
                                    interpolationFactor  * mCurve[indexHigher];
        }
      }
    }
  }

private:
  nsTArray<float> mCurve;
};

WaveShaperNode::WaveShaperNode(AudioContext* aContext)
  : AudioNode(aContext,
              2,
              ChannelCountMode::Max,
              ChannelInterpretation::Speakers)
  , mCurve(nullptr)
{
  NS_HOLD_JS_OBJECTS(this, WaveShaperNode);

  WaveShaperNodeEngine* engine = new WaveShaperNodeEngine(this);
  mStream = aContext->Graph()->CreateAudioNodeStream(engine, MediaStreamGraph::INTERNAL_STREAM);
}

WaveShaperNode::~WaveShaperNode()
{
  ClearCurve();
}

void
WaveShaperNode::ClearCurve()
{
  mCurve = nullptr;
  NS_DROP_JS_OBJECTS(this, WaveShaperNode);
}

JSObject*
WaveShaperNode::WrapObject(JSContext *aCx, JS::Handle<JSObject*> aScope)
{
  return WaveShaperNodeBinding::Wrap(aCx, aScope, this);
}

void
WaveShaperNode::SetCurve(const Float32Array* aCurve)
{
  nsTArray<float> curve;
  if (aCurve) {
    mCurve = aCurve->Obj();

    curve.SetLength(aCurve->Length());
    PodCopy(curve.Elements(), aCurve->Data(), aCurve->Length());
  } else {
    mCurve = nullptr;
  }

  AudioNodeStream* ns = static_cast<AudioNodeStream*>(mStream.get());
  MOZ_ASSERT(ns, "Why don't we have a stream here?");
  ns->SetRawArrayData(curve);
}

}
}

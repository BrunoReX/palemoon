/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include "nsCycleCollectionParticipant.h"
#include "nsString.h"
#include "nsWrapperCache.h"
#include "nsAutoPtr.h"

#include "mozilla/Attributes.h"

#include "EnableWebSpeechRecognitionCheck.h"

struct JSContext;

namespace mozilla {
namespace dom {

class SpeechRecognition;

class SpeechRecognitionAlternative MOZ_FINAL : public nsISupports,
                                               public nsWrapperCache,
                                               public EnableWebSpeechRecognitionCheck
{
public:
  SpeechRecognitionAlternative(SpeechRecognition* aParent);
  ~SpeechRecognitionAlternative();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(SpeechRecognitionAlternative)

  nsISupports* GetParentObject() const;

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aScope) MOZ_OVERRIDE;

  void GetTranscript(nsString& aRetVal) const;

  float Confidence() const;

  nsString mTranscript;
  float mConfidence;
private:
  nsRefPtr<SpeechRecognition> mParent;
};

} // namespace dom
} // namespace mozilla

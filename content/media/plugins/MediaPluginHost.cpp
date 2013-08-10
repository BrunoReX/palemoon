/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "mozilla/Preferences.h"
#include "mozilla/TimeStamp.h"
#include "nsTimeRanges.h"
#include "MediaResource.h"
#include "nsHTMLMediaElement.h"
#include "MediaPluginHost.h"
#include "nsXPCOMStrings.h"
#include "nsISeekableStream.h"
#include "pratom.h"
#include "MediaPluginReader.h"
#include "nsIGfxInfo.h"
#include "gfxCrashReporterUtils.h"

#include "MPAPI.h"

using namespace MPAPI;

Decoder::Decoder() :
  mResource(NULL), mPrivate(NULL)
{
}

namespace mozilla {

static MediaResource *GetResource(Decoder *aDecoder)
{
  return reinterpret_cast<MediaResource *>(aDecoder->mResource);
}

static bool Read(Decoder *aDecoder, char *aBuffer, int64_t aOffset, uint32_t aCount, uint32_t* aBytes)
{
  MediaResource *resource = GetResource(aDecoder);
  if (aOffset != resource->Tell()) {
    nsresult rv = resource->Seek(nsISeekableStream::NS_SEEK_SET, aOffset);
    if (NS_FAILED(rv)) {
      return false;
    }
  }
  nsresult rv = resource->Read(aBuffer, aCount, aBytes);
  if (NS_FAILED(rv)) {
    return false;
  }
  return true;
}

static uint64_t GetLength(Decoder *aDecoder)
{
  return GetResource(aDecoder)->GetLength();
}

static void SetMetaDataReadMode(Decoder *aDecoder)
{
  GetResource(aDecoder)->SetReadMode(MediaCacheStream::MODE_METADATA);
}

static void SetPlaybackReadMode(Decoder *aDecoder)
{
  GetResource(aDecoder)->SetReadMode(MediaCacheStream::MODE_PLAYBACK);
}

class GetIntPrefEvent : public nsRunnable {
public:
  GetIntPrefEvent(const char* aPref, int32_t* aResult)
    : mPref(aPref), mResult(aResult) {}
  NS_IMETHOD Run() {
    return Preferences::GetInt(mPref, mResult);
  }
private:
  const char* mPref;
  int32_t*    mResult;
};

static bool GetIntPref(const char* aPref, int32_t* aResult)
{
  // GetIntPref() is called on the decoder thread, but the Preferences API
  // can only be called on the main thread. Post a runnable and wait.
  NS_ENSURE_TRUE(aPref, false);
  NS_ENSURE_TRUE(aResult, false);
  nsCOMPtr<GetIntPrefEvent> event = new GetIntPrefEvent(aPref, aResult);
  return NS_SUCCEEDED(NS_DispatchToMainThread(event, NS_DISPATCH_SYNC));
}

static PluginHost sPluginHost = {
  Read,
  GetLength,
  SetMetaDataReadMode,
  SetPlaybackReadMode,
  GetIntPref
};

void MediaPluginHost::TryLoad(const char *name)
{
  bool forceEnabled =
      Preferences::GetBool("stagefright.force-enabled", false);
  bool disabled =
      Preferences::GetBool("stagefright.disabled", false);

  if (disabled) {
    NS_WARNING("XXX stagefright disabled\n");
    return;
  }

  ScopedGfxFeatureReporter reporter("Stagefright", forceEnabled);

  if (!forceEnabled) {
    nsCOMPtr<nsIGfxInfo> gfxInfo = do_GetService("@mozilla.org/gfx/info;1");
    if (gfxInfo) {
      int32_t status;
      if (NS_SUCCEEDED(gfxInfo->GetFeatureStatus(nsIGfxInfo::FEATURE_STAGEFRIGHT, &status))) {
        if (status != nsIGfxInfo::FEATURE_NO_INFO) {
          NS_WARNING("XXX stagefright blacklisted\n");
          return;
        }
      }
    }
  }

  reporter.SetSuccessful();

  PRLibrary *lib = PR_LoadLibrary(name);
  if (lib) {
    Manifest *manifest = static_cast<Manifest *>(PR_FindSymbol(lib, "MPAPI_MANIFEST"));
    if (manifest)
      mPlugins.AppendElement(manifest);
  }
}

MediaPluginHost::MediaPluginHost() {
  MOZ_COUNT_CTOR(MediaPluginHost);
#if defined(ANDROID) && !defined(MOZ_WIDGET_GONK)
  TryLoad("lib/libomxplugin.so");
#elif defined(ANDROID) && defined(MOZ_WIDGET_GONK)
  TryLoad("libomxplugin.so");
#endif
}

MediaPluginHost::~MediaPluginHost() {
  MOZ_COUNT_DTOR(MediaPluginHost);
}

bool MediaPluginHost::FindDecoder(const nsACString& aMimeType, const char* const** aCodecs)
{
  const char *chars;
  size_t len = NS_CStringGetData(aMimeType, &chars, nullptr);
  for (size_t n = 0; n < mPlugins.Length(); ++n) {
    Manifest *plugin = mPlugins[n];
    const char* const *codecs;
    if (plugin->CanDecode(chars, len, &codecs)) {
      if (aCodecs)
        *aCodecs = codecs;
      return true;
    }
  }
  return false;
}

MPAPI::Decoder *MediaPluginHost::CreateDecoder(MediaResource *aResource, const nsACString& aMimeType)
{
  const char *chars;
  size_t len = NS_CStringGetData(aMimeType, &chars, nullptr);

  Decoder *decoder = new Decoder();
  if (!decoder) {
    return nullptr;
  }
  decoder->mResource = aResource;

  for (size_t n = 0; n < mPlugins.Length(); ++n) {
    Manifest *plugin = mPlugins[n];
    const char* const *codecs;
    if (!plugin->CanDecode(chars, len, &codecs)) {
      continue;
    }
    if (plugin->CreateDecoder(&sPluginHost, decoder, chars, len)) {
      return decoder;
    }
  }

  return nullptr;
}

void MediaPluginHost::DestroyDecoder(Decoder *aDecoder)
{
  aDecoder->DestroyDecoder(aDecoder);
  delete aDecoder;
}

MediaPluginHost *sMediaPluginHost = nullptr;
MediaPluginHost *GetMediaPluginHost()
{
  if (!sMediaPluginHost) {
    sMediaPluginHost = new MediaPluginHost();
  }
  return sMediaPluginHost;
}

void MediaPluginHost::Shutdown()
{
  if (sMediaPluginHost) {
    delete sMediaPluginHost;
    sMediaPluginHost = nullptr;
  }
}

} // namespace mozilla

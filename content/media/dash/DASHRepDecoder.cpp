/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */

/* This Source Code Form Is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* DASH - Dynamic Adaptive Streaming over HTTP
 *
 * DASH is an adaptive bitrate streaming technology where a multimedia file is
 * partitioned into one or more segments and delivered to a client using HTTP.
 *
 * see DASHDecoder.cpp for info on DASH interaction with the media engine.*/

#include "prlog.h"
#include "VideoUtils.h"
#include "SegmentBase.h"
#include "MediaDecoderStateMachine.h"
#include "DASHReader.h"
#include "MediaResource.h"
#include "DASHRepDecoder.h"

namespace mozilla {

#ifdef PR_LOGGING
extern PRLogModuleInfo* gMediaDecoderLog;
#define LOG(msg, ...) PR_LOG(gMediaDecoderLog, PR_LOG_DEBUG, \
                             ("%p [DASHRepDecoder] " msg, this, __VA_ARGS__))
#define LOG1(msg) PR_LOG(gMediaDecoderLog, PR_LOG_DEBUG, \
                         ("%p [DASHRepDecoder] " msg, this))
#else
#define LOG(msg, ...)
#define LOG1(msg)
#endif

MediaDecoderStateMachine*
DASHRepDecoder::CreateStateMachine()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  // Do not create; just return current state machine.
  return mDecoderStateMachine;
}

nsresult
DASHRepDecoder::SetStateMachine(MediaDecoderStateMachine* aSM)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mDecoderStateMachine = aSM;
  return NS_OK;
}

void
DASHRepDecoder::SetResource(MediaResource* aResource)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mResource = aResource;
}

void
DASHRepDecoder::SetMPDRepresentation(Representation const * aRep)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mMPDRepresentation = aRep;
}

void
DASHRepDecoder::SetReader(WebMReader* aReader)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mReader = aReader;
}

nsresult
DASHRepDecoder::Load(MediaResource* aResource,
                       nsIStreamListener** aListener,
                       MediaDecoder* aCloneDonor)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  NS_ENSURE_TRUE(mMPDRepresentation, NS_ERROR_NOT_INITIALIZED);

  // Get init range and index range from MPD.
  SegmentBase const * segmentBase = mMPDRepresentation->GetSegmentBase();
  NS_ENSURE_TRUE(segmentBase, NS_ERROR_NULL_POINTER);

  // Get and set init range.
  segmentBase->GetInitRange(&mInitByteRange.mStart, &mInitByteRange.mEnd);
  NS_ENSURE_TRUE(!mInitByteRange.IsNull(), NS_ERROR_NOT_INITIALIZED);
  mReader->SetInitByteRange(mInitByteRange);

  // Get and set index range.
  segmentBase->GetIndexRange(&mIndexByteRange.mStart, &mIndexByteRange.mEnd);
  NS_ENSURE_TRUE(!mIndexByteRange.IsNull(), NS_ERROR_NOT_INITIALIZED);
  mReader->SetIndexByteRange(mIndexByteRange);

  // Determine byte range to Open.
  // For small deltas between init and index ranges, we need to bundle the byte
  // range requests together in order to deal with |MediaCache|'s control of
  // seeking (see |MediaCache|::|Update|). |MediaCache| will not initiate a
  // |ChannelMediaResource|::|CacheClientSeek| for the INDEX byte range if the
  // delta between it and the INIT byte ranges is less than
  // |SEEK_VS_READ_THRESHOLD|. To get around this, request all metadata bytes
  // now so |MediaCache| can assume the bytes are en route.
  int64_t delta = NS_MAX(mIndexByteRange.mStart, mInitByteRange.mStart)
                - NS_MIN(mIndexByteRange.mEnd, mInitByteRange.mEnd);
  MediaByteRange byteRange;
  if (delta <= SEEK_VS_READ_THRESHOLD) {
    byteRange.mStart = NS_MIN(mIndexByteRange.mStart, mInitByteRange.mStart);
    byteRange.mEnd = NS_MAX(mIndexByteRange.mEnd, mInitByteRange.mEnd);
    // Loading everything in one chunk .
    mMetadataChunkCount = 1;
  } else {
    byteRange = mInitByteRange;
    // Loading in two chunks: init and index.
    mMetadataChunkCount = 2;
  }
  mCurrentByteRange = byteRange;
  return mResource->OpenByteRange(nullptr, byteRange);
}

void
DASHRepDecoder::NotifyDownloadEnded(nsresult aStatus)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  if (!mMainDecoder) {
    LOG("Error! Main Decoder is reported as null: mMainDecoder [%p]",
        mMainDecoder.get());
    DecodeError();
    return;
  }

  if (NS_SUCCEEDED(aStatus)) {
    // Decrement counter as metadata chunks are downloaded.
    // Note: Reader gets next chunk download via |ChannelMediaResource|:|Seek|.
    if (mMetadataChunkCount > 0) {
      LOG("Metadata chunk [%d] downloaded: range requested [%d - %d]",
          mMetadataChunkCount,
          mCurrentByteRange.mStart, mCurrentByteRange.mEnd);
      mMetadataChunkCount--;
    } else {
      // Notify main decoder that a DATA byte range is downloaded.
      LOG("Byte range downloaded: status [%x] range requested [%d - %d]",
          aStatus, mCurrentByteRange.mStart, mCurrentByteRange.mEnd);
      mMainDecoder->NotifyDownloadEnded(this, aStatus,
                                        mCurrentByteRange);
    }
  } else if (aStatus == NS_BINDING_ABORTED) {
    LOG("MPD download has been cancelled by the user: aStatus [%x].", aStatus);
    if (mMainDecoder) {
      mMainDecoder->LoadAborted();
    }
    return;
  } else if (aStatus != NS_BASE_STREAM_CLOSED) {
    LOG("Network error trying to download MPD: aStatus [%x].", aStatus);
    NetworkError();
  }
}

void
DASHRepDecoder::OnReadMetadataCompleted()
{
  NS_ASSERTION(OnDecodeThread(), "Should be on decode thread.");

  LOG1("Metadata has been read.");
  nsCOMPtr<nsIRunnable> event =
    NS_NewRunnableMethod(this, &DASHRepDecoder::LoadNextByteRange);
  nsresult rv = NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
  if (NS_FAILED(rv)) {
    LOG("Error dispatching parse event to main thread: rv[%x]", rv);
    DecodeError();
    return;
  }
}

void
DASHRepDecoder::LoadNextByteRange()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (!mResource) {
    LOG1("Error: resource is reported as null!");
    DecodeError();
    return;
  }

  // Populate the array of subsegment byte ranges if it's empty.
  nsresult rv;
  if (mByteRanges.IsEmpty()) {
    if (!mReader) {
      LOG1("Error: mReader should not be null!");
      DecodeError();
      return;
    }
    rv = mReader->GetIndexByteRanges(mByteRanges);
    // If empty, just fail.
    if (NS_FAILED(rv) || mByteRanges.IsEmpty()) {
      LOG1("Error getting list of subsegment byte ranges.");
      DecodeError();
      return;
    }
  }

  // Get byte range for subsegment.
  if (mSubsegmentIdx < mByteRanges.Length()) {
    mCurrentByteRange = mByteRanges[mSubsegmentIdx];
  } else {
    mCurrentByteRange.Clear();
    LOG("End of subsegments: index [%d] out of range.", mSubsegmentIdx);
    return;
  }

  // Open byte range corresponding to subsegment.
  rv = mResource->OpenByteRange(nullptr, mCurrentByteRange);
  if (NS_FAILED(rv)) {
    LOG("Error opening byte range [%d - %d]: rv [%x].",
        mCurrentByteRange.mStart, mCurrentByteRange.mEnd, rv);
    NetworkError();
    return;
  }
  // Increment subsegment index for next load.
  mSubsegmentIdx++;
}

nsresult
DASHRepDecoder::GetByteRangeForSeek(int64_t const aOffset,
                                      MediaByteRange& aByteRange)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  // Check data ranges, if available.
  for (int i = 0; i < mByteRanges.Length(); i++) {
    NS_ENSURE_FALSE(mByteRanges[i].IsNull(), NS_ERROR_NOT_INITIALIZED);
    if (mByteRanges[i].mStart <= aOffset && aOffset <= mByteRanges[i].mEnd) {
      mCurrentByteRange = aByteRange = mByteRanges[i];
      mSubsegmentIdx = i;
      return NS_OK;
    }
  }
  // Check metadata ranges; init range.
  if (mInitByteRange.mStart <= aOffset && aOffset <= mInitByteRange.mEnd) {
    mCurrentByteRange = aByteRange = mInitByteRange;
    mSubsegmentIdx = 0;
    return NS_OK;
  }
  // ... index range.
  if (mIndexByteRange.mStart <= aOffset && aOffset <= mIndexByteRange.mEnd) {
    mCurrentByteRange = aByteRange = mIndexByteRange;
    mSubsegmentIdx = 0;
    return NS_OK;
  }

  aByteRange.Clear();
  if (mByteRanges.IsEmpty()) {
    // Assume mByteRanges will be populated after metadata is read.
    LOG("Can't get range for offset [%d].", aOffset);
    return NS_ERROR_NOT_AVAILABLE;
  } else {
    // Cannot seek to an unknown offset.
    // XXX Revisit this for dynamic MPD profiles if MPD is regularly updated.
    LOG("Error! Offset [%d] is in an unknown range!", aOffset);
    return NS_ERROR_ILLEGAL_VALUE;
  }
}

void
DASHRepDecoder::NetworkError()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mMainDecoder) { mMainDecoder->NetworkError(); }
}

void
DASHRepDecoder::SetDuration(double aDuration)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mMainDecoder) { mMainDecoder->SetDuration(aDuration); }
}

void
DASHRepDecoder::SetInfinite(bool aInfinite)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mMainDecoder) { mMainDecoder->SetInfinite(aInfinite); }
}

void
DASHRepDecoder::SetSeekable(bool aSeekable)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mMainDecoder) { mMainDecoder->SetSeekable(aSeekable); }
}

void
DASHRepDecoder::Progress(bool aTimer)
{
  if (mMainDecoder) { mMainDecoder->Progress(aTimer); }
}

void
DASHRepDecoder::NotifyDataArrived(const char* aBuffer,
                                    uint32_t aLength,
                                    int64_t aOffset)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  LOG("Data bytes [%d - %d] arrived via buffer [%p].",
      aOffset, aOffset+aLength, aBuffer);
  // Notify reader directly, since call to |MediaDecoderStateMachine|::
  // |NotifyDataArrived| will go to |DASHReader|::|NotifyDataArrived|, which
  // has no way to forward the notification to the correct sub-reader.
  if (mReader) {
    mReader->NotifyDataArrived(aBuffer, aLength, aOffset);
  }
  // Forward to main decoder which will notify state machine.
  if (mMainDecoder) {
    mMainDecoder->NotifyDataArrived(aBuffer, aLength, aOffset);
  }
}

void
DASHRepDecoder::NotifyBytesDownloaded()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mMainDecoder) { mMainDecoder->NotifyBytesDownloaded(); }
}

void
DASHRepDecoder::NotifySuspendedStatusChanged()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  if (mMainDecoder) { mMainDecoder->NotifySuspendedStatusChanged(); }
}

bool
DASHRepDecoder::OnStateMachineThread() const
{
  return (mMainDecoder ? mMainDecoder->OnStateMachineThread() : false);
}

bool
DASHRepDecoder::OnDecodeThread() const
{
  return (mMainDecoder ? mMainDecoder->OnDecodeThread() : false);
}

ReentrantMonitor&
DASHRepDecoder::GetReentrantMonitor()
{
  return mMainDecoder->GetReentrantMonitor();
}

mozilla::layers::ImageContainer*
DASHRepDecoder::GetImageContainer()
{
  NS_ASSERTION(mMainDecoder && mMainDecoder->OnDecodeThread(),
               "Should be on decode thread.");
  return (mMainDecoder ? mMainDecoder->GetImageContainer() : nullptr);
}

void
DASHRepDecoder::DecodeError()
{
  if (NS_IsMainThread()) {
    MediaDecoder::DecodeError();
  } else {
    nsCOMPtr<nsIRunnable> event =
      NS_NewRunnableMethod(this, &MediaDecoder::DecodeError);
    nsresult rv = NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
    if (NS_FAILED(rv)) {
      LOG("Error dispatching DecodeError event to main thread: rv[%x]", rv);
    }
  }
}

void
DASHRepDecoder::ReleaseStateMachine()
{
  NS_ASSERTION(NS_IsMainThread(), "Must be on main thread.");

  // Since state machine owns mReader, remove reference to it.
  mReader = nullptr;

  MediaDecoder::ReleaseStateMachine();
}

} // namespace mozilla

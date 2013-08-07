/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla code.
 *
 * The Initial Developer of the Original Code is 
 *   Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Brad Lassey <blassey@mozilla.com>
 *  Kyle Huey <me@kylehuey.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsBuiltinDecoderStateMachine.h"
#include "nsBuiltinDecoder.h"
#include "nsRawReader.h"
#include "nsRawDecoder.h"
#include "VideoUtils.h"

nsRawReader::nsRawReader(nsBuiltinDecoder* aDecoder)
  : nsBuiltinDecoderReader(aDecoder),
    mCurrentFrame(0), mFrameSize(0)
{
  MOZ_COUNT_CTOR(nsRawReader);
}

nsRawReader::~nsRawReader()
{
  MOZ_COUNT_DTOR(nsRawReader);
}

nsresult nsRawReader::Init(nsBuiltinDecoderReader* aCloneDonor)
{
  return NS_OK;
}

nsresult nsRawReader::ResetDecode()
{
  mCurrentFrame = 0;
  return nsBuiltinDecoderReader::ResetDecode();
}

nsresult nsRawReader::ReadMetadata(nsVideoInfo* aInfo)
{
  NS_ASSERTION(mDecoder->OnDecodeThread(),
               "Should be on decode thread.");

  nsMediaStream* stream = mDecoder->GetStream();
  NS_ASSERTION(stream, "Decoder has no media stream");

  if (!ReadFromStream(stream, reinterpret_cast<PRUint8*>(&mMetadata),
                      sizeof(mMetadata)))
    return NS_ERROR_FAILURE;

  // Validate the header
  if (!(mMetadata.headerPacketID == 0 /* Packet ID of 0 for the header*/ &&
        mMetadata.codecID == RAW_ID /* "YUV" */ &&
        mMetadata.majorVersion == 0 &&
        mMetadata.minorVersion == 1))
    return NS_ERROR_FAILURE;

  PRUint32 dummy;
  if (!MulOverflow32(mMetadata.frameWidth, mMetadata.frameHeight, dummy))
    return NS_ERROR_FAILURE;


  if (mMetadata.aspectDenominator == 0 ||
      mMetadata.framerateDenominator == 0)
    return NS_ERROR_FAILURE; // Invalid data

  // Determine and verify frame display size.
  float pixelAspectRatio = static_cast<float>(mMetadata.aspectNumerator) / 
                            mMetadata.aspectDenominator;
  nsIntSize display(mMetadata.frameWidth, mMetadata.frameHeight);
  ScaleDisplayByAspectRatio(display, pixelAspectRatio);
  mPicture = nsIntRect(0, 0, mMetadata.frameWidth, mMetadata.frameHeight);
  nsIntSize frameSize(mMetadata.frameWidth, mMetadata.frameHeight);
  if (!nsVideoInfo::ValidateVideoRegion(frameSize, mPicture, display)) {
    // Video track's frame sizes will overflow. Fail.
    return NS_ERROR_FAILURE;
  }

  mInfo.mHasVideo = true;
  mInfo.mHasAudio = false;
  mInfo.mDisplay = display;

  mFrameRate = static_cast<float>(mMetadata.framerateNumerator) /
               mMetadata.framerateDenominator;

  // Make some sanity checks
  if (mFrameRate > 45 ||
      mFrameRate == 0 ||
      pixelAspectRatio == 0 ||
      mMetadata.frameWidth > 2000 ||
      mMetadata.frameHeight > 2000 ||
      mMetadata.chromaChannelBpp != 4 ||
      mMetadata.lumaChannelBpp != 8 ||
      mMetadata.colorspace != 1 /* 4:2:0 */)
    return NS_ERROR_FAILURE;

  mFrameSize = mMetadata.frameWidth * mMetadata.frameHeight *
    (mMetadata.lumaChannelBpp + mMetadata.chromaChannelBpp) / 8.0 +
    sizeof(nsRawPacketHeader);

  PRInt64 length = stream->GetLength();
  if (length != -1) {
    mozilla::ReentrantMonitorAutoEnter autoMonitor(mDecoder->GetReentrantMonitor());
    mDecoder->GetStateMachine()->SetDuration(USECS_PER_S *
                                           (length - sizeof(nsRawVideoHeader)) /
                                           (mFrameSize * mFrameRate));
  }

  *aInfo = mInfo;

  return NS_OK;
}

 bool nsRawReader::DecodeAudioData()
{
  NS_ASSERTION(mDecoder->OnStateMachineThread() || mDecoder->OnDecodeThread(),
               "Should be on state machine thread or decode thread.");
  return false;
}

// Helper method that either reads until it gets aLength bytes 
// or returns false
bool nsRawReader::ReadFromStream(nsMediaStream *aStream, PRUint8* aBuf,
                                   PRUint32 aLength)
{
  while (aLength > 0) {
    PRUint32 bytesRead = 0;
    nsresult rv;

    rv = aStream->Read(reinterpret_cast<char*>(aBuf), aLength, &bytesRead);
    NS_ENSURE_SUCCESS(rv, false);

    if (bytesRead == 0) {
      return false;
    }

    aLength -= bytesRead;
    aBuf += bytesRead;
  }

  return true;
}

bool nsRawReader::DecodeVideoFrame(bool &aKeyframeSkip,
                                     PRInt64 aTimeThreshold)
{
  NS_ASSERTION(mDecoder->OnDecodeThread(),
               "Should be on decode thread.");

  // Record number of frames decoded and parsed. Automatically update the
  // stats counters using the AutoNotifyDecoded stack-based class.
  PRUint32 parsed = 0, decoded = 0;
  nsMediaDecoder::AutoNotifyDecoded autoNotify(mDecoder, parsed, decoded);

  if (!mFrameSize)
    return false; // Metadata read failed.  We should refuse to play.

  PRInt64 currentFrameTime = USECS_PER_S * mCurrentFrame / mFrameRate;
  PRUint32 length = mFrameSize - sizeof(nsRawPacketHeader);

  nsAutoArrayPtr<PRUint8> buffer(new PRUint8[length]);
  nsMediaStream* stream = mDecoder->GetStream();
  NS_ASSERTION(stream, "Decoder has no media stream");

  // We're always decoding one frame when called
  while(true) {
    nsRawPacketHeader header;

    // Read in a packet header and validate
    if (!(ReadFromStream(stream, reinterpret_cast<PRUint8*>(&header),
                         sizeof(header))) ||
        !(header.packetID == 0xFF && header.codecID == RAW_ID /* "YUV" */)) {
      return false;
    }

    if (!ReadFromStream(stream, buffer, length)) {
      return false;
    }

    parsed++;

    if (currentFrameTime >= aTimeThreshold)
      break;

    mCurrentFrame++;
    currentFrameTime += static_cast<double>(USECS_PER_S) / mFrameRate;
  }

  VideoData::YCbCrBuffer b;
  b.mPlanes[0].mData = buffer;
  b.mPlanes[0].mStride = mMetadata.frameWidth * mMetadata.lumaChannelBpp / 8.0;
  b.mPlanes[0].mHeight = mMetadata.frameHeight;
  b.mPlanes[0].mWidth = mMetadata.frameWidth;

  PRUint32 cbcrStride = mMetadata.frameWidth * mMetadata.chromaChannelBpp / 8.0;

  b.mPlanes[1].mData = buffer + mMetadata.frameHeight * b.mPlanes[0].mStride;
  b.mPlanes[1].mStride = cbcrStride;
  b.mPlanes[1].mHeight = mMetadata.frameHeight / 2;
  b.mPlanes[1].mWidth = mMetadata.frameWidth / 2;

  b.mPlanes[2].mData = b.mPlanes[1].mData + mMetadata.frameHeight * cbcrStride / 2;
  b.mPlanes[2].mStride = cbcrStride;
  b.mPlanes[2].mHeight = mMetadata.frameHeight / 2;
  b.mPlanes[2].mWidth = mMetadata.frameWidth / 2;

  VideoData *v = VideoData::Create(mInfo,
                                   mDecoder->GetImageContainer(),
                                   -1,
                                   currentFrameTime,
                                   currentFrameTime + (USECS_PER_S / mFrameRate),
                                   b,
                                   1, // In raw video every frame is a keyframe
                                   -1,
                                   mPicture);
  if (!v)
    return false;

  mVideoQueue.Push(v);
  mCurrentFrame++;
  decoded++;
  currentFrameTime += USECS_PER_S / mFrameRate;

  return true;
}

nsresult nsRawReader::Seek(PRInt64 aTime, PRInt64 aStartTime, PRInt64 aEndTime, PRInt64 aCurrentTime)
{
  NS_ASSERTION(mDecoder->OnDecodeThread(),
               "Should be on decode thread.");

  nsMediaStream *stream = mDecoder->GetStream();
  NS_ASSERTION(stream, "Decoder has no media stream");

  PRUint32 frame = mCurrentFrame;
  if (aTime >= UINT_MAX)
    return NS_ERROR_FAILURE;
  mCurrentFrame = aTime * mFrameRate / USECS_PER_S;

  PRUint32 offset;
  if (!MulOverflow32(mCurrentFrame, mFrameSize, offset))
    return NS_ERROR_FAILURE;

  offset += sizeof(nsRawVideoHeader);

  nsresult rv = stream->Seek(nsISeekableStream::NS_SEEK_SET, offset);
  NS_ENSURE_SUCCESS(rv, rv);

  mVideoQueue.Erase();

  while(mVideoQueue.GetSize() == 0) {
    bool keyframeSkip = false;
    if (!DecodeVideoFrame(keyframeSkip, 0)) {
      mCurrentFrame = frame;
      return NS_ERROR_FAILURE;
    }

    {
      mozilla::ReentrantMonitorAutoEnter autoMonitor(mDecoder->GetReentrantMonitor());
      if (mDecoder->GetDecodeState() ==
          nsBuiltinDecoderStateMachine::DECODER_STATE_SHUTDOWN) {
        mCurrentFrame = frame;
        return NS_ERROR_FAILURE;
      }
    }

    nsAutoPtr<VideoData> video(mVideoQueue.PeekFront());
    if (video && video->mEndTime < aTime) {
      mVideoQueue.PopFront();
      video = nsnull;
    } else {
      video.forget();
    }
  }

  return NS_OK;
}

nsresult nsRawReader::GetBuffered(nsTimeRanges* aBuffered, PRInt64 aStartTime)
{
  return NS_OK;
}

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
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
 * The Original Code is Mozilla.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Patrick McManus <mcmanus@ducksong.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsHttp.h"
#include "SpdySession.h"
#include "SpdyStream.h"
#include "nsHttpConnection.h"
#include "nsHttpHandler.h"
#include "prnetdb.h"
#include "mozilla/Telemetry.h"
#include "mozilla/Preferences.h"
#include "prprf.h"

#ifdef DEBUG
// defined by the socket transport service while active
extern PRThread *gSocketThread;
#endif

namespace mozilla {
namespace net {

// SpdySession has multiple inheritance of things that implement
// nsISupports, so this magic is taken from nsHttpPipeline that
// implements some of the same abstract classes.
NS_IMPL_THREADSAFE_ADDREF(SpdySession)
NS_IMPL_THREADSAFE_RELEASE(SpdySession)
NS_INTERFACE_MAP_BEGIN(SpdySession)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsAHttpConnection)
NS_INTERFACE_MAP_END

SpdySession::SpdySession(nsAHttpTransaction *aHttpTransaction,
                         nsISocketTransport *aSocketTransport,
                         PRInt32 firstPriority)
  : mSocketTransport(aSocketTransport),
    mSegmentReader(nsnull),
    mSegmentWriter(nsnull),
    mSendingChunkSize(kSendingChunkSize),
    mNextStreamID(1),
    mConcurrentHighWater(0),
    mDownstreamState(BUFFERING_FRAME_HEADER),
    mInputFrameBufferSize(kDefaultBufferSize),
    mInputFrameBufferUsed(0),
    mInputFrameDataLast(false),
    mInputFrameDataStream(nsnull),
    mNeedsCleanup(nsnull),
    mDecompressBufferSize(kDefaultBufferSize),
    mDecompressBufferUsed(0),
    mShouldGoAway(false),
    mClosed(false),
    mCleanShutdown(false),
    mGoAwayID(0),
    mMaxConcurrent(kDefaultMaxConcurrent),
    mConcurrent(0),
    mServerPushedResources(0),
    mOutputQueueSize(kDefaultQueueSize),
    mOutputQueueUsed(0),
    mOutputQueueSent(0)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  LOG3(("SpdySession::SpdySession %p transaction 1 = %p",
        this, aHttpTransaction));
  
  mStreamIDHash.Init();
  mStreamTransactionHash.Init();
  mConnection = aHttpTransaction->Connection();
  mInputFrameBuffer = new char[mInputFrameBufferSize];
  mDecompressBuffer = new char[mDecompressBufferSize];
  mOutputQueueBuffer = new char[mOutputQueueSize];
  zlibInit();
  
  mSendingChunkSize = gHttpHandler->SpdySendingChunkSize();
  AddStream(aHttpTransaction, firstPriority);
}

PLDHashOperator
SpdySession::ShutdownEnumerator(nsAHttpTransaction *key,
                                nsAutoPtr<SpdyStream> &stream,
                                void *closure)
{
  SpdySession *self = static_cast<SpdySession *>(closure);
 
  // On a clean server hangup the server sets the GoAwayID to be the ID of
  // the last transaction it processed. If the ID of stream in the
  // local session is greater than that it can safely be restarted because the
  // server guarantees it was not partially processed.
  if (self->mCleanShutdown && (stream->StreamID() > self->mGoAwayID))
    stream->Close(NS_ERROR_NET_RESET); // can be restarted
  else
    stream->Close(NS_ERROR_ABORT);

  return PL_DHASH_NEXT;
}

SpdySession::~SpdySession()
{
  LOG3(("SpdySession::~SpdySession %p mDownstreamState=%X",
        this, mDownstreamState));

  inflateEnd(&mDownstreamZlib);
  deflateEnd(&mUpstreamZlib);
  
  mStreamTransactionHash.Enumerate(ShutdownEnumerator, this);
  Telemetry::Accumulate(Telemetry::SPDY_PARALLEL_STREAMS, mConcurrentHighWater);
  Telemetry::Accumulate(Telemetry::SPDY_REQUEST_PER_CONN, (mNextStreamID - 1) / 2);
  Telemetry::Accumulate(Telemetry::SPDY_SERVER_INITIATED_STREAMS,
                        mServerPushedResources);
}

void
SpdySession::LogIO(SpdySession *self, SpdyStream *stream, const char *label,
                   const char *data, PRUint32 datalen)
{
  if (!LOG4_ENABLED())
    return;
  
  LOG4(("SpdySession::LogIO %p stream=%p id=0x%X [%s]",
        self, stream, stream ? stream->StreamID() : 0, label));

  // Max line is (16 * 3) + 10(prefix) + newline + null
  char linebuf[128];
  PRUint32 index;
  char *line = linebuf;

  linebuf[127] = 0;

  for (index = 0; index < datalen; ++index) {
    if (!(index % 16)) {
      if (index) {
        *line = 0;
        LOG4(("%s", linebuf));
      }
      line = linebuf;
      PR_snprintf(line, 128, "%08X: ", index);
      line += 10;
    }
    PR_snprintf(line, 128 - (line - linebuf), "%02X ",
                ((unsigned char *)data)[index]);
    line += 3;
  }
  if (index) {
    *line = 0;
    LOG4(("%s", linebuf));
  }
}

typedef nsresult  (*Control_FX) (SpdySession *self);
static Control_FX sControlFunctions[] = 
{
  nsnull,
  SpdySession::HandleSynStream,
  SpdySession::HandleSynReply,
  SpdySession::HandleRstStream,
  SpdySession::HandleSettings,
  SpdySession::HandleNoop,
  SpdySession::HandlePing,
  SpdySession::HandleGoAway,
  SpdySession::HandleHeaders,
  SpdySession::HandleWindowUpdate
};

bool
SpdySession::RoomForMoreConcurrent()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  return (mConcurrent < mMaxConcurrent);
}

bool
SpdySession::RoomForMoreStreams()
{
  if (mNextStreamID + mStreamTransactionHash.Count() * 2 > kMaxStreamID)
    return false;

  return !mShouldGoAway;
}

PRUint32
SpdySession::RegisterStreamID(SpdyStream *stream)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  LOG3(("SpdySession::RegisterStreamID session=%p stream=%p id=0x%X "
        "concurrent=%d",this, stream, mNextStreamID, mConcurrent));

  NS_ABORT_IF_FALSE(mNextStreamID < 0xfffffff0,
                    "should have stopped admitting streams");
  
  PRUint32 result = mNextStreamID;
  mNextStreamID += 2;

  // We've used up plenty of ID's on this session. Start
  // moving to a new one before there is a crunch involving
  // server push streams or concurrent non-registered submits
  if (mNextStreamID >= kMaxStreamID)
    mShouldGoAway = true;

  mStreamIDHash.Put(result, stream);
  return result;
}

bool
SpdySession::AddStream(nsAHttpTransaction *aHttpTransaction,
                       PRInt32 aPriority)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  NS_ABORT_IF_FALSE(!mStreamTransactionHash.Get(aHttpTransaction),
                    "AddStream duplicate transaction pointer");

  aHttpTransaction->SetConnection(this);
  SpdyStream *stream = new SpdyStream(aHttpTransaction,
                                      this,
                                      mSocketTransport,
                                      mSendingChunkSize,
                                      &mUpstreamZlib,
                                      aPriority);

  
  LOG3(("SpdySession::AddStream session=%p stream=%p NextID=0x%X (tentative)",
        this, stream, mNextStreamID));

  mStreamTransactionHash.Put(aHttpTransaction, stream);

  if (RoomForMoreConcurrent()) {
    LOG3(("SpdySession::AddStream %p stream %p activated immediately.",
          this, stream));
    ActivateStream(stream);
  }
  else {
    LOG3(("SpdySession::AddStream %p stream %p queued.",
          this, stream));
    mQueuedStreams.Push(stream);
  }
  
  return true;
}

void
SpdySession::ActivateStream(SpdyStream *stream)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  mConcurrent++;
  if (mConcurrent > mConcurrentHighWater)
    mConcurrentHighWater = mConcurrent;
  LOG3(("SpdySession::AddStream %p activating stream %p Currently %d "
        "streams in session, high water mark is %d",
        this, stream, mConcurrent, mConcurrentHighWater));

  mReadyForWrite.Push(stream);
  SetWriteCallbacks();

  // Kick off the SYN transmit without waiting for the poll loop
  PRUint32 countRead;
  ReadSegments(nsnull, kDefaultBufferSize, &countRead);
}

void
SpdySession::ProcessPending()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  while (RoomForMoreConcurrent()) {
    SpdyStream *stream = static_cast<SpdyStream *>(mQueuedStreams.PopFront());
    if (!stream)
      return;
    LOG3(("SpdySession::ProcessPending %p stream %p activated from queue.",
          this, stream));
    ActivateStream(stream);
  }
}

void
SpdySession::SetWriteCallbacks()
{
  if (mConnection && (GetWriteQueueSize() || mOutputQueueUsed))
      mConnection->ResumeSend();
}

void
SpdySession::FlushOutputQueue()
{
  if (!mSegmentReader || !mOutputQueueUsed)
    return;
  
  nsresult rv;
  PRUint32 countRead;
  PRUint32 avail = mOutputQueueUsed - mOutputQueueSent;

  rv = mSegmentReader->
    OnReadSegment(mOutputQueueBuffer.get() + mOutputQueueSent, avail,
                                     &countRead);
  LOG3(("SpdySession::FlushOutputQueue %p sz=%d rv=%x actual=%d",
        this, avail, rv, countRead));
  
  // Dont worry about errors on write, we will pick this up as a read error too
  if (NS_FAILED(rv))
    return;
  
  if (countRead == avail) {
    mOutputQueueUsed = 0;
    mOutputQueueSent = 0;
    return;
  }

  mOutputQueueSent += countRead;

  // If the output queue is close to filling up and we have sent out a good
  // chunk of data from the beginning then realign it.
  
  if ((mOutputQueueSent >= kQueueMinimumCleanup) &&
      ((mOutputQueueSize - mOutputQueueUsed) < kQueueTailRoom)) {
    mOutputQueueUsed -= mOutputQueueSent;
    memmove(mOutputQueueBuffer.get(),
            mOutputQueueBuffer.get() + mOutputQueueSent,
            mOutputQueueUsed);
    mOutputQueueSent = 0;
  }
}

void
SpdySession::DontReuse()
{
  mShouldGoAway = true;
  if (!mStreamTransactionHash.Count())
    Close(NS_OK);
}

PRUint32
SpdySession::GetWriteQueueSize()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  return mUrgentForWrite.GetSize() + mReadyForWrite.GetSize();
}

void
SpdySession::ChangeDownstreamState(enum stateType newState)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  LOG3(("SpdyStream::ChangeDownstreamState() %p from %X to %X",
        this, mDownstreamState, newState));
  mDownstreamState = newState;
}

void
SpdySession::ResetDownstreamState()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  LOG3(("SpdyStream::ResetDownstreamState() %p", this));
  ChangeDownstreamState(BUFFERING_FRAME_HEADER);

  if (mInputFrameDataLast && mInputFrameDataStream) {
    mInputFrameDataLast = false;
    if (!mInputFrameDataStream->RecvdFin()) {
      mInputFrameDataStream->SetRecvdFin(true);
      --mConcurrent;
      ProcessPending();
    }
  }
  mInputFrameBufferUsed = 0;
  mInputFrameDataStream = nsnull;
}

void
SpdySession::EnsureBuffer(nsAutoArrayPtr<char> &buf,
                          PRUint32 newSize,
                          PRUint32 preserve,
                          PRUint32 &objSize)
{
  if (objSize >= newSize)
      return;
  
  // Leave a little slop on the new allocation - add 2KB to
  // what we need and then round the result up to a 4KB (page)
  // boundary.

  objSize = (newSize + 2048 + 4095) & ~4095;
  
  nsAutoArrayPtr<char> tmp(new char[objSize]);
  memcpy(tmp, buf, preserve);
  buf = tmp;
}

void
SpdySession::zlibInit()
{
  mDownstreamZlib.zalloc = SpdyStream::zlib_allocator;
  mDownstreamZlib.zfree = SpdyStream::zlib_destructor;
  mDownstreamZlib.opaque = Z_NULL;

  inflateInit(&mDownstreamZlib);

  mUpstreamZlib.zalloc = SpdyStream::zlib_allocator;
  mUpstreamZlib.zfree = SpdyStream::zlib_destructor;
  mUpstreamZlib.opaque = Z_NULL;

  deflateInit(&mUpstreamZlib, Z_DEFAULT_COMPRESSION);
  deflateSetDictionary(&mUpstreamZlib,
                       reinterpret_cast<const unsigned char *>
                       (SpdyStream::kDictionary),
                       strlen(SpdyStream::kDictionary) + 1);

}

nsresult
SpdySession::DownstreamUncompress(char *blockStart, PRUint32 blockLen)
{
  mDecompressBufferUsed = 0;

  mDownstreamZlib.avail_in = blockLen;
  mDownstreamZlib.next_in = reinterpret_cast<unsigned char *>(blockStart);

  do {
    mDownstreamZlib.next_out =
      reinterpret_cast<unsigned char *>(mDecompressBuffer.get()) +
      mDecompressBufferUsed;
    mDownstreamZlib.avail_out = mDecompressBufferSize - mDecompressBufferUsed;
    int zlib_rv = inflate(&mDownstreamZlib, Z_NO_FLUSH);

    if (zlib_rv == Z_NEED_DICT)
      inflateSetDictionary(&mDownstreamZlib,
                           reinterpret_cast<const unsigned char *>
                           (SpdyStream::kDictionary),
                           strlen(SpdyStream::kDictionary) + 1);
    
    if (zlib_rv == Z_DATA_ERROR || zlib_rv == Z_MEM_ERROR)
      return NS_ERROR_FAILURE;

    mDecompressBufferUsed += mDecompressBufferSize - mDecompressBufferUsed -
      mDownstreamZlib.avail_out;
    
    // When there is no more output room, but input still available then
    // increase the output space
    if (zlib_rv == Z_OK &&
        !mDownstreamZlib.avail_out && mDownstreamZlib.avail_in) {
      LOG3(("SpdySession::DownstreamUncompress %p Large Headers - so far %d",
            this, mDecompressBufferSize));
      EnsureBuffer(mDecompressBuffer,
                   mDecompressBufferSize + 4096,
                   mDecompressBufferUsed,
                   mDecompressBufferSize);
    }
  }
  while (mDownstreamZlib.avail_in);
  return NS_OK;
}

nsresult
SpdySession::FindHeader(nsCString name,
                        nsDependentCSubstring &value)
{
  const unsigned char *nvpair = reinterpret_cast<unsigned char *>
    (mDecompressBuffer.get()) + 2;
  const unsigned char *lastHeaderByte = reinterpret_cast<unsigned char *>
    (mDecompressBuffer.get()) + mDecompressBufferUsed;
  if (lastHeaderByte < nvpair)
    return NS_ERROR_ILLEGAL_VALUE;
  PRUint16 numPairs =
    PR_ntohs(reinterpret_cast<PRUint16 *>(mDecompressBuffer.get())[0]);
  for (PRUint16 index = 0; index < numPairs; ++index) {
    if (lastHeaderByte < nvpair + 2)
      return NS_ERROR_ILLEGAL_VALUE;
    PRUint32 nameLen = (nvpair[0] << 8) + nvpair[1];
    if (lastHeaderByte < nvpair + 2 + nameLen)
      return NS_ERROR_ILLEGAL_VALUE;
    nsDependentCSubstring nameString =
      Substring(reinterpret_cast<const char *>(nvpair) + 2,
                reinterpret_cast<const char *>(nvpair) + 2 + nameLen);
    if (lastHeaderByte < nvpair + 4 + nameLen)
      return NS_ERROR_ILLEGAL_VALUE;
    PRUint16 valueLen = (nvpair[2 + nameLen] << 8) + nvpair[3 + nameLen];
    if (lastHeaderByte < nvpair + 4 + nameLen + valueLen)
      return NS_ERROR_ILLEGAL_VALUE;
    if (nameString.Equals(name)) {
      value.Assign(((char *)nvpair) + 4 + nameLen, valueLen);
      return NS_OK;
    }
    nvpair += 4 + nameLen + valueLen;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

nsresult
SpdySession::ConvertHeaders(nsDependentCSubstring &status,
                            nsDependentCSubstring &version)
{
  mFlatHTTPResponseHeaders.Truncate();
  mFlatHTTPResponseHeadersOut = 0;
  mFlatHTTPResponseHeaders.SetCapacity(mDecompressBufferUsed + 64);

  // Connection, Keep-Alive and chunked transfer encodings are to be
  // removed.

  // Content-Length is 'advisory'.. we will not strip it because it can
  // create UI feedback.
  
  mFlatHTTPResponseHeaders.Append(version);
  mFlatHTTPResponseHeaders.Append(NS_LITERAL_CSTRING(" "));
  mFlatHTTPResponseHeaders.Append(status);
  mFlatHTTPResponseHeaders.Append(NS_LITERAL_CSTRING("\r\n"));

  const unsigned char *nvpair = reinterpret_cast<unsigned char *>
    (mDecompressBuffer.get()) + 2;
  const unsigned char *lastHeaderByte = reinterpret_cast<unsigned char *>
    (mDecompressBuffer.get()) + mDecompressBufferUsed;

  if (lastHeaderByte < nvpair)
    return NS_ERROR_ILLEGAL_VALUE;

  PRUint16 numPairs =
    PR_ntohs(reinterpret_cast<PRUint16 *>(mDecompressBuffer.get())[0]);

  for (PRUint16 index = 0; index < numPairs; ++index) {
    if (lastHeaderByte < nvpair + 2)
      return NS_ERROR_ILLEGAL_VALUE;

    PRUint32 nameLen = (nvpair[0] << 8) + nvpair[1];
    if (lastHeaderByte < nvpair + 2 + nameLen)
      return NS_ERROR_ILLEGAL_VALUE;

    nsDependentCSubstring nameString =
      Substring(reinterpret_cast<const char *>(nvpair) + 2,
                reinterpret_cast<const char *>(nvpair) + 2 + nameLen);

    if (lastHeaderByte < nvpair + 4 + nameLen)
      return NS_ERROR_ILLEGAL_VALUE;

    // Look for illegal characters in the nameString.
    // This includes upper case characters and nulls (as they will
    // break the fixup-nulls-in-value-string algorithm)
    // Look for upper case characters in the name. They are illegal.
    for (char *cPtr = nameString.BeginWriting();
         cPtr && cPtr < nameString.EndWriting();
         ++cPtr) {
      if (*cPtr <= 'Z' && *cPtr >= 'A') {
        nsCString toLog(nameString);

        LOG3(("SpdySession::ConvertHeaders session=%p stream=%p "
              "upper case response header found. [%s]\n",
              this, mInputFrameDataStream, toLog.get()));

        return NS_ERROR_ILLEGAL_VALUE;
      }

      // check for null characters
      if (*cPtr == '\0')
        return NS_ERROR_ILLEGAL_VALUE;
    }

    // HTTP Chunked responses are not legal over spdy. We do not need
    // to look for chunked specifically because it is the only HTTP
    // allowed default encoding and we did not negotiate further encodings
    // via TE
    if (nameString.Equals(NS_LITERAL_CSTRING("transfer-encoding"))) {
      LOG3(("SpdySession::ConvertHeaders session=%p stream=%p "
            "transfer-encoding found. Chunked is invalid and no TE sent.",
            this, mInputFrameDataStream));

      return NS_ERROR_ILLEGAL_VALUE;
    }

    PRUint16 valueLen = (nvpair[2 + nameLen] << 8) + nvpair[3 + nameLen];
    if (lastHeaderByte < nvpair + 4 + nameLen + valueLen)
      return NS_ERROR_ILLEGAL_VALUE;

    if (!nameString.Equals(NS_LITERAL_CSTRING("version")) &&
        !nameString.Equals(NS_LITERAL_CSTRING("status")) &&
        !nameString.Equals(NS_LITERAL_CSTRING("connection")) &&
        !nameString.Equals(NS_LITERAL_CSTRING("keep-alive"))) {
      nsDependentCSubstring valueString =
        Substring(reinterpret_cast<const char *>(nvpair) + 4 + nameLen,
                  reinterpret_cast<const char *>(nvpair) + 4 + nameLen +
                  valueLen);
      
      mFlatHTTPResponseHeaders.Append(nameString);
      mFlatHTTPResponseHeaders.Append(NS_LITERAL_CSTRING(": "));

      // expand NULL bytes in the value string
      for (char *cPtr = valueString.BeginWriting();
           cPtr && cPtr < valueString.EndWriting();
           ++cPtr) {
        if (*cPtr != 0) {
          mFlatHTTPResponseHeaders.Append(*cPtr);
          continue;
        }

        // NULLs are really "\r\nhdr: "
        mFlatHTTPResponseHeaders.Append(NS_LITERAL_CSTRING("\r\n"));
        mFlatHTTPResponseHeaders.Append(nameString);
        mFlatHTTPResponseHeaders.Append(NS_LITERAL_CSTRING(": "));
      }

      mFlatHTTPResponseHeaders.Append(NS_LITERAL_CSTRING("\r\n"));
    }
    nvpair += 4 + nameLen + valueLen;
  }

  mFlatHTTPResponseHeaders.Append(
    NS_LITERAL_CSTRING("X-Firefox-Spdy: 1\r\n\r\n"));
  LOG (("decoded response headers are:\n%s",
        mFlatHTTPResponseHeaders.get()));
  
  return NS_OK;
}

void
SpdySession::GeneratePing(PRUint32 aID)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession::GeneratePing %p 0x%X\n", this, aID));

  EnsureBuffer(mOutputQueueBuffer, mOutputQueueUsed + 12,
               mOutputQueueUsed, mOutputQueueSize);
  char *packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  mOutputQueueUsed += 12;

  packet[0] = kFlag_Control;
  packet[1] = 2;                                  /* version 2 */
  packet[2] = 0;
  packet[3] = CONTROL_TYPE_PING;
  packet[4] = 0;                                  /* flags */
  packet[5] = 0;
  packet[6] = 0;
  packet[7] = 4;                                  /* length */
  
  aID = PR_htonl(aID);
  memcpy(packet + 8, &aID, 4);

  FlushOutputQueue();
}

void
SpdySession::GenerateRstStream(PRUint32 aStatusCode, PRUint32 aID)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession::GenerateRst %p 0x%X %d\n", this, aID, aStatusCode));

  EnsureBuffer(mOutputQueueBuffer, mOutputQueueUsed + 16,
               mOutputQueueUsed, mOutputQueueSize);
  char *packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  mOutputQueueUsed += 16;

  packet[0] = kFlag_Control;
  packet[1] = 2;                                  /* version 2 */
  packet[2] = 0;
  packet[3] = CONTROL_TYPE_RST_STREAM;
  packet[4] = 0;                                  /* flags */
  packet[5] = 0;
  packet[6] = 0;
  packet[7] = 8;                                  /* length */
  
  aID = PR_htonl(aID);
  memcpy(packet + 8, &aID, 4);
  aStatusCode = PR_htonl(aStatusCode);
  memcpy(packet + 12, &aStatusCode, 4);

  FlushOutputQueue();
}

void
SpdySession::GenerateGoAway()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession::GenerateGoAway %p\n", this));

  EnsureBuffer(mOutputQueueBuffer, mOutputQueueUsed + 12,
               mOutputQueueUsed, mOutputQueueSize);
  char *packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  mOutputQueueUsed += 12;

  memset(packet, 0, 12);
  packet[0] = kFlag_Control;
  packet[1] = 2;                                  /* version 2 */
  packet[3] = CONTROL_TYPE_GOAWAY;
  packet[7] = 4;                                  /* data length */
  
  // last-good-stream-id are bytes 8-11, when we accept server push this will
  // need to be set non zero

  FlushOutputQueue();
}

void
SpdySession::CleanupStream(SpdyStream *aStream, nsresult aResult)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession::CleanupStream %p %p 0x%x %X\n",
        this, aStream, aStream->StreamID(), aResult));

  if (!aStream->RecvdFin() && aStream->StreamID()) {
    LOG3(("Stream had not processed recv FIN, sending RST"));
    GenerateRstStream(RST_CANCEL, aStream->StreamID());
    --mConcurrent;
    ProcessPending();
  }
  
  // Check if partial frame reader
  if (aStream == mInputFrameDataStream) {
    LOG3(("Stream had active partial read frame on close"));
    ChangeDownstreamState(DISCARDING_DATA_FRAME);
    mInputFrameDataStream = nsnull;
  }

  // check the streams blocked on write, this is linear but the list
  // should be pretty short.
  PRUint32 size = mReadyForWrite.GetSize();
  for (PRUint32 count = 0; count < size; ++count) {
    SpdyStream *stream = static_cast<SpdyStream *>(mReadyForWrite.PopFront());
    if (stream != aStream)
      mReadyForWrite.Push(stream);
  }

  // Check the streams blocked on urgent (i.e. window update) writing.
  // This should also be short.
  size = mUrgentForWrite.GetSize();
  for (PRUint32 count = 0; count < size; ++count) {
    SpdyStream *stream = static_cast<SpdyStream *>(mUrgentForWrite.PopFront());
    if (stream != aStream)
      mUrgentForWrite.Push(stream);
  }

  // Check the streams queued for activation. Because we normally accept a high
  // level of parallelization this should also be short.
  size = mQueuedStreams.GetSize();
  for (PRUint32 count = 0; count < size; ++count) {
    SpdyStream *stream = static_cast<SpdyStream *>(mQueuedStreams.PopFront());
    if (stream != aStream)
      mQueuedStreams.Push(stream);
  }

  // Remove the stream from the ID hash table. (this one isn't short, which is
  // why it is hashed.)
  mStreamIDHash.Remove(aStream->StreamID());

  // Send the stream the close() indication
  aStream->Close(aResult);

  // removing from the stream transaction hash will
  // delete the SpdyStream and drop the reference to
  // its transaction
  mStreamTransactionHash.Remove(aStream->Transaction());

  if (mShouldGoAway && !mStreamTransactionHash.Count())
    Close(NS_OK);
}

nsresult
SpdySession::HandleSynStream(SpdySession *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_SYN_STREAM,
                    "wrong control type");
  
  if (self->mInputFrameDataSize < 18) {
    LOG3(("SpdySession::HandleSynStream %p SYN_STREAM too short data=%d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint32 streamID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);
  PRUint32 associatedID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[3]);

  LOG3(("SpdySession::HandleSynStream %p recv SYN_STREAM (push) "
        "for ID 0x%X associated with 0x%X.",
        self, streamID, associatedID));
    
  if (streamID & 0x01) {                   // test for odd stream ID
    LOG3(("SpdySession::HandleSynStream %p recvd SYN_STREAM id must be even.",
          self));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  ++(self->mServerPushedResources);

  // Anytime we start using the high bit of stream ID (either client or server)
  // begin to migrate to a new session.
  if (streamID >= kMaxStreamID)
    self->mShouldGoAway = true;

  // Need to decompress the headers even though we aren't using them yet in
  // order to keep the compression context consistent for other syn_reply frames
  nsresult rv = self->DownstreamUncompress(self->mInputFrameBuffer + 18,
                                           self->mInputFrameDataSize - 10);
  if (NS_FAILED(rv)) {
    LOG(("SpdySession::HandleSynStream uncompress failed\n"));
    return rv;
  }

  // todo populate cache. For now, just reject server push p3
  self->GenerateRstStream(RST_REFUSED_STREAM, streamID);
  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession::HandleSynReply(SpdySession *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_SYN_REPLY,
                    "wrong control type");

  if (self->mInputFrameDataSize < 8) {
    LOG3(("SpdySession::HandleSynReply %p SYN REPLY too short data=%d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }
  
  PRUint32 streamID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);
  self->mInputFrameDataStream = self->mStreamIDHash.Get(streamID);
  if (!self->mInputFrameDataStream) {
    LOG3(("SpdySession::HandleSynReply %p lookup streamID in syn_reply "
          "0x%X failed. NextStreamID = 0x%x", self, streamID,
          self->mNextStreamID));
    if (streamID >= self->mNextStreamID)
      self->GenerateRstStream(RST_INVALID_STREAM, streamID);
    
    // It is likely that this is a reply to a stream ID that has been canceled.
    // For the most part we would like to ignore it, but the header needs to be
    // be parsed to keep the compression context synchronized
    self->DownstreamUncompress(self->mInputFrameBuffer + 14,
                               self->mInputFrameDataSize - 6);
    self->ResetDownstreamState();
    return NS_OK;
  }

  self->mInputFrameDataStream->UpdateTransportReadEvents(
    self->mInputFrameDataSize);

  if (self->mInputFrameDataStream->GetFullyOpen()) {
    // "If an endpoint receives multiple SYN_REPLY frames for the same active
    // stream ID, it must drop the stream, and send a RST_STREAM for the
    // stream with the error PROTOCOL_ERROR."
    //
    // In addition to that we abort the session - this is a serious protocol
    // violation.

    self->GenerateRstStream(RST_PROTOCOL_ERROR, streamID);
    return NS_ERROR_ILLEGAL_VALUE;
  }
  self->mInputFrameDataStream->SetFullyOpen();

  self->mInputFrameDataLast = self->mInputFrameBuffer[4] & kFlag_Data_FIN;

  if (self->mInputFrameBuffer[4] & kFlag_Data_UNI) {
    LOG3(("SynReply had unidirectional flag set on it - nonsensical"));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  LOG3(("SpdySession::HandleSynReply %p SYN_REPLY for 0x%X fin=%d",
        self, streamID, self->mInputFrameDataLast));
  
  // The spdystream needs to see flattened http headers
  // The Frame Buffer currently holds the complete SYN_REPLY
  // frame. The interesting data is at offset 14, where the
  // compressed name/value header block lives.
  // We unpack that into the mDecompressBuffer - we can't do
  // it streamed because the version and status information
  // is not guaranteed to be first. This is then finally
  // converted to HTTP format in mFlatHTTPResponseHeaders

  nsresult rv = self->DownstreamUncompress(self->mInputFrameBuffer + 14,
                                           self->mInputFrameDataSize - 6);
  if (NS_FAILED(rv)) {
    LOG(("SpdySession::HandleSynReply uncompress failed\n"));
    return rv;
  }
  
  Telemetry::Accumulate(Telemetry::SPDY_SYN_REPLY_SIZE,
                        self->mInputFrameDataSize - 6);
  if (self->mDecompressBufferUsed) {
    PRUint32 ratio =
      (self->mInputFrameDataSize - 6) * 100 / self->mDecompressBufferUsed;
    Telemetry::Accumulate(Telemetry::SPDY_SYN_REPLY_RATIO, ratio);
  }

  // status and version are required.
  nsDependentCSubstring status, version;
  rv = self->FindHeader(NS_LITERAL_CSTRING("status"), status);
  if (NS_FAILED(rv))
    return rv;

  rv = self->FindHeader(NS_LITERAL_CSTRING("version"), version);
  if (NS_FAILED(rv))
    return rv;

  rv = self->ConvertHeaders(status, version);
  if (NS_FAILED(rv))
    return rv;

  self->ChangeDownstreamState(PROCESSING_CONTROL_SYN_REPLY);
  return NS_OK;
}

nsresult
SpdySession::HandleRstStream(SpdySession *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_RST_STREAM,
                    "wrong control type");

  if (self->mInputFrameDataSize != 8) {
    LOG3(("SpdySession::HandleRstStream %p RST_STREAM wrong length data=%d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint8 flags = reinterpret_cast<PRUint8 *>(self->mInputFrameBuffer.get())[4];

  PRUint32 streamID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);

  self->mDownstreamRstReason =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[3]);

  LOG3(("SpdySession::HandleRstStream %p RST_STREAM Reason Code %u ID %x "
        "flags %x", self, self->mDownstreamRstReason, streamID, flags));

  if (flags != 0) {
    LOG3(("SpdySession::HandleRstStream %p RST_STREAM with flags is illegal",
          self));
    return NS_ERROR_ILLEGAL_VALUE;
  }
  
  if (self->mDownstreamRstReason == RST_INVALID_STREAM ||
      self->mDownstreamRstReason == RST_FLOW_CONTROL_ERROR) {
    // basically just ignore this
    self->ResetDownstreamState();
    return NS_OK;
  }

  self->mInputFrameDataStream = self->mStreamIDHash.Get(streamID);
  if (!self->mInputFrameDataStream) {
    LOG3(("SpdySession::HandleRstStream %p lookup streamID for RST Frame "
          "0x%X failed", self, streamID));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  self->ChangeDownstreamState(PROCESSING_CONTROL_RST_STREAM);
  return NS_OK;
}

nsresult
SpdySession::HandleSettings(SpdySession *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_SETTINGS,
                    "wrong control type");

  if (self->mInputFrameDataSize < 4) {
    LOG3(("SpdySession::HandleSettings %p SETTINGS wrong length data=%d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint32 numEntries =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);

  // Ensure frame is large enough for supplied number of entries
  // Each entry is 8 bytes, frame data is reduced by 4 to account for
  // the NumEntries value.
  if ((self->mInputFrameDataSize - 4) < (numEntries * 8)) {
    LOG3(("SpdySession::HandleSettings %p SETTINGS wrong length data=%d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  LOG3(("SpdySession::HandleSettings %p SETTINGS Control Frame with %d entries",
        self, numEntries));

  for (PRUint32 index = 0; index < numEntries; ++index) {
    // To clarify the v2 spec:
    // Each entry is a 24 bits of a little endian id
    // followed by 8 bits of flags
    // followed by a 32 bit big endian value
    
    unsigned char *setting = reinterpret_cast<unsigned char *>
      (self->mInputFrameBuffer.get()) + 12 + index * 8;

    PRUint32 id = (setting[2] << 16) + (setting[1] << 8) + setting[0];
    PRUint32 flags = setting[3];
    PRUint32 value =  PR_ntohl(reinterpret_cast<PRUint32 *>(setting)[1]);

    LOG3(("Settings ID %d, Flags %X, Value %d", id, flags, value));

    switch (id)
    {
    case SETTINGS_TYPE_UPLOAD_BW:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_UL_BW, value);
      break;
      
    case SETTINGS_TYPE_DOWNLOAD_BW:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_DL_BW, value);
      break;
      
    case SETTINGS_TYPE_RTT:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_RTT, value);
      break;
      
    case SETTINGS_TYPE_MAX_CONCURRENT:
      self->mMaxConcurrent = value;
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_MAX_STREAMS, value);
      break;
      
    case SETTINGS_TYPE_CWND:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_CWND, value);
      break;
      
    case SETTINGS_TYPE_DOWNLOAD_RETRANS_RATE:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_RETRANS, value);
      break;
      
    case SETTINGS_TYPE_INITIAL_WINDOW:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_IW, value >> 10);
      break;
      
    default:
      break;
    }
    
  }
  
  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession::HandleNoop(SpdySession *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_NOOP,
                    "wrong control type");

  if (self->mInputFrameDataSize != 0) {
    LOG3(("SpdySession::HandleNoop %p NOP had data %d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  LOG3(("SpdySession::HandleNoop %p NOP.", self));

  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession::HandlePing(SpdySession *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_PING,
                    "wrong control type");

  if (self->mInputFrameDataSize != 4) {
    LOG3(("SpdySession::HandlePing %p PING had wrong amount of data %d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint32 pingID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);

  LOG3(("SpdySession::HandlePing %p PING ID 0x%X.", self, pingID));

  if (pingID & 0x01) {
    // We never expect to see an odd PING beacuse we never generate PING.
    // The spec mandates ignoring this
    LOG3(("SpdySession::HandlePing %p PING ID from server was odd.",
          self));
  }
  else {
    self->GeneratePing(pingID);
  }
    
  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession::HandleGoAway(SpdySession *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_GOAWAY,
                    "wrong control type");

  if (self->mInputFrameDataSize != 4) {
    LOG3(("SpdySession::HandleGoAway %p GOAWAY had wrong amount of data %d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  self->mShouldGoAway = true;
  self->mGoAwayID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);
  self->mCleanShutdown = true;
  
  LOG3(("SpdySession::HandleGoAway %p GOAWAY Last-Good-ID 0x%X.",
        self, self->mGoAwayID));
  self->ResumeRecv();
  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession::HandleHeaders(SpdySession *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_HEADERS,
                    "wrong control type");

  if (self->mInputFrameDataSize < 10) {
    LOG3(("SpdySession::HandleHeaders %p HEADERS had wrong amount of data %d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint32 streamID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);

  // this is actually not legal in the HTTP mapping of SPDY. All
  // headers are in the syn or syn reply. Log and ignore it.

  // in v3 this will be legal and we must remember to note
  // NS_NET_STATUS_RECEIVING_FROM from it

  LOG3(("SpdySession::HandleHeaders %p HEADERS for Stream 0x%X. "
        "They are ignored in the HTTP/SPDY mapping.",
        self, streamID));
  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession::HandleWindowUpdate(SpdySession *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_WINDOW_UPDATE,
                    "wrong control type");
  LOG3(("SpdySession::HandleWindowUpdate %p WINDOW UPDATE was "
        "received. WINDOW UPDATE is no longer defined in v2. Ignoring.",
        self));

  self->ResetDownstreamState();
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsAHttpTransaction. It is expected that nsHttpConnection is the caller
// of these methods
//-----------------------------------------------------------------------------

void
SpdySession::OnTransportStatus(nsITransport* aTransport,
                               nsresult aStatus,
                               PRUint64 aProgress)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  switch (aStatus) {
    // These should appear only once, deliver to the first
    // transaction on the session.
  case NS_NET_STATUS_RESOLVING_HOST:
  case NS_NET_STATUS_RESOLVED_HOST:
  case NS_NET_STATUS_CONNECTING_TO:
  case NS_NET_STATUS_CONNECTED_TO:
  {
    SpdyStream *target = mStreamIDHash.Get(1);
    if (target)
      target->Transaction()->OnTransportStatus(aTransport, aStatus, aProgress);
    break;
  }

  default:
    // The other transport events are ignored here because there is no good
    // way to map them to the right transaction in spdy. Instead, the events
    // are generated again from the spdy code and passed directly to the
    // correct transaction.

    // NS_NET_STATUS_SENDING_TO:
    // This is generated by the socket transport when (part) of
    // a transaction is written out
    //
    // There is no good way to map it to the right transaction in spdy,
    // so it is ignored here and generated separately when the SYN_STREAM
    // is sent from SpdyStream::TransmitFrame

    // NS_NET_STATUS_WAITING_FOR:
    // Created by nsHttpConnection when the request has been totally sent.
    // There is no good way to map it to the right transaction in spdy,
    // so it is ignored here and generated separately when the same
    // condition is complete in SpdyStream when there is no more
    // request body left to be transmitted.

    // NS_NET_STATUS_RECEIVING_FROM
    // Generated in spdysession whenever we read a data frame or a syn_reply
    // that can be attributed to a particular stream/transaction

    break;
  }
}

// ReadSegments() is used to write data to the network. Generally, HTTP
// request data is pulled from the approriate transaction and
// converted to SPDY data. Sometimes control data like window-update are
// generated instead.

nsresult
SpdySession::ReadSegments(nsAHttpSegmentReader *reader,
                          PRUint32 count,
                          PRUint32 *countRead)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  
  nsresult rv;
  *countRead = 0;

  // First priority goes to frames that were writing to the network but were
  // blocked part way through. Then to frames that have no streams (e.g ping
  // reply) and then third to streams marked urgent (generally they have
  // window updates), and finally to streams generally
  // ready to send data frames (http requests).

  LOG3(("SpdySession::ReadSegments %p", this));

  SpdyStream *stream;
  
  stream = static_cast<SpdyStream *>(mUrgentForWrite.PopFront());
  if (!stream)
    stream = static_cast<SpdyStream *>(mReadyForWrite.PopFront());
  if (!stream) {
    LOG3(("SpdySession %p could not identify a stream to write; suspending.",
          this));
    FlushOutputQueue();
    SetWriteCallbacks();
    return NS_BASE_STREAM_WOULD_BLOCK;
  }
  
  LOG3(("SpdySession %p will write from SpdyStream %p", this, stream));

  NS_ABORT_IF_FALSE(!mSegmentReader || !reader || (mSegmentReader == reader),
                    "Inconsistent Write Function Callback");

  if (reader)
    mSegmentReader = reader;
  rv = stream->ReadSegments(this, count, countRead);

  // Not every permutation of stream->ReadSegents produces data (and therefore
  // tries to flush the output queue) - SENDING_FIN_STREAM can be an example
  // of that. But we might still have old data buffered that would be good
  // to flush.
  FlushOutputQueue();

  if (stream->RequestBlockedOnRead()) {
    
    // We are blocked waiting for input - either more http headers or
    // any request body data. When more data from the request stream
    // becomes available the httptransaction will call conn->ResumeSend().
    
    LOG3(("SpdySession::ReadSegments %p dealing with block on read", this));

    // call readsegments again if there are other streams ready
    // to run in this session
    if (GetWriteQueueSize())
      rv = NS_OK;
    else
      rv = NS_BASE_STREAM_WOULD_BLOCK;
    SetWriteCallbacks();
    return rv;
  }
  
  if (NS_FAILED(rv)) {
    LOG3(("SpdySession::ReadSegments %p returning FAIL code %X",
          this, rv));
    return rv;
  }
  
  if (*countRead > 0) {
    LOG3(("SpdySession::ReadSegments %p stream=%p generated end of frame %d",
          this, stream, *countRead));
    mReadyForWrite.Push(stream);
    SetWriteCallbacks();
    return rv;
  }
  
  LOG3(("SpdySession::ReadSegments %p stream=%p stream send complete",
        this, stream));
  
  /* we now want to recv data */
  ResumeRecv();

  // call readsegments again if there are other streams ready
  // to go in this session
  SetWriteCallbacks();

  return rv;
}

// WriteSegments() is used to read data off the socket. Generally this is
// just the SPDY frame header and from there the appropriate SPDYStream
// is identified from the Stream-ID. The http transaction associated with
// that read then pulls in the data directly, which it will feed to
// OnWriteSegment(). That function will gateway it into http and feed
// it to the appropriate transaction.

// we call writer->OnWriteSegment to get a spdy header.. and decide if it is
// data or control.. if it is control, just deal with it.
// if it is data, identify the spdy stream
// call stream->WriteSegemnts which can call this::OnWriteSegment to get the
// data. It always gets full frames if they are part of the stream

nsresult
SpdySession::WriteSegments(nsAHttpSegmentWriter *writer,
                           PRUint32 count,
                           PRUint32 *countWritten)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  
  nsresult rv;
  *countWritten = 0;

  if (mClosed)
    return NS_ERROR_FAILURE;

  SetWriteCallbacks();
  
  // We buffer all control frames and act on them in this layer.
  // We buffer the first 8 bytes of data frames (the header) but
  // the actual data is passed through unprocessed.
  
  if (mDownstreamState == BUFFERING_FRAME_HEADER) {
    // The first 8 bytes of every frame is header information that
    // we are going to want to strip before passing to http. That is
    // true of both control and data packets.
    
    NS_ABORT_IF_FALSE(mInputFrameBufferUsed < 8,
                      "Frame Buffer Used Too Large for State");

    rv = writer->OnWriteSegment(mInputFrameBuffer + mInputFrameBufferUsed,
                                8 - mInputFrameBufferUsed,
                                countWritten);
    if (NS_FAILED(rv)) {
      LOG3(("SpdySession %p buffering frame header read failure %x\n",
            this, rv));
      // maybe just blocked reading from network
      if (rv == NS_BASE_STREAM_WOULD_BLOCK)
        ResumeRecv();
      return rv;
    }

    LogIO(this, nsnull, "Reading Frame Header",
          mInputFrameBuffer + mInputFrameBufferUsed, *countWritten);

    mInputFrameBufferUsed += *countWritten;

    if (mInputFrameBufferUsed < 8)
    {
      LOG3(("SpdySession::WriteSegments %p "
            "BUFFERING FRAME HEADER incomplete size=%d",
            this, mInputFrameBufferUsed));
      return rv;
    }

    // For both control and data frames the second 32 bit word of the header
    // is 8-flags, 24-length. (network byte order)
    mInputFrameDataSize =
      PR_ntohl(reinterpret_cast<PRUint32 *>(mInputFrameBuffer.get())[1]);
    mInputFrameDataSize &= 0x00ffffff;
    mInputFrameDataRead = 0;
    
    if (mInputFrameBuffer[0] & kFlag_Control) {
      EnsureBuffer(mInputFrameBuffer, mInputFrameDataSize + 8, 8,
                   mInputFrameBufferSize);
      ChangeDownstreamState(BUFFERING_CONTROL_FRAME);
      
      // The first 32 bit word of the header is
      // 1 ctrl - 15 version - 16 type
      PRUint16 version =
        PR_ntohs(reinterpret_cast<PRUint16 *>(mInputFrameBuffer.get())[0]);
      version &= 0x7fff;
      
      mFrameControlType =
        PR_ntohs(reinterpret_cast<PRUint16 *>(mInputFrameBuffer.get())[1]);
      
      LOG3(("SpdySession::WriteSegments %p - Control Frame Identified "
            "type %d version %d data len %d",
            this, mFrameControlType, version, mInputFrameDataSize));

      if (mFrameControlType >= CONTROL_TYPE_LAST ||
          mFrameControlType <= CONTROL_TYPE_FIRST)
        return NS_ERROR_ILLEGAL_VALUE;

      // The protocol document says this value must be 1 even though this
      // is known as version 2.. Testing interop indicates that is a typo
      // in the protocol document
      if (version != 2) {
        return NS_ERROR_ILLEGAL_VALUE;
      }
    }
    else {
      ChangeDownstreamState(PROCESSING_DATA_FRAME);

      PRUint32 streamID =
        PR_ntohl(reinterpret_cast<PRUint32 *>(mInputFrameBuffer.get())[0]);
      mInputFrameDataStream = mStreamIDHash.Get(streamID);
      if (!mInputFrameDataStream) {
        LOG3(("SpdySession::WriteSegments %p lookup streamID 0x%X failed. "
              "Next = 0x%x", this, streamID, mNextStreamID));
        if (streamID >= mNextStreamID)
          GenerateRstStream(RST_INVALID_STREAM, streamID);
        ChangeDownstreamState(DISCARDING_DATA_FRAME);
      }
      mInputFrameDataLast = (mInputFrameBuffer[4] & kFlag_Data_FIN);
      Telemetry::Accumulate(Telemetry::SPDY_CHUNK_RECVD,
                            mInputFrameDataSize >> 10);
      LOG3(("Start Processing Data Frame. "
            "Session=%p Stream ID 0x%x Stream Ptr %p Fin=%d Len=%d",
            this, streamID, mInputFrameDataStream, mInputFrameDataLast,
            mInputFrameDataSize));

      if (mInputFrameBuffer[4] & kFlag_Data_ZLIB) {
        LOG3(("Data flag has ZLIB flag set which is not valid >=2 spdy"));
        return NS_ERROR_ILLEGAL_VALUE;
      }
    }
  }

  if (mDownstreamState == PROCESSING_CONTROL_RST_STREAM) {
    if (mDownstreamRstReason == RST_REFUSED_STREAM)
      rv = NS_ERROR_NET_RESET;            //we can retry this 100% safely
    else if (mDownstreamRstReason == RST_CANCEL ||
             mDownstreamRstReason == RST_PROTOCOL_ERROR ||
             mDownstreamRstReason == RST_INTERNAL_ERROR ||
             mDownstreamRstReason == RST_UNSUPPORTED_VERSION)
      rv = NS_ERROR_NET_INTERRUPT;
    else
      rv = NS_ERROR_ILLEGAL_VALUE;

    if (mDownstreamRstReason != RST_REFUSED_STREAM &&
        mDownstreamRstReason != RST_CANCEL)
      mShouldGoAway = true;

    // mInputFrameDataStream is reset by ChangeDownstreamState
    SpdyStream *stream = mInputFrameDataStream;
    ResetDownstreamState();
    CleanupStream(stream, rv);
    return NS_OK;
  }

  if (mDownstreamState == PROCESSING_DATA_FRAME ||
      mDownstreamState == PROCESSING_CONTROL_SYN_REPLY) {

    mSegmentWriter = writer;
    rv = mInputFrameDataStream->WriteSegments(this, count, countWritten);
    mSegmentWriter = nsnull;

    if (rv == NS_BASE_STREAM_CLOSED) {
      // This will happen when the transaction figures out it is EOF, generally
      // due to a content-length match being made
      SpdyStream *stream = mInputFrameDataStream;
      if (mInputFrameDataRead == mInputFrameDataSize)
        ResetDownstreamState();
      CleanupStream(stream, NS_OK);
      NS_ABORT_IF_FALSE(!mNeedsCleanup, "double cleanup out of data frame");
      return NS_OK;
    }
    
    if (mNeedsCleanup) {
      CleanupStream(mNeedsCleanup, NS_OK);
      mNeedsCleanup = nsnull;
    }

    // In v3 this is where we would generate a window update

    return rv;
  }

  if (mDownstreamState == DISCARDING_DATA_FRAME) {
    char trash[4096];
    PRUint32 count = NS_MIN(4096U, mInputFrameDataSize - mInputFrameDataRead);

    if (!count) {
      ResetDownstreamState();
      ResumeRecv();
      return NS_BASE_STREAM_WOULD_BLOCK;
    }

    rv = writer->OnWriteSegment(trash, count, countWritten);

    if (NS_FAILED(rv)) {
      LOG3(("SpdySession %p discard frame read failure %x\n", this, rv));
      // maybe just blocked reading from network
      if (rv == NS_BASE_STREAM_WOULD_BLOCK)
        ResumeRecv();
      return rv;
    }

    LogIO(this, nsnull, "Discarding Frame", trash, *countWritten);

    mInputFrameDataRead += *countWritten;

    if (mInputFrameDataRead == mInputFrameDataSize)
      ResetDownstreamState();
    return rv;
  }
  
  if (mDownstreamState != BUFFERING_CONTROL_FRAME) {
    // this cannot happen
    NS_ABORT_IF_FALSE(false, "Not in Bufering Control Frame State");
    return NS_ERROR_UNEXPECTED;
  }

  NS_ABORT_IF_FALSE(mInputFrameBufferUsed == 8,
                    "Frame Buffer Header Not Present");

  rv = writer->OnWriteSegment(mInputFrameBuffer + 8 + mInputFrameDataRead,
                              mInputFrameDataSize - mInputFrameDataRead,
                              countWritten);
  if (NS_FAILED(rv)) {
    LOG3(("SpdySession %p buffering control frame read failure %x\n",
          this, rv));
    // maybe just blocked reading from network
    if (rv == NS_BASE_STREAM_WOULD_BLOCK)
      ResumeRecv();
    return rv;
  }

  LogIO(this, nsnull, "Reading Control Frame",
        mInputFrameBuffer + 8 + mInputFrameDataRead, *countWritten);

  mInputFrameDataRead += *countWritten;

  if (mInputFrameDataRead != mInputFrameDataSize)
    return NS_OK;

  // This check is actually redundant, the control type was previously
  // checked to make sure it was in range, but we will check it again
  // at time of use to make sure a regression doesn't creep in.
  if (mFrameControlType >= CONTROL_TYPE_LAST ||
      mFrameControlType <= CONTROL_TYPE_FIRST) 
  {
    NS_ABORT_IF_FALSE(false, "control type out of range");
    return NS_ERROR_ILLEGAL_VALUE;
  }
  rv = sControlFunctions[mFrameControlType](this);

  NS_ABORT_IF_FALSE(NS_FAILED(rv) ||
                    mDownstreamState != BUFFERING_CONTROL_FRAME,
                    "Control Handler returned OK but did not change state");

  if (mShouldGoAway && !mStreamTransactionHash.Count())
    Close(NS_OK);
  return rv;
}

void
SpdySession::Close(nsresult aReason)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  if (mClosed)
    return;

  LOG3(("SpdySession::Close %p %X", this, aReason));

  mClosed = true;
  mStreamTransactionHash.Enumerate(ShutdownEnumerator, this);
  if (NS_SUCCEEDED(aReason))
    GenerateGoAway();
  mConnection = nsnull;
  mSegmentReader = nsnull;
  mSegmentWriter = nsnull;
}

void
SpdySession::CloseTransaction(nsAHttpTransaction *aTransaction,
                              nsresult aResult)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession::CloseTransaction %p %p %x", this, aTransaction, aResult));

  // Generally this arrives as a cancel event from the connection manager.

  // need to find the stream and call CleanupStream() on it.
  SpdyStream *stream = mStreamTransactionHash.Get(aTransaction);
  if (!stream) {
    LOG3(("SpdySession::CloseTransaction %p %p %x - not found.",
          this, aTransaction, aResult));
    return;
  }
  LOG3(("SpdySession::CloseTranscation probably a cancel. "
        "this=%p, trans=%p, result=%x, streamID=0x%X stream=%p",
        this, aTransaction, aResult, stream->StreamID(), stream));
  CleanupStream(stream, aResult);
  ResumeRecv();
}


//-----------------------------------------------------------------------------
// nsAHttpSegmentReader
//-----------------------------------------------------------------------------

nsresult
SpdySession::OnReadSegment(const char *buf,
                           PRUint32 count,
                           PRUint32 *countRead)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  
  nsresult rv;
  
  // If we can release old queued data then we can try and write the new
  // data directly to the network without using the output queue at all
  if (mOutputQueueUsed)
    FlushOutputQueue();

  if (!mOutputQueueUsed && mSegmentReader) {
    // try and write directly without output queue
    rv = mSegmentReader->OnReadSegment(buf, count, countRead);

    if (rv == NS_BASE_STREAM_WOULD_BLOCK)
      *countRead = 0;
    else if (NS_FAILED(rv))
      return rv;
    
    if (*countRead < count) {
      PRUint32 required = count - *countRead;
      // assuming a commitment() happened, this ensurebuffer is a nop
      // but just in case the queuesize is too small for the required data
      // call ensurebuffer().
      EnsureBuffer(mOutputQueueBuffer, required, 0, mOutputQueueSize);
      memcpy(mOutputQueueBuffer.get(), buf + *countRead, required);
      mOutputQueueUsed = required;
    }
    
    *countRead = count;
    return NS_OK;
  }

  // At this point we are going to buffer the new data in the output
  // queue if it fits. By coalescing multiple small submissions into one larger
  // buffer we can get larger writes out to the network later on.

  // This routine should not be allowed to fill up the output queue
  // all on its own - at least kQueueReserved bytes are always left
  // for other routines to use - but this is an all-or-nothing function,
  // so if it will not all fit just return WOULD_BLOCK

  if ((mOutputQueueUsed + count) > (mOutputQueueSize - kQueueReserved))
    return NS_BASE_STREAM_WOULD_BLOCK;
  
  memcpy(mOutputQueueBuffer.get() + mOutputQueueUsed, buf, count);
  mOutputQueueUsed += count;
  *countRead = count;

  FlushOutputQueue();

  return NS_OK;
}

nsresult
SpdySession::CommitToSegmentSize(PRUint32 count)
{
  if (mOutputQueueUsed)
    FlushOutputQueue();

  // would there be enough room to buffer this if needed?
  if ((mOutputQueueUsed + count) <= (mOutputQueueSize - kQueueReserved))
    return NS_OK;
  
  // if we are using part of our buffers already, try again later
  if (mOutputQueueUsed)
    return NS_BASE_STREAM_WOULD_BLOCK;

  // not enough room to buffer even with completely empty buffers.
  // normal frames are max 4kb, so the only case this can really happen
  // is a SYN_STREAM with technically unbounded headers. That is highly
  // unlikely, but possible. Create enough room for it because the buffers
  // will be necessary - SSL does not absorb writes of very large sizes
  // in single sends.

  EnsureBuffer(mOutputQueueBuffer, count + kQueueReserved, 0, mOutputQueueSize);

  NS_ABORT_IF_FALSE((mOutputQueueUsed + count) <=
                    (mOutputQueueSize - kQueueReserved),
                    "buffer not as large as expected");

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsAHttpSegmentWriter
//-----------------------------------------------------------------------------

nsresult
SpdySession::OnWriteSegment(char *buf,
                            PRUint32 count,
                            PRUint32 *countWritten)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  nsresult rv;

  if (!mSegmentWriter) {
    // the only way this could happen would be if Close() were called on the
    // stack with WriteSegments()
    return NS_ERROR_FAILURE;
  }
  
  if (mDownstreamState == PROCESSING_DATA_FRAME) {

    if (mInputFrameDataLast &&
        mInputFrameDataRead == mInputFrameDataSize) {
      // This will result in Close() being called
      NS_ABORT_IF_FALSE(!mNeedsCleanup, "mNeedsCleanup unexpectedly set");
      mNeedsCleanup = mInputFrameDataStream;

      LOG3(("SpdySession::OnWriteSegment %p - recorded downstream fin of "
            "stream %p 0x%X", this, mInputFrameDataStream,
            mInputFrameDataStream->StreamID()));
      *countWritten = 0;
      ResetDownstreamState();
      return NS_BASE_STREAM_CLOSED;
    }
    
    count = NS_MIN(count, mInputFrameDataSize - mInputFrameDataRead);
    rv = mSegmentWriter->OnWriteSegment(buf, count, countWritten);
    if (NS_FAILED(rv))
      return rv;

    LogIO(this, mInputFrameDataStream, "Reading Data Frame",
          buf, *countWritten);

    mInputFrameDataRead += *countWritten;
    
    mInputFrameDataStream->UpdateTransportReadEvents(*countWritten);
    if ((mInputFrameDataRead == mInputFrameDataSize) && !mInputFrameDataLast)
      ResetDownstreamState();

    return rv;
  }
  
  if (mDownstreamState == PROCESSING_CONTROL_SYN_REPLY) {
    
    if (mFlatHTTPResponseHeaders.Length() == mFlatHTTPResponseHeadersOut &&
        mInputFrameDataLast) {
      *countWritten = 0;
      ResetDownstreamState();
      return NS_BASE_STREAM_CLOSED;
    }
      
    count = NS_MIN(count,
                   mFlatHTTPResponseHeaders.Length() -
                   mFlatHTTPResponseHeadersOut);
    memcpy(buf,
           mFlatHTTPResponseHeaders.get() + mFlatHTTPResponseHeadersOut,
           count);
    mFlatHTTPResponseHeadersOut += count;
    *countWritten = count;

    if (mFlatHTTPResponseHeaders.Length() == mFlatHTTPResponseHeadersOut &&
        !mInputFrameDataLast)
      ResetDownstreamState();
    return NS_OK;
  }

  return NS_ERROR_UNEXPECTED;
}

//-----------------------------------------------------------------------------
// Modified methods of nsAHttpConnection
//-----------------------------------------------------------------------------

nsresult
SpdySession::ResumeSend()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession::ResumeSend %p", this));

  if (!mConnection)
    return NS_ERROR_FAILURE;

  return mConnection->ResumeSend();
}

void
SpdySession::TransactionHasDataToWrite(nsAHttpTransaction *caller)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession::TransactionHasDataToWrite %p trans=%p", this, caller));

  // a trapped signal from the http transaction to the connection that
  // it is no longer blocked on read.

  SpdyStream *stream = mStreamTransactionHash.Get(caller);
  if (!stream) {
    LOG3(("SpdySession::TransactionHasDataToWrite %p caller %p not found",
          this, caller));
    return;
  }
  
  LOG3(("SpdySession::TransactionHasDataToWrite %p ID is %x",
        this, stream->StreamID()));

  mReadyForWrite.Push(stream);
}

void
SpdySession::TransactionHasDataToWrite(SpdyStream *stream)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession::TransactionHasDataToWrite %p stream=%p ID=%x",
        this, stream, stream->StreamID()));

  mReadyForWrite.Push(stream);
  SetWriteCallbacks();
}

nsresult
SpdySession::ResumeRecv()
{
  if (!mConnection)
    return NS_ERROR_FAILURE;

  return mConnection->ResumeRecv();
}

bool
SpdySession::IsPersistent()
{
  return true;
}

nsresult
SpdySession::TakeTransport(nsISocketTransport **,
                           nsIAsyncInputStream **,
                           nsIAsyncOutputStream **)
{
  NS_ABORT_IF_FALSE(false, "TakeTransport of SpdySession");
  return NS_ERROR_UNEXPECTED;
}

nsHttpConnection *
SpdySession::TakeHttpConnection()
{
  NS_ABORT_IF_FALSE(false, "TakeHttpConnection of SpdySession");
  return nsnull;
}

nsISocketTransport *
SpdySession::Transport()
{
  if (!mConnection)
    return nsnull;
  return mConnection->Transport();
}

//-----------------------------------------------------------------------------
// unused methods of nsAHttpTransaction
// We can be sure of this because SpdySession is only constructed in
// nsHttpConnection and is never passed out of that object
//-----------------------------------------------------------------------------

void
SpdySession::SetConnection(nsAHttpConnection *)
{
  // This is unexpected
  NS_ABORT_IF_FALSE(false, "SpdySession::SetConnection()");
}

void
SpdySession::GetSecurityCallbacks(nsIInterfaceRequestor **,
                                  nsIEventTarget **)
{
  // This is unexpected
  NS_ABORT_IF_FALSE(false, "SpdySession::GetSecurityCallbacks()");
}

void
SpdySession::SetSSLConnectFailed()
{
  NS_ABORT_IF_FALSE(false, "SpdySession::SetSSLConnectFailed()");
}

bool
SpdySession::IsDone()
{
  NS_ABORT_IF_FALSE(false, "SpdySession::IsDone()");
  return false;
}

nsresult
SpdySession::Status()
{
  NS_ABORT_IF_FALSE(false, "SpdySession::Status()");
  return NS_ERROR_UNEXPECTED;
}

PRUint32
SpdySession::Available()
{
  NS_ABORT_IF_FALSE(false, "SpdySession::Available()");
  return 0;
}

nsHttpRequestHead *
SpdySession::RequestHead()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  NS_ABORT_IF_FALSE(false,
                    "SpdySession::RequestHead() "
                    "should not be called after SPDY is setup");
  return NULL;
}

PRUint32
SpdySession::Http1xTransactionCount()
{
  return 0;
}

//-----------------------------------------------------------------------------
// Pass through methods of nsAHttpConnection
//-----------------------------------------------------------------------------

nsAHttpConnection *
SpdySession::Connection()
{
  NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  return mConnection;
}

nsresult
SpdySession::OnHeadersAvailable(nsAHttpTransaction *transaction,
                                nsHttpRequestHead *requestHead,
                                nsHttpResponseHead *responseHead,
                                bool *reset)
{
  return mConnection->OnHeadersAvailable(transaction,
                                         requestHead,
                                         responseHead,
                                         reset);
}

void
SpdySession::GetConnectionInfo(nsHttpConnectionInfo **connInfo)
{
  mConnection->GetConnectionInfo(connInfo);
}

void
SpdySession::GetSecurityInfo(nsISupports **supports)
{
  mConnection->GetSecurityInfo(supports);
}

bool
SpdySession::IsReused()
{
  return mConnection->IsReused();
}

nsresult
SpdySession::PushBack(const char *buf, PRUint32 len)
{
  return mConnection->PushBack(buf, len);
}

bool
SpdySession::LastTransactionExpectedNoContent()
{
  return mConnection->LastTransactionExpectedNoContent();
}

void
SpdySession::SetLastTransactionExpectedNoContent(bool val)
{
  mConnection->SetLastTransactionExpectedNoContent(val);
}

} // namespace mozilla::net
} // namespace mozilla


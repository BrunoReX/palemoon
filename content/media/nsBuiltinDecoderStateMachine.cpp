/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: ML 1.1/GPL 2.0/LGPL 2.1
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
 * The Initial Developer of the Original Code is the Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Chris Double <chris.double@double.co.nz>
 *  Chris Pearce <chris@pearce.org.nz>
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

#include <limits>
#include "nsAudioStream.h"
#include "nsTArray.h"
#include "nsBuiltinDecoder.h"
#include "nsBuiltinDecoderReader.h"
#include "nsBuiltinDecoderStateMachine.h"
#include "mozilla/mozalloc.h"
#include "VideoUtils.h"
#include "nsTimeRanges.h"

using namespace mozilla;
using namespace mozilla::layers;

#ifdef PR_LOGGING
extern PRLogModuleInfo* gBuiltinDecoderLog;
#define LOG(type, msg) PR_LOG(gBuiltinDecoderLog, type, msg)
#else
#define LOG(type, msg)
#endif

// Wait this number of seconds when buffering, then leave and play
// as best as we can if the required amount of data hasn't been
// retrieved.
#define BUFFERING_WAIT 30

// The amount of data to retrieve during buffering is computed based
// on the download rate. BUFFERING_MIN_RATE is the minimum download
// rate to be used in that calculation to help avoid constant buffering
// attempts at a time when the average download rate has not stabilised.
#define BUFFERING_MIN_RATE 50000
#define BUFFERING_RATE(x) ((x)< BUFFERING_MIN_RATE ? BUFFERING_MIN_RATE : (x))

// If audio queue has less than this many ms of decoded audio, we won't risk
// trying to decode the video, we'll skip decoding video up to the next
// keyframe. We may increase this value for an individual decoder if we
// encounter video frames which take a long time to decode.
static const PRUint32 LOW_AUDIO_MS = 300;

// If more than this many ms of decoded audio is queued, we'll hold off
// decoding more audio. If we increase the low audio threshold (see
// LOW_AUDIO_MS above) we'll also increase this value to ensure it's not
// less than the low audio threshold.
const PRInt64 AMPLE_AUDIO_MS = 1000;

// Maximum number of bytes we'll allocate and write at once to the audio
// hardware when the audio stream contains missing samples and we're
// writing silence in order to fill the gap. We limit our silence-writes
// to 32KB in order to avoid allocating an impossibly large chunk of
// memory if we encounter a large chunk of silence.
const PRUint32 SILENCE_BYTES_CHUNK = 32 * 1024;

// If we have fewer than LOW_VIDEO_FRAMES decoded frames, and
// we're not "pumping video", we'll skip the video up to the next keyframe
// which is at or after the current playback position.
static const PRUint32 LOW_VIDEO_FRAMES = 1;

// If we've got more than AMPLE_VIDEO_FRAMES decoded video frames waiting in
// the video queue, we will not decode any more video frames until some have
// been consumed by the play state machine thread.
static const PRUint32 AMPLE_VIDEO_FRAMES = 10;

// Arbitrary "frame duration" when playing only audio.
static const int AUDIO_DURATION_MS = 40;

// If we increase our "low audio threshold" (see LOW_AUDIO_MS above), we
// use this as a factor in all our calculations. Increasing this will cause
// us to be more likely to increase our low audio threshold, and to
// increase it by more.
static const int THRESHOLD_FACTOR = 2;

// If we have less than this much undecoded data available, we'll consider
// ourselves to be running low on undecoded data. We determine how much
// undecoded data we have remaining using the reader's GetBuffered()
// implementation.
static const PRInt64 LOW_DATA_THRESHOLD_MS = 5000;

// LOW_DATA_THRESHOLD_MS needs to be greater than AMPLE_AUDIO_MS, otherwise
// the skip-to-keyframe logic can activate when we're running low on data.
PR_STATIC_ASSERT(LOW_DATA_THRESHOLD_MS > AMPLE_AUDIO_MS);

// Amount of excess ms of data to add in to the "should we buffer" calculation.
static const PRUint32 EXHAUSTED_DATA_MARGIN_MS = 60;

// If we enter buffering within QUICK_BUFFER_THRESHOLD_MS seconds of starting
// decoding, we'll enter "quick buffering" mode, which exits a lot sooner than
// normal buffering mode. This exists so that if the decode-ahead exhausts the
// downloaded data while decode/playback is just starting up (for example
// after a seek while the media is still playing, or when playing a media
// as soon as it's load started), we won't necessarily stop for 30s and wait
// for buffering. We may actually be able to playback in this case, so exit
// buffering early and try to play. If it turns out we can't play, we'll fall
// back to buffering normally.
static const PRUint32 QUICK_BUFFER_THRESHOLD_MS = 2000;

// If we're quick buffering, we'll remain in buffering mode while we have less than
// QUICK_BUFFERING_LOW_DATA_MS of decoded data available.
static const PRUint32 QUICK_BUFFERING_LOW_DATA_MS = 1000;

// If QUICK_BUFFERING_LOW_DATA_MS is > AMPLE_AUDIO_MS, we won't exit
// quick buffering in a timely fashion, as the decode pauses when it
// reaches AMPLE_AUDIO_MS decoded data, and thus we'll never reach
// QUICK_BUFFERING_LOW_DATA_MS.
PR_STATIC_ASSERT(QUICK_BUFFERING_LOW_DATA_MS <= AMPLE_AUDIO_MS);

static TimeDuration MsToDuration(PRInt64 aMs) {
  return TimeDuration::FromMilliseconds(static_cast<double>(aMs));
}

static PRInt64 DurationToMs(TimeDuration aDuration) {
  return static_cast<PRInt64>(aDuration.ToSeconds() * 1000);
}

class nsAudioMetadataEventRunner : public nsRunnable
{
private:
  nsCOMPtr<nsBuiltinDecoder> mDecoder;
public:
  nsAudioMetadataEventRunner(nsBuiltinDecoder* aDecoder, PRUint32 aChannels,
                             PRUint32 aRate) :
    mDecoder(aDecoder),
    mChannels(aChannels),
    mRate(aRate)
  {
  }

  NS_IMETHOD Run()
  {
    mDecoder->MetadataLoaded(mChannels, mRate);
    return NS_OK;
  }

  const PRUint32 mChannels;
  const PRUint32 mRate;
};

nsBuiltinDecoderStateMachine::nsBuiltinDecoderStateMachine(nsBuiltinDecoder* aDecoder,
                                                           nsBuiltinDecoderReader* aReader) :
  mDecoder(aDecoder),
  mState(DECODER_STATE_DECODING_METADATA),
  mAudioMonitor("media.audiostream"),
  mCbCrSize(0),
  mPlayDuration(0),
  mStartTime(-1),
  mEndTime(-1),
  mSeekTime(0),
  mReader(aReader),
  mCurrentFrameTime(0),
  mAudioStartTime(-1),
  mAudioEndTime(-1),
  mVideoFrameEndTime(-1),
  mVolume(1.0),
  mSeekable(PR_TRUE),
  mPositionChangeQueued(PR_FALSE),
  mAudioCompleted(PR_FALSE),
  mGotDurationFromMetaData(PR_FALSE),
  mStopDecodeThreads(PR_TRUE),
  mQuickBuffering(PR_FALSE),
  mEventManager(aDecoder)
{
  MOZ_COUNT_CTOR(nsBuiltinDecoderStateMachine);
}

nsBuiltinDecoderStateMachine::~nsBuiltinDecoderStateMachine()
{
  MOZ_COUNT_DTOR(nsBuiltinDecoderStateMachine);
}

PRBool nsBuiltinDecoderStateMachine::HasFutureAudio() const {
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  NS_ASSERTION(HasAudio(), "Should only call HasFutureAudio() when we have audio");
  // We've got audio ready to play if:
  // 1. We've not completed playback of audio, and
  // 2. we either have more than the threshold of decoded audio available, or
  //    we've completely decoded all audio (but not finished playing it yet
  //    as per 1).
  return !mAudioCompleted &&
         (AudioDecodedMs() > LOW_AUDIO_MS || mReader->mAudioQueue.IsFinished());
}

PRBool nsBuiltinDecoderStateMachine::HaveNextFrameData() const {
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  return (!HasAudio() || HasFutureAudio()) &&
         (!HasVideo() || mReader->mVideoQueue.GetSize() > 0);
}

PRInt64 nsBuiltinDecoderStateMachine::GetDecodedAudioDuration() {
  NS_ASSERTION(OnDecodeThread(), "Should be on decode thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  PRInt64 audioDecoded = mReader->mAudioQueue.Duration();
  if (mAudioEndTime != -1) {
    audioDecoded += mAudioEndTime - GetMediaTime();
  }
  return audioDecoded;
}

void nsBuiltinDecoderStateMachine::DecodeLoop()
{
  NS_ASSERTION(OnDecodeThread(), "Should be on decode thread.");

  // We want to "pump" the decode until we've got a few frames/samples decoded
  // before we consider whether decode is falling behind.
  PRBool audioPump = PR_TRUE;
  PRBool videoPump = PR_TRUE;

  // If the video decode is falling behind the audio, we'll start dropping the
  // inter-frames up until the next keyframe which is at or before the current
  // playback position. skipToNextKeyframe is PR_TRUE if we're currently
  // skipping up to the next keyframe.
  PRBool skipToNextKeyframe = PR_FALSE;

  // Once we've decoded more than videoPumpThreshold video frames, we'll
  // no longer be considered to be "pumping video".
  const unsigned videoPumpThreshold = AMPLE_VIDEO_FRAMES / 2;

  // After the audio decode fills with more than audioPumpThresholdMs ms
  // of decoded audio, we'll start to check whether the audio or video decode
  // is falling behind.
  const unsigned audioPumpThresholdMs = LOW_AUDIO_MS * 2;

  // Our local low audio threshold. We may increase this if we're slow to
  // decode video frames, in order to reduce the chance of audio underruns.
  PRInt64 lowAudioThreshold = LOW_AUDIO_MS;

  // Our local ample audio threshold. If we increase lowAudioThreshold, we'll
  // also increase this too appropriately (we don't want lowAudioThreshold to
  // be greater than ampleAudioThreshold, else we'd stop decoding!).
  PRInt64 ampleAudioThreshold = AMPLE_AUDIO_MS;

  MediaQueue<VideoData>& videoQueue = mReader->mVideoQueue;
  MediaQueue<SoundData>& audioQueue = mReader->mAudioQueue;

  MonitorAutoEnter mon(mDecoder->GetMonitor());

  PRBool videoPlaying = HasVideo();
  PRBool audioPlaying = HasAudio();

  // Main decode loop.
  while (mState != DECODER_STATE_SHUTDOWN &&
         !mStopDecodeThreads &&
         (videoPlaying || audioPlaying))
  {
    // We don't want to consider skipping to the next keyframe if we've
    // only just started up the decode loop, so wait until we've decoded
    // some frames before enabling the keyframe skip logic on video.
    if (videoPump &&
        static_cast<PRUint32>(videoQueue.GetSize()) >= videoPumpThreshold)
    {
      videoPump = PR_FALSE;
    }

    // We don't want to consider skipping to the next keyframe if we've
    // only just started up the decode loop, so wait until we've decoded
    // some audio data before enabling the keyframe skip logic on audio.
    if (audioPump && GetDecodedAudioDuration() >= audioPumpThresholdMs) {
      audioPump = PR_FALSE;
    }

    // We'll skip the video decode to the nearest keyframe if we're low on
    // audio, or if we're low on video, provided we're not running low on
    // data to decode. If we're running low on downloaded data to decode,
    // we won't start keyframe skipping, as we'll be pausing playback to buffer
    // soon anyway and we'll want to be able to display frames immediately
    // after buffering finishes.
    if (mState == DECODER_STATE_DECODING &&
        !skipToNextKeyframe &&
        videoPlaying &&
        ((!audioPump && audioPlaying && GetDecodedAudioDuration() < lowAudioThreshold) ||
         (!videoPump &&
           videoPlaying &&
           static_cast<PRUint32>(videoQueue.GetSize()) < LOW_VIDEO_FRAMES)) &&
        !HasLowUndecodedData())

    {
      skipToNextKeyframe = PR_TRUE;
      LOG(PR_LOG_DEBUG, ("Skipping video decode to the next keyframe"));
    }

    // Video decode.
    if (videoPlaying &&
        static_cast<PRUint32>(videoQueue.GetSize()) < AMPLE_VIDEO_FRAMES)
    {
      // Time the video decode, so that if it's slow, we can increase our low
      // audio threshold to reduce the chance of an audio underrun while we're
      // waiting for a video decode to complete.
      TimeDuration decodeTime;
      {
        PRInt64 currentTime = GetMediaTime();
        MonitorAutoExit exitMon(mDecoder->GetMonitor());
        TimeStamp start = TimeStamp::Now();
        videoPlaying = mReader->DecodeVideoFrame(skipToNextKeyframe, currentTime);
        decodeTime = TimeStamp::Now() - start;
      }
      if (THRESHOLD_FACTOR * DurationToMs(decodeTime) > lowAudioThreshold &&
          !HasLowUndecodedData())
      {
        lowAudioThreshold =
          NS_MIN(THRESHOLD_FACTOR * DurationToMs(decodeTime), AMPLE_AUDIO_MS);
        ampleAudioThreshold = NS_MAX(THRESHOLD_FACTOR * lowAudioThreshold,
                                     ampleAudioThreshold);
        LOG(PR_LOG_DEBUG,
            ("Slow video decode, set lowAudioThreshold=%lld ampleAudioThreshold=%lld",
             lowAudioThreshold, ampleAudioThreshold));
      }
    }

    // Audio decode.
    if (audioPlaying &&
        (GetDecodedAudioDuration() < ampleAudioThreshold || audioQueue.GetSize() == 0))
    {
      MonitorAutoExit exitMon(mDecoder->GetMonitor());
      audioPlaying = mReader->DecodeAudioData();
    }
    
    // Notify to ensure that the AudioLoop() is not waiting, in case it was
    // waiting for more audio to be decoded.
    mDecoder->GetMonitor().NotifyAll();

    if (!IsPlaying()) {
      // Update the ready state, so that the play DOM events fire. We only
      // need to do this if we're not playing; if we're playing the playback
      // code will do an update whenever it advances a frame.
      UpdateReadyState();
    }

    if (mState != DECODER_STATE_SHUTDOWN &&
        !mStopDecodeThreads &&
        (!audioPlaying || (GetDecodedAudioDuration() >= ampleAudioThreshold &&
                           audioQueue.GetSize() > 0))
        &&
        (!videoPlaying ||
          static_cast<PRUint32>(videoQueue.GetSize()) >= AMPLE_VIDEO_FRAMES))
    {
      // All active bitstreams' decode is well ahead of the playback
      // position, we may as well wait for the playback to catch up. Note the
      // audio push thread acquires and notifies the decoder monitor every time
      // it pops SoundData off the audio queue. So if the audio push thread pops
      // the last SoundData off the audio queue right after that queue reported
      // it was non-empty here, we'll receive a notification on the decoder
      // monitor which will wake us up shortly after we sleep, thus preventing
      // both the decode and audio push threads waiting at the same time.
      // See bug 620326.
      mon.Wait();
    }

  } // End decode loop.

  if (!mStopDecodeThreads &&
      mState != DECODER_STATE_SHUTDOWN &&
      mState != DECODER_STATE_SEEKING)
  {
    mState = DECODER_STATE_COMPLETED;
    mDecoder->GetMonitor().NotifyAll();
  }

  LOG(PR_LOG_DEBUG, ("Shutting down DecodeLoop this=%p", this));
}

PRBool nsBuiltinDecoderStateMachine::IsPlaying()
{
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  return !mPlayStartTime.IsNull();
}

void nsBuiltinDecoderStateMachine::AudioLoop()
{
  NS_ASSERTION(OnAudioThread(), "Should be on audio thread.");
  LOG(PR_LOG_DEBUG, ("Begun audio thread/loop"));
  PRInt64 audioDuration = 0;
  PRInt64 audioStartTime = -1;
  PRUint32 channels, rate;
  double volume = -1;
  PRBool setVolume;
  PRInt32 minWriteSamples = -1;
  PRInt64 samplesAtLastSleep = 0;
  {
    MonitorAutoEnter mon(mDecoder->GetMonitor());
    mAudioCompleted = PR_FALSE;
    audioStartTime = mAudioStartTime;
    channels = mInfo.mAudioChannels;
    rate = mInfo.mAudioRate;
    NS_ASSERTION(audioStartTime != -1, "Should have audio start time by now");
  }
  while (1) {

    // Wait while we're not playing, and we're not shutting down, or we're
    // playing and we've got no audio to play.
    {
      MonitorAutoEnter mon(mDecoder->GetMonitor());
      NS_ASSERTION(mState != DECODER_STATE_DECODING_METADATA,
                   "Should have meta data before audio started playing.");
      while (mState != DECODER_STATE_SHUTDOWN &&
             !mStopDecodeThreads &&
             (!IsPlaying() ||
              mState == DECODER_STATE_BUFFERING ||
              (mReader->mAudioQueue.GetSize() == 0 &&
               !mReader->mAudioQueue.AtEndOfStream())))
      {
        samplesAtLastSleep = audioDuration;
        mon.Wait();
      }

      // If we're shutting down, break out and exit the audio thread.
      if (mState == DECODER_STATE_SHUTDOWN ||
          mStopDecodeThreads ||
          mReader->mAudioQueue.AtEndOfStream())
      {
        break;
      }

      // We only want to go to the expense of taking the audio monitor and
      // changing the volume if it's the first time we've entered the loop
      // (as we must sync the volume in case it's changed since the
      // nsAudioStream was created) or if the volume has changed.
      setVolume = volume != mVolume;
      volume = mVolume;
    }

    if (setVolume || minWriteSamples == -1) {
      MonitorAutoEnter audioMon(mAudioMonitor);
      if (mAudioStream) {
        if (setVolume) {
          mAudioStream->SetVolume(volume);
        }
        if (minWriteSamples == -1) {
          minWriteSamples = mAudioStream->GetMinWriteSamples();
        }
      }
    }
    NS_ASSERTION(mReader->mAudioQueue.GetSize() > 0,
                 "Should have data to play");
    // See if there's missing samples in the audio stream. If there is, push
    // silence into the audio hardware, so we can play across the gap.
    const SoundData* s = mReader->mAudioQueue.PeekFront();

    // Calculate the number of samples that have been pushed onto the audio
    // hardware.
    PRInt64 playedSamples = 0;
    if (!MsToSamples(audioStartTime, rate, playedSamples)) {
      NS_WARNING("Int overflow converting playedSamples");
      break;
    }
    if (!AddOverflow(playedSamples, audioDuration, playedSamples)) {
      NS_WARNING("Int overflow adding playedSamples");
      break;
    }

    // Calculate the timestamp of the next chunk of audio in numbers of
    // samples.
    PRInt64 sampleTime = 0;
    if (!MsToSamples(s->mTime, rate, sampleTime)) {
      NS_WARNING("Int overflow converting sampleTime");
      break;
    }
    PRInt64 missingSamples = 0;
    if (!AddOverflow(sampleTime, -playedSamples, missingSamples)) {
      NS_WARNING("Int overflow adding missingSamples");
      break;
    }

    if (missingSamples > 0) {
      // The next sound chunk begins some time after the end of the last chunk
      // we pushed to the sound hardware. We must push silence into the audio
      // hardware so that the next sound chunk begins playback at the correct
      // time.
      missingSamples = NS_MIN(static_cast<PRInt64>(PR_UINT32_MAX), missingSamples);
      audioDuration += PlaySilence(static_cast<PRUint32>(missingSamples),
                                   channels, playedSamples);
    } else {
      audioDuration += PlayFromAudioQueue(sampleTime, channels);
    }
    {
      MonitorAutoEnter mon(mDecoder->GetMonitor());
      PRInt64 playedMs;
      if (!SamplesToMs(audioDuration, rate, playedMs)) {
        NS_WARNING("Int overflow calculating playedMs");
        break;
      }
      if (!AddOverflow(audioStartTime, playedMs, mAudioEndTime)) {
        NS_WARNING("Int overflow calculating audio end time");
        break;
      }

      PRInt64 audioAhead = mAudioEndTime - GetMediaTime();
      if (audioAhead > AMPLE_AUDIO_MS &&
          audioDuration - samplesAtLastSleep > minWriteSamples)
      {
        samplesAtLastSleep = audioDuration;
        // We've pushed enough audio onto the hardware that we've queued up a
        // significant amount ahead of the playback position. The decode
        // thread will be going to sleep, so we won't get any new samples
        // anyway, so sleep until we need to push to the hardware again.
        Wait(AMPLE_AUDIO_MS / 2);
        // Kick the decode thread; since above we only do a NotifyAll when
        // we pop an audio chunk of the queue, the decoder won't wake up if
        // we've got no more decoded chunks to push to the hardware. We can
        // hit this condition if the last sample in the stream doesn't have
        // it's EOS flag set, and the decode thread sleeps just after decoding
        // that packet, but before realising there's no more packets.
        mon.NotifyAll();
      }
    }
  }
  if (mReader->mAudioQueue.AtEndOfStream() &&
      mState != DECODER_STATE_SHUTDOWN &&
      !mStopDecodeThreads)
  {
    // Last sample pushed to audio hardware, wait for the audio to finish,
    // before the audio thread terminates.
    MonitorAutoEnter audioMon(mAudioMonitor);
    if (mAudioStream) {
      PRBool seeking = PR_FALSE;
      PRInt64 oldPosition = -1;

      {
        MonitorAutoExit audioExit(mAudioMonitor);
        MonitorAutoEnter mon(mDecoder->GetMonitor());
        PRInt64 position = GetMediaTime();
        while (oldPosition != position &&
               mAudioEndTime - position > 0 &&
               mState != DECODER_STATE_SEEKING &&
               mState != DECODER_STATE_SHUTDOWN)
        {
          const PRInt64 DRAIN_BLOCK_MS = 100;
          Wait(NS_MIN(mAudioEndTime - position, DRAIN_BLOCK_MS));
          oldPosition = position;
          position = GetMediaTime();
        }
        if (mState == DECODER_STATE_SEEKING) {
          seeking = PR_TRUE;
        }
      }

      if (!seeking && mAudioStream && !mAudioStream->IsPaused()) {
        mAudioStream->Drain();

        // Fire one last event for any extra samples that didn't fill a framebuffer.
        mEventManager.Drain(mAudioEndTime);
      }
    }
    LOG(PR_LOG_DEBUG, ("%p Reached audio stream end.", mDecoder));
  }
  {
    MonitorAutoEnter mon(mDecoder->GetMonitor());
    mAudioCompleted = PR_TRUE;
    UpdateReadyState();
    // Kick the decode and state machine threads; they may be sleeping waiting
    // for this to finish.
    mDecoder->GetMonitor().NotifyAll();
  }
  LOG(PR_LOG_DEBUG, ("Audio stream finished playing, audio thread exit"));
}

PRUint32 nsBuiltinDecoderStateMachine::PlaySilence(PRUint32 aSamples,
                                                   PRUint32 aChannels,
                                                   PRUint64 aSampleOffset)

{
  MonitorAutoEnter audioMon(mAudioMonitor);
  if (!mAudioStream || mAudioStream->IsPaused()) {
    // The state machine has paused since we've released the decoder
    // monitor and acquired the audio monitor. Don't write any audio.
    return 0;
  }
  PRUint32 maxSamples = SILENCE_BYTES_CHUNK / aChannels;
  PRUint32 samples = NS_MIN(aSamples, maxSamples);
  PRUint32 numValues = samples * aChannels;
  nsAutoArrayPtr<SoundDataValue> buf(new SoundDataValue[numValues]);
  memset(buf.get(), 0, sizeof(SoundDataValue) * numValues);
  mAudioStream->Write(buf, numValues, PR_TRUE);
  // Dispatch events to the DOM for the audio just written.
  mEventManager.QueueWrittenAudioData(buf.get(), numValues,
                                      (aSampleOffset + samples) * aChannels);
  return samples;
}

PRUint32 nsBuiltinDecoderStateMachine::PlayFromAudioQueue(PRUint64 aSampleOffset,
                                                          PRUint32 aChannels)
{
  nsAutoPtr<SoundData> sound(mReader->mAudioQueue.PopFront());
  {
    MonitorAutoEnter mon(mDecoder->GetMonitor());
    NS_WARN_IF_FALSE(IsPlaying(), "Should be playing");
    // Awaken the decode loop if it's waiting for space to free up in the
    // audio queue.
    mDecoder->GetMonitor().NotifyAll();
  }
  PRInt64 offset = -1;
  PRUint32 samples = 0;
  {
    MonitorAutoEnter audioMon(mAudioMonitor);
    if (!mAudioStream) {
      return 0;
    }
    // The state machine could have paused since we've released the decoder
    // monitor and acquired the audio monitor. Rather than acquire both
    // monitors, the audio stream also maintains whether its paused or not.
    // This prevents us from doing a blocking write while holding the audio
    // monitor while paused; we would block, and the state machine won't be
    // able to acquire the audio monitor in order to resume or destroy the
    // audio stream.
    if (!mAudioStream->IsPaused()) {
      mAudioStream->Write(sound->mAudioData,
                          sound->AudioDataLength(),
                          PR_TRUE);

      offset = sound->mOffset;
      samples = sound->mSamples;

      // Dispatch events to the DOM for the audio just written.
      mEventManager.QueueWrittenAudioData(sound->mAudioData.get(),
                                          sound->AudioDataLength(),
                                          (aSampleOffset + samples) * aChannels);
    } else {
      mReader->mAudioQueue.PushFront(sound);
      sound.forget();
    }
  }
  if (offset != -1) {
    mDecoder->UpdatePlaybackOffset(offset);
  }
  return samples;
}

nsresult nsBuiltinDecoderStateMachine::Init(nsDecoderStateMachine* aCloneDonor)
{
  nsBuiltinDecoderReader* cloneReader = nsnull;
  if (aCloneDonor) {
    cloneReader = static_cast<nsBuiltinDecoderStateMachine*>(aCloneDonor)->mReader;
  }
  return mReader->Init(cloneReader);
}

void nsBuiltinDecoderStateMachine::StopPlayback(eStopMode aMode)
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread),
               "Should be on state machine thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  mDecoder->mPlaybackStatistics.Stop(TimeStamp::Now());

  // Reset mPlayStartTime before we pause/shutdown the nsAudioStream. This is
  // so that if the audio loop is about to write audio, it will have the chance
  // to check to see if we're paused and not write the audio. If not, the
  // audio thread can block in the write, and we deadlock trying to acquire
  // the audio monitor upon resume playback.
  if (IsPlaying()) {
    mPlayDuration += TimeStamp::Now() - mPlayStartTime;
    mPlayStartTime = TimeStamp();
  }
  if (HasAudio()) {
    MonitorAutoExit exitMon(mDecoder->GetMonitor());
    MonitorAutoEnter audioMon(mAudioMonitor);
    if (mAudioStream) {
      if (aMode == AUDIO_PAUSE) {
        mAudioStream->Pause();
      } else if (aMode == AUDIO_SHUTDOWN) {
        mAudioStream->Shutdown();
        mAudioStream = nsnull;
        mEventManager.Clear();
      }
    }
  }
}

void nsBuiltinDecoderStateMachine::StartPlayback()
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread),
               "Should be on state machine thread.");
  NS_ASSERTION(!IsPlaying(), "Shouldn't be playing when StartPlayback() is called");
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  LOG(PR_LOG_DEBUG, ("%p StartPlayback", mDecoder));
  mDecoder->mPlaybackStatistics.Start(TimeStamp::Now());
  if (HasAudio()) {
    PRInt32 rate = mInfo.mAudioRate;
    PRInt32 channels = mInfo.mAudioChannels;

    {
      MonitorAutoExit exitMon(mDecoder->GetMonitor());
      MonitorAutoEnter audioMon(mAudioMonitor);
      if (mAudioStream) {
        // We have an audiostream, so it must have been paused the last time
        // StopPlayback() was called.
        mAudioStream->Resume();
      } else {
        // No audiostream, create one.
        mAudioStream = nsAudioStream::AllocateStream();
        mAudioStream->Init(channels, rate, MOZ_SOUND_DATA_FORMAT);
        mAudioStream->SetVolume(mVolume);
      }
    }
  }
  mPlayStartTime = TimeStamp::Now();
  mDecoder->GetMonitor().NotifyAll();
}

void nsBuiltinDecoderStateMachine::UpdatePlaybackPositionInternal(PRInt64 aTime)
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread),
               "Should be on state machine thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  NS_ASSERTION(mStartTime >= 0, "Should have positive mStartTime");
  mCurrentFrameTime = aTime - mStartTime;
  NS_ASSERTION(mCurrentFrameTime >= 0, "CurrentTime should be positive!");
  if (aTime > mEndTime) {
    NS_ASSERTION(mCurrentFrameTime > GetDuration(),
                 "CurrentTime must be after duration if aTime > endTime!");
    mEndTime = aTime;
    nsCOMPtr<nsIRunnable> event =
      NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::DurationChanged);
    NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
  }
}

void nsBuiltinDecoderStateMachine::UpdatePlaybackPosition(PRInt64 aTime)
{
  UpdatePlaybackPositionInternal(aTime);

  if (!mPositionChangeQueued) {
    mPositionChangeQueued = PR_TRUE;
    nsCOMPtr<nsIRunnable> event =
      NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::PlaybackPositionChanged);
    NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
  }

  // Notify DOM of any queued up audioavailable events
  mEventManager.DispatchPendingEvents(GetMediaTime());
}

void nsBuiltinDecoderStateMachine::ClearPositionChangeFlag()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  mPositionChangeQueued = PR_FALSE;
}

nsHTMLMediaElement::NextFrameStatus nsBuiltinDecoderStateMachine::GetNextFrameStatus()
{
  MonitorAutoEnter mon(mDecoder->GetMonitor());
  if (IsBuffering() || IsSeeking()) {
    return nsHTMLMediaElement::NEXT_FRAME_UNAVAILABLE_BUFFERING;
  } else if (HaveNextFrameData()) {
    return nsHTMLMediaElement::NEXT_FRAME_AVAILABLE;
  }
  return nsHTMLMediaElement::NEXT_FRAME_UNAVAILABLE;
}

void nsBuiltinDecoderStateMachine::SetVolume(double volume)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  MonitorAutoEnter mon(mDecoder->GetMonitor());
  mVolume = volume;
}

double nsBuiltinDecoderStateMachine::GetCurrentTime() const
{
  NS_ASSERTION(NS_IsMainThread() ||
               mDecoder->OnStateMachineThread() ||
               OnDecodeThread(),
               "Should be on main, decode, or state machine thread.");

  return static_cast<double>(mCurrentFrameTime) / 1000.0;
}

PRInt64 nsBuiltinDecoderStateMachine::GetDuration()
{
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  if (mEndTime == -1 || mStartTime == -1)
    return -1;
  return mEndTime - mStartTime;
}

void nsBuiltinDecoderStateMachine::SetDuration(PRInt64 aDuration)
{
  NS_ASSERTION(NS_IsMainThread() || mDecoder->OnStateMachineThread(),
    "Should be on main or state machine thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  if (mStartTime != -1) {
    mEndTime = mStartTime + aDuration;
  } else {
    mStartTime = 0;
    mEndTime = aDuration;
  }
}

void nsBuiltinDecoderStateMachine::SetSeekable(PRBool aSeekable)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  mSeekable = aSeekable;
}

void nsBuiltinDecoderStateMachine::Shutdown()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  // Once we've entered the shutdown state here there's no going back.
  MonitorAutoEnter mon(mDecoder->GetMonitor());

  // Change state before issuing shutdown request to threads so those
  // threads can start exiting cleanly during the Shutdown call.
  LOG(PR_LOG_DEBUG, ("%p Changed state to SHUTDOWN", mDecoder));
  mState = DECODER_STATE_SHUTDOWN;
  mDecoder->GetMonitor().NotifyAll();
}

void nsBuiltinDecoderStateMachine::StartDecoding()
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread),
               "Should be on state machine thread.");
  MonitorAutoEnter mon(mDecoder->GetMonitor());
  if (mState != DECODER_STATE_DECODING) {
    mDecodeStartTime = TimeStamp::Now();
  }
  mState = DECODER_STATE_DECODING;
}

void nsBuiltinDecoderStateMachine::Play()
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  // When asked to play, switch to decoding state only if
  // we are currently buffering. In other cases, we'll start playing anyway
  // when the state machine notices the decoder's state change to PLAYING.
  MonitorAutoEnter mon(mDecoder->GetMonitor());
  if (mState == DECODER_STATE_BUFFERING) {
    LOG(PR_LOG_DEBUG, ("%p Changed state from BUFFERING to DECODING", mDecoder));
    mState = DECODER_STATE_DECODING;
    mDecodeStartTime = TimeStamp::Now();
    mDecoder->GetMonitor().NotifyAll();
  }
}

void nsBuiltinDecoderStateMachine::ResetPlayback()
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread),
               "Should be on state machine thread.");
  mVideoFrameEndTime = -1;
  mAudioStartTime = -1;
  mAudioEndTime = -1;
  mAudioCompleted = PR_FALSE;
}

void nsBuiltinDecoderStateMachine::Seek(double aTime)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  MonitorAutoEnter mon(mDecoder->GetMonitor());
  // nsBuiltinDecoder::mPlayState should be SEEKING while we seek, and
  // in that case nsBuiltinDecoder shouldn't be calling us.
  NS_ASSERTION(mState != DECODER_STATE_SEEKING,
               "We shouldn't already be seeking");
  NS_ASSERTION(mState >= DECODER_STATE_DECODING,
               "We should have loaded metadata");
  double t = aTime * 1000.0;
  if (t > PR_INT64_MAX) {
    // Prevent integer overflow.
    return;
  }

  mSeekTime = static_cast<PRInt64>(t) + mStartTime;
  NS_ASSERTION(mSeekTime >= mStartTime && mSeekTime <= mEndTime,
               "Can only seek in range [0,duration]");

  // Bound the seek time to be inside the media range.
  NS_ASSERTION(mStartTime != -1, "Should know start time by now");
  NS_ASSERTION(mEndTime != -1, "Should know end time by now");
  mSeekTime = NS_MIN(mSeekTime, mEndTime);
  mSeekTime = NS_MAX(mStartTime, mSeekTime);
  LOG(PR_LOG_DEBUG, ("%p Changed state to SEEKING (to %f)", mDecoder, aTime));
  mState = DECODER_STATE_SEEKING;
}

void nsBuiltinDecoderStateMachine::StopDecodeThreads()
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread),
               "Should be on state machine thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  mStopDecodeThreads = PR_TRUE;
  mDecoder->GetMonitor().NotifyAll();
  if (mDecodeThread) {
    {
      MonitorAutoExit exitMon(mDecoder->GetMonitor());
      mDecodeThread->Shutdown();
    }
    mDecodeThread = nsnull;
  }
  if (mAudioThread) {
    {
      MonitorAutoExit exitMon(mDecoder->GetMonitor());
      mAudioThread->Shutdown();
    }
    mAudioThread = nsnull;
  }
}

nsresult
nsBuiltinDecoderStateMachine::StartDecodeThreads()
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread),
               "Should be on state machine thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  mStopDecodeThreads = PR_FALSE;
  if (!mDecodeThread && mState < DECODER_STATE_COMPLETED) {
    nsresult rv = NS_NewThread(getter_AddRefs(mDecodeThread));
    if (NS_FAILED(rv)) {
      mState = DECODER_STATE_SHUTDOWN;
      return rv;
    }
    nsCOMPtr<nsIRunnable> event =
      NS_NewRunnableMethod(this, &nsBuiltinDecoderStateMachine::DecodeLoop);
    mDecodeThread->Dispatch(event, NS_DISPATCH_NORMAL);
  }
  if (HasAudio() && !mAudioThread) {
    nsresult rv = NS_NewThread(getter_AddRefs(mAudioThread));
    if (NS_FAILED(rv)) {
      mState = DECODER_STATE_SHUTDOWN;
      return rv;
    }
    nsCOMPtr<nsIRunnable> event =
      NS_NewRunnableMethod(this, &nsBuiltinDecoderStateMachine::AudioLoop);
    mAudioThread->Dispatch(event, NS_DISPATCH_NORMAL);
  }
  return NS_OK;
}

PRInt64 nsBuiltinDecoderStateMachine::AudioDecodedMs() const
{
  NS_ASSERTION(HasAudio(),
               "Should only call AudioDecodedMs() when we have audio");
  // The amount of audio we have decoded is the amount of audio data we've
  // already decoded and pushed to the hardware, plus the amount of audio
  // data waiting to be pushed to the hardware.
  PRInt64 pushed = (mAudioEndTime != -1) ? (mAudioEndTime - GetMediaTime()) : 0;
  return pushed + mReader->mAudioQueue.Duration();
}

PRBool nsBuiltinDecoderStateMachine::HasLowDecodedData(PRInt64 aAudioMs) const
{
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  // We consider ourselves low on decoded data if we're low on audio,
  // provided we've not decoded to the end of the audio stream, or
  // if we're only playing video and we're low on video frames, provided
  // we've not decoded to the end of the video stream.
  return ((HasAudio() &&
           !mReader->mAudioQueue.IsFinished() &&
           AudioDecodedMs() < aAudioMs)
          ||
         (!HasAudio() &&
          HasVideo() &&
          !mReader->mVideoQueue.IsFinished() &&
          static_cast<PRUint32>(mReader->mVideoQueue.GetSize()) < LOW_VIDEO_FRAMES));
}

PRBool nsBuiltinDecoderStateMachine::HasLowUndecodedData() const
{
  return GetUndecodedData() < LOW_DATA_THRESHOLD_MS;
}

PRInt64 nsBuiltinDecoderStateMachine::GetUndecodedData() const
{
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  NS_ASSERTION(mState > DECODER_STATE_DECODING_METADATA,
               "Must have loaded metadata for GetBuffered() to work");
  nsTimeRanges buffered;
  
  nsresult res = mDecoder->GetBuffered(&buffered);
  NS_ENSURE_SUCCESS(res, 0);
  double currentTime = GetCurrentTime();

  nsIDOMTimeRanges* r = static_cast<nsIDOMTimeRanges*>(&buffered);
  PRUint32 length = 0;
  res = r->GetLength(&length);
  NS_ENSURE_SUCCESS(res, 0);

  for (PRUint32 index = 0; index < length; ++index) {
    double start, end;
    res = r->Start(index, &start);
    NS_ENSURE_SUCCESS(res, 0);

    res = r->End(index, &end);
    NS_ENSURE_SUCCESS(res, 0);

    if (start <= currentTime && end >= currentTime) {
      return static_cast<PRInt64>((end - currentTime) * 1000);
    }
  }
  return 0;
}

void nsBuiltinDecoderStateMachine::SetFrameBufferLength(PRUint32 aLength)
{
  NS_ASSERTION(aLength >= 512 && aLength <= 16384,
               "The length must be between 512 and 16384");
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  mEventManager.SetSignalBufferLength(aLength);
}

nsresult nsBuiltinDecoderStateMachine::Run()
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread),
               "Should be on state machine thread.");
  nsMediaStream* stream = mDecoder->GetCurrentStream();
  NS_ENSURE_TRUE(stream, NS_ERROR_NULL_POINTER);

  while (PR_TRUE) {
    MonitorAutoEnter mon(mDecoder->GetMonitor());
    switch (mState) {
    case DECODER_STATE_SHUTDOWN:
      if (IsPlaying()) {
        StopPlayback(AUDIO_SHUTDOWN);
      }
      StopDecodeThreads();
      NS_ASSERTION(mState == DECODER_STATE_SHUTDOWN,
                   "How did we escape from the shutdown state???");
      return NS_OK;

    case DECODER_STATE_DECODING_METADATA:
      {
        LoadMetadata();
        if (mState == DECODER_STATE_SHUTDOWN) {
          continue;
        }

        VideoData* videoData = FindStartTime();
        if (videoData) {
          nsIntSize display = mInfo.mDisplay;
          float aspect = mInfo.mPixelAspectRatio;
          {
            MonitorAutoExit exitMon(mDecoder->GetMonitor());
            RenderVideoFrame(videoData, TimeStamp::Now(), display, aspect);
          }
        }

        // Start the decode threads, so that we can pre buffer the streams.
        // and calculate the start time in order to determine the duration.
        if (NS_FAILED(StartDecodeThreads())) {
          continue;
        }

        NS_ASSERTION(mStartTime != -1, "Must have start time");
        NS_ASSERTION((!HasVideo() && !HasAudio()) ||
                     !mSeekable || mEndTime != -1,
                     "Active seekable media should have end time");
        NS_ASSERTION(!mSeekable || GetDuration() != -1, "Seekable media should have duration");
        LOG(PR_LOG_DEBUG, ("%p Media goes from %lldms to %lldms (duration %lldms) seekable=%d",
                           mDecoder, mStartTime, mEndTime, GetDuration(), mSeekable));

        if (mState == DECODER_STATE_SHUTDOWN)
          continue;

        // Inform the element that we've loaded the metadata and the first frame,
        // setting the default framebuffer size for audioavailable events.  Also,
        // if there is audio, let the MozAudioAvailable event manager know about
        // the metadata.
        if (HasAudio()) {
          mEventManager.Init(mInfo.mAudioChannels, mInfo.mAudioRate);
          // Set the buffer length at the decoder level to be able, to be able
          // to retrive the value via media element method. The RequestFrameBufferLength
          // will call the nsBuiltinDecoderStateMachine::SetFrameBufferLength().
          PRUint32 frameBufferLength = mInfo.mAudioChannels * FRAMEBUFFER_LENGTH_PER_CHANNEL;
          mDecoder->RequestFrameBufferLength(frameBufferLength);
        }
        nsCOMPtr<nsIRunnable> metadataLoadedEvent =
          new nsAudioMetadataEventRunner(mDecoder, mInfo.mAudioChannels, mInfo.mAudioRate);
        NS_DispatchToMainThread(metadataLoadedEvent, NS_DISPATCH_NORMAL);

        if (mState == DECODER_STATE_DECODING_METADATA) {
          LOG(PR_LOG_DEBUG, ("%p Changed state from DECODING_METADATA to DECODING", mDecoder));
          StartDecoding();
        }

        // Start playback.
        if (mDecoder->GetState() == nsBuiltinDecoder::PLAY_STATE_PLAYING) {
          if (!IsPlaying()) {
            StartPlayback();
          }
        }
      }
      break;

    case DECODER_STATE_DECODING:
      {
        if (NS_FAILED(StartDecodeThreads())) {
          continue;
        }

        AdvanceFrame();

        if (mState != DECODER_STATE_DECODING)
          continue;
      }
      break;

    case DECODER_STATE_SEEKING:
      {
        // During the seek, don't have a lock on the decoder state,
        // otherwise long seek operations can block the main thread.
        // The events dispatched to the main thread are SYNC calls.
        // These calls are made outside of the decode monitor lock so
        // it is safe for the main thread to makes calls that acquire
        // the lock since it won't deadlock. We check the state when
        // acquiring the lock again in case shutdown has occurred
        // during the time when we didn't have the lock.
        PRInt64 seekTime = mSeekTime;
        mDecoder->StopProgressUpdates();

        PRBool currentTimeChanged = false;
        PRInt64 mediaTime = GetMediaTime();
        if (mediaTime != seekTime) {
          currentTimeChanged = true;
          UpdatePlaybackPositionInternal(seekTime);
        }

        // SeekingStarted will do a UpdateReadyStateForData which will
        // inform the element and its users that we have no frames
        // to display
        {
          MonitorAutoExit exitMon(mDecoder->GetMonitor());
          nsCOMPtr<nsIRunnable> startEvent =
            NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::SeekingStarted);
          NS_DispatchToMainThread(startEvent, NS_DISPATCH_SYNC);
        }

        if (currentTimeChanged) {
          // The seek target is different than the current playback position,
          // we'll need to seek the playback position, so shutdown our decode
          // and audio threads.
          StopPlayback(AUDIO_SHUTDOWN);
          StopDecodeThreads();
          ResetPlayback();
          nsresult res;
          {
            MonitorAutoExit exitMon(mDecoder->GetMonitor());
            // Now perform the seek. We must not hold the state machine monitor
            // while we seek, since the seek decodes.
            res = mReader->Seek(seekTime,
                                mStartTime,
                                mEndTime,
                                mediaTime);
          }
          if (NS_SUCCEEDED(res)){
            SoundData* audio = HasAudio() ? mReader->mAudioQueue.PeekFront() : nsnull;
            NS_ASSERTION(!audio || (audio->mTime <= seekTime &&
                                    seekTime <= audio->mTime + audio->mDuration),
                         "Seek target should lie inside the first audio block after seek");
            PRInt64 startTime = (audio && audio->mTime < seekTime) ? audio->mTime : seekTime;
            mAudioStartTime = startTime;
            mPlayDuration = MsToDuration(startTime - mStartTime);
            if (HasVideo()) {
              nsAutoPtr<VideoData> video(mReader->mVideoQueue.PeekFront());
              if (video) {
                NS_ASSERTION(video->mTime <= seekTime && seekTime <= video->mEndTime,
                             "Seek target should lie inside the first frame after seek");
                nsIntSize display = mInfo.mDisplay;
                float aspect = mInfo.mPixelAspectRatio;
                {
                  MonitorAutoExit exitMon(mDecoder->GetMonitor());
                  RenderVideoFrame(video, TimeStamp::Now(), display, aspect);
                }
                mReader->mVideoQueue.PopFront();
                nsCOMPtr<nsIRunnable> event =
                  NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::Invalidate);
                NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
              }
            }
          }
        }
        mDecoder->StartProgressUpdates();
        if (mState == DECODER_STATE_SHUTDOWN)
          continue;

        // Try to decode another frame to detect if we're at the end...
        LOG(PR_LOG_DEBUG, ("Seek completed, mCurrentFrameTime=%lld\n", mCurrentFrameTime));

        // Change state to DECODING or COMPLETED now. SeekingStopped will
        // call nsBuiltinDecoderStateMachine::Seek to reset our state to SEEKING
        // if we need to seek again.
        
        nsCOMPtr<nsIRunnable> stopEvent;
        if (GetMediaTime() == mEndTime) {
          LOG(PR_LOG_DEBUG, ("%p Changed state from SEEKING (to %lldms) to COMPLETED",
                             mDecoder, seekTime));
          stopEvent = NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::SeekingStoppedAtEnd);
          mState = DECODER_STATE_COMPLETED;
        } else {
          LOG(PR_LOG_DEBUG, ("%p Changed state from SEEKING (to %lldms) to DECODING",
                             mDecoder, seekTime));
          stopEvent = NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::SeekingStopped);
          StartDecoding();
        }
        mDecoder->GetMonitor().NotifyAll();

        {
          MonitorAutoExit exitMon(mDecoder->GetMonitor());
          NS_DispatchToMainThread(stopEvent, NS_DISPATCH_SYNC);
        }

        // Reset quick buffering status. This ensures that if we began the
        // seek while quick-buffering, we won't bypass quick buffering mode
        // if we need to buffer after the seek.
        mQuickBuffering = PR_FALSE;
      }
      break;

    case DECODER_STATE_BUFFERING:
      {
        if (IsPlaying()) {
          StopPlayback(AUDIO_PAUSE);
          mDecoder->GetMonitor().NotifyAll();
        }

        TimeStamp now = TimeStamp::Now();
        NS_ASSERTION(!mBufferingStart.IsNull(), "Must know buffering start time.");

        // We will remain in the buffering state if we've not decoded enough
        // data to begin playback, or if we've not downloaded a reasonable
        // amount of data inside our buffering time.
        TimeDuration elapsed = now - mBufferingStart;
        PRBool isLiveStream = mDecoder->GetCurrentStream()->GetLength() == -1;
        if ((isLiveStream || !mDecoder->CanPlayThrough()) &&
             elapsed < TimeDuration::FromSeconds(BUFFERING_WAIT) &&
             (mQuickBuffering ? HasLowDecodedData(QUICK_BUFFERING_LOW_DATA_MS)
                              : (GetUndecodedData() < BUFFERING_WAIT * 1000)) &&
             !stream->IsDataCachedToEndOfStream(mDecoder->mDecoderPosition) &&
             !stream->IsSuspended())
        {
          LOG(PR_LOG_DEBUG,
              ("Buffering: %.3lfs/%ds, timeout in %.3lfs %s",
               GetUndecodedData() / 1000.0,
               BUFFERING_WAIT,
               BUFFERING_WAIT - elapsed.ToSeconds(),
               (mQuickBuffering ? "(quick exit)" : "")));
          Wait(1000);
          if (mState == DECODER_STATE_SHUTDOWN)
            continue;
        } else {
          LOG(PR_LOG_DEBUG, ("%p Changed state from BUFFERING to DECODING", mDecoder));
          LOG(PR_LOG_DEBUG, ("%p Buffered for %.3lfs",
                             mDecoder,
                             (now - mBufferingStart).ToSeconds()));
          StartDecoding();
        }

        if (mState != DECODER_STATE_BUFFERING) {
          // Notify to allow blocked decoder thread to continue
          mDecoder->GetMonitor().NotifyAll();
          UpdateReadyState();
          if (mDecoder->GetState() == nsBuiltinDecoder::PLAY_STATE_PLAYING) {
            if (!IsPlaying()) {
              StartPlayback();
            }
          }
        }
        break;
      }

    case DECODER_STATE_COMPLETED:
      {
        if (NS_FAILED(StartDecodeThreads())) {
          continue;
        }

        // Play the remaining media. We want to run AdvanceFrame() at least
        // once to ensure the current playback position is advanced to the
        // end of the media, and so that we update the readyState.
        do {
          AdvanceFrame();
        } while (mState == DECODER_STATE_COMPLETED &&
                 (mReader->mVideoQueue.GetSize() > 0 ||
                 (HasAudio() && !mAudioCompleted)));

        if (mAudioStream) {
          // Close the audio stream so that next time audio is used a new stream
          // is created. The StopPlayback call also resets the IsPlaying() state
          // so audio is restarted correctly.
          StopPlayback(AUDIO_SHUTDOWN);
        }

        if (mState != DECODER_STATE_COMPLETED)
          continue;

        LOG(PR_LOG_DEBUG, ("Shutting down the state machine thread"));
        StopDecodeThreads();

        if (mDecoder->GetState() == nsBuiltinDecoder::PLAY_STATE_PLAYING) {
          PRInt64 videoTime = HasVideo() ? mVideoFrameEndTime : 0;
          PRInt64 clockTime = NS_MAX(mEndTime, NS_MAX(videoTime, GetAudioClock()));
          UpdatePlaybackPosition(clockTime);
          {
            MonitorAutoExit exitMon(mDecoder->GetMonitor());
            nsCOMPtr<nsIRunnable> event =
              NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::PlaybackEnded);
            NS_DispatchToMainThread(event, NS_DISPATCH_SYNC);
          }
        }

        if (mState == DECODER_STATE_COMPLETED) {
          // We've finished playback. Shutdown the state machine thread, 
          // in order to save memory on thread stacks, particuarly on Linux.
          nsCOMPtr<nsIRunnable> event =
            new ShutdownThreadEvent(mDecoder->mStateMachineThread);
          NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
          mDecoder->mStateMachineThread = nsnull;
          return NS_OK;
        }
      }
      break;
    }
  }

  return NS_OK;
}

void nsBuiltinDecoderStateMachine::RenderVideoFrame(VideoData* aData,
                                                    TimeStamp aTarget,
                                                    nsIntSize aDisplaySize,
                                                    float aAspectRatio)
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread), "Should be on state machine thread.");
  mDecoder->GetMonitor().AssertNotCurrentThreadIn();

  if (aData->mDuplicate) {
    return;
  }

  nsRefPtr<Image> image = aData->mImage;
  if (image) {
    mDecoder->SetVideoData(gfxIntSize(aDisplaySize.width, aDisplaySize.height),
                           aAspectRatio, image, aTarget);
  }
}

PRInt64
nsBuiltinDecoderStateMachine::GetAudioClock()
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread), "Should be on state machine thread.");
  if (!mAudioStream || !HasAudio())
    return -1;
  PRInt64 t = mAudioStream->GetPosition();
  return (t == -1) ? -1 : t + mAudioStartTime;
}

void nsBuiltinDecoderStateMachine::AdvanceFrame()
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread), "Should be on state machine thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  // When it's time to display a frame, decode the frame and display it.
  if (mDecoder->GetState() == nsBuiltinDecoder::PLAY_STATE_PLAYING) {
    if (HasAudio() && mAudioStartTime == -1 && !mAudioCompleted) {
      // We've got audio (so we should sync off the audio clock), but we've not
      // played a sample on the audio thread, so we can't get a time from the
      // audio clock. Just wait and then return, to give the audio clock time
      // to tick.  This should really wait for a specific signal from the audio
      // thread rather than polling after a sleep.  See bug 568431 comment 4.
      Wait(AUDIO_DURATION_MS);
      return;
    }

    // Determine the clock time. If we've got audio, and we've not reached
    // the end of the audio, use the audio clock. However if we've finished
    // audio, or don't have audio, use the system clock.
    PRInt64 clock_time = -1;
    if (!IsPlaying()) {
      clock_time = DurationToMs(mPlayDuration) + mStartTime;
    } else {
      PRInt64 audio_time = GetAudioClock();
      if (HasAudio() && !mAudioCompleted && audio_time != -1) {
        clock_time = audio_time;
        // Resync against the audio clock, while we're trusting the
        // audio clock. This ensures no "drift", particularly on Linux.
        mPlayDuration = MsToDuration(clock_time - mStartTime);
        mPlayStartTime = TimeStamp::Now();
      } else {
        // Sound is disabled on this system. Sync to the system clock.
        clock_time = DurationToMs(TimeStamp::Now() - mPlayStartTime + mPlayDuration);
        // Ensure the clock can never go backwards.
        NS_ASSERTION(mCurrentFrameTime <= clock_time, "Clock should go forwards");
        clock_time = NS_MAX(mCurrentFrameTime, clock_time) + mStartTime;
      }
    }

    // Skip frames up to the frame at the playback position, and figure out
    // the time remaining until it's time to display the next frame.
    PRInt64 remainingTime = AUDIO_DURATION_MS;
    NS_ASSERTION(clock_time >= mStartTime, "Should have positive clock time.");
    nsAutoPtr<VideoData> currentFrame;
    if (mReader->mVideoQueue.GetSize() > 0) {
      VideoData* frame = mReader->mVideoQueue.PeekFront();
      while (clock_time >= frame->mTime) {
        mVideoFrameEndTime = frame->mEndTime;
        currentFrame = frame;
        mReader->mVideoQueue.PopFront();
        mDecoder->UpdatePlaybackOffset(frame->mOffset);
        if (mReader->mVideoQueue.GetSize() == 0)
          break;
        frame = mReader->mVideoQueue.PeekFront();
      }
      // Current frame has already been presented, wait until it's time to
      // present the next frame.
      if (frame && !currentFrame) {
        PRInt64 now = IsPlaying()
          ? DurationToMs(TimeStamp::Now() - mPlayStartTime + mPlayDuration)
          : DurationToMs(mPlayDuration);
        remainingTime = frame->mTime - mStartTime - now;
      }
    }

    // Check to see if we don't have enough data to play up to the next frame.
    // If we don't, switch to buffering mode.
    nsMediaStream* stream = mDecoder->GetCurrentStream();
    if (mState == DECODER_STATE_DECODING &&
        mDecoder->GetState() == nsBuiltinDecoder::PLAY_STATE_PLAYING &&
        HasLowDecodedData(remainingTime + EXHAUSTED_DATA_MARGIN_MS) &&
        !stream->IsDataCachedToEndOfStream(mDecoder->mDecoderPosition) &&
        !stream->IsSuspended() &&
        (JustExitedQuickBuffering() || HasLowUndecodedData()))
    {
      if (currentFrame) {
        mReader->mVideoQueue.PushFront(currentFrame.forget());
      }
      StartBuffering();
      return;
    }

    // We've got enough data to keep playing until at least the next frame.
    // Start playing now if need be.
    if (!IsPlaying()) {
      StartPlayback();
      mDecoder->GetMonitor().NotifyAll();
    }

    if (currentFrame) {
      // Decode one frame and display it.
      TimeStamp presTime = mPlayStartTime - mPlayDuration +
                           MsToDuration(currentFrame->mTime - mStartTime);
      NS_ASSERTION(currentFrame->mTime >= mStartTime, "Should have positive frame time");
      {
        nsIntSize display = mInfo.mDisplay;
        float aspect = mInfo.mPixelAspectRatio;
        {
          MonitorAutoExit exitMon(mDecoder->GetMonitor());
          // If we have video, we want to increment the clock in steps of the frame
          // duration.
          RenderVideoFrame(currentFrame, presTime, display, aspect);
        }
      }
      mDecoder->GetFrameStatistics().NotifyPresentedFrame();
      PRInt64 now = DurationToMs(TimeStamp::Now() - mPlayStartTime + mPlayDuration);
      remainingTime = currentFrame->mEndTime - mStartTime - now;
      currentFrame = nsnull;
    }

    // Kick the decode thread in case it filled its buffers and put itself
    // to sleep.
    mDecoder->GetMonitor().NotifyAll();

    // Cap the current time to the larger of the audio and video end time.
    // This ensures that if we're running off the system clock, we don't
    // advance the clock to after the media end time.
    if (mVideoFrameEndTime != -1 || mAudioEndTime != -1) {
      // These will be non -1 if we've displayed a video frame, or played an audio sample.
      clock_time = NS_MIN(clock_time, NS_MAX(mVideoFrameEndTime, mAudioEndTime));
      if (clock_time > GetMediaTime()) {
        // Only update the playback position if the clock time is greater
        // than the previous playback position. The audio clock can
        // sometimes report a time less than its previously reported in
        // some situations, and we need to gracefully handle that.
        UpdatePlaybackPosition(clock_time);
      }
    }

    // If the number of audio/video samples queued has changed, either by
    // this function popping and playing a video sample, or by the audio
    // thread popping and playing an audio sample, we may need to update our
    // ready state. Post an update to do so.
    UpdateReadyState();

    if (remainingTime > 0) {
      Wait(remainingTime);
    }
  } else {
    if (IsPlaying()) {
      StopPlayback(AUDIO_PAUSE);
      mDecoder->GetMonitor().NotifyAll();
    }

    if (mState == DECODER_STATE_DECODING ||
        mState == DECODER_STATE_COMPLETED) {
      mDecoder->GetMonitor().Wait();
    }
  }
}

void nsBuiltinDecoderStateMachine::Wait(PRInt64 aMs) {
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  TimeStamp end = TimeStamp::Now() + MsToDuration(aMs);
  TimeStamp now;
  while ((now = TimeStamp::Now()) < end &&
         mState != DECODER_STATE_SHUTDOWN &&
         mState != DECODER_STATE_SEEKING)
  {
    PRInt64 ms = static_cast<PRInt64>(NS_round((end - now).ToSeconds() * 1000));
    if (ms == 0 || ms > PR_UINT32_MAX) {
      break;
    }
    NS_ASSERTION(ms <= aMs && ms > 0,
                 "nsBuiltinDecoderStateMachine::Wait interval very wrong!");
    mDecoder->GetMonitor().Wait(PR_MillisecondsToInterval(static_cast<PRUint32>(ms)));
  }
}

VideoData* nsBuiltinDecoderStateMachine::FindStartTime()
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread), "Should be on state machine thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();
  PRInt64 startTime = 0;
  mStartTime = 0;
  VideoData* v = nsnull;
  PRInt64 dataOffset = mInfo.mDataOffset;
  {
    MonitorAutoExit exitMon(mDecoder->GetMonitor());
    v = mReader->FindStartTime(dataOffset, startTime);
  }
  if (startTime != 0) {
    mStartTime = startTime;
    if (mGotDurationFromMetaData) {
      NS_ASSERTION(mEndTime != -1,
                   "We should have mEndTime as supplied duration here");
      // We were specified a duration from a Content-Duration HTTP header.
      // Adjust mEndTime so that mEndTime-mStartTime matches the specified
      // duration.
      mEndTime = mStartTime + mEndTime;
    }
  }
  // Set the audio start time to be start of media. If this lies before the
  // first acutal audio sample we have, we'll inject silence during playback
  // to ensure the audio starts at the correct time.
  mAudioStartTime = mStartTime;
  LOG(PR_LOG_DEBUG, ("%p Media start time is %lldms", mDecoder, mStartTime));
  return v;
}

void nsBuiltinDecoderStateMachine::FindEndTime() 
{
  NS_ASSERTION(OnStateMachineThread(), "Should be on state machine thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  nsMediaStream* stream = mDecoder->GetCurrentStream();

  // Seek to the end of file to find the length and duration.
  PRInt64 length = stream->GetLength();
  NS_ASSERTION(length > 0, "Must have a content length to get end time");

  mEndTime = 0;
  PRInt64 endTime = 0;
  {
    MonitorAutoExit exitMon(mDecoder->GetMonitor());
    endTime = mReader->FindEndTime(length);
  }
  if (endTime != -1) {
    mEndTime = endTime;
  }

  LOG(PR_LOG_DEBUG, ("%p Media end time is %lldms", mDecoder, mEndTime));   
}

void nsBuiltinDecoderStateMachine::UpdateReadyState() {
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  nsCOMPtr<nsIRunnable> event;
  switch (GetNextFrameStatus()) {
    case nsHTMLMediaElement::NEXT_FRAME_UNAVAILABLE_BUFFERING:
      event = NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::NextFrameUnavailableBuffering);
      break;
    case nsHTMLMediaElement::NEXT_FRAME_AVAILABLE:
      event = NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::NextFrameAvailable);
      break;
    case nsHTMLMediaElement::NEXT_FRAME_UNAVAILABLE:
      event = NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::NextFrameUnavailable);
      break;
    default:
      PR_NOT_REACHED("unhandled frame state");
  }

  NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
}

void nsBuiltinDecoderStateMachine::LoadMetadata()
{
  NS_ASSERTION(IsCurrentThread(mDecoder->mStateMachineThread),
               "Should be on state machine thread.");
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  LOG(PR_LOG_DEBUG, ("Loading Media Headers"));
  nsresult res;
  nsVideoInfo info;
  {
    MonitorAutoExit exitMon(mDecoder->GetMonitor());
    res = mReader->ReadMetadata(&info);
  }
  mInfo = info;

  if (NS_FAILED(res) || (!info.mHasVideo && !info.mHasAudio)) {
    mState = DECODER_STATE_SHUTDOWN;      
    nsCOMPtr<nsIRunnable> event =
      NS_NewRunnableMethod(mDecoder, &nsBuiltinDecoder::DecodeError);
    NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
    return;
  }
  mDecoder->StartProgressUpdates();
  mGotDurationFromMetaData = (GetDuration() != -1);
}

PRBool nsBuiltinDecoderStateMachine::JustExitedQuickBuffering()
{
  return !mDecodeStartTime.IsNull() &&
    mQuickBuffering &&
    (TimeStamp::Now() - mDecodeStartTime) < TimeDuration::FromSeconds(QUICK_BUFFER_THRESHOLD_MS);
}

void nsBuiltinDecoderStateMachine::StartBuffering()
{
  mDecoder->GetMonitor().AssertCurrentThreadIn();

  TimeDuration decodeDuration = TimeStamp::Now() - mDecodeStartTime;
  // Go into quick buffering mode provided we've not just left buffering using
  // a "quick exit". This stops us flip-flopping between playing and buffering
  // when the download speed is similar to the decode speed.
  mQuickBuffering =
    !JustExitedQuickBuffering() &&
    decodeDuration < TimeDuration::FromMilliseconds(QUICK_BUFFER_THRESHOLD_MS);
  mBufferingStart = TimeStamp::Now();

  // We need to tell the element that buffering has started.
  // We can't just directly send an asynchronous runnable that
  // eventually fires the "waiting" event. The problem is that
  // there might be pending main-thread events, such as "data
  // received" notifications, that mean we're not actually still
  // buffering by the time this runnable executes. So instead
  // we just trigger UpdateReadyStateForData; when it runs, it
  // will check the current state and decide whether to tell
  // the element we're buffering or not.
  UpdateReadyState();
  mState = DECODER_STATE_BUFFERING;
  LOG(PR_LOG_DEBUG, ("Changed state from DECODING to BUFFERING, decoded for %.3lfs",
                     decodeDuration.ToSeconds()));
  nsMediaDecoder::Statistics stats = mDecoder->GetStatistics();
  LOG(PR_LOG_DEBUG, ("Playback rate: %.1lfKB/s%s download rate: %.1lfKB/s%s",
    stats.mPlaybackRate/1024, stats.mPlaybackRateReliable ? "" : " (unreliable)",
    stats.mDownloadRate/1024, stats.mDownloadRateReliable ? "" : " (unreliable)"));
}

nsresult nsBuiltinDecoderStateMachine::GetBuffered(nsTimeRanges* aBuffered) {
  nsMediaStream* stream = mDecoder->GetCurrentStream();
  NS_ENSURE_TRUE(stream, NS_ERROR_FAILURE);
  stream->Pin();
  nsresult res = mReader->GetBuffered(aBuffered, mStartTime);
  stream->Unpin();
  return res;
}

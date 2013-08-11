// In each list of tests below, test file types that are not supported should
// be ignored. To make sure tests respect that, we include a file of type
// "bogus/duh" in each list.

// These are small test files, good for just seeing if something loads. We
// really only need one test file per backend here.
var gSmallTests = [
  { name:"r11025_s16_c1.wav", type:"audio/x-wav", duration:1.0 },
  { name:"320x240.ogv", type:"video/ogg", width:320, height:240 },
  { name:"bogus.duh", type:"bogus/duh" }
];

// Used by test_play_twice.  Need one test file per decoder backend, plus
// anything for testing bugs that occur when replying a played file.
var gReplayTests = gSmallTests.concat([
  { name:"bug533822.ogg", type:"audio/ogg" },
]);

// These are files that we want to make sure we can play through.  We can
// also check metadata.  Put files of the same type together in this list so if
// something crashes we have some idea of which backend is responsible.
// Used by test_playback, which expects no error event and one ended event.
var gPlayTests = [
  // 8-bit samples
  { name:"r11025_u8_c1.wav", type:"audio/x-wav", duration:1.0 },
  // 8-bit samples, file is truncated
  { name:"r11025_u8_c1_trunc.wav", type:"audio/x-wav", duration:1.8 },
  // file has trailing non-PCM data
  { name:"r11025_s16_c1_trailing.wav", type:"audio/x-wav", duration:1.0 },
  // file with list chunk
  { name:"r16000_u8_c1_list.wav", type:"audio/x-wav", duration:4.2 },

  // Ogg stream with eof marker
  { name:"bug461281.ogg", type:"application/ogg" },
  // oggz-chop stream
  { name:"bug482461.ogv", type:"video/ogg", duration:4.24 },
  // With first frame a "duplicate" (empty) frame.
  { name:"bug500311.ogv", type:"video/ogg", duration:1.96 },
  // Small audio file
  { name:"small-shot.ogg", type:"video/ogg" },
  // More audio in file than video.
  { name: "short-video.ogv", type:"video/ogg", duration:1.081 },
  // First Theora data packet is zero bytes.
  { name:"bug504613.ogv", type:"video/ogg" },
  // Multiple audio streams.
  { name:"bug516323.ogv", type:"video/ogg", duration:4.424 },
  // Encoded with vorbis beta1, includes unusually sized codebooks
  { name:"beta-phrasebook.ogg", type:"audio/ogg", duration:4 },
  // Small file, only 1 frame with audio only.
  { name:"bug520493.ogg", type:"audio/ogg", duration:0.458 },
  // Small file with vorbis comments with 0 length values and names.
  { name:"bug520500.ogg", type:"audio/ogg", duration:0.123 },
  { name:"bug523816.ogv", type:"video/ogg", duration:0.5 },

  // Various weirdly formed Ogg files
  { name:"bug499519.ogv", type:"video/ogg", duration:0.24 },
  { name:"bug506094.ogv", type:"video/ogg", duration:0 },
  { name:"bug501279.ogg", type:"audio/ogg", duration:0 },
  { name:"bug498855-1.ogv", type:"video/ogg", duration:0.2 },
  { name:"bug498855-2.ogv", type:"video/ogg", duration:0.2 },
  { name:"bug498855-3.ogv", type:"video/ogg", duration:0.2 },
  { name:"bug504644.ogv", type:"video/ogg", duration:1.56 },
  { name:"chain.ogv", type:"video/ogg", duration:Number.NaN },
  // Actual duration is ~200ms, we have X-Content-Duration lie about it.
  { name:"bug520908.ogv", type:"video/ogg", duration:9000 },

  { name:"bogus.duh", type:"bogus/duh" }
];

// These are files that must fire an error during load or playback, and do not
// cause a crash.  Put files of the same type together in this list so if
// something crashes we have some idea of which backend is responsible.  Used
// by test_playback_errors, which expects one error event and no ended event.
// Put files of the same type together in this list so if something crashes
// we have some idea of which backend is responsible.
var gErrorTests = [
  { name:"bogus.wav", type:"audio/x-wav" },
  { name:"bogus.ogv", type:"video/ogg" },
  { name:"448636.ogv", type:"video/ogg" },
  { name:"bug495129.ogv", type:"video/ogg", duration:2.52 },
  { name:"bug504843.ogv", type:"video/ogg", duration:1.233 },
  { name:"bug498380.ogv", type:"video/ogg" },
  { name:"bogus.duh", type:"bogus/duh" }
];

// These are files that have nontrivial duration and are useful for seeking within.
var gSeekTests = [
  { name:"r11025_s16_c1.wav", type:"audio/x-wav", duration:1.0 },
  { name:"seek.ogv", type:"video/ogg", duration:3.966 },
  { name:"320x240.ogv", type:"video/ogg", duration:0.233 },
  { name:"bogus.duh", type:"bogus/duh", duration:123 }
];

// These are files suitable for using with a "new Audio" constructor.
var gAudioTests = [
  { name:"r11025_s16_c1.wav", type:"audio/x-wav", duration:1.0 },
  { name:"sound.ogg", type:"audio/ogg" },
  { name:"bogus.duh", type:"bogus/duh", duration:123 }
];

// These are files suitable for testing various decoder failures that are
// expected to fire MEDIA_ERR_DECODE.  Used by test_decode_error, which expects
// an error and emptied event, and no loadedmetadata or ended event.
var gDecodeErrorTests = [
  // Valid files with unsupported codecs
  { name:"r11025_msadpcm_c1.wav", type:"audio/x-wav" },
  { name:"dirac.ogg", type:"video/ogg" },
  // Invalid files
  { name:"bogus.wav", type:"audio/x-wav" },
  { name:"bogus.ogv", type:"video/ogg" },

  { name:"bogus.duh", type:"bogus/duh" }
];

function checkMetadata(msg, e, test) {
  if (test.width) {
    is(e.videoWidth, test.width, msg + " video width");
  }
  if (test.height) {
    is(e.videoHeight, test.height, msg + " video height");
  }
  if (test.duration) {
    ok(Math.abs(e.duration - test.duration) < 0.1,
       msg + " duration should be around " + test.duration);
  }
}

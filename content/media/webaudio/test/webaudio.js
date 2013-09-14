// Helpers for Web Audio tests

function expectException(func, exceptionCode) {
  var threw = false;
  try {
    func();
  } catch (ex) {
    threw = true;
    ok(ex instanceof DOMException, "Expect a DOM exception");
    is(ex.code, exceptionCode, "Expect the correct exception code");
  }
  ok(threw, "The exception was thrown");
}

function expectTypeError(func) {
  var threw = false;
  try {
    func();
  } catch (ex) {
    threw = true;
    ok(ex instanceof TypeError, "Expect a TypeError");
  }
  ok(threw, "The exception was thrown");
}

function fuzzyCompare(a, b) {
  return Math.abs(a - b) < 9e-3;
}

function compareBuffers(buf1, buf2,
                        /*optional*/ offset,
                        /*optional*/ length,
                        /*optional*/ sourceOffset,
                        /*optional*/ destOffset,
                        /*optional*/ skipLengthCheck) {
  if (!skipLengthCheck) {
    is(buf1.length, buf2.length, "Buffers must have the same length");
  }
  if (length == undefined) {
    length = buf1.length - (offset || 0);
  }
  sourceOffset = sourceOffset || 0;
  destOffset = destOffset || 0;
  var difference = 0;
  var maxDifference = 0;
  var firstBadIndex = -1;
  for (var i = offset || 0; i < Math.min(buf1.length, (offset || 0) + length); ++i) {
    if (!fuzzyCompare(buf1[i + sourceOffset], buf2[i + destOffset])) {
      difference++;
      maxDifference = Math.max(maxDifference, Math.abs(buf1[i + sourceOffset] - buf2[i + destOffset]));
      if (firstBadIndex == -1) {
        firstBadIndex = i;
      }
    }
  };

  is(difference, 0, "Found " + difference + " different samples, maxDifference: " +
     maxDifference + ", first bad index: " + firstBadIndex +
     " with source offset " + sourceOffset + " and desitnation offset " +
     destOffset);
}

function getEmptyBuffer(context, length) {
  return context.createBuffer(gTest.numberOfChannels, length, context.sampleRate);
}

/**
 * This function assumes that the test file defines a single gTest variable with
 * the following properties and methods:
 *
 * + length: mandatory property equal to the total number of frames which we
 *           are waiting to see in the output.
 * + numberOfChannels: optional property which specifies the number of channels
 *                     in the output.  The default value is 2.
 * + createGraph: mandatory method which takes a context object and does
 *                everything needed in order to set up the Web Audio graph.
 *                This function returns the node to be inspected.
 * + createGraphAsync: async version of createGraph.  This function takes
 *                     a callback which should be called with an argument
 *                     set to the node to be inspected when the callee is
 *                     ready to proceed with the test.  Either this function
 *                     or createGraph must be provided.
 * + createExpectedBuffers: optional method which takes a context object and
 *                          returns either one expected buffer or an array of
 *                          them, designating what is expected to be observed
 *                          in the output.  If omitted, the output is expected
 *                          to be silence.  The sum of the length of the expected
 *                          buffers should be equal to gTest.length.  This
 *                          function is guaranteed to be called before createGraph.
 */
function runTest()
{
  function done() {
    SimpleTest.finish();
  }

  SimpleTest.waitForExplicitFinish();
  addLoadEvent(function() {
    if (!gTest.numberOfChannels) {
      gTest.numberOfChannels = 2; // default
    }

    var testLength;

    function runTestOnContext(context, callback, testOutput) {
      if (!gTest.createExpectedBuffers) {
        // Assume that the output is silence
        var expectedBuffers = getEmptyBuffer(context, gTest.length);
      } else {
        var expectedBuffers = gTest.createExpectedBuffers(context);
      }
      if (!(expectedBuffers instanceof Array)) {
        expectedBuffers = [expectedBuffers];
      }
      var expectedFrames = 0;
      for (var i = 0; i < expectedBuffers.length; ++i) {
        is(expectedBuffers[i].numberOfChannels, gTest.numberOfChannels,
           "Correct number of channels for expected buffer " + i);
        expectedFrames += expectedBuffers[i].length;
      }
      is(expectedFrames, gTest.length, "Correct number of expected frames");

      if (gTest.createGraphAsync) {
        gTest.createGraphAsync(context, function(nodeToInspect) {
          testOutput(nodeToInspect, expectedBuffers, callback);
        });
      } else {
        testOutput(gTest.createGraph(context), expectedBuffers, callback);
      }
    }

    function testOnNormalContext(callback) {
      function testOutput(nodeToInspect, expectedBuffers, callback) {
        testLength = 0;
        var sp = context.createScriptProcessor(expectedBuffers[0].length, gTest.numberOfChannels);
        nodeToInspect.connect(sp);
        sp.connect(context.destination);
        sp.onaudioprocess = function(e) {
          var expectedBuffer = expectedBuffers.shift();
          testLength += expectedBuffer.length;
          is(e.inputBuffer.numberOfChannels, expectedBuffer.numberOfChannels,
             "Correct number of input buffer channels");
          for (var i = 0; i < e.inputBuffer.numberOfChannels; ++i) {
            compareBuffers(e.inputBuffer.getChannelData(i), expectedBuffer.getChannelData(i));
          }
          if (expectedBuffers.length == 0) {
            sp.onaudioprocess = null;
            callback();
          }
        };
      }
      var context = new AudioContext();
      runTestOnContext(context, callback, testOutput);
    }

    function testOnOfflineContext(callback, sampleRate) {
      function testOutput(nodeToInspect, expectedBuffers, callback) {
        nodeToInspect.connect(context.destination);
        context.oncomplete = function(e) {
          var samplesSeen = 0;
          while (expectedBuffers.length) {
            var expectedBuffer = expectedBuffers.shift();
            is(e.renderedBuffer.numberOfChannels, expectedBuffer.numberOfChannels,
               "Correct number of input buffer channels");
            for (var i = 0; i < e.renderedBuffer.numberOfChannels; ++i) {
              compareBuffers(e.renderedBuffer.getChannelData(i),
                             expectedBuffer.getChannelData(i),
                             undefined,
                             expectedBuffer.length,
                             samplesSeen,
                             undefined,
                             true);
            }
            samplesSeen += expectedBuffer.length;
          }
          callback();
        };
        context.startRendering();
      }
      var context = new OfflineAudioContext(gTest.numberOfChannels, testLength, sampleRate);
      runTestOnContext(context, callback, testOutput);
    }

    testOnNormalContext(function() {
      testOnOfflineContext(function() {
        testOnOfflineContext(done, 44100);
      }, 48000);
    });
  });
}

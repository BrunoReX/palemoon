/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 sts=2 et
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Necko Test Code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Shawn Wilsher <me@shawnwilsher.com> (Original Author)
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

/**
 * This file tests the methods on NetUtil.jsm.
 */

do_load_httpd_js();

Components.utils.import("resource://gre/modules/NetUtil.jsm");

////////////////////////////////////////////////////////////////////////////////
//// Helper Methods

/**
 * Reads the contents of a file and returns it as a string.
 *
 * @param aFile
 *        The file to return from.
 * @return the contents of the file in the form of a string.
 */
function getFileContents(aFile)
{
  let fstream = Cc["@mozilla.org/network/file-input-stream;1"].
                createInstance(Ci.nsIFileInputStream);
  fstream.init(aFile, -1, 0, 0);

  let cstream = Cc["@mozilla.org/intl/converter-input-stream;1"].
                createInstance(Ci.nsIConverterInputStream);
  cstream.init(fstream, "UTF-8", 0, 0);

  let string  = {};
  cstream.readString(-1, string);
  cstream.close();
  return string.value;
}

////////////////////////////////////////////////////////////////////////////////
//// Tests

function test_async_write_file()
{
  do_test_pending();

  // First, we need an output file to write to.
  let file = Cc["@mozilla.org/file/directory_service;1"].
             getService(Ci.nsIProperties).
             get("TmpD", Ci.nsIFile);
  file.append("NetUtil-async-test-file.tmp");
  file.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0666);

  // Then, we need an output stream to our output file.
  let ostream = Cc["@mozilla.org/network/file-output-stream;1"].
                createInstance(Ci.nsIFileOutputStream);
  ostream.init(file, -1, -1, 0);

  // Finally, we need an input stream to take data from.
  const TEST_DATA = "this is a test string";
  let istream = Cc["@mozilla.org/io/string-input-stream;1"].
                createInstance(Ci.nsIStringInputStream);
  istream.setData(TEST_DATA, TEST_DATA.length);

  NetUtil.asyncCopy(istream, ostream, function(aResult) {
    // Make sure the copy was successful!
    do_check_true(Components.isSuccessCode(aResult));

    // Check the file contents.
    do_check_eq(TEST_DATA, getFileContents(file));

    // Remove the file, and finish the test.
    file.remove(false);
    do_test_finished();
    run_next_test();
  });
}

function test_async_write_file_nsISafeOutputStream()
{
  do_test_pending();

  // First, we need an output file to write to.
  let file = Cc["@mozilla.org/file/directory_service;1"].
             getService(Ci.nsIProperties).
             get("TmpD", Ci.nsIFile);
  file.append("NetUtil-async-test-file.tmp");
  file.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0666);

  // Then, we need an output stream to our output file.
  let ostream = Cc["@mozilla.org/network/safe-file-output-stream;1"].
                createInstance(Ci.nsIFileOutputStream);
  ostream.init(file, -1, -1, 0);

  // Finally, we need an input stream to take data from.
  const TEST_DATA = "this is a test string";
  let istream = Cc["@mozilla.org/io/string-input-stream;1"].
                createInstance(Ci.nsIStringInputStream);
  istream.setData(TEST_DATA, TEST_DATA.length);

  NetUtil.asyncCopy(istream, ostream, function(aResult) {
    // Make sure the copy was successful!
    do_check_true(Components.isSuccessCode(aResult));

    // Check the file contents.
    do_check_eq(TEST_DATA, getFileContents(file));

    // Remove the file, and finish the test.
    file.remove(false);
    do_test_finished();
    run_next_test();
  });
}

function test_newURI_no_spec_throws()
{
  try {
    NetUtil.newURI();
    do_throw("should throw!");
  }
  catch (e) {
    do_check_eq(e.result, Cr.NS_ERROR_INVALID_ARG);
  }

  run_next_test();
}

function test_newURI()
{
  let ios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);

  // Check that we get the same URI back from the IO service and the utility method.
  const TEST_URI = "http://mozilla.org";
  let iosURI = ios.newURI(TEST_URI, null, null);
  let NetUtilURI = NetUtil.newURI(TEST_URI);
  do_check_true(iosURI.equals(NetUtilURI));

  run_next_test();
}

function test_asyncFetch_no_channel()
{
  try {
    NetUtil.asyncFetch(null, function() { });
    do_throw("should throw!");
  }
  catch (e) {
    do_check_eq(e.result, Cr.NS_ERROR_INVALID_ARG);
  }

  run_next_test();
}

function test_asyncFetch_no_callback()
{
  try {
    NetUtil.asyncFetch({ });
    do_throw("should throw!");
  }
  catch (e) {
    do_check_eq(e.result, Cr.NS_ERROR_INVALID_ARG);
  }

  run_next_test();
}

function test_asyncFetch()
{
  const TEST_DATA = "this is a test string";

  // Start the http server, and register our handler.
  let server = new nsHttpServer();
  server.registerPathHandler("/test", function(aRequest, aResponse) {
    aResponse.setStatusLine(aRequest.httpVersion, 200, "OK");
    aResponse.setHeader("Content-Type", "text/plain", false);
    aResponse.write(TEST_DATA);
  });
  server.start(4444);

  // Create our channel.
  let ios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);
  let channel = ios.newChannel("http://localhost:4444/test", null, null);

  // Open our channel asynchronously.
  NetUtil.asyncFetch(channel, function(aInputStream, aResult) {
    // Check that we had success.
    do_check_true(Components.isSuccessCode(aResult));

    // Check that we got the right data.
    do_check_eq(aInputStream.available(), TEST_DATA.length);
    let is = Cc["@mozilla.org/scriptableinputstream;1"].
             createInstance(Ci.nsIScriptableInputStream);
    is.init(aInputStream);
    let result = is.read(TEST_DATA.length);
    do_check_eq(TEST_DATA, result);

    server.stop(run_next_test);
  });
}

function test_asyncFetch_does_not_block()
{
  // Create our channel that has no data.
  let ios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);
  let channel = ios.newChannel("data:text/plain,", null, null);

  // Open our channel asynchronously.
  NetUtil.asyncFetch(channel, function(aInputStream, aResult) {
    // Check that we had success.
    do_check_true(Components.isSuccessCode(aResult));

    // Check that reading a byte throws that the stream was closed (as opposed
    // saying it would block).
    let is = Cc["@mozilla.org/scriptableinputstream;1"].
             createInstance(Ci.nsIScriptableInputStream);
    is.init(aInputStream);
    try {
      is.read(1);
      do_throw("should throw!");
    }
    catch (e) {
      do_check_eq(e.result, Cr.NS_BASE_STREAM_CLOSED);
    }

    run_next_test();
  });
}

////////////////////////////////////////////////////////////////////////////////
//// Test Runner

let tests = [
  test_async_write_file,
  test_async_write_file_nsISafeOutputStream,
  test_newURI_no_spec_throws,
  test_newURI,
  test_asyncFetch_no_channel,
  test_asyncFetch_no_callback,
  test_asyncFetch,
  test_asyncFetch_does_not_block,
];
let index = 0;

function run_next_test()
{
  if (index < tests.length) {
    do_test_pending();
    print("Running the next test: " + tests[index].name);
    tests[index++]();
  }

  do_test_finished();
}

function run_test()
{
  do_test_pending();
  run_next_test();
}


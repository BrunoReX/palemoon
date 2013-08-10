/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let gTestRoot = getRootDirectory(gTestPath);
let gBugWindow = null;
let gIterations = 5;

function onLoad() {
  gBugWindow.close();
}

function onUnload() {
  if (!gIterations) {
    finish();
  } else {
    gBugWindow = window.openDialog(gTestRoot + "bug839193.xul");
    gIterations--;
  }
}

// This test is about leaks, which are handled by the test harness, so
// there are no actual checks here. Whether or not this test passes or fails
// will be apparent by the checks the harness performs.
function test() {
  waitForExplicitFinish();
  Components.classes["@mozilla.org/observer-service;1"]
            .getService(Components.interfaces.nsIObserverService)
            .addObserver(onLoad, "bug839193-loaded", false);
  Components.classes["@mozilla.org/observer-service;1"]
            .getService(Components.interfaces.nsIObserverService)
            .addObserver(onUnload, "bug839193-unloaded", false);

  gBugWindow = window.openDialog(gTestRoot + "bug839193.xul");
}

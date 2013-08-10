/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This test makes sure that the style editor does not store any
// content CSS files in the permanent cache when opened from PB mode.
function test() {
  waitForExplicitFinish();

  gPrefService.setBoolPref("browser.privatebrowsing.keep_current_session", true);
  let pb = Cc["@mozilla.org/privatebrowsing;1"].
           getService(Ci.nsIPrivateBrowsingService);
  pb.privateBrowsingEnabled = true;

  function checkCache() {
    checkDiskCacheFor(TEST_HOST);
    pb.privateBrowsingEnabled = false;
    gPrefService.clearUserPref("browser.privatebrowsing.keep_current_session");
    finish();
  }

  gBrowser.selectedBrowser.addEventListener("load", function onLoad() {
    gBrowser.selectedBrowser.removeEventListener("load", onLoad, true);
    cache.evictEntries(Ci.nsICache.STORE_ANYWHERE);
    launchStyleEditorChrome(function(aChrome) {
      aChrome.addChromeListener({
        onEditorAdded: function(aChrome, aEditor) {
          if (aEditor.isLoaded) {
            checkCache();
          } else {
            aEditor.addActionListener({
              onLoad: checkCache
            });
          }
        }
      });
    });
  }, true);

  content.location = 'http://' + TEST_HOST + '/browser/browser/devtools/styleeditor/test/test_private.html';
}

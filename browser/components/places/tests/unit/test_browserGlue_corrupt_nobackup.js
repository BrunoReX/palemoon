/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
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
 * The Original Code is Places Unit Test code.
 *
 * The Initial Developer of the Original Code is Mozilla Corp.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Marco Bonardo <mak77@bonardo.net>
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
 * Tests that nsBrowserGlue correctly imports from bookmarks.html if database
 * is corrupt but a JSON backup is not available.
 */

const NS_PLACES_INIT_COMPLETE_TOPIC = "places-init-complete";

// Create an observer for the Places notifications
var os = Cc["@mozilla.org/observer-service;1"].
         getService(Ci.nsIObserverService);
var observer = {
  observe: function thn_observe(aSubject, aTopic, aData) {
    if (aTopic == NS_PLACES_INIT_COMPLETE_TOPIC) {
        os.removeObserver(this, NS_PLACES_INIT_COMPLETE_TOPIC);
        var hs = Cc["@mozilla.org/browser/nav-history-service;1"].
                 getService(Ci.nsINavHistoryService);
      // Check the database was corrupt.
      // nsBrowserGlue uses databaseStatus to manage initialization.
      do_check_eq(hs.databaseStatus, hs.DATABASE_STATUS_CORRUPT);

      // Enqueue next part of the test.
      var tm = Cc["@mozilla.org/thread-manager;1"].
               getService(Ci.nsIThreadManager);
      tm.mainThread.dispatch({
        run: function() {
          continue_test();
        }
      }, Ci.nsIThread.DISPATCH_NORMAL);
    }
  }
};

function run_test() {
  // XXX bug 507199
  // This test is temporarily disabled!
  return;

  os.addObserver(observer, NS_PLACES_INIT_COMPLETE_TOPIC, false);

  // Create bookmarks.html in the profile.
  create_bookmarks_html("bookmarks.glue.html");
  // Remove JSON backup from profile.
  remove_all_JSON_backups();

  // Remove current database file.
  var db = gProfD.clone();
  db.append("places.sqlite");
  if (db.exists()) {
    db.remove(false);
    do_check_false(db.exists());
  }
  // Create a corrupt database.
  corruptDB = gTestDir.clone();
  corruptDB.append("corruptDB.sqlite");
  corruptDB.copyTo(gProfD, "places.sqlite");
  do_check_true(db.exists());

  // Initialize nsBrowserGlue before Places.
  Cc["@mozilla.org/browser/browserglue;1"].getService(Ci.nsIBrowserGlue);

  // Initialize Places through the History Service.
  var hs = Cc["@mozilla.org/browser/nav-history-service;1"].
           getService(Ci.nsINavHistoryService);

  // Wait for init-complete notification before going on.
  do_test_pending();
}

function continue_test() {
  var bs = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
           getService(Ci.nsINavBookmarksService);

  var itemId = bs.getIdForItemAt(bs.toolbarFolder, SMART_BOOKMARKS_ON_TOOLBAR);
  do_check_neq(itemId, -1);
  do_check_eq(bs.getItemTitle(itemId), "example");

  do_test_finished();
}

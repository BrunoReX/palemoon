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
 * Tests that nsBrowserGlue correctly restores bookmarks from a JSON backup if
 * database has been created and one backup is available.
 */

function run_test() {
  // Create our bookmarks.html copying bookmarks.glue.html to the profile
  // folder.  It will be ignored.
  create_bookmarks_html("bookmarks.glue.html");

  // Create our JSON backup copying bookmarks.glue.json to the profile
  // folder.  It will be ignored.
  create_JSON_backup("bookmarks.glue.json");

  // Remove current database file.
  let db = gProfD.clone();
  db.append("places.sqlite");
  if (db.exists()) {
    db.remove(false);
    do_check_false(db.exists());
  }

  // Initialize nsBrowserGlue before Places.
  Cc["@mozilla.org/browser/browserglue;1"].getService(Ci.nsIBrowserGlue);

  // Initialize Places through the History Service.
  let hs = Cc["@mozilla.org/browser/nav-history-service;1"].
           getService(Ci.nsINavHistoryService);
  // Check a new database has been created.
  // nsBrowserGlue uses databaseStatus to manage initialization.
  do_check_eq(hs.databaseStatus, hs.DATABASE_STATUS_CREATE);

  // Restore could take some time, usually less than 1s.
  // We will poll later in continue_test() to be sure restore has finished.
  do_test_pending();
  do_timeout(1000, "continue_test();");
}

function continue_test() {
  let bs = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
           getService(Ci.nsINavBookmarksService);

  if (bs.getIdForItemAt(bs.toolbarFolder, 0) == -1) {
    // Not enough time to complete restore, poll again later.
    do_timeout(1000, "continue_test();");
    return;
  }

  // Check that JSON backup has been restored.
  let itemId = bs.getIdForItemAt(bs.toolbarFolder, SMART_BOOKMARKS_ON_TOOLBAR);
  do_check_eq(bs.getItemTitle(itemId), "examplejson");

  do_test_finished();
}

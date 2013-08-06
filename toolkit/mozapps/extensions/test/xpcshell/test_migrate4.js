/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Checks that we migrate data from a previous version of the sqlite database

// The test extension uses an insecure update url.
Services.prefs.setBoolPref("extensions.checkUpdateSecurity", false);

var addon1 = {
  id: "addon1@tests.mozilla.org",
  version: "1.0",
  name: "Test 1",
  targetApplications: [{
    id: "xpcshell@tests.mozilla.org",
    minVersion: "1",
    maxVersion: "2"
  }]
};

var addon2 = {
  id: "addon2@tests.mozilla.org",
  version: "2.0",
  name: "Test 2",
  targetApplications: [{
    id: "xpcshell@tests.mozilla.org",
    minVersion: "1",
    maxVersion: "2"
  }]
};

var addon3 = {
  id: "addon3@tests.mozilla.org",
  version: "2.0",
  name: "Test 3",
  targetApplications: [{
    id: "xpcshell@tests.mozilla.org",
    minVersion: "1",
    maxVersion: "2"
  }]
};

var addon4 = {
  id: "addon4@tests.mozilla.org",
  version: "2.0",
  name: "Test 4",
  targetApplications: [{
    id: "xpcshell@tests.mozilla.org",
    minVersion: "1",
    maxVersion: "2"
  }]
};

var addon5 = {
  id: "addon5@tests.mozilla.org",
  version: "2.0",
  name: "Test 5",
  updateURL: "http://localhost:4444/data/test_migrate4.rdf",
  targetApplications: [{
    id: "xpcshell@tests.mozilla.org",
    minVersion: "0",
    maxVersion: "1"
  }]
};

var addon6 = {
  id: "addon6@tests.mozilla.org",
  version: "1.0",
  name: "Test 6",
  updateURL: "http://localhost:4444/data/test_migrate4.rdf",
  targetApplications: [{
    id: "xpcshell@tests.mozilla.org",
    minVersion: "0",
    maxVersion: "1"
  }]
};

const profileDir = gProfD.clone();
profileDir.append("extensions");

do_load_httpd_js();
var testserver;

function prepare_profile() {
  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1.9.2");

  // Create and configure the HTTP server.
  testserver = new nsHttpServer();
  testserver.registerDirectory("/data/", do_get_file("data"));
  testserver.registerDirectory("/addons/", do_get_file("addons"));
  testserver.start(4444);

  writeInstallRDFForExtension(addon1, profileDir);
  writeInstallRDFForExtension(addon2, profileDir);
  writeInstallRDFForExtension(addon3, profileDir);
  writeInstallRDFForExtension(addon4, profileDir);
  writeInstallRDFForExtension(addon5, profileDir);
  writeInstallRDFForExtension(addon6, profileDir);

  startupManager();
  AddonManager.getAddonsByIDs(["addon1@tests.mozilla.org",
                               "addon2@tests.mozilla.org",
                               "addon3@tests.mozilla.org",
                               "addon4@tests.mozilla.org",
                               "addon5@tests.mozilla.org",
                               "addon6@tests.mozilla.org"],
                               function([a1, a2, a3, a4, a5, a6]) {
    a2.userDisabled = true;
    a2.applyBackgroundUpdates = false;
    a4.userDisabled = true;
    a6.userDisabled = true;

    a6.findUpdates({
      onUpdateAvailable: function(aAddon, aInstall6) {
        AddonManager.getInstallForURL("http://localhost:4444/addons/test_migrate4_7.xpi", function(aInstall7) {
          completeAllInstalls([aInstall6, aInstall7], function() {
            restartManager();

            AddonManager.getAddonsByIDs(["addon1@tests.mozilla.org",
                                         "addon2@tests.mozilla.org",
                                         "addon3@tests.mozilla.org",
                                         "addon4@tests.mozilla.org",
                                         "addon5@tests.mozilla.org",
                                         "addon6@tests.mozilla.org"],
                                         function([a1, a2, a3, a4, a5, a6]) {
              a3.userDisabled = true;
              a4.userDisabled = false;

              a5.findUpdates({
                onUpdateFinished: function() {
                  shutdownManager();

                  perform_migration();
                }
              }, AddonManager.UPDATE_WHEN_USER_REQUESTED);
            });
          });
        }, "application/x-xpinstall");
      }
    }, AddonManager.UPDATE_WHEN_USER_REQUESTED);
  });
}

function perform_migration() {
  let dbfile = gProfD.clone();
  dbfile.append("extensions.sqlite");
  let db = AM_Cc["@mozilla.org/storage/service;1"].
           getService(AM_Ci.mozIStorageService).
           openDatabase(dbfile);
  db.schemaVersion = 1;
  Services.prefs.setIntPref("extensions.databaseSchema", 1);
  db.close();

  gAppInfo.version = "2"
  startupManager(true);
  test_results();
}

function test_results() {
  check_startup_changes("installed", []);
  check_startup_changes("updated", []);
  check_startup_changes("uninstalled", []);
  check_startup_changes("disabled", []);
  check_startup_changes("enabled", []);

  AddonManager.getAddonsByIDs(["addon1@tests.mozilla.org",
                               "addon2@tests.mozilla.org",
                               "addon3@tests.mozilla.org",
                               "addon4@tests.mozilla.org",
                               "addon5@tests.mozilla.org",
                               "addon6@tests.mozilla.org",
                               "addon7@tests.mozilla.org"],
                               function([a1, a2, a3, a4, a5, a6, a7]) {
    // addon1 was enabled
    do_check_neq(a1, null);
    do_check_false(a1.userDisabled);
    do_check_false(a1.appDisabled);
    do_check_true(a1.isActive);
    do_check_true(a1.applyBackgroundUpdates);
    do_check_true(a1.foreignInstall);

    // addon2 was disabled
    do_check_neq(a2, null);
    do_check_true(a2.userDisabled);
    do_check_false(a2.appDisabled);
    do_check_false(a2.isActive);
    do_check_false(a2.applyBackgroundUpdates);
    do_check_true(a2.foreignInstall);

    // addon3 was pending-disable in the database
    do_check_neq(a3, null);
    do_check_true(a3.userDisabled);
    do_check_false(a3.appDisabled);
    do_check_false(a3.isActive);
    do_check_true(a3.applyBackgroundUpdates);
    do_check_true(a3.foreignInstall);

    // addon4 was pending-enable in the database
    do_check_neq(a4, null);
    do_check_false(a4.userDisabled);
    do_check_false(a4.appDisabled);
    do_check_true(a4.isActive);
    do_check_true(a4.applyBackgroundUpdates);
    do_check_true(a4.foreignInstall);

    // addon5 was enabled in the database but needed a compatibiltiy update
    do_check_neq(a5, null);
    do_check_false(a5.userDisabled);
    do_check_false(a5.appDisabled);
    do_check_true(a5.isActive);
    do_check_true(a5.applyBackgroundUpdates);
    do_check_true(a5.foreignInstall);

    // addon6 was disabled and compatible but a new version has been installed
    do_check_neq(a6, null);
    do_check_eq(a6.version, "2.0");
    do_check_true(a6.userDisabled);
    do_check_false(a6.appDisabled);
    do_check_false(a6.isActive);
    do_check_true(a6.applyBackgroundUpdates);
    do_check_true(a6.foreignInstall);
    do_check_eq(a6.sourceURI.spec, "http://localhost:4444/addons/test_migrate4_6.xpi");
    do_check_eq(a6.releaseNotesURI.spec, "http://example.com/updateInfo.xhtml");

    // addon7 was installed manually
    do_check_neq(a7, null);
    do_check_eq(a7.version, "1.0");
    do_check_false(a7.userDisabled);
    do_check_false(a7.appDisabled);
    do_check_true(a7.isActive);
    do_check_true(a7.applyBackgroundUpdates);
    do_check_false(a7.foreignInstall);
    do_check_eq(a7.sourceURI.spec, "http://localhost:4444/addons/test_migrate4_7.xpi");
    do_check_eq(a7.releaseNotesURI, null);
    testserver.stop(do_test_finished);
  });
}

function run_test() {
  do_test_pending();

  prepare_profile();
}

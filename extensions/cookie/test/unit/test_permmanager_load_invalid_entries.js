/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

var DEBUG_TEST = false;

function run_test() {
  // Setup a profile directory.
  var dir = do_get_profile();
  // Get the db file.
  var file = dir.clone();
  file.append("permissions.sqlite");

  var storage = Cc["@mozilla.org/storage/service;1"]
                  .getService(Ci.mozIStorageService);

  // Create database.
  var connection = storage.openDatabase(file);
  // The file should now exist.
  do_check_true(file.exists());

  connection.schemaVersion = 3;
  connection.executeSimpleSQL(
    "CREATE TABLE moz_hosts (" +
      " id INTEGER PRIMARY KEY" +
      ",host TEXT" +
      ",type TEXT" +
      ",permission INTEGER" +
      ",expireType INTEGER" +
      ",expireTime INTEGER" +
      ",appId INTEGER" +
      ",isInBrowserElement INTEGER" +
    ")");

  // Now we can inject garbadge in the database.
  var garbadge = [
    // Regular entry.
    { host: '42', type: '0', permission: 1, expireType: 0, expireTime: 0,
      appId: 0, isInBrowserElement: 0 },

    // Special values in host (some being invalid).
    { host: 'scheme:file', type: '1', permission: 0, expireType: 0,
      expireTime: 0, appId: 0, isInBrowserElement: 0 },
    { host: '192.168.0.1', type: '2', permission: 0, expireType: 0,
      expireTime: 0, appId: 0, isInBrowserElement: 0 },
    { host: '2001:0db8:0000:0000:0000:ff00:0042:8329', type: '3', permission: 0,
      expireType: 0, expireTime: 0, appId: 0, isInBrowserElement: 0 },
    { host: '::1', type: '4', permission: 0, expireType: 0, expireTime: 0,
      appId: 0, isInBrowserElement: 0 },

    // Permission is UNKNOWN_ACTION.
    { host: '42', type: '5', permission: Ci.nsIPermissionManager.UNKNOWN_ACTION,
      expireType: 0, expireTime: 0, appId: 0, isInBrowserElement: 0 },

    // Permission is out of range.
    { host: '42', type: '6', permission: 100, expireType: 0, expireTime: 0,
      appId: 0, isInBrowserElement: 0 },
    { host: '42', type: '7', permission: -100, expireType: 0, expireTime: 0,
      appId: 0, isInBrowserElement: 0 },

    // ExpireType is out of range.
    { host: '42', type: '8', permission: 1, expireType: -100, expireTime: 0,
      appId: 0, isInBrowserElement: 0 },
    { host: '42', type: '9', permission: 1, expireType: 100, expireTime: 0,
      appId: 0, isInBrowserElement: 0 },

    // ExpireTime is at 0 with ExpireType = Time.
    { host: '42', type: '10', permission: 1,
      expireType: Ci.nsIPermissionManager.EXPIRE_TIME, expireTime: 0, appId: 0,
      isInBrowserElement: 0 },

    // ExpireTime has a value with ExpireType != Time
    { host: '42', type: '11', permission: 1,
      expireType: Ci.nsIPermissionManager.EXPIRE_SESSION, expireTime: 1000,
      appId: 0, isInBrowserElement: 0 },
    { host: '42', type: '12', permission: 1,
      expireType: Ci.nsIPermissionManager.EXPIRE_NEVER, expireTime: 1000,
      appId: 0, isInBrowserElement: 0 },

    // ExpireTime is negative.
    { host: '42', type: '13', permission: 1,
      expireType: Ci.nsIPermissionManager.EXPIRE_TIME, expireTime: -1,
      appId: 0, isInBrowserElement: 0 },

    // AppId is negative.
    { host: '42', type: '14', permission: 1, expireType: 0, expireTime: 0,
      appId: -1, isInBrowserElement: 0 },

    // IsInBrowserElement is negative or higher than 1.
    { host: '42', type: '15', permission: 1, expireType: 0, expireTime: 0,
      appId: 0, isInBrowserElement: -1 },
    { host: '42', type: '16', permission: 1, expireType: 0, expireTime: 0,
      appId: 0, isInBrowserElement: 10 },

    // This insertion should be the last one. It is used to make sure we always
    // load it regardless of the previous entries validities.
    { host: 'example.org', type: 'test-load-invalid-entries',
      permission: Ci.nsIPermissionManager.ALLOW_ACTION, expireType: 0,
      expireTime: 0, appId: 0, isInBrowserElement: 0 },
  ];

  for (var i=0; i<garbadge.length; ++i) {
    if (DEBUG_TEST) {
      dump("\n value #" + i + "\n\n");
    }
    var data = garbadge[i];
    connection.executeSimpleSQL(
      "INSERT INTO moz_hosts " +
      " (id, host, type, permission, expireType, expireTime, appId, isInBrowserElement) " +
      "VALUES (" + i + ", '" + data.host + "', '" + data.type + "', "
                 + data.permission + ", " + data.expireType + ", "
                 + data.expireTime + ", " + data.appId + ", "
                 + data.isInBrowserElement + ")"
    );
  }

  // Initialize the permission manager service
  var pm = Cc["@mozilla.org/permissionmanager;1"]
             .getService(Ci.nsIPermissionManager);

  // The schema should still be 3. We want this test to be updated for each
  // schema update.
  do_check_eq(connection.schemaVersion, 3);

  // This permission should always be there.
  let principal = Cc["@mozilla.org/scriptsecuritymanager;1"]
                    .getService(Ci.nsIScriptSecurityManager)
                    .getNoAppCodebasePrincipal(NetUtil.newURI("http://example.org"));
  do_check_eq(pm.testPermissionFromPrincipal(principal, 'test-load-invalid-entries'), Ci.nsIPermissionManager.ALLOW_ACTION);
}

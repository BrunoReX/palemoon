Cu.import("resource://services-sync/engines.js");
Cu.import("resource://services-sync/status.js");
Cu.import("resource://services-sync/constants.js");

Cu.import("resource://services-sync/util.js");
Svc.DefaultPrefs.set("registerEngines", "");
Cu.import("resource://services-sync/service.js");
Cu.import("resource://services-sync/record.js");

initTestLogging();

function CatapultEngine() {
  SyncEngine.call(this, "Catapult");
}
CatapultEngine.prototype = {
  __proto__: SyncEngine.prototype,
  exception: null, // tests fill this in
  sync: function sync() {
    throw this.exception;
  }
};

function sync_httpd_setup() {
  let collectionsHelper = track_collections_helper();
  let upd = collectionsHelper.with_updated_collection;
  let collections = collectionsHelper.collections;

  let catapultEngine = Engines.get("catapult");
  let engines        = {catapult: {version: catapultEngine.version,
                                   syncID:  catapultEngine.syncID}};

  // Track these using the collections helper, which keeps modified times
  // up-to-date.
  let clientsColl = new ServerCollection({}, true);
  let keysWBO     = new ServerWBO("keys");
  let globalWBO   = new ServerWBO("global", {storageVersion: STORAGE_VERSION,
                                             syncID: Utils.makeGUID(),
                                             engines: engines});

  let handlers = {
    "/1.1/johndoe/info/collections":    collectionsHelper.handler,
    "/1.1/johndoe/storage/meta/global": upd("meta",    globalWBO.handler()),
    "/1.1/johndoe/storage/clients":     upd("clients", clientsColl.handler()),
    "/1.1/johndoe/storage/crypto/keys": upd("crypto",  keysWBO.handler())
  };
  return httpd_setup(handlers);
}

function setUp() {
  Service.username = "johndoe";
  Service.password = "ilovejane";
  Service.passphrase = "aabcdeabcdeabcdeabcdeabcde";
  Service.clusterURL = "http://localhost:8080/";
  new FakeCryptoService();
}

function generateAndUploadKeys() {
  generateNewKeys();
  let serverKeys = CollectionKeys.asWBO("crypto", "keys");
  serverKeys.encrypt(Weave.Service.syncKeyBundle);
  return serverKeys.upload("http://localhost:8080/1.1/johndoe/storage/crypto/keys").success;
}


add_test(function test_backoff500() {
  _("Test: HTTP 500 sets backoff status.");
  setUp();
  let server = sync_httpd_setup();

  let engine = Engines.get("catapult");
  engine.enabled = true;
  engine.exception = {status: 500};

  try {
    do_check_false(Status.enforceBackoff);

    // Forcibly create and upload keys here -- otherwise we don't get to the 500!
    do_check_true(generateAndUploadKeys());

    Service.login();
    Service.sync();
    do_check_true(Status.enforceBackoff);
  } finally {
    Status.resetBackoff();
    Service.startOver();
  }
  server.stop(run_next_test);
});

add_test(function test_backoff503() {
  _("Test: HTTP 503 with Retry-After header leads to backoff notification and sets backoff status.");
  setUp();
  let server = sync_httpd_setup();

  const BACKOFF = 42;
  let engine = Engines.get("catapult");
  engine.enabled = true;
  engine.exception = {status: 503,
                      headers: {"retry-after": BACKOFF}};

  let backoffInterval;
  Svc.Obs.add("weave:service:backoff:interval", function (subject) {
    backoffInterval = subject;
  });

  try {
    do_check_false(Status.enforceBackoff);

    do_check_true(generateAndUploadKeys());

    Service.login();
    Service.sync();

    do_check_true(Status.enforceBackoff);
    do_check_eq(backoffInterval, BACKOFF);
  } finally {
    Status.resetBackoff();
    Service.startOver();
  }
  server.stop(run_next_test);
});

add_test(function test_overQuota() {
  _("Test: HTTP 400 with body error code 14 means over quota.");
  setUp();
  let server = sync_httpd_setup();

  let engine = Engines.get("catapult");
  engine.enabled = true;
  engine.exception = {status: 400,
                      toString: function() "14"};

  try {
    do_check_eq(Status.sync, SYNC_SUCCEEDED);

    do_check_true(generateAndUploadKeys());

    Service.login();
    Service.sync();

    do_check_eq(Status.sync, OVER_QUOTA);
  } finally {
    Status.resetSync();
    Service.startOver();
  }
  server.stop(run_next_test);
});

add_test(function test_service_networkError() {
  _("Test: Connection refused error from Service.sync() leads to the right status code.");
  setUp();
  // Provoke connection refused.
  Service.clusterURL = "http://localhost:12345/";
  Service._ignorableErrorCount = 0;

  try {
    do_check_eq(Status.sync, SYNC_SUCCEEDED);

    Service._loggedIn = true;
    Service.sync();

    do_check_eq(Status.sync, LOGIN_FAILED_NETWORK_ERROR);
    do_check_eq(Service._ignorableErrorCount, 1);
  } finally {
    Status.resetSync();
    Service.startOver();
  }
  run_next_test();
});

add_test(function test_service_offline() {
  _("Test: Wanting to sync in offline mode leads to the right status code but does not increment the ignorable error count.");
  setUp();
  Services.io.offline = true;
  Service._ignorableErrorCount = 0;

  try {
    do_check_eq(Status.sync, SYNC_SUCCEEDED);

    Service._loggedIn = true;
    Service.sync();

    do_check_eq(Status.sync, LOGIN_FAILED_NETWORK_ERROR);
    do_check_eq(Service._ignorableErrorCount, 0);
  } finally {
    Status.resetSync();
    Service.startOver();
  }
  Services.io.offline = false;
  run_next_test();
});

add_test(function test_service_reset_ignorableErrorCount() {
  _("Test: Successful sync resets the ignorable error count.");
  setUp();
  let server = sync_httpd_setup();
  Service._ignorableErrorCount = 10;

  // Disable the engine so that sync completes.
  let engine = Engines.get("catapult");
  engine.enabled = false;

  try {
    do_check_eq(Status.sync, SYNC_SUCCEEDED);

    do_check_true(generateAndUploadKeys());

    Service.login();
    Service.sync();

    do_check_eq(Status.sync, SYNC_SUCCEEDED);
    do_check_eq(Service._ignorableErrorCount, 0);
  } finally {
    Status.resetSync();
    Service.startOver();
  }
  server.stop(run_next_test);
});

add_test(function test_engine_networkError() {
  _("Test: Network related exceptions from engine.sync() lead to the right status code.");
  setUp();
  let server = sync_httpd_setup();
  Service._ignorableErrorCount = 0;

  let engine = Engines.get("catapult");
  engine.enabled = true;
  engine.exception = Components.Exception("NS_ERROR_UNKNOWN_HOST",
                                          Cr.NS_ERROR_UNKNOWN_HOST);

  try {
    do_check_eq(Status.sync, SYNC_SUCCEEDED);

    do_check_true(generateAndUploadKeys());

    Service.login();
    Service.sync();

    do_check_eq(Status.sync, LOGIN_FAILED_NETWORK_ERROR);
    do_check_eq(Service._ignorableErrorCount, 1);
  } finally {
    Status.resetSync();
    Service.startOver();
  }
  server.stop(run_next_test);
});

add_test(function test_resource_timeout() {
  setUp();
  let server = sync_httpd_setup();

  let engine = Engines.get("catapult");
  engine.enabled = true;
  // Resource throws this when it encounters a timeout.
  engine.exception = Components.Exception("Aborting due to channel inactivity.",
                                          Cr.NS_ERROR_NET_TIMEOUT);

  try {
    do_check_eq(Status.sync, SYNC_SUCCEEDED);

    do_check_true(generateAndUploadKeys());

    Service.login();
    Service.sync();

    do_check_eq(Status.sync, LOGIN_FAILED_NETWORK_ERROR);
  } finally {
    Status.resetSync();
    Service.startOver();
  }
  server.stop(run_next_test);
});


// Slightly misplaced test as it doesn't actually test checkServerError,
// but the observer for "weave:engine:sync:apply-failed".
// This test should be the last one since it monkeypatches the engine object
// and we should only have one engine object throughout the file (bug 629664).
add_test(function test_engine_applyFailed() {
  setUp();
  let server = sync_httpd_setup();

  let engine = Engines.get("catapult");
  engine.enabled = true;
  delete engine.exception;
  engine.sync = function sync() {
    Svc.Obs.notify("weave:engine:sync:applied", {newFailed:1}, "steam");
  };

  try {
    do_check_eq(Status.engines["steam"], undefined);

    do_check_true(generateAndUploadKeys());

    Service.login();
    Service.sync();

    do_check_eq(Status.engines["steam"], ENGINE_APPLY_FAIL);
  } finally {
    Status.resetSync();
    Service.startOver();
  }
  server.stop(run_next_test);
});


function run_test() {
  Engines.register(CatapultEngine);
  run_next_test();
}

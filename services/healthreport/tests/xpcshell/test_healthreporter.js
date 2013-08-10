/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://services-common/observers.js");
Cu.import("resource://services-common/preferences.js");
Cu.import("resource://gre/modules/commonjs/promise/core.js");
Cu.import("resource://gre/modules/services/healthreport/healthreporter.jsm");
Cu.import("resource://gre/modules/services/datareporting/policy.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Task.jsm");
Cu.import("resource://testing-common/services-common/bagheeraserver.js");
Cu.import("resource://testing-common/services/metrics/mocks.jsm");
Cu.import("resource://testing-common/services/healthreport/utils.jsm");


const SERVER_HOSTNAME = "localhost";
const SERVER_PORT = 8080;
const SERVER_URI = "http://" + SERVER_HOSTNAME + ":" + SERVER_PORT;
const MILLISECONDS_PER_DAY = 24 * 60 * 60 * 1000;


function defineNow(policy, now) {
  print("Adjusting fake system clock to " + now);
  Object.defineProperty(policy, "now", {
    value: function customNow() {
      return now;
    },
    writable: true,
  });
}

function getJustReporter(name, uri=SERVER_URI, inspected=false) {
  let branch = "healthreport.testing. " + name + ".";

  let prefs = new Preferences(branch + "healthreport.");
  prefs.set("documentServerURI", uri);
  prefs.set("dbName", name);

  let reporter;

  let policyPrefs = new Preferences(branch + "policy.");
  let policy = new DataReportingPolicy(policyPrefs, prefs, {
    onRequestDataUpload: function (request) {
      reporter.requestDataUpload(request);
    },

    onNotifyDataPolicy: function (request) { },

    onRequestRemoteDelete: function (request) {
      reporter.deleteRemoteData(request);
    },
  });

  let type = inspected ? InspectedHealthReporter : HealthReporter;
  reporter = new type(branch + "healthreport.", policy);

  return reporter;
}

function getReporter(name, uri, inspected) {
  let reporter = getJustReporter(name, uri, inspected);
  return reporter.onInit();
}

function getReporterAndServer(name, namespace="test") {
  return Task.spawn(function get() {
    let reporter = yield getReporter(name, SERVER_URI);
    reporter.serverNamespace = namespace;

    let server = new BagheeraServer(SERVER_URI);
    server.createNamespace(namespace);

    server.start(SERVER_PORT);

    throw new Task.Result([reporter, server]);
  });
}

function shutdownServer(server) {
  let deferred = Promise.defer();
  server.stop(deferred.resolve.bind(deferred));

  return deferred.promise;
}

function run_test() {
  run_next_test();
}

add_task(function test_constructor() {
  let reporter = yield getReporter("constructor");

  do_check_eq(reporter.lastPingDate.getTime(), 0);
  do_check_null(reporter.lastSubmitID);

  reporter.lastSubmitID = "foo";
  do_check_eq(reporter.lastSubmitID, "foo");
  reporter.lastSubmitID = null;
  do_check_null(reporter.lastSubmitID);

  let failed = false;
  try {
    new HealthReporter("foo.bar");
  } catch (ex) {
    failed = true;
    do_check_true(ex.message.startsWith("Branch must end"));
  } finally {
    do_check_true(failed);
    failed = false;
  }

  reporter._shutdown();
});

add_task(function test_shutdown_normal() {
  let reporter = yield getReporter("shutdown_normal");

  // We can't send "quit-application" notification because the xpcshell runner
  // will shut down!
  reporter._initiateShutdown();
  reporter._waitForShutdown();
});

add_task(function test_shutdown_storage_in_progress() {
  let reporter = yield getJustReporter("shutdown_storage_in_progress", SERVER_URI, true);

  reporter.onStorageCreated = function () {
    print("Faking shutdown during storage initialization.");
    reporter._initiateShutdown();
  };

  reporter._waitForShutdown();
  do_check_eq(reporter.collectorShutdownCount, 0);
  do_check_eq(reporter.storageCloseCount, 1);
});

// Ensure that a shutdown triggered while collector is initializing results in
// shutdown and storage closure.
add_task(function test_shutdown_collector_in_progress() {
  let reporter = yield getJustReporter("shutdown_collect_in_progress", SERVER_URI, true);

  reporter.onCollectorInitialized = function () {
    print("Faking shutdown during collector initialization.");
    reporter._initiateShutdown();
  };

  // This will hang if shutdown logic is busted.
  reporter._waitForShutdown();
  do_check_eq(reporter.collectorShutdownCount, 1);
  do_check_eq(reporter.storageCloseCount, 1);
});

add_task(function test_register_providers_from_category_manager() {
  const category = "healthreporter-js-modules";

  let cm = Cc["@mozilla.org/categorymanager;1"]
             .getService(Ci.nsICategoryManager);
  cm.addCategoryEntry(category, "DummyProvider",
                      "resource://testing-common/services/metrics/mocks.jsm",
                      false, true);

  let reporter = yield getReporter("category_manager");
  do_check_eq(reporter._collector._providers.size, 0);
  yield reporter.registerProvidersFromCategoryManager(category);
  do_check_eq(reporter._collector._providers.size, 1);

  reporter._shutdown();
});

add_task(function test_json_payload_simple() {
  let reporter = yield getReporter("json_payload_simple");

  let now = new Date();
  let payload = yield reporter.getJSONPayload();
  let original = JSON.parse(payload);

  do_check_eq(original.version, 1);
  do_check_eq(original.thisPingDate, reporter._formatDate(now));
  do_check_eq(Object.keys(original.data.last).length, 0);
  do_check_eq(Object.keys(original.data.days).length, 0);

  reporter.lastPingDate = new Date(now.getTime() - 24 * 60 * 60 * 1000 - 10);

  original = JSON.parse(yield reporter.getJSONPayload());
  do_check_eq(original.lastPingDate, reporter._formatDate(reporter.lastPingDate));

  // This could fail if we cross UTC day boundaries at the exact instance the
  // test is executed. Let's tempt fate.
  do_check_eq(original.thisPingDate, reporter._formatDate(now));

  reporter._shutdown();
});

add_task(function test_json_payload_dummy_provider() {
  let reporter = yield getReporter("json_payload_dummy_provider");

  yield reporter.registerProvider(new DummyProvider());
  yield reporter.collectMeasurements();
  let payload = yield reporter.getJSONPayload();
  print(payload);
  let o = JSON.parse(payload);

  do_check_eq(Object.keys(o.data.last).length, 1);
  do_check_true("DummyProvider.DummyMeasurement.1" in o.data.last);

  reporter._shutdown();
});

add_task(function test_json_payload_multiple_days() {
  let reporter = yield getReporter("json_payload_multiple_days");
  let provider = new DummyProvider();
  yield reporter.registerProvider(provider);

  let now = new Date();
  let m = provider.getMeasurement("DummyMeasurement", 1);
  for (let i = 0; i < 200; i++) {
    let date = new Date(now.getTime() - i * MILLISECONDS_PER_DAY);
    yield m.incrementDailyCounter("daily-counter", date);
    yield m.addDailyDiscreteNumeric("daily-discrete-numeric", i, date);
    yield m.addDailyDiscreteNumeric("daily-discrete-numeric", i + 100, date);
    yield m.addDailyDiscreteText("daily-discrete-text", "" + i, date);
    yield m.addDailyDiscreteText("daily-discrete-text", "" + (i + 50), date);
    yield m.setDailyLastNumeric("daily-last-numeric", date.getTime(), date);
  }

  let payload = yield reporter.getJSONPayload();
  print(payload);
  let o = JSON.parse(payload);

  do_check_eq(Object.keys(o.data.days).length, 180);
  let today = reporter._formatDate(now);
  do_check_true(today in o.data.days);

  reporter._shutdown();
});

add_task(function test_idle_daily() {
  let reporter = yield getReporter("idle_daily");
  let provider = new DummyProvider();
  yield reporter.registerProvider(provider);

  let now = new Date();
  let m = provider.getMeasurement("DummyMeasurement", 1);
  for (let i = 0; i < 200; i++) {
    let date = new Date(now.getTime() - i * MILLISECONDS_PER_DAY);
    yield m.incrementDailyCounter("daily-counter", date);
  }

  let values = yield m.getValues();
  do_check_eq(values.days.size, 200);

  Services.obs.notifyObservers(null, "idle-daily", null);

  let values = yield m.getValues();
  do_check_eq(values.days.size, 180);

  reporter._shutdown();
});

add_task(function test_data_submission_transport_failure() {
  let reporter = yield getReporter("data_submission_transport_failure");
  reporter.serverURI = "http://localhost:8080/";
  reporter.serverNamespace = "test00";

  let deferred = Promise.defer();
  let request = new DataSubmissionRequest(deferred, new Date(Date.now + 30000));
  reporter.requestDataUpload(request);

  yield deferred.promise;
  do_check_eq(request.state, request.SUBMISSION_FAILURE_SOFT);

  reporter._shutdown();
});

add_task(function test_data_submission_success() {
  let [reporter, server] = yield getReporterAndServer("data_submission_success");

  do_check_eq(reporter.lastPingDate.getTime(), 0);
  do_check_false(reporter.haveRemoteData());

  let deferred = Promise.defer();

  let request = new DataSubmissionRequest(deferred, new Date());
  reporter.requestDataUpload(request);
  yield deferred.promise;
  do_check_eq(request.state, request.SUBMISSION_SUCCESS);
  do_check_true(reporter.lastPingDate.getTime() > 0);
  do_check_true(reporter.haveRemoteData());

  reporter._shutdown();
  yield shutdownServer(server);
});

add_task(function test_recurring_daily_pings() {
  let [reporter, server] = yield getReporterAndServer("recurring_daily_pings");
  reporter.registerProvider(new DummyProvider());

  let policy = reporter._policy;

  defineNow(policy, policy._futureDate(-24 * 60 * 68 * 1000));
  policy.recordUserAcceptance();
  defineNow(policy, policy.nextDataSubmissionDate);
  let promise = policy.checkStateAndTrigger();
  do_check_neq(promise, null);
  yield promise;

  let lastID = reporter.lastSubmitID;
  do_check_neq(lastID, null);
  do_check_true(server.hasDocument(reporter.serverNamespace, lastID));

  // Skip forward to next scheduled submission time.
  defineNow(policy, policy.nextDataSubmissionDate);
  promise = policy.checkStateAndTrigger();
  do_check_neq(promise, null);
  yield promise;
  do_check_neq(reporter.lastSubmitID, lastID);
  do_check_true(server.hasDocument(reporter.serverNamespace, reporter.lastSubmitID));
  do_check_false(server.hasDocument(reporter.serverNamespace, lastID));

  reporter._shutdown();
  yield shutdownServer(server);
});

add_task(function test_request_remote_data_deletion() {
  let [reporter, server] = yield getReporterAndServer("request_remote_data_deletion");

  let policy = reporter._policy;
  defineNow(policy, policy._futureDate(-24 * 60 * 60 * 1000));
  policy.recordUserAcceptance();
  defineNow(policy, policy.nextDataSubmissionDate);
  yield policy.checkStateAndTrigger();
  let id = reporter.lastSubmitID;
  do_check_neq(id, null);
  do_check_true(server.hasDocument(reporter.serverNamespace, id));

  defineNow(policy, policy._futureDate(10 * 1000));

  let promise = reporter.requestDeleteRemoteData();
  do_check_neq(promise, null);
  yield promise;
  do_check_null(reporter.lastSubmitID);
  do_check_false(reporter.haveRemoteData());
  do_check_false(server.hasDocument(reporter.serverNamespace, id));

  reporter._shutdown();
  yield shutdownServer(server);
});

add_task(function test_policy_accept_reject() {
  let [reporter, server] = yield getReporterAndServer("policy_accept_reject");

  let policy = reporter._policy;

  do_check_false(policy.dataSubmissionPolicyAccepted);
  do_check_false(reporter.willUploadData);

  policy.recordUserAcceptance();
  do_check_true(policy.dataSubmissionPolicyAccepted);
  do_check_true(reporter.willUploadData);

  policy.recordUserRejection();
  do_check_false(policy.dataSubmissionPolicyAccepted);
  do_check_false(reporter.willUploadData);

  reporter._shutdown();
  yield shutdownServer(server);
});

add_task(function test_upload_save_payload() {
  let [reporter, server] = yield getReporterAndServer("upload_save_payload");

  let deferred = Promise.defer();
  let request = new DataSubmissionRequest(deferred, new Date(), false);

  yield reporter._uploadData(request);
  let json = yield reporter.getLastPayload();
  do_check_true("thisPingDate" in json);

  reporter._shutdown();
  yield shutdownServer(server);
});


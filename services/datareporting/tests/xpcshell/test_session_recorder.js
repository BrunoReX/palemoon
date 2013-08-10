/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const {utils: Cu} = Components;

Cu.import("resource://gre/modules/commonjs/promise/core.js");
Cu.import("resource://gre/modules/services/datareporting/sessions.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://services-common/utils.js");


function run_test() {
  run_next_test();
}

function monkeypatchStartupInfo(recorder, start=new Date(), offset=500) {
  Object.defineProperty(recorder, "_getStartupInfo", {
    value: function _getStartupInfo() {
      return {
        process: start,
        main: new Date(start.getTime() + offset),
        firstPaint: new Date(start.getTime() + 2 * offset),
        sessionRestored: new Date(start.getTime() + 3 * offset),
      };
    }
  });
}

function sleep(wait) {
  let deferred = Promise.defer();

  let timer = CommonUtils.namedTimer(function onTimer() {
    deferred.resolve();
  }, wait, deferred.promise, "_sleepTimer");

  return deferred.promise;
}

function getRecorder(name, start, offset) {
  let recorder = new SessionRecorder("testing." + name + ".");
  monkeypatchStartupInfo(recorder, start, offset);

  return recorder;
}

add_test(function test_basic() {
  let recorder = getRecorder("basic");
  recorder.onStartup();
  recorder.onShutdown();

  run_next_test();
});

add_task(function test_current_properties() {
  let now = new Date();
  let recorder = getRecorder("current_properties", now);
  yield sleep(25);
  recorder.onStartup();

  do_check_eq(recorder.startDate.getTime(), now.getTime());
  do_check_eq(recorder.activeTicks, 0);
  do_check_true(recorder.totalTime > 0);
  do_check_eq(recorder.main, 500);
  do_check_eq(recorder.firstPaint, 1000);
  do_check_eq(recorder.sessionRestored, 1500);

  recorder.incrementActiveTicks();
  do_check_eq(recorder.activeTicks, 1);

  recorder.onShutdown();
});

add_task(function test_previous_clean() {
  let now = new Date();
  let recorder = getRecorder("previous_clean", now);
  yield sleep(25);
  recorder.onStartup();

  recorder.incrementActiveTicks();
  recorder.incrementActiveTicks();

  yield sleep(25);
  recorder.onShutdown();

  let total = recorder.totalTime;

  yield sleep(25);
  let now2 = new Date();
  let recorder2 = getRecorder("previous_clean", now2, 100);
  yield sleep(25);
  recorder2.onStartup();

  do_check_eq(recorder2.startDate.getTime(), now2.getTime());
  do_check_eq(recorder2.main, 100);
  do_check_eq(recorder2.firstPaint, 200);
  do_check_eq(recorder2.sessionRestored, 300);

  let sessions = recorder2.getPreviousSessions();
  do_check_eq(Object.keys(sessions).length, 1);
  do_check_true(0 in sessions);
  let session = sessions[0];
  do_check_true(session.clean);
  do_check_eq(session.startDate.getTime(), now.getTime());
  do_check_eq(session.main, 500);
  do_check_eq(session.firstPaint, 1000);
  do_check_eq(session.sessionRestored, 1500);
  do_check_eq(session.totalTime, total);
  do_check_eq(session.activeTicks, 2);

  recorder2.onShutdown();
});

add_task(function test_previous_abort() {
  let now = new Date();
  let recorder = getRecorder("previous_abort", now);
  yield sleep(25);
  recorder.onStartup();
  recorder.incrementActiveTicks();
  yield sleep(25);
  let total = recorder.totalTime;
  yield sleep(25);

  let now2 = new Date();
  let recorder2 = getRecorder("previous_abort", now2);
  yield sleep(25);
  recorder2.onStartup();

  let sessions = recorder2.getPreviousSessions();
  do_check_eq(Object.keys(sessions).length, 1);
  do_check_true(0 in sessions);
  let session = sessions[0];
  do_check_false(session.clean);
  do_check_eq(session.totalTime, total);

  recorder.onShutdown();
  recorder2.onShutdown();
});

add_task(function test_multiple_sessions() {
  for (let i = 0; i < 10; i++) {
    let recorder = getRecorder("multiple_sessions");
    yield sleep(25);
    recorder.onStartup();
    for (let j = 0; j < i; j++) {
      recorder.incrementActiveTicks();
    }
    yield sleep(25);
    recorder.onShutdown();
    yield sleep(25);
  }

  let recorder = getRecorder("multiple_sessions");
  recorder.onStartup();

  let sessions = recorder.getPreviousSessions();
  do_check_eq(Object.keys(sessions).length, 10);

  for (let [i, session] in Iterator(sessions)) {
    do_check_eq(session.activeTicks, i);

    if (i > 0) {
      do_check_true(session.startDate.getTime() > sessions[i-1].startDate.getTime());
    }
  }

  // #6 is preserved since >=.
  let threshold = sessions[6].startDate;
  recorder.pruneOldSessions(threshold);

  sessions = recorder.getPreviousSessions();
  do_check_eq(Object.keys(sessions).length, 4);

  recorder.pruneOldSessions(threshold);
  sessions = recorder.getPreviousSessions();
  do_check_eq(Object.keys(sessions).length, 4);
  do_check_eq(recorder._prunedIndex, 5);

  recorder.onShutdown();
});

add_task(function test_record_activity() {
  let recorder = getRecorder("record_activity");
  yield sleep(25);
  recorder.onStartup();
  let total = recorder.totalTime;
  yield sleep(25);

  for (let i = 0; i < 3; i++) {
    Services.obs.notifyObservers(null, "user-interaction-active", null);
    yield sleep(25);
    do_check_true(recorder.totalTime > total);
    total = recorder.totalTime;
  }

  do_check_eq(recorder.activeTicks, 3);

  // Now send inactive. We should increment total time but not active.
  Services.obs.notifyObservers(null, "user-interaction-inactive", null);
  do_check_eq(recorder.activeTicks, 3);
  do_check_true(recorder.totalTime > total);
  total = recorder.totalTime;
  yield sleep(25);

  // If we send active again, this should be counted as inactive.
  Services.obs.notifyObservers(null, "user-interaction-active", null);
  do_check_eq(recorder.activeTicks, 3);
  do_check_true(recorder.totalTime > total);
  total = recorder.totalTime;
  yield sleep(25);

  // If we send active again, this should be counted as active.
  Services.obs.notifyObservers(null, "user-interaction-active", null);
  do_check_eq(recorder.activeTicks, 4);

  Services.obs.notifyObservers(null, "user-interaction-active", null);
  do_check_eq(recorder.activeTicks, 5);

  recorder.onShutdown();
});


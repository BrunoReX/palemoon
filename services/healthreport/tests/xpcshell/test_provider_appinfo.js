/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const {interfaces: Ci, results: Cr, utils: Cu} = Components;

Cu.import("resource://gre/modules/Metrics.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/services/healthreport/providers.jsm");
Cu.import("resource://testing-common/services/healthreport/utils.jsm");


function run_test() {
  run_next_test();
}

add_test(function test_constructor() {
  let provider = new AppInfoProvider();

  run_next_test();
});

add_task(function test_collect_smoketest() {
  let storage = yield Metrics.Storage("collect_smoketest");
  let provider = new AppInfoProvider();
  yield provider.init(storage);

  let now = new Date();
  yield provider.collectConstantData();

  let m = provider.getMeasurement("appinfo", 1);
  let data = yield storage.getMeasurementValues(m.id);
  let serializer = m.serializer(m.SERIALIZE_JSON);
  let d = serializer.singular(data.singular);

  do_check_eq(d.vendor, "Mozilla");
  do_check_eq(d.name, "xpcshell");
  do_check_eq(d.id, "xpcshell@tests.mozilla.org");
  do_check_eq(d.version, "1");
  do_check_eq(d.appBuildID, "20121107");
  do_check_eq(d.platformVersion, "p-ver");
  do_check_eq(d.platformBuildID, "20121106");
  do_check_eq(d.os, "XPCShell");
  do_check_eq(d.xpcomabi, "noarch-spidermonkey");

  do_check_eq(data.days.size, 1);
  do_check_true(data.days.hasDay(now));
  let day = data.days.getDay(now);
  do_check_eq(day.size, 1);
  do_check_true(day.has("isDefaultBrowser"));

  // TODO Bug 827189 Actually test this properly. On some local builds, this
  // is always -1 (the service throws). On buildbot, it seems to always be 0.
  do_check_neq(day.get("isDefaultBrowser"), 1);

  yield provider.shutdown();
  yield storage.close();
});

add_task(function test_record_version() {
  let storage = yield Metrics.Storage("record_version");

  let provider = new AppInfoProvider();
  let now = new Date();
  yield provider.init(storage);

  // The provider records information on startup.
  let m = provider.getMeasurement("versions", 1);
  let data = yield m.getValues();

  do_check_true(data.days.hasDay(now));
  let day = data.days.getDay(now);
  do_check_eq(day.size, 1);
  do_check_true(day.has("version"));
  let value = day.get("version");
  do_check_true(Array.isArray(value));
  do_check_eq(value.length, 1);
  let ai = getAppInfo();
  do_check_eq(value, ai.version);

  yield provider.shutdown();
  yield storage.close();
});

add_task(function test_record_version_change() {
  let storage = yield Metrics.Storage("record_version_change");

  let provider = new AppInfoProvider();
  let now = new Date();
  yield provider.init(storage);
  yield provider.shutdown();

  let ai = getAppInfo();
  ai.version = "2";
  updateAppInfo(ai);

  provider = new AppInfoProvider();
  yield provider.init(storage);

  // There should be 2 records in the versions history.
  let m = provider.getMeasurement("versions", 1);
  let data = yield m.getValues();
  do_check_true(data.days.hasDay(now));
  let day = data.days.getDay(now);
  let value = day.get("version");
  do_check_true(Array.isArray(value));
  do_check_eq(value.length, 2);
  do_check_eq(value[1], "2");

  yield provider.shutdown();
  yield storage.close();
});

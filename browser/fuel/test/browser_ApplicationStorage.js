function test() {
  // test for existence of values
  var hasItem = Application.storage.has("fuel-test-missing");
  is(hasItem, false, "Check 'Application.storage.has' for non-existing item");
  Application.storage.set("fuel-test", "dummy");
  hasItem = Application.storage.has("fuel-test");
  is(hasItem, true, "Check 'Application.storage.has' for existing item");

  // test getting non-existing and existing values
  var itemValue = Application.storage.get("fuel-test-missing", "default");
  is(itemValue, "default", "Check 'Application.storage.get' for non-existing item");
  itemValue = Application.storage.get("fuel-test", "default");
  is(itemValue, "dummy", "Check 'Application.storage.get' for existing item");

  // test for overwriting an existing value
  Application.storage.set("fuel-test", "smarty");
  itemValue = Application.storage.get("fuel-test", "default");
  is(itemValue, "smarty", "Check 'Application.storage.get' for overwritten item");

  // check for change event when setting a value
  waitForExplicitFinish();
  Application.storage.events.addListener("change", onStorageChange);
  Application.storage.set("fuel-test", "change event");
}

function onStorageChange(evt) {
  is(evt.data, "fuel-test", "Check 'Application.storage.set' fired a change event");
  Application.storage.events.removeListener("change", onStorageChange);
  finish();
}

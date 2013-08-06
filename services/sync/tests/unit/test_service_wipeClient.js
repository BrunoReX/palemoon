Cu.import("resource://services-sync/record.js");
Cu.import("resource://services-sync/engines.js");
Cu.import("resource://services-sync/util.js");

Svc.DefaultPrefs.set("registerEngines", "");
Cu.import("resource://services-sync/service.js");


function CanDecryptEngine() {
  SyncEngine.call(this, "CanDecrypt");
}
CanDecryptEngine.prototype = {
  __proto__: SyncEngine.prototype,

  // Override these methods with mocks for the test
  canDecrypt: function canDecrypt() {
    return true;
  },

  wasWiped: false,
  wipeClient: function wipeClient() {
    this.wasWiped = true;
  }
};
Engines.register(CanDecryptEngine);


function CannotDecryptEngine() {
  SyncEngine.call(this, "CannotDecrypt");
}
CannotDecryptEngine.prototype = {
  __proto__: SyncEngine.prototype,

  // Override these methods with mocks for the test
  canDecrypt: function canDecrypt() {
    return false;
  },

  wasWiped: false,
  wipeClient: function wipeClient() {
    this.wasWiped = true;
  }
};
Engines.register(CannotDecryptEngine);


function test_withEngineList() {
  try {
    _("Ensure initial scenario.");
    do_check_false(Engines.get("candecrypt").wasWiped);
    do_check_false(Engines.get("cannotdecrypt").wasWiped);
    
    _("Wipe local engine data.");
    Service.wipeClient(["candecrypt", "cannotdecrypt"]);

    _("Ensure only the engine that can decrypt was wiped.");
    do_check_true(Engines.get("candecrypt").wasWiped);
    do_check_false(Engines.get("cannotdecrypt").wasWiped);
  } finally {
    Engines.get("candecrypt").wasWiped = false;
    Engines.get("cannotdecrypt").wasWiped = false;
    Service.startOver();
  }
}

function test_startOver_clears_keys() {
  CollectionKeys.generateNewKeys();
  do_check_true(!!CollectionKeys.keyForCollection());
  Service.startOver();
  do_check_false(!!CollectionKeys.keyForCollection());
}

function run_test() {
  test_withEngineList();
  test_startOver_clears_keys();
}

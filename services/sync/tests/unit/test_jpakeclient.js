Cu.import("resource://services-sync/log4moz.js");
Cu.import("resource://services-sync/identity.js");
Cu.import("resource://services-sync/jpakeclient.js");
Cu.import("resource://services-sync/constants.js");
Cu.import("resource://services-sync/util.js");

const JPAKE_LENGTH_SECRET     = 8;
const JPAKE_LENGTH_CLIENTID   = 256;

/*
 * Simple server.
 */

function check_headers(request) {
  // There shouldn't be any Basic auth
  do_check_false(request.hasHeader("Authorization"));

  // Ensure key exchange ID is set and the right length
  do_check_true(request.hasHeader("X-KeyExchange-Id"));
  do_check_eq(request.getHeader("X-KeyExchange-Id").length,
              JPAKE_LENGTH_CLIENTID);
}

function new_channel() {
  // Create a new channel and register it with the server.
  let cid = Math.floor(Math.random() * 10000);
  while (channels[cid])
    cid = Math.floor(Math.random() * 10000);
  let channel = channels[cid] = new ServerChannel();
  server.registerPathHandler("/" + cid, channel.handler());
  return cid;
}

let server;
let channels = {};  // Map channel -> ServerChannel object
function server_new_channel(request, response) {
  check_headers(request);
  let cid = new_channel();
  let body = JSON.stringify("" + cid);
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.bodyOutputStream.write(body, body.length);
}

let error_report;
function server_report(request, response) {
  check_headers(request);

  if (request.hasHeader("X-KeyExchange-Log"))
    error_report = request.getHeader("X-KeyExchange-Log");

  if (request.hasHeader("X-KeyExchange-Cid")) {
    let cid = request.getHeader("X-KeyExchange-Cid");
    let channel = channels[cid];
    if (channel)
      channel.clear();
  }

  response.setStatusLine(request.httpVersion, 200, "OK");
}

function ServerChannel() {
  this.data = "{}";
  this.getCount = 0;
}
ServerChannel.prototype = {

  GET: function GET(request, response) {
    if (!this.data) {
      response.setStatusLine(request.httpVersion, 404, "Not Found");
      return;
    }
    if (request.hasHeader("If-None-Match")) {
      let etag = request.getHeader("If-None-Match");
      if (etag == this._etag) {
        response.setStatusLine(request.httpVersion, 304, "Not Modified");
        return;
      }
    }
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.bodyOutputStream.write(this.data, this.data.length);

    // Automatically clear the channel after 6 successful GETs.
    this.getCount += 1;
    if (this.getCount == 6)
      this.clear();
  },

  PUT: function PUT(request, response) {
    this.data = readBytesFromInputStream(request.bodyInputStream);
    this._etag = '"' + Utils.sha1(this.data) + '"';
    response.setHeader("ETag", this._etag);
    response.setStatusLine(request.httpVersion, 200, "OK");
  },

  clear: function clear() {
    delete this.data;
  },

  handler: function handler() {
    let self = this;
    return function(request, response) {
      check_headers(request);
      let method = self[request.method];
      return method.apply(self, arguments);
    };
  }

};


const DATA = {"msg": "eggstreamly sekrit"};
const POLLINTERVAL = 50;

function run_test() {
  Svc.Prefs.set("jpake.serverURL", "http://localhost:8080/");
  Svc.Prefs.set("jpake.pollInterval", POLLINTERVAL);
  Svc.Prefs.set("jpake.maxTries", 5);
  Svc.Prefs.set("jpake.firstMsgMaxTries", 5);
  // Ensure clean up
  Svc.Obs.add("profile-before-change", function() {
    Svc.Prefs.resetBranch("");
  });

  // Ensure PSM is initialized.
  Cc["@mozilla.org/psm;1"].getService(Ci.nsISupports);

  // Simulate Sync setup with credentials in place. We want to make
  // sure the J-PAKE requests don't include those data.
  let id = new Identity(PWDMGR_PASSWORD_REALM, "johndoe");
  id.password = "ilovejane";
  ID.set("WeaveID", id);

  server = httpd_setup({"/new_channel": server_new_channel,
                        "/report":      server_report});

  initTestLogging("Trace");
  run_next_test();
}


add_test(function test_success_receiveNoPIN() {
  _("Test a successful exchange started by receiveNoPIN().");

  let snd = new JPAKEClient({
    displayPIN: function displayPIN() {
      do_throw("displayPIN shouldn't have been called!");
    },
    onAbort: function onAbort(error) {
      do_throw("Shouldn't have aborted!" + error);
    },
    onComplete: function onComplete() {}
  });

  let rec = new JPAKEClient({
    displayPIN: function displayPIN(pin) {
      _("Received PIN " + pin + ". Entering it in the other computer...");
      this.cid = pin.slice(JPAKE_LENGTH_SECRET);
      Utils.nextTick(function() { snd.sendWithPIN(pin, DATA); });
    },
    onAbort: function onAbort(error) {
      do_throw("Shouldn't have aborted! " + error);
    },
    onComplete: function onComplete(a) {
      // Ensure channel was cleared, no error report.
      do_check_eq(channels[this.cid].data, undefined);
      do_check_eq(error_report, undefined);
      run_next_test();
    }
  });
  rec.receiveNoPIN();
});


add_test(function test_firstMsgMaxTries() {
  _("Test abort when sender doesn't upload anything.");

  let rec = new JPAKEClient({
    displayPIN: function displayPIN(pin) {
      _("Received PIN " + pin + ". Doing nothing...");
      this.cid = pin.slice(JPAKE_LENGTH_SECRET);
    },
    onAbort: function onAbort(error) {
      do_check_eq(error, JPAKE_ERROR_TIMEOUT);
      // Ensure channel was cleared, error report was sent.
      do_check_eq(channels[this.cid].data, undefined);
      do_check_eq(error_report, JPAKE_ERROR_TIMEOUT);
      error_report = undefined;
      run_next_test();
    },
    onComplete: function onComplete() {
      do_throw("Shouldn't have completed! ");
    }
  });
  rec.receiveNoPIN();
});


add_test(function test_wrongPIN() {
  _("Test abort when PINs don't match.");

  let snd = new JPAKEClient({
    displayPIN: function displayPIN() {
      do_throw("displayPIN shouldn't have been called!");
    },
    onAbort: function onAbort(error) {
      do_check_eq(error, JPAKE_ERROR_KEYMISMATCH);
      do_check_eq(error_report, JPAKE_ERROR_KEYMISMATCH);
      error_report = undefined;
    },
    onComplete: function onComplete() {
      do_throw("Shouldn't have completed!");
    }
  });

  let rec = new JPAKEClient({
    displayPIN: function displayPIN(pin) {
      this.cid = pin.slice(JPAKE_LENGTH_SECRET);
      let secret = pin.slice(0, JPAKE_LENGTH_SECRET);
      secret = [char for each (char in secret)].reverse().join("");
      let new_pin = secret + this.cid;
      _("Received PIN " + pin + ", but I'm entering " + new_pin);

      Utils.nextTick(function() { snd.sendWithPIN(new_pin, DATA); });
    },
    onAbort: function onAbort(error) {
      do_check_eq(error, JPAKE_ERROR_NODATA);
      // Ensure channel was cleared.
      do_check_eq(channels[this.cid].data, undefined);
      run_next_test();
    },
    onComplete: function onComplete() {
      do_throw("Shouldn't have completed! ");
    }
  });
  rec.receiveNoPIN();
});


add_test(function test_abort_receiver() {
  _("Test user abort on receiving side.");

  let rec = new JPAKEClient({
    onComplete: function onComplete(data) {
      do_throw("onComplete shouldn't be called.");
    },
    onAbort: function onAbort(error) {
      // Manual abort = userabort.
      do_check_eq(error, JPAKE_ERROR_USERABORT);
      // Ensure channel was cleared.
      do_check_eq(channels[this.cid].data, undefined);
      do_check_eq(error_report, JPAKE_ERROR_USERABORT);
      error_report = undefined;
      run_next_test();
    },
    displayPIN: function displayPIN(pin) {
      this.cid = pin.slice(JPAKE_LENGTH_SECRET);
      Utils.nextTick(function() { rec.abort(); });
    }
  });
  rec.receiveNoPIN();
});


add_test(function test_abort_sender() {
  _("Test user abort on sending side.");

  let snd = new JPAKEClient({
    displayPIN: function displayPIN() {
      do_throw("displayPIN shouldn't have been called!");
    },
    onAbort: function onAbort(error) {
      // Manual abort == userabort.
      do_check_eq(error, JPAKE_ERROR_USERABORT);
      do_check_eq(error_report, JPAKE_ERROR_USERABORT);
      error_report = undefined;
    },
    onComplete: function onComplete() {
      do_throw("Shouldn't have completed!");
    }
  });

  let rec = new JPAKEClient({
    onComplete: function onComplete(data) {
      do_throw("onComplete shouldn't be called.");
    },
    onAbort: function onAbort(error) {
      do_check_eq(error, JPAKE_ERROR_NODATA);
      // Ensure channel was cleared, no error report.
      do_check_eq(channels[this.cid].data, undefined);
      do_check_eq(error_report, undefined);
      run_next_test();
    },
    displayPIN: function displayPIN(pin) {
      _("Received PIN " + pin + ". Entering it in the other computer...");
      this.cid = pin.slice(JPAKE_LENGTH_SECRET);
      Utils.nextTick(function() { snd.sendWithPIN(pin, DATA); });
      Utils.namedTimer(function() { snd.abort(); },
                       POLLINTERVAL, this, "_abortTimer");
    }
  });
  rec.receiveNoPIN();
});


add_test(function test_wrongmessage() {
  let cid = new_channel();
  channels[cid].data = JSON.stringify({type: "receiver2", payload: {}});
  let snd = new JPAKEClient({
    onComplete: function onComplete(data) {
      do_throw("onComplete shouldn't be called.");
    },
    onAbort: function onAbort(error) {
      do_check_eq(error, JPAKE_ERROR_WRONGMESSAGE);
      run_next_test();
    }
  });
  snd.sendWithPIN("01234567" + cid, DATA);
});


add_test(function test_error_channel() {
  Svc.Prefs.set("jpake.serverURL", "http://localhost:12345/");

  let rec = new JPAKEClient({
    onComplete: function onComplete(data) {
      do_throw("onComplete shouldn't be called.");
    },
    onAbort: function onAbort(error) {
      do_check_eq(error, JPAKE_ERROR_CHANNEL);
      Svc.Prefs.reset("jpake.serverURL");
      run_next_test();
    },
    displayPIN: function displayPIN(pin) {}
  });
  rec.receiveNoPIN();
});


add_test(function test_error_network() {
  Svc.Prefs.set("jpake.serverURL", "http://localhost:12345/");

  let snd = new JPAKEClient({
    onComplete: function onComplete(data) {
      do_throw("onComplete shouldn't be called.");
    },
    onAbort: function onAbort(error) {
      do_check_eq(error, JPAKE_ERROR_NETWORK);
      Svc.Prefs.reset("jpake.serverURL");
      run_next_test();
    }
  });
  snd.sendWithPIN("0123456789ab", DATA);
});


add_test(function tearDown() {
  server.stop(run_next_test);
});

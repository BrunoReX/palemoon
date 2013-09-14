/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;
const Cr = Components.results;
const CC = Components.Constructor;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const InputStreamPump = CC(
        "@mozilla.org/network/input-stream-pump;1", "nsIInputStreamPump", "init"),
      AsyncStreamCopier = CC(
        "@mozilla.org/network/async-stream-copier;1", "nsIAsyncStreamCopier", "init"),
      ScriptableInputStream = CC(
        "@mozilla.org/scriptableinputstream;1", "nsIScriptableInputStream", "init"),
      BinaryInputStream = CC(
        "@mozilla.org/binaryinputstream;1", "nsIBinaryInputStream", "setInputStream"),
      StringInputStream = CC(
        '@mozilla.org/io/string-input-stream;1', 'nsIStringInputStream'),
      ArrayBufferInputStream = CC(
        '@mozilla.org/io/arraybuffer-input-stream;1', 'nsIArrayBufferInputStream'),
      MultiplexInputStream = CC(
        '@mozilla.org/io/multiplex-input-stream;1', 'nsIMultiplexInputStream');

const kCONNECTING = 'connecting';
const kOPEN = 'open';
const kCLOSING = 'closing';
const kCLOSED = 'closed';

const BUFFER_SIZE = 65536;

// XXX we have no TCPError implementation right now because it's really hard to
// do on b2g18.  On mozilla-central we want a proper TCPError that ideally
// sub-classes DOMError.  Bug 867872 has been filed to implement this and
// contains a documented TCPError.webidl that maps all the error codes we use in
// this file to slightly more readable explanations.
function createTCPError(aWindow, aErrorName, aErrorType) {
  return new (aWindow ? aWindow.DOMError : DOMError)(aErrorName);
}


/*
 * Debug logging function
 */

let debug = false;
function LOG(msg) {
  if (debug)
    dump("TCPSocket: " + msg + "\n");
}

/*
 * nsITCPSocketEvent object
 */

function TCPSocketEvent(type, sock, data) {
  this._type = type;
  this._target = sock;
  this._data = data;
}

TCPSocketEvent.prototype = {
  __exposedProps__: {
    type: 'r',
    target: 'r',
    data: 'r'
  },
  get type() {
    return this._type;
  },
  get target() {
    return this._target;
  },
  get data() {
    return this._data;
  }
}

/*
 * nsIDOMTCPSocket object
 */

function TCPSocket() {
  this._readyState = kCLOSED;

  this._onopen = null;
  this._ondrain = null;
  this._ondata = null;
  this._onerror = null;
  this._onclose = null;

  this._binaryType = "string";

  this._host = "";
  this._port = 0;
  this._ssl = false;

  this.useWin = null;
}

TCPSocket.prototype = {
  __exposedProps__: {
    open: 'r',
    host: 'r',
    port: 'r',
    ssl: 'r',
    bufferedAmount: 'r',
    suspend: 'r',
    resume: 'r',
    close: 'r',
    send: 'r',
    readyState: 'r',
    binaryType: 'r',
    onopen: 'rw',
    ondrain: 'rw',
    ondata: 'rw',
    onerror: 'rw',
    onclose: 'rw'
  },
  // The binary type, "string" or "arraybuffer"
  _binaryType: null,

  // Internal
  _hasPrivileges: null,

  // Raw socket streams
  _transport: null,
  _socketInputStream: null,
  _socketOutputStream: null,

  // Input stream machinery
  _inputStreamPump: null,
  _inputStreamScriptable: null,
  _inputStreamBinary: null,

  // Output stream machinery
  _multiplexStream: null,
  _multiplexStreamCopier: null,

  _asyncCopierActive: false,
  _waitingForDrain: false,
  _suspendCount: 0,

  // Reported parent process buffer
  _bufferedAmount: 0,

  // IPC socket actor
  _socketBridge: null,

  // Public accessors.
  get readyState() {
    return this._readyState;
  },
  get binaryType() {
    return this._binaryType;
  },
  get host() {
    return this._host;
  },
  get port() {
    return this._port;
  },
  get ssl() {
    return this._ssl;
  },
  get bufferedAmount() {
    if (this._inChild) {
      return this._bufferedAmount;
    }
    return this._multiplexStream.available();
  },
  get onopen() {
    return this._onopen;
  },
  set onopen(f) {
    this._onopen = f;
  },
  get ondrain() {
    return this._ondrain;
  },
  set ondrain(f) {
    this._ondrain = f;
  },
  get ondata() {
    return this._ondata;
  },
  set ondata(f) {
    this._ondata = f;
  },
  get onerror() {
    return this._onerror;
  },
  set onerror(f) {
    this._onerror = f;
  },
  get onclose() {
    return this._onclose;
  },
  set onclose(f) {
    this._onclose = f;
  },

  // Helper methods.
  _createTransport: function ts_createTransport(host, port, sslMode) {
    let options, optlen;
    if (sslMode) {
      options = [sslMode];
      optlen = 1;
    } else {
      options = null;
      optlen = 0;
    }
    return Cc["@mozilla.org/network/socket-transport-service;1"]
             .getService(Ci.nsISocketTransportService)
             .createTransport(options, optlen, host, port, null);
  },

  _ensureCopying: function ts_ensureCopying() {
    let self = this;
    if (this._asyncCopierActive) {
      return;
    }
    this._asyncCopierActive = true;
    this._multiplexStreamCopier.asyncCopy({
      onStartRequest: function ts_output_onStartRequest() {
      },
      onStopRequest: function ts_output_onStopRequest(request, context, status) {
        self._asyncCopierActive = false;
        self._multiplexStream.removeStream(0);

        if (!Components.isSuccessCode(status)) {
          // Note that we can/will get an error here as well as in the
          // onStopRequest for inbound data.
          self._maybeReportErrorAndCloseIfOpen(status);
          return;
        }

        if (self._multiplexStream.count) {
          self._ensureCopying();
        } else {
          if (self._waitingForDrain) {
            self._waitingForDrain = false;
            self.callListener("drain");
          }
          if (self._readyState === kCLOSING) {
            self._socketOutputStream.close();
            self._readyState = kCLOSED;
            self.callListener("close");
          }
        }
      }
    }, null);
  },

  callListener: function ts_callListener(type, data) {
    if (!this["on" + type])
      return;

    this["on" + type].call(null, new TCPSocketEvent(type, this, data || ""));
  },

  /* nsITCPSocketInternal methods */
  callListenerError: function ts_callListenerError(type, name) {
    // XXX we're not really using TCPError at this time, so there's only a name
    // attribute to pass.
    this.callListener(type, createTCPError(this.useWin, name));
  },

  callListenerData: function ts_callListenerString(type, data) {
    this.callListener(type, data);
  },

  callListenerArrayBuffer: function ts_callListenerArrayBuffer(type, data) {
    this.callListener(type, data);
  },

  callListenerVoid: function ts_callListenerVoid(type) {
    this.callListener(type);
  },

  updateReadyStateAndBuffered: function ts_setReadyState(readyState, bufferedAmount) {
    this._readyState = readyState;
    this._bufferedAmount = bufferedAmount;
  },
  /* end nsITCPSocketInternal methods */

  initWindowless: function ts_initWindowless() {
    return Services.prefs.getBoolPref("dom.mozTCPSocket.enabled");
  },

  init: function ts_init(aWindow) {
    if (!this.initWindowless())
      return null;

    let principal = aWindow.document.nodePrincipal;
    let secMan = Cc["@mozilla.org/scriptsecuritymanager;1"]
                   .getService(Ci.nsIScriptSecurityManager);

    let perm = principal == secMan.getSystemPrincipal()
                 ? Ci.nsIPermissionManager.ALLOW_ACTION
                 : Services.perms.testExactPermissionFromPrincipal(principal, "tcp-socket");

    this._hasPrivileges = perm == Ci.nsIPermissionManager.ALLOW_ACTION;

    let util = aWindow.QueryInterface(
      Ci.nsIInterfaceRequestor
    ).getInterface(Ci.nsIDOMWindowUtils);

    this.useWin = XPCNativeWrapper.unwrap(aWindow);
    this.innerWindowID = util.currentInnerWindowID;
    LOG("window init: " + this.innerWindowID);
  },

  observe: function(aSubject, aTopic, aData) {
    if (aTopic == "inner-window-destroyed") {
      let wId = aSubject.QueryInterface(Ci.nsISupportsPRUint64).data;
      if (wId == this.innerWindowID) {
        LOG("inner-window-destroyed: " + this.innerWindowID);

        // This window is now dead, so we want to clear the callbacks
        // so that we don't get a "can't access dead object" when the
        // underlying stream goes to tell us that we are closed
        this.onopen = null;
        this.ondrain = null;
        this.ondata = null;
        this.onerror = null;
        this.onclose = null;

        this.useWin = null;

        // Clean up our socket
        this.close();
      }
    }
  },

  // nsIDOMTCPSocket
  open: function ts_open(host, port, options) {
    if (!this.initWindowless())
      return null;

    this._inChild = Cc["@mozilla.org/xre/app-info;1"].getService(Ci.nsIXULRuntime)
                       .processType != Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT;
    LOG("content process: " + (this._inChild ? "true" : "false"));

    // in the testing case, init won't be called and
    // hasPrivileges will be null. We want to proceed to test.
    if (this._hasPrivileges !== true && this._hasPrivileges !== null) {
      throw new Error("TCPSocket does not have permission in this context.\n");
    }
    let that = new TCPSocket();

    that.useWin = this.useWin;
    that.innerWindowID = this.innerWindowID;
    that._inChild = this._inChild;

    LOG("window init: " + that.innerWindowID);
    Services.obs.addObserver(that, "inner-window-destroyed", true);

    LOG("startup called");
    LOG("Host info: " + host + ":" + port);

    that._readyState = kCONNECTING;
    that._host = host;
    that._port = port;
    if (options !== undefined) {
      if (options.useSSL) {
          that._ssl = 'ssl';
      } else {
          that._ssl = false;
      }
      that._binaryType = options.binaryType || that._binaryType;
    }

    LOG("SSL: " + that.ssl);

    if (this._inChild) {
      that._socketBridge = Cc["@mozilla.org/tcp-socket-child;1"]
                             .createInstance(Ci.nsITCPSocketChild);
      that._socketBridge.open(that, host, port, !!that._ssl,
                              that._binaryType, this.useWin, this.useWin || this);
      return that;
    }

    let transport = that._transport = this._createTransport(host, port, that._ssl);
    transport.setEventSink(that, Services.tm.currentThread);

    that._socketInputStream = transport.openInputStream(0, 0, 0);
    that._socketOutputStream = transport.openOutputStream(
      Ci.nsITransport.OPEN_UNBUFFERED, 0, 0);

    // If the other side is not listening, we will
    // get an onInputStreamReady callback where available
    // raises to indicate the connection was refused.
    that._socketInputStream.asyncWait(
      that, that._socketInputStream.WAIT_CLOSURE_ONLY, 0, Services.tm.currentThread);

    if (that._binaryType === "arraybuffer") {
      that._inputStreamBinary = new BinaryInputStream(that._socketInputStream);
    } else {
      that._inputStreamScriptable = new ScriptableInputStream(that._socketInputStream);
    }

    that._multiplexStream = new MultiplexInputStream();

    that._multiplexStreamCopier = new AsyncStreamCopier(
      that._multiplexStream,
      that._socketOutputStream,
      // (nsSocketTransport uses gSocketTransportService)
      Cc["@mozilla.org/network/socket-transport-service;1"]
        .getService(Ci.nsIEventTarget),
      /* source buffered */ true, /* sink buffered */ false,
      BUFFER_SIZE, /* close source*/ false, /* close sink */ false);

    return that;
  },

  close: function ts_close() {
    if (this._readyState === kCLOSED || this._readyState === kCLOSING)
      return;

    LOG("close called");
    this._readyState = kCLOSING;

    if (this._inChild) {
      this._socketBridge.close();
      return;
    }

    if (!this._multiplexStream.count) {
      this._socketOutputStream.close();
    }
    this._socketInputStream.close();
  },

  send: function ts_send(data, byteOffset, byteLength) {
    if (this._readyState !== kOPEN) {
      throw new Error("Socket not open.");
    }

    if (this._binaryType === "arraybuffer") {
      byteLength = byteLength || data.byteLength;
    }

    if (this._inChild) {
      this._socketBridge.send(data, byteOffset, byteLength);
    }

    let length = this._binaryType === "arraybuffer" ? byteLength : data.length;

    var newBufferedAmount = this.bufferedAmount + length;
    var bufferNotFull = newBufferedAmount < BUFFER_SIZE;
    if (this._inChild) {
      return bufferNotFull;
    }

    let new_stream;
    if (this._binaryType === "arraybuffer") {
      new_stream = new ArrayBufferInputStream();
      new_stream.setData(data, byteOffset, byteLength);
    } else {
      new_stream = new StringInputStream();
      new_stream.setData(data, length);
    }
    this._multiplexStream.appendStream(new_stream);

    if (newBufferedAmount >= BUFFER_SIZE) {
      // If we buffered more than some arbitrary amount of data,
      // (65535 right now) we should tell the caller so they can
      // wait until ondrain is called if they so desire. Once all the
      //buffered data has been written to the socket, ondrain is
      // called.
      this._waitingForDrain = true;
    }

    this._ensureCopying();
    return bufferNotFull;
  },

  suspend: function ts_suspend() {
    if (this._inChild) {
      this._socketBridge.suspend();
      return;
    }

    if (this._inputStreamPump) {
      this._inputStreamPump.suspend();
    } else {
      ++this._suspendCount;
    }
  },

  resume: function ts_resume() {
    if (this._inChild) {
      this._socketBridge.resume();
      return;
    }

    if (this._inputStreamPump) {
      this._inputStreamPump.resume();
    } else {
      --this._suspendCount;
    }
  },

  _maybeReportErrorAndCloseIfOpen: function(status) {
    // If we're closed, we've already reported the error or just don't need to
    // report the error.
    if (this._readyState === kCLOSED)
      return;
    this._readyState = kCLOSED;

    if (!Components.isSuccessCode(status)) {
      // Convert the status code to an appropriate error message.  Raw constants
      // are used inline in all cases for consistency.  Some error codes are
      // available in Components.results, some aren't.  Network error codes are
      // effectively stable, NSS error codes are officially not, but we have no
      // symbolic way to dynamically resolve them anyways (other than an ability
      // to determine the error class.)
      let errName, errType;
      // security module? (and this is an error)
      if ((status & 0xff0000) === 0x5a0000) {
        const nsINSSErrorsService = Ci.nsINSSErrorsService;
        let nssErrorsService = Cc['@mozilla.org/nss_errors_service;1']
                                 .getService(nsINSSErrorsService);
        let errorClass;
        // getErrorClass will throw a generic NS_ERROR_FAILURE if the error code is
        // somehow not in the set of covered errors.
        try {
          errorClass = nssErrorsService.getErrorClass(status);
        }
        catch (ex) {
          errorClass = 'SecurityProtocol';
        }
        switch (errorClass) {
          case nsINSSErrorsService.ERROR_CLASS_SSL_PROTOCOL:
            errType = 'SecurityProtocol';
            break;
          case nsINSSErrorsService.ERROR_CLASS_BAD_CERT:
            errType = 'SecurityCertificate';
            break;
          // no default is required; the platform impl automatically defaults to
          // ERROR_CLASS_SSL_PROTOCOL.
        }

        // NSS_SEC errors (happen below the base value because of negative vals)
        if ((status & 0xffff) <
            Math.abs(nsINSSErrorsService.NSS_SEC_ERROR_BASE)) {
          // The bases are actually negative, so in our positive numeric space, we
          // need to subtract the base off our value.
          let nssErr = Math.abs(nsINSSErrorsService.NSS_SEC_ERROR_BASE) -
                         (status & 0xffff);
          switch (nssErr) {
            case 11: // SEC_ERROR_EXPIRED_CERTIFICATE, sec(11)
              errName = 'SecurityExpiredCertificateError';
              break;
            case 12: // SEC_ERROR_REVOKED_CERTIFICATE, sec(12)
              errName = 'SecurityRevokedCertificateError';
              break;
            // per bsmith, we will be unable to tell these errors apart very soon,
            // so it makes sense to just folder them all together already.
            case 13: // SEC_ERROR_UNKNOWN_ISSUER, sec(13)
            case 20: // SEC_ERROR_UNTRUSTED_ISSUER, sec(20)
            case 21: // SEC_ERROR_UNTRUSTED_CERT, sec(21)
            case 36: // SEC_ERROR_CA_CERT_INVALID, sec(36)
              errName = 'SecurityUntrustedCertificateIssuerError';
              break;
            case 90: // SEC_ERROR_INADEQUATE_KEY_USAGE, sec(90)
              errName = 'SecurityInadequateKeyUsageError';
              break;
            case 176: // SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED, sec(176)
              errName = 'SecurityCertificateSignatureAlgorithmDisabledError';
              break;
            default:
              errName = 'SecurityError';
              break;
          }
        }
        // NSS_SSL errors
        else {
          let sslErr = Math.abs(nsINSSErrorsService.NSS_SSL_ERROR_BASE) -
                         (status & 0xffff);
          switch (sslErr) {
            case 3: // SSL_ERROR_NO_CERTIFICATE, ssl(3)
              errName = 'SecurityNoCertificateError';
              break;
            case 4: // SSL_ERROR_BAD_CERTIFICATE, ssl(4)
              errName = 'SecurityBadCertificateError';
              break;
            case 8: // SSL_ERROR_UNSUPPORTED_CERTIFICATE_TYPE, ssl(8)
              errName = 'SecurityUnsupportedCertificateTypeError';
              break;
            case 9: // SSL_ERROR_UNSUPPORTED_VERSION, ssl(9)
              errName = 'SecurityUnsupportedTLSVersionError';
              break;
            case 12: // SSL_ERROR_BAD_CERT_DOMAIN, ssl(12)
              errName = 'SecurityCertificateDomainMismatchError';
              break;
            default:
              errName = 'SecurityError';
              break;
          }
        }
      }
      // must be network
      else {
        errType = 'Network';
        switch (status) {
          // connect to host:port failed
          case 0x804B000C: // NS_ERROR_CONNECTION_REFUSED, network(13)
            errName = 'ConnectionRefusedError';
            break;
          // network timeout error
          case 0x804B000E: // NS_ERROR_NET_TIMEOUT, network(14)
            errName = 'NetworkTimeoutError';
            break;
          // hostname lookup failed
          case 0x804B001E: // NS_ERROR_UNKNOWN_HOST, network(30)
            errName = 'DomainNotFoundError';
            break;
          case 0x804B0047: // NS_ERROR_NET_INTERRUPT, network(71)
            errName = 'NetworkInterruptError';
            break;
          default:
            errName = 'NetworkError';
            break;
        }
      }
      let err = createTCPError(this.useWin, errName, errType);
      this.callListener("error", err);
    }
    this.callListener("close");
  },

  // nsITransportEventSink (Triggered by transport.setEventSink)
  onTransportStatus: function ts_onTransportStatus(
    transport, status, progress, max) {
    if (status === Ci.nsISocketTransport.STATUS_CONNECTED_TO) {
      this._readyState = kOPEN;
      this.callListener("open");

      this._inputStreamPump = new InputStreamPump(
        this._socketInputStream, -1, -1, 0, 0, false
      );

      while (this._suspendCount--) {
        this._inputStreamPump.suspend();
      }

      this._inputStreamPump.asyncRead(this, null);
    }
  },

  // nsIAsyncInputStream (Triggered by _socketInputStream.asyncWait)
  // Only used for detecting connection refused
  onInputStreamReady: function ts_onInputStreamReady(input) {
    try {
      input.available();
    } catch (e) {
      // NS_ERROR_CONNECTION_REFUSED
      this._maybeReportErrorAndCloseIfOpen(0x804B000C);
    }
  },

  // nsIRequestObserver (Triggered by _inputStreamPump.asyncRead)
  onStartRequest: function ts_onStartRequest(request, context) {
  },

  // nsIRequestObserver (Triggered by _inputStreamPump.asyncRead)
  onStopRequest: function ts_onStopRequest(request, context, status) {
    let buffered_output = this._multiplexStream.count !== 0;

    this._inputStreamPump = null;

    let statusIsError = !Components.isSuccessCode(status);

    if (buffered_output && !statusIsError) {
      // If we have some buffered output still, and status is not an
      // error, the other side has done a half-close, but we don't
      // want to be in the close state until we are done sending
      // everything that was buffered. We also don't want to call onclose
      // yet.
      return;
    }

    // We call this even if there is no error.
    this._maybeReportErrorAndCloseIfOpen(status);
  },

  // nsIStreamListener (Triggered by _inputStreamPump.asyncRead)
  onDataAvailable: function ts_onDataAvailable(request, context, inputStream, offset, count) {
    if (this._binaryType === "arraybuffer") {
      let buffer = new (this.useWin ? this.useWin.ArrayBuffer : ArrayBuffer)(count);
      this._inputStreamBinary.readArrayBuffer(count, buffer);
      this.callListener("data", buffer);
    } else {
      this.callListener("data", this._inputStreamScriptable.read(count));
    }
  },

  classID: Components.ID("{cda91b22-6472-11e1-aa11-834fec09cd0a}"),

  classInfo: XPCOMUtils.generateCI({
    classID: Components.ID("{cda91b22-6472-11e1-aa11-834fec09cd0a}"),
    contractID: "@mozilla.org/tcp-socket;1",
    classDescription: "Client TCP Socket",
    interfaces: [
      Ci.nsIDOMTCPSocket,
    ],
    flags: Ci.nsIClassInfo.DOM_OBJECT,
  }),

  QueryInterface: XPCOMUtils.generateQI([
    Ci.nsIDOMTCPSocket,
    Ci.nsITCPSocketInternal,
    Ci.nsIDOMGlobalPropertyInitializer,
    Ci.nsIObserver,
    Ci.nsISupportsWeakReference
  ])
}

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([TCPSocket]);

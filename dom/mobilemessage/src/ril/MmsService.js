/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 sts=2 et filetype=javascript
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

Cu.import("resource://gre/modules/NetUtil.jsm");

const RIL_MMSSERVICE_CONTRACTID = "@mozilla.org/mms/rilmmsservice;1";
const RIL_MMSSERVICE_CID = Components.ID("{217ddd76-75db-4210-955d-8806cd8d87f9}");

let DEBUG = false;

// Read debug setting from pref.
try {
  let debugPref = Services.prefs.getBoolPref("mms.debugging.enabled");
  DEBUG = DEBUG || debugPref;
} catch (e) {}

const kSmsSendingObserverTopic           = "sms-sending";
const kSmsSentObserverTopic              = "sms-sent";
const kSmsFailedObserverTopic            = "sms-failed";
const kSmsReceivedObserverTopic          = "sms-received";
const kSmsRetrievingObserverTopic        = "sms-retrieving";

const kNetworkInterfaceStateChangedTopic = "network-interface-state-changed";
const kXpcomShutdownObserverTopic        = "xpcom-shutdown";
const kPrefenceChangedObserverTopic      = "nsPref:changed";

// HTTP status codes:
// @see http://tools.ietf.org/html/rfc2616#page-39
const HTTP_STATUS_OK = 200;

const CONFIG_SEND_REPORT_NEVER       = 0;
const CONFIG_SEND_REPORT_DEFAULT_NO  = 1;
const CONFIG_SEND_REPORT_DEFAULT_YES = 2;
const CONFIG_SEND_REPORT_ALWAYS      = 3;

const TIME_TO_BUFFER_MMS_REQUESTS    = 30000;
const TIME_TO_RELEASE_MMS_CONNECTION = 30000;

const PREF_RETRIEVAL_MODE      = 'dom.mms.retrieval_mode';
const RETRIEVAL_MODE_MANUAL    = "manual";
const RETRIEVAL_MODE_AUTOMATIC = "automatic";
const RETRIEVAL_MODE_AUTOMATIC_HOME = "automatic-home";
const RETRIEVAL_MODE_NEVER     = "never";


//Internal const values.
const DELIVERY_RECEIVED       = "received";
const DELIVERY_NOT_DOWNLOADED = "not-downloaded";
const DELIVERY_SENDING        = "sending";
const DELIVERY_SENT           = "sent";
const DELIVERY_ERROR          = "error";

const DELIVERY_STATUS_SUCCESS  = "success";
const DELIVERY_STATUS_PENDING  = "pending";
const DELIVERY_STATUS_ERROR    = "error";
const DELIVERY_STATUS_REJECTED = "rejected";
const DELIVERY_STATUS_MANUAL   = "manual";

const PREF_SEND_RETRY_COUNT =
  Services.prefs.getIntPref("dom.mms.sendRetryCount");

const PREF_SEND_RETRY_INTERVAL =
  Services.prefs.getIntPref("dom.mms.sendRetryInterval");

const PREF_RETRIEVAL_RETRY_COUNT =
  Services.prefs.getIntPref("dom.mms.retrievalRetryCount");

const PREF_RETRIEVAL_RETRY_INTERVALS = (function () {
  let intervals =
    Services.prefs.getCharPref("dom.mms.retrievalRetryIntervals").split(",");
  for (let i = 0; i < PREF_RETRIEVAL_RETRY_COUNT; ++i) {
    intervals[i] = parseInt(intervals[i], 10);
    // If one of the intervals isn't valid (e.g., 0 or NaN),
    // assign a 10-minute interval to it as a default.
    if (!intervals[i]) {
      intervals[i] = 600000;
    }
  }
  intervals.length = PREF_RETRIEVAL_RETRY_COUNT;
  return intervals;
})();

XPCOMUtils.defineLazyServiceGetter(this, "gpps",
                                   "@mozilla.org/network/protocol-proxy-service;1",
                                   "nsIProtocolProxyService");

XPCOMUtils.defineLazyServiceGetter(this, "gUUIDGenerator",
                                   "@mozilla.org/uuid-generator;1",
                                   "nsIUUIDGenerator");

XPCOMUtils.defineLazyServiceGetter(this, "gRIL",
                                   "@mozilla.org/ril;1",
                                   "nsIRadioInterfaceLayer");

XPCOMUtils.defineLazyServiceGetter(this, "gMobileMessageDatabaseService",
                                   "@mozilla.org/mobilemessage/rilmobilemessagedatabaseservice;1",
                                   "nsIRilMobileMessageDatabaseService");

XPCOMUtils.defineLazyServiceGetter(this, "gMobileMessageService",
                                   "@mozilla.org/mobilemessage/mobilemessageservice;1",
                                   "nsIMobileMessageService");

XPCOMUtils.defineLazyServiceGetter(this, "gSystemMessenger",
                                   "@mozilla.org/system-message-internal;1",
                                   "nsISystemMessagesInternal");

XPCOMUtils.defineLazyGetter(this, "MMS", function () {
  let MMS = {};
  Cu.import("resource://gre/modules/MmsPduHelper.jsm", MMS);
  return MMS;
});

XPCOMUtils.defineLazyGetter(this, "gMmsConnection", function () {
  let conn = {
    QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver]),

    /** MMS proxy settings. */
    mmsc: null,
    proxy: null,
    port: null,

    // For keeping track of the radio status.
    radioDisabled: false,

    proxyInfo: null,
    settings: ["ril.mms.mmsc",
               "ril.mms.mmsproxy",
               "ril.mms.mmsport",
               "ril.radio.disabled"],
    connected: false,

    //A queue to buffer the MMS HTTP requests when the MMS network
    //is not yet connected. The buffered requests will be cleared
    //if the MMS network fails to be connected within a timer.
    pendingCallbacks: [],

    /** MMS network connection reference count. */
    refCount: 0,

    connectTimer: Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer),

    disconnectTimer: Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer),

    /**
     * Callback when |connectTimer| is timeout or cancelled by shutdown.
     */
    onConnectTimerTimeout: function onConnectTimerTimeout() {
      if (DEBUG) debug("onConnectTimerTimeout: " + this.pendingCallbacks.length
                       + " pending callbacks");
      while (this.pendingCallbacks.length) {
        let callback = this.pendingCallbacks.shift();
        callback(false);
      }
    },

    /**
     * Callback when |disconnectTimer| is timeout or cancelled by shutdown.
     */
    onDisconnectTimerTimeout: function onDisconnectTimerTimeout() {
      if (DEBUG) debug("onDisconnectTimerTimeout: deactivate the MMS data call.");
      if (this.connected) {
        gRIL.deactivateDataCallByType("mms");
      }
    },

    init: function init() {
      Services.obs.addObserver(this, kNetworkInterfaceStateChangedTopic,
                               false);
      Services.obs.addObserver(this, kXpcomShutdownObserverTopic, false);
      this.settings.forEach(function(name) {
        Services.prefs.addObserver(name, this, false);
      }, this);

      try {
        this.mmsc = Services.prefs.getCharPref("ril.mms.mmsc");
        if (this.mmsc.endsWith("/")) {
          this.mmsc = this.mmsc.substr(0, this.mmsc.length - 1);
        }
        this.proxy = Services.prefs.getCharPref("ril.mms.mmsproxy");
        this.port = Services.prefs.getIntPref("ril.mms.mmsport");
        this.updateProxyInfo();
      } catch (e) {
        if (DEBUG) debug("Unable to initialize the MMS proxy settings from " +
                         "the preference. This could happen at the first-run. " +
                         "Should be available later.");
        this.clearMmsProxySettings();
      }

      try {
        this.radioDisabled = Services.prefs.getBoolPref("ril.radio.disabled");
      } catch (e) {
        if (DEBUG) debug("Getting preference 'ril.radio.disabled' fails.");
        this.radioDisabled = false;
      }

      this.connected = gRIL.getDataCallStateByType("mms") ==
        Ci.nsINetworkInterface.NETWORK_STATE_CONNECTED;
    },

    /**
     * Return the roaming status of voice call.
     *
     * @return true if voice call is roaming.
     */
    isVoiceRoaming: function isVoiceRoaming() {
      let isRoaming = gRIL.rilContext.voice.roaming;
      if (DEBUG) debug("isVoiceRoaming = " + isRoaming);
      return isRoaming;
    },

    /**
     * Acquire the MMS network connection.
     *
     * @param callback
     *        Callback function when either the connection setup is done,
     *        timeout, or failed. Accepts a boolean value that indicates
     *        whether the connection is ready.
     *
     * @return true if the MMS network connection is already acquired and the
     *              callback is done; false otherwise.
     */
    acquire: function acquire(callback) {
      this.connectTimer.cancel();

      // If the MMS network is not yet connected, buffer the
      // MMS request and try to setup the MMS network first.
      if (!this.connected) {
        if (DEBUG) debug("acquire: buffer the MMS request and setup the MMS data call.");
        this.pendingCallbacks.push(callback);
        gRIL.setupDataCallByType("mms");

        // Set a timer to clear the buffered MMS requests if the
        // MMS network fails to be connected within a time period.
        this.connectTimer.
          initWithCallback(this.onConnectTimerTimeout.bind(this),
                           TIME_TO_BUFFER_MMS_REQUESTS,
                           Ci.nsITimer.TYPE_ONE_SHOT);
        return false;
      }

      this.refCount++;

      callback(true);
      return true;
    },

    /**
     * Release the MMS network connection.
     */
    release: function release() {
      this.refCount--;
      if (this.refCount <= 0) {
        this.refCount = 0;

        // Set a timer to delay the release of MMS network connection,
        // since the MMS requests often come consecutively in a short time.
        this.disconnectTimer.
          initWithCallback(this.onDisconnectTimerTimeout.bind(this),
                           TIME_TO_RELEASE_MMS_CONNECTION,
                           Ci.nsITimer.TYPE_ONE_SHOT);
      }
    },

    /**
     * Update the MMS proxy info.
     */
    updateProxyInfo: function updateProxyInfo() {
      if (this.proxy === null || this.port === null) {
        if (DEBUG) debug("updateProxyInfo: proxy or port is not yet decided." );
        return;
      }

      this.proxyInfo =
        gpps.newProxyInfo("http", this.proxy, this.port,
                          Ci.nsIProxyInfo.TRANSPARENT_PROXY_RESOLVES_HOST,
                          -1, null);
      if (DEBUG) debug("updateProxyInfo: " + JSON.stringify(this.proxyInfo));
    },

    /**
     * Clear the MMS proxy settings.
     */
    clearMmsProxySettings: function clearMmsProxySettings() {
      this.mmsc = null;
      this.proxy = null;
      this.port = null;
      this.proxyInfo = null;
    },

    shutdown: function shutdown() {
      Services.obs.removeObserver(this, kNetworkInterfaceStateChangedTopic);
      this.settings.forEach(function(name) {
        Services.prefs.removeObserver(name, this);
      }, this);
      this.connectTimer.cancel();
      this.onConnectTimerTimeout();
      this.disconnectTimer.cancel();
      this.onDisconnectTimerTimeout();
    },

    // nsIObserver

    observe: function observe(subject, topic, data) {
      switch (topic) {
        case kNetworkInterfaceStateChangedTopic: {
          this.connected =
            gRIL.getDataCallStateByType("mms") ==
              Ci.nsINetworkInterface.NETWORK_STATE_CONNECTED;

          if (!this.connected) {
            return;
          }

          if (DEBUG) debug("Got the MMS network connected! Resend the buffered " +
                           "MMS requests: number: " + this.pendingCallbacks.length);
          this.connectTimer.cancel();
          while (this.pendingCallbacks.length) {
            let callback = this.pendingCallbacks.shift();
            callback(true);
          }
          break;
        }
        case kPrefenceChangedObserverTopic: {
          if (data == "ril.radio.disabled") {
            try {
              this.radioDisabled = Services.prefs.getBoolPref("ril.radio.disabled");
            } catch (e) {
              if (DEBUG) debug("Updating preference 'ril.radio.disabled' fails.");
              this.radioDisabled = false;
            }
            return;
          }

          try {
            switch (data) {
              case "ril.mms.mmsc":
                this.mmsc = Services.prefs.getCharPref("ril.mms.mmsc");
                if (this.mmsc.endsWith("/")) {
                  this.mmsc = this.mmsc.substr(0, this.mmsc.length - 1);
                }
                break;
              case "ril.mms.mmsproxy":
                this.proxy = Services.prefs.getCharPref("ril.mms.mmsproxy");
                this.updateProxyInfo();
                break;
              case "ril.mms.mmsport":
                this.port = Services.prefs.getIntPref("ril.mms.mmsport");
                this.updateProxyInfo();
                break;
              default:
                break;
            }
          } catch (e) {
            if (DEBUG) debug("Failed to update the MMS proxy settings from the" +
                             "preference.");
            this.clearMmsProxySettings();
          }
          break;
        }
        case kXpcomShutdownObserverTopic: {
          Services.obs.removeObserver(this, kXpcomShutdownObserverTopic);
          this.shutdown();
        }
      }
    }
  };
  conn.init();

  return conn;
});

function MmsProxyFilter(url) {
  this.url = url;
}
MmsProxyFilter.prototype = {

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIProtocolProxyFilter]),

  // nsIProtocolProxyFilter

  applyFilter: function applyFilter(proxyService, uri, proxyInfo) {
    let url = uri.prePath + uri.path;
    if (url.endsWith("/")) {
      url = url.substr(0, url.length - 1);
    }

    if (this.url != url) {
      if (DEBUG) debug("applyFilter: content uri = " + this.url +
                       " is not matched url = " + url + " .");
      return proxyInfo;
    }
    // Fall-through, reutrn the MMS proxy info.
    if (DEBUG) debug("applyFilter: MMSC is matched: " +
                     JSON.stringify({ url: this.url,
                                      proxyInfo: gMmsConnection.proxyInfo }));
    return gMmsConnection.proxyInfo ? gMmsConnection.proxyInfo : proxyInfo;
  }
};

XPCOMUtils.defineLazyGetter(this, "gMmsTransactionHelper", function () {
  return {
    /**
     * Send MMS request to MMSC.
     *
     * @param method
     *        "GET" or "POST".
     * @param url
     *        Target url string.
     * @param istream
     *        An nsIInputStream instance as data source to be sent or null.
     * @param callback
     *        A callback function that takes two arguments: one for http
     *        status, the other for wrapped PDU data for further parsing.
     */
    sendRequest: function sendRequest(method, url, istream, callback) {
      // TODO: bug 810226 - Support GPRS bearer for MMS transmission and reception.

      gMmsConnection.acquire((function (method, url, istream, callback,
                                        connected) {
        if (!connected) {
          // Connection timeout or failed. Report error.
          gMmsConnection.release();
          if (callback) {
            callback(0, null);
          }
          return;
        }

        if (DEBUG) debug("sendRequest: register proxy filter to " + url);
        let proxyFilter = new MmsProxyFilter(url);
        gpps.registerFilter(proxyFilter, 0);

        let releaseMmsConnectionAndCallback = (function (httpStatus, data) {
          gpps.unregisterFilter(proxyFilter);
          // Always release the MMS network connection before callback.
          gMmsConnection.release();
          if (callback) {
            callback(httpStatus, data);
          }
        }).bind(this);

        try {
          let xhr = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"]
                    .createInstance(Ci.nsIXMLHttpRequest);

          // Basic setups
          xhr.open(method, url, true);
          xhr.responseType = "arraybuffer";
          if (istream) {
            xhr.setRequestHeader("Content-Type",
                                 "application/vnd.wap.mms-message");
            xhr.setRequestHeader("Content-Length", istream.available());
          }

          // UAProf headers.
          let uaProfUrl, uaProfTagname = "x-wap-profile";
          try {
            uaProfUrl = Services.prefs.getCharPref('wap.UAProf.url');
            uaProfTagname = Services.prefs.getCharPref('wap.UAProf.tagname');
          } catch (e) {}

          if (uaProfUrl) {
            xhr.setRequestHeader(uaProfTagname, uaProfUrl);
          }

          // Setup event listeners
          xhr.onerror = function () {
            if (DEBUG) debug("xhr error, response headers: " +
                             xhr.getAllResponseHeaders());
            releaseMmsConnectionAndCallback(xhr.status, null);
          };
          xhr.onreadystatechange = function () {
            if (xhr.readyState != Ci.nsIXMLHttpRequest.DONE) {
              return;
            }

            let data = null;
            switch (xhr.status) {
              case HTTP_STATUS_OK: {
                if (DEBUG) debug("xhr success, response headers: "
                                 + xhr.getAllResponseHeaders());

                let array = new Uint8Array(xhr.response);
                if (false) {
                  for (let begin = 0; begin < array.length; begin += 20) {
                    let partial = array.subarray(begin, begin + 20);
                    if (DEBUG) debug("res: " + JSON.stringify(partial));
                  }
                }

                data = {array: array, offset: 0};
                break;
              }
              default: {
                if (DEBUG) debug("xhr done, but status = " + xhr.status +
                                 ", statusText = " + xhr.statusText);
                break;
              }
            }

            releaseMmsConnectionAndCallback(xhr.status, data);
          }

          // Send request
          xhr.send(istream);
        } catch (e) {
          if (DEBUG) debug("xhr error, can't send: " + e.message);
          releaseMmsConnectionAndCallback(0, null);
        }
      }).bind(this, method, url, istream, callback));
    },

    /**
     * Count number of recipients(to, cc, bcc fields).
     *
     * @param recipients
     *        The recipients in MMS message object.
     * @return the number of recipients
     * @see OMA-TS-MMS_CONF-V1_3-20110511-C section 10.2.5
     */
    countRecipients: function countRecipients(recipients) {
      if (recipients && recipients.address) {
        return 1;
      }
      let totalRecipients = 0;
      if (!Array.isArray(recipients)) {
        return 0;
      }
      totalRecipients += recipients.length;
      for (let ix = 0; ix < recipients.length; ++ix) {
        if (recipients[ix].address.length > MMS.MMS_MAX_LENGTH_RECIPIENT) {
          throw new Error("MMS_MAX_LENGTH_RECIPIENT error");
        }
        if (recipients[ix].type === "email") {
          let found = recipients[ix].address.indexOf("<");
          let lenMailbox = recipients[ix].address.length - found;
          if(lenMailbox > MMS.MMS_MAX_LENGTH_MAILBOX_PORTION) {
            throw new Error("MMS_MAX_LENGTH_MAILBOX_PORTION error");
          }
        }
      }
      return totalRecipients;
    },

    /**
     * Check maximum values of MMS parameters.
     *
     * @param msg
     *        The MMS message object.
     * @return true if the lengths are less than the maximum values of MMS
     *         parameters.
     * @see OMA-TS-MMS_CONF-V1_3-20110511-C section 10.2.5
     */
    checkMaxValuesParameters: function checkMaxValuesParameters(msg) {
      let subject = msg.headers["subject"];
      if (subject && subject.length > MMS.MMS_MAX_LENGTH_SUBJECT) {
        return false;
      }

      let totalRecipients = 0;
      try {
        totalRecipients += this.countRecipients(msg.headers["to"]);
        totalRecipients += this.countRecipients(msg.headers["cc"]);
        totalRecipients += this.countRecipients(msg.headers["bcc"]);
      } catch (ex) {
        if (DEBUG) debug("Exception caught : " + ex);
        return false;
      }

      if (totalRecipients < 1 ||
          totalRecipients > MMS.MMS_MAX_TOTAL_RECIPIENTS) {
        return false;
      }

      if (!Array.isArray(msg.parts)) {
        return true;
      }
      for (let i = 0; i < msg.parts.length; i++) {
        if (msg.parts[i].headers["content-type"] &&
          msg.parts[i].headers["content-type"].params) {
          let name = msg.parts[i].headers["content-type"].params["name"];
          if (name && name.length > MMS.MMS_MAX_LENGTH_NAME_CONTENT_TYPE) {
            return false;
          }
        }
      }
      return true;
    }
  };
});

/**
 * Send M-NotifyResp.ind back to MMSC.
 *
 * @param transactionId
 *        X-Mms-Transaction-ID of the message.
 * @param status
 *        X-Mms-Status of the response.
 * @param reportAllowed
 *        X-Mms-Report-Allowed of the response.
 *
 * @see OMA-TS-MMS_ENC-V1_3-20110913-A section 6.2
 */
function NotifyResponseTransaction(transactionId, status, reportAllowed) {
  let headers = {};

  // Mandatory fields
  headers["x-mms-message-type"] = MMS.MMS_PDU_TYPE_NOTIFYRESP_IND;
  headers["x-mms-transaction-id"] = transactionId;
  headers["x-mms-mms-version"] = MMS.MMS_VERSION;
  headers["x-mms-status"] = status;
  // Optional fields
  headers["x-mms-report-allowed"] = reportAllowed;

  this.istream = MMS.PduHelper.compose(null, {headers: headers});
}
NotifyResponseTransaction.prototype = {
  /**
   * @param callback [optional]
   *        A callback function that takes one argument -- the http status.
   */
  run: function run(callback) {
    let requestCallback;
    if (callback) {
      requestCallback = function (httpStatus, data) {
        // `The MMS Client SHOULD ignore the associated HTTP POST response
        // from the MMS Proxy-Relay.` ~ OMA-TS-MMS_CTR-V1_3-20110913-A
        // section 8.2.2 "Notification".
        callback(httpStatus);
      };
    }
    gMmsTransactionHelper.sendRequest("POST", gMmsConnection.mmsc,
                                      this.istream, requestCallback);
  }
};

/**
 * Retrieve message back from MMSC.
 *
 * @param contentLocation
 *        X-Mms-Content-Location of the message.
 */
function RetrieveTransaction(contentLocation) {
  this.contentLocation = contentLocation;
}
RetrieveTransaction.prototype = {
  /**
   * We need to keep a reference to the timer to assure the timer is fired.
   */
  timer: null,

  /**
   * @param callback [optional]
   *        A callback function that takes two arguments: one for X-Mms-Status,
   *        the other for the parsed M-Retrieve.conf message.
   */
  run: function run(callback) {
    this.retryCount = 0;
    let that = this;
    this.retrieve((function retryCallback(mmsStatus, msg) {
      if (MMS.MMS_PDU_STATUS_DEFERRED == mmsStatus &&
          that.retryCount < PREF_RETRIEVAL_RETRY_COUNT) {
        if (that.timer == null) {
          that.timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
        }

        that.timer.initWithCallback((function (){
                                      this.retrieve(retryCallback);
                                    }).bind(that),
                                    PREF_RETRIEVAL_RETRY_INTERVALS[that.retryCount],
                                    Ci.nsITimer.TYPE_ONE_SHOT);
        that.retryCount++;
        return;
      }
      if (callback) {
        callback(mmsStatus, msg);
      }
    }).bind(this));
  },

  /**
   * @param callback
   *        A callback function that takes two arguments: one for X-Mms-Status,
   *        the other for the parsed M-Retrieve.conf message.
   */
  retrieve: function retrieve(callback) {
    gMmsTransactionHelper.sendRequest("GET", this.contentLocation, null,
                                      (function (httpStatus, data) {
      if ((httpStatus != HTTP_STATUS_OK) || !data) {
        callback(MMS.MMS_PDU_STATUS_DEFERRED, null);
        return;
      }

      let retrieved = MMS.PduHelper.parse(data, null);
      if (!retrieved || (retrieved.type != MMS.MMS_PDU_TYPE_RETRIEVE_CONF)) {
        callback(MMS.MMS_PDU_STATUS_UNRECOGNISED, null);
        return;
      }

      // Fix default header field values.
      if (retrieved.headers["x-mms-delivery-report"] == null) {
        retrieved.headers["x-mms-delivery-report"] = false;
      }

      let retrieveStatus = retrieved.headers["x-mms-retrieve-status"];
      if ((retrieveStatus != null) &&
          (retrieveStatus != MMS.MMS_PDU_ERROR_OK)) {
        callback(MMS.translatePduErrorToStatus(retrieveStatus),
                        retrieved);
        return;
      }

      callback(MMS.MMS_PDU_STATUS_RETRIEVED, retrieved);
    }).bind(this));
  }
};

/**
 * SendTransaction.
 *   Class for sending M-Send.req to MMSC
 *   @throws Error("Check max values parameters fail.")
 */
function SendTransaction(msg) {
  msg.headers["x-mms-message-type"] = MMS.MMS_PDU_TYPE_SEND_REQ;
  if (!msg.headers["x-mms-transaction-id"]) {
    // Create an unique transaction id
    let tid = gUUIDGenerator.generateUUID().toString();
    msg.headers["x-mms-transaction-id"] = tid;
  }
  msg.headers["x-mms-mms-version"] = MMS.MMS_VERSION;

  // Let MMS Proxy Relay insert from address automatically for us
  msg.headers["from"] = null;

  msg.headers["date"] = new Date();
  msg.headers["x-mms-message-class"] = "personal";
  msg.headers["x-mms-expiry"] = 7 * 24 * 60 * 60;
  msg.headers["x-mms-priority"] = 129;
  msg.headers["x-mms-read-report"] = true;
  msg.headers["x-mms-delivery-report"] = true;

  if (!gMmsTransactionHelper.checkMaxValuesParameters(msg)) {
    //We should notify end user that the header format is wrong.
    if (DEBUG) debug("Check max values parameters fail.");
    throw new Error("Check max values parameters fail.");
  }

  if (msg.parts) {
    let contentType = {
      params: {
        // `The type parameter must be specified and its value is the MIME
        // media type of the "root" body part.` ~ RFC 2387 clause 3.1
        type: msg.parts[0].headers["content-type"].media,
      },
    };

    // `The Content-Type in M-Send.req and M-Retrieve.conf SHALL be
    // application/vnd.wap.multipart.mixed when there is no presentation, and
    // application/vnd.wap.multipart.related SHALL be used when there is SMIL
    // presentation available.` ~ OMA-TS-MMS_CONF-V1_3-20110913-A clause 10.2.1
    if (contentType.params.type === "application/smil") {
      contentType.media = "application/vnd.wap.multipart.related";

      // `The start parameter, if given, is the content-ID of the compound
      // object's "root".` ~ RFC 2387 clause 3.2
      contentType.params.start = msg.parts[0].headers["content-id"];
    } else {
      contentType.media = "application/vnd.wap.multipart.mixed";
    }

    // Assign to Content-Type
    msg.headers["content-type"] = contentType;
  }

  if (DEBUG) debug("msg: " + JSON.stringify(msg));

  this.msg = msg;
}
SendTransaction.prototype = {
  /**
   * We need to keep a reference to the timer to assure the timer is fired.
   */
  timer: null,

  istreamComposed: false,

  /**
   * @param parts
   *        'parts' property of a parsed MMS message.
   * @param callback [optional]
   *        A callback function that takes zero argument.
   */
  loadBlobs: function loadBlobs(parts, callback) {
    let callbackIfValid = function callbackIfValid() {
      if (DEBUG) debug("All parts loaded: " + JSON.stringify(parts));
      if (callback) {
        callback();
      }
    }

    if (!parts || !parts.length) {
      callbackIfValid();
      return;
    }

    let numPartsToLoad = parts.length;
    for each (let part in parts) {
      if (!(part.content instanceof Ci.nsIDOMBlob)) {
        numPartsToLoad--;
        if (!numPartsToLoad) {
          callbackIfValid();
          return;
        }
        continue;
      }
      let fileReader = Cc["@mozilla.org/files/filereader;1"]
                       .createInstance(Ci.nsIDOMFileReader);
      fileReader.addEventListener("loadend",
        (function onloadend(part, event) {
        let arrayBuffer = event.target.result;
        part.content = new Uint8Array(arrayBuffer);
        numPartsToLoad--;
        if (!numPartsToLoad) {
          callbackIfValid();
        }
      }).bind(null, part));
      fileReader.readAsArrayBuffer(part.content);
    };
  },

  /**
   * @param callback [optional]
   *        A callback function that takes two arguments: one for
   *        X-Mms-Response-Status, the other for the parsed M-Send.conf message.
   */
  run: function run(callback) {
    if (!this.istreamComposed) {
      this.loadBlobs(this.msg.parts, (function () {
        this.istream = MMS.PduHelper.compose(null, this.msg);
        this.istreamComposed = true;
        this.run(callback);
      }).bind(this));
      return;
    }

    let callbackIfValid = function callbackIfValid(mmsStatus, msg) {
      if (callback) {
        callback(mmsStatus, msg);
      }
    }

    if (!this.istream) {
      callbackIfValid(MMS.MMS_PDU_ERROR_PERMANENT_FAILURE, null);
      return;
    }

    this.retryCount = 0;
    let retryCallback = (function (mmsStatus, msg) {
      if ((MMS.MMS_PDU_ERROR_TRANSIENT_FAILURE == mmsStatus ||
            MMS.MMS_PDU_ERROR_PERMANENT_FAILURE == mmsStatus) &&
          this.retryCount < PREF_SEND_RETRY_COUNT) {
        if (this.timer == null) {
          this.timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
        }

        this.retryCount++;

        this.timer.initWithCallback(this.send.bind(this, retryCallback),
                                    PREF_SEND_RETRY_INTERVAL,
                                    Ci.nsITimer.TYPE_ONE_SHOT);
        return;
      }

      callbackIfValid(mmsStatus, msg);
    }).bind(this);
    this.send(retryCallback);
  },

  /**
   * @param callback
   *        A callback function that takes two arguments: one for
   *        X-Mms-Response-Status, the other for the parsed M-Send.conf message.
   */
  send: function send(callback) {
    gMmsTransactionHelper.sendRequest("POST", gMmsConnection.mmsc, this.istream,
                                      function (httpStatus, data) {
      if (httpStatus != HTTP_STATUS_OK) {
        callback(MMS.MMS_PDU_ERROR_TRANSIENT_FAILURE, null);
        return;
      }

      if (!data) {
        callback(MMS.MMS_PDU_ERROR_PERMANENT_FAILURE, null);
        return;
      }

      let response = MMS.PduHelper.parse(data, null);
      if (!response || (response.type != MMS.MMS_PDU_TYPE_SEND_CONF)) {
        callback(MMS.MMS_PDU_RESPONSE_ERROR_UNSUPPORTED_MESSAGE, null);
        return;
      }

      let responseStatus = response.headers["x-mms-response-status"];
      callback(responseStatus, response);
    });
  }
};

/**
 * Send M-acknowledge.ind back to MMSC.
 *
 * @param transactionId
 *        X-Mms-Transaction-ID of the message.
 * @param reportAllowed
 *        X-Mms-Report-Allowed of the response.
 *
 * @see OMA-TS-MMS_ENC-V1_3-20110913-A section 6.4
 */
function AcknowledgeTransaction(transactionId, reportAllowed) {
  let headers = {};

  // Mandatory fields
  headers["x-mms-message-type"] = MMS.MMS_PDU_TYPE_ACKNOWLEDGE_IND;
  headers["x-mms-transaction-id"] = transactionId;
  headers["x-mms-mms-version"] = MMS.MMS_VERSION;
  // Optional fields
  headers["x-mms-report-allowed"] = reportAllowed;

  this.istream = MMS.PduHelper.compose(null, {headers: headers});
}
AcknowledgeTransaction.prototype = {
  /**
   * @param callback [optional]
   *        A callback function that takes one argument -- the http status.
   */
  run: function run(callback) {
    let requestCallback;
    if (callback) {
      requestCallback = function (httpStatus, data) {
        // `The MMS Client SHOULD ignore the associated HTTP POST response
        // from the MMS Proxy-Relay.` ~ OMA-TS-MMS_CTR-V1_3-20110913-A
        // section 8.2.3 "Retrieving an MM".
        callback(httpStatus);
      };
    }
    gMmsTransactionHelper.sendRequest("POST", gMmsConnection.mmsc,
                                      this.istream, requestCallback);
  }
};

/**
 * MmsService
 */
function MmsService() {
  // TODO: bug 810084 - support application identifier
}
MmsService.prototype = {

  classID:   RIL_MMSSERVICE_CID,
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIMmsService,
                                         Ci.nsIWapPushApplication]),
  /*
   * Whether or not should we enable X-Mms-Report-Allowed in M-NotifyResp.ind
   * and M-Acknowledge.ind PDU.
   */
  confSendDeliveryReport: CONFIG_SEND_REPORT_DEFAULT_YES,

  /**
   * Calculate Whether or not should we enable X-Mms-Report-Allowed.
   *
   * @param config
   *        Current configure value.
   * @param wish
   *        Sender wish. Could be undefined, false, or true.
   */
  getReportAllowed: function getReportAllowed(config, wish) {
    if ((config == CONFIG_SEND_REPORT_DEFAULT_NO)
        || (config == CONFIG_SEND_REPORT_DEFAULT_YES)) {
      if (wish != null) {
        config += (wish ? 1 : -1);
      }
    }
    return config >= CONFIG_SEND_REPORT_DEFAULT_YES;
  },

  /**
   * Convert intermediate message to indexedDB savable object.
   *
   * @param retrievalMode
   *        Retrieval mode for MMS receiving setting.
   * @param intermediate
   *        Intermediate MMS message parsed from PDU.
   */
  convertIntermediateToSavable: function convertIntermediateToSavable(intermediate,
                                                                      retrievalMode) {
    intermediate.type = "mms";
    intermediate.delivery = DELIVERY_NOT_DOWNLOADED;

    switch(retrievalMode) {
      case RETRIEVAL_MODE_MANUAL:
        intermediate.deliveryStatus = [DELIVERY_STATUS_MANUAL];
        break;
      case RETRIEVAL_MODE_NEVER:
        intermediate.deliveryStatus = [DELIVERY_STATUS_REJECTED];
        break;
      case RETRIEVAL_MODE_AUTOMATIC:
        intermediate.deliveryStatus = [DELIVERY_STATUS_PENDING];
        break;
      case RETRIEVAL_MODE_AUTOMATIC_HOME:
        if (gMmsConnection.isVoiceRoaming()) {
          intermediate.deliveryStatus = [DELIVERY_STATUS_MANUAL];
        } else {
          intermediate.deliveryStatus = [DELIVERY_STATUS_PENDING];
        }
        break;
    }

    intermediate.timestamp = Date.now();
    intermediate.sender = null;
    intermediate.transactionId = intermediate.headers["x-mms-transaction-id"];
    if (intermediate.headers.from) {
      intermediate.sender = intermediate.headers.from.address;
    } else {
      intermediate.sender = "anonymous";
    }
    intermediate.receivers = [];
    return intermediate;
  },

  /**
   * Merge the retrieval confirmation into the savable message.
   *
   * @param intermediate
   *        Intermediate MMS message parsed from PDU, which carries
            the retrieval confirmation.
   * @param savable
   *        The indexedDB savable MMS message, which is going to be
   *        merged with the extra retrieval confirmation.
   */
  mergeRetrievalConfirmation: function mergeRetrievalConfirmation(intermediate, savable) {
    savable.timestamp = Date.now();
    if (intermediate.headers.from) {
      savable.sender = intermediate.headers.from.address;
    } else {
      savable.sender = "anonymous";
    }
    savable.receivers = [];
    // We don't have Bcc in recevied MMS message.
    for each (let type in ["cc", "to"]) {
      if (intermediate.headers[type]) {
        if (intermediate.headers[type] instanceof Array) {
          for (let index in intermediate.headers[type]) {
            savable.receivers.push(intermediate.headers[type][index].address)
          }
        } else {
          savable.receivers.push(intermediate.headers[type].address);
        }
      }
    }

    savable.delivery = DELIVERY_RECEIVED;
    savable.deliveryStatus = [DELIVERY_STATUS_SUCCESS];
    for (let field in intermediate.headers) {
      savable.headers[field] = intermediate.headers[field];
    }
    if (intermediate.parts) {
      savable.parts = intermediate.parts;
    }
    if (intermediate.content) {
      savable.content = intermediate.content;
    }
    return savable;
  },

  /**
   * @param aContentLocation
   *        X-Mms-Content-Location of the message.
   * @param aCallback [optional]
   *        A callback function that takes two arguments: one for X-Mms-Status,
   *        the other parsed MMS message.
   * @param aDomMessage
   *        The nsIDOMMozMmsMessage object.
   */
  retrieveMessage: function retrieveMessage(aContentLocation, aCallback, aDomMessage) {
    // Notifying observers an MMS message is retrieving.
    Services.obs.notifyObservers(aDomMessage, kSmsRetrievingObserverTopic, null);

    let transaction = new RetrieveTransaction(aContentLocation);
    transaction.run(aCallback);
  },

  /**
   * A helper to broadcast the system message to launch registered apps
   * like Costcontrol, Notification and Message app... etc.
   *
   * @param aName
   *        The system message name.
   * @param aDomMessage
   *        The nsIDOMMozMmsMessage object.
   */
  broadcastMmsSystemMessage: function broadcastMmsSystemMessage(aName, aDomMessage) {
    if (DEBUG) debug("Broadcasting the MMS system message: " + aName);

    // Sadly we cannot directly broadcast the aDomMessage object
    // because the system message mechamism will rewrap the object
    // based on the content window, which needs to know the properties.
    gSystemMessenger.broadcastMessage(aName, {
      type:           aDomMessage.type,
      id:             aDomMessage.id,
      threadId:       aDomMessage.threadId,
      delivery:       aDomMessage.delivery,
      deliveryStatus: aDomMessage.deliveryStatus,
      sender:         aDomMessage.sender,
      receivers:      aDomMessage.receivers,
      timestamp:      aDomMessage.timestamp,
      read:           aDomMessage.read,
      subject:        aDomMessage.subject,
      smil:           aDomMessage.smil,
      attachments:    aDomMessage.attachments,
      expiryDate:     aDomMessage.expiryDate
    });
  },

  /**
   * A helper function to broadcast system message and notify observers that
   * an MMS is sent.
   *
   * @params aDomMessage
   *         The nsIDOMMozMmsMessage object.
   */
  broadcastSentMessageEvent: function broadcastSentMessageEvent(aDomMessage) {
    // Broadcasting a 'sms-sent' system message to open apps.
    this.broadcastMmsSystemMessage("sms-sent", aDomMessage);

    // Notifying observers an MMS message is sent.
    Services.obs.notifyObservers(aDomMessage, kSmsSentObserverTopic, null);
  },

  /**
   * A helper function to broadcast system message and notify observers that
   * an MMS is received.
   *
   * @params aDomMessage
   *         The nsIDOMMozMmsMessage object.
   */
  broadcastReceivedMessageEvent :function broadcastReceivedMessageEvent(aDomMessage) {
    // Broadcasting a 'sms-received' system message to open apps.
    this.broadcastMmsSystemMessage("sms-received", aDomMessage);

    // Notifying observers an MMS message is received.
    Services.obs.notifyObservers(aDomMessage, kSmsReceivedObserverTopic, null);
  },

  /**
   * Callback for retrieveMessage.
   */
  retrieveMessageCallback: function retrieveMessageCallback(wish,
                                                            savableMessage,
                                                            mmsStatus,
                                                            retrievedMessage) {
    if (DEBUG) debug("retrievedMessage = " + JSON.stringify(retrievedMessage));

    // The absence of the field does not indicate any default
    // value. So we go check the same field in the retrieved
    // message instead.
    if (wish == null && retrievedMessage) {
      wish = retrievedMessage.headers["x-mms-delivery-report"];
    }

    let reportAllowed = this.getReportAllowed(this.confSendDeliveryReport,
                                              wish);
    let transactionId = retrievedMessage.headers["x-mms-transaction-id"];

    // If the mmsStatus isn't MMS_PDU_STATUS_RETRIEVED after retrieving,
    // something must be wrong with MMSC, so stop updating the DB record.
    // We could send a message to content to notify the user the MMS
    // retrieving failed. The end user has to retrieve the MMS again.
    if (MMS.MMS_PDU_STATUS_RETRIEVED !== mmsStatus) {
      let transaction = new NotifyResponseTransaction(transactionId,
                                                      mmsStatus,
                                                      reportAllowed);
      transaction.run();
      // Retrieved fail after retry, so we update the delivery status in DB and
      // notify this domMessage that error happen.
      gMobileMessageDatabaseService.setMessageDelivery(id,
                                                       null,
                                                       null,
                                                       DELIVERY_STATUS_ERROR,
                                                       (function (rv, domMessage) {
        this.broadcastReceivedMessageEvent(domMessage);
      }).bind(this));
      return;
    }

    savableMessage = this.mergeRetrievalConfirmation(retrievedMessage,
                                                     savableMessage);

    gMobileMessageDatabaseService.saveReceivedMessage(savableMessage,
        (function (rv, domMessage) {
      let success = Components.isSuccessCode(rv);

      // Cite 6.2.1 "Transaction Flow" in OMA-TS-MMS_ENC-V1_3-20110913-A:
      // The M-NotifyResp.ind response PDU SHALL provide a message retrieval
      // status code. The status ‘retrieved’ SHALL be used only if the MMS
      // Client has successfully retrieved the MM prior to sending the
      // NotifyResp.ind response PDU.
      let transaction =
        new NotifyResponseTransaction(transactionId,
                                      success ? MMS.MMS_PDU_STATUS_RETRIEVED
                                              : MMS.MMS_PDU_STATUS_DEFERRED,
                                      reportAllowed);
      transaction.run();

      if (!success) {
        // At this point we could send a message to content to notify the user
        // that storing an incoming MMS failed, most likely due to a full disk.
        // The end user has to retrieve the MMS again.
        if (DEBUG) debug("Could not store MMS " + domMessage.id +
                         ", error code " + rv);
        return;
      }

      this.broadcastReceivedMessageEvent(domMessage);
    }).bind(this));
  },

  /**
   * Callback for saveReceivedMessage.
   */
  saveReceivedMessageCallback: function saveReceivedMessageCallback(retrievalMode,
                                                                    savableMessage,
                                                                    rv,
                                                                    domMessage) {
    let success = Components.isSuccessCode(rv);
    if (!success) {
      // At this point we could send a message to content to notify the
      // user that storing an incoming MMS notification indication failed,
      // ost likely due to a full disk.
      if (DEBUG) debug("Could not store MMS " + JSON.stringify(savableMessage) +
            ", error code " + rv);
      // Because MMSC will resend the notification indication once we don't
      // response the notification. Hope the end user will clean some space
      // for the resent notification indication.
      return;
    }

    // For X-Mms-Report-Allowed and X-Mms-Transaction-Id
    let wish = savableMessage.headers["x-mms-delivery-report"];
    let transactionId = savableMessage.headers["x-mms-transaction-id"];

    this.broadcastReceivedMessageEvent(domMessage);

    // In roaming environment, we send notify response only in
    // automatic retrieval mode.
    if ((retrievalMode !== RETRIEVAL_MODE_AUTOMATIC) &&
        gMmsConnection.isVoiceRoaming()) {
      return;
    }

    if (RETRIEVAL_MODE_MANUAL === retrievalMode ||
        RETRIEVAL_MODE_NEVER === retrievalMode) {
      let mmsStatus = RETRIEVAL_MODE_NEVER === retrievalMode
                    ? MMS.MMS_PDU_STATUS_REJECTED
                    : MMS.MMS_PDU_STATUS_DEFERRED;

      // For X-Mms-Report-Allowed
      let reportAllowed = this.getReportAllowed(this.confSendDeliveryReport,
                                                wish);

      let transaction = new NotifyResponseTransaction(transactionId,
                                                      mmsStatus,
                                                      reportAllowed);
      transaction.run();
      return;
    }
    let url = savableMessage.headers["x-mms-content-location"].uri;

    // For RETRIEVAL_MODE_AUTOMATIC or RETRIEVAL_MODE_AUTOMATIC_HOME but not
    // roaming, proceed to retrieve MMS.
    this.retrieveMessage(url,
                         this.retrieveMessageCallback.bind(this,
                                                           wish,
                                                           savableMessage),
                         domMessage);
  },

  /**
   * Handle incoming M-Notification.ind PDU.
   *
   * @param notification
   *        The parsed MMS message object.
   */
  handleNotificationIndication: function handleNotificationIndication(notification) {
    let transactionId = notification.headers["x-mms-transaction-id"];
    gMobileMessageDatabaseService.getMessageRecordByTransactionId(transactionId,
        (function (aRv, aMessageRecord) {
      if (Ci.nsIMobileMessageCallback.SUCCESS_NO_ERROR === aRv
          && aMessageRecord) {
        if (DEBUG) debug("We already got the NotificationIndication with transactionId = "
                         + transactionId + " before.");
        return;
      }

      let retrievalMode = RETRIEVAL_MODE_MANUAL;
      try {
        retrievalMode = Services.prefs.getCharPref(PREF_RETRIEVAL_MODE);
      } catch (e) {}

      let savableMessage = this.convertIntermediateToSavable(notification, retrievalMode);

      gMobileMessageDatabaseService
        .saveReceivedMessage(savableMessage,
                             this.saveReceivedMessageCallback.bind(this,
                                                                   retrievalMode,
                                                                   savableMessage));
    }).bind(this));
  },

  /**
   * Handle incoming M-Delivery.ind PDU.
   *
   * @param msg
   *        The MMS message object.
   */
  handleDeliveryIndication: function handleDeliveryIndication(msg) {
    // TODO Bug 850140 Two things we need to do in the future:
    //
    // 1. Use gMobileMessageDatabaseService.setMessageDelivery() to reset
    // the delivery status to "success" or "error" for a specific receiver.
    //
    // 2. Fire "mms-delivery-success" or "mms-delivery-error" observer
    // topics to MobileMessageManager.
    let messageId = msg.headers["message-id"];
    if (DEBUG) debug("handleDeliveryIndication: got delivery report for " + messageId);
  },

  /**
   * A utility function to convert the MmsParameters dictionary object
   * to a database-savable message.
   *
   * @param aParams
   *        The MmsParameters dictionay object.
   *
   * Notes:
   *
   * OMA-TS-MMS-CONF-V1_3-20110913-A section 10.2.2 "Message Content Encoding":
   *
   * A name for multipart object SHALL be encoded using name-parameter for Content-Type
   * header in WSP multipart headers. In decoding, name-parameter of Content-Type SHALL
   * be used if available. If name-parameter of Content-Type is not available, filename
   * parameter of Content-Disposition header SHALL be used if available. If neither
   * name-parameter of Content-Type header nor filename parameter of Content-Disposition
   * header is available, Content-Location header SHALL be used if available.
   */
  createSavableFromParams: function createSavableFromParams(aParams) {
    if (DEBUG) debug("createSavableFromParams: aParams: " + JSON.stringify(aParams));
    let message = {};
    let smil = aParams.smil;

    // |message.headers|
    let headers = message["headers"] = {};
    let receivers = aParams.receivers;
    if (receivers.length != 0) {
      let headersTo = headers["to"] = [];
      for (let i = 0; i < receivers.length; i++) {
        headersTo.push({"address": receivers[i], "type": "PLMN"});
      }
    }
    if (aParams.subject) {
      headers["subject"] = aParams.subject;
    }

    // |message.parts|
    let attachments = aParams.attachments;
    if (attachments.length != 0 || smil) {
      let parts = message["parts"] = [];

      // Set the SMIL part if needed.
      if (smil) {
        let part = {
          "headers": {
            "content-type": {
              "media": "application/smil",
              "params": {
                "name": "smil.xml",
                "charset": {
                  "charset": "utf-8"
                }
              }
            },
            "content-length": smil.length,
            "content-location": "smil.xml",
            "content-id": "<smil>"
          },
          "content": smil
        };
        parts.push(part);
      }

      // Set other parts for attachments if needed.
      for (let i = 0; i < attachments.length; i++) {
        let attachment = attachments[i];
        let content = attachment.content;
        let location = attachment.location;

        let params = {
          "name": location
        };

        if (content.type && content.type.indexOf("text/") == 0) {
          params.charset = {
            "charset": "utf-8"
          };
        }

        let part = {
          "headers": {
            "content-type": {
              "media": content.type,
              "params": params
            },
            "content-length": content.size,
            "content-location": location,
            "content-id": attachment.id
          },
          "content": content
        };
        parts.push(part);
      }
    }

    // The following attributes are needed for saving message into DB.
    message["type"] = "mms";
    message["deliveryStatusRequested"] = true;
    message["timestamp"] = Date.now();
    message["receivers"] = receivers;

    if (DEBUG) debug("createSavableFromParams: message: " + JSON.stringify(message));
    return message;
  },

  // nsIMmsService

  send: function send(aParams, aRequest) {
    if (DEBUG) debug("send: aParams: " + JSON.stringify(aParams));
    if (aParams.receivers.length == 0) {
      aRequest.notifySendMessageFailed(Ci.nsIMobileMessageCallback.INTERNAL_ERROR);
      return;
    }

    let self = this;

    let sendTransactionCb = function sendTransactionCb(aRecordId, aErrorCode) {
      if (DEBUG) debug("The error code of sending transaction: " + aErrorCode);
      let isSentSuccess = (aErrorCode == Ci.nsIMobileMessageCallback.SUCCESS_NO_ERROR);
      gMobileMessageDatabaseService
        .setMessageDelivery(aRecordId,
                            null,
                            isSentSuccess ? DELIVERY_SENT : DELIVERY_ERROR,
                            isSentSuccess ? null : DELIVERY_STATUS_ERROR,
                            function notifySetDeliveryResult(aRv, aDomMessage) {
        if (DEBUG) debug("Marking the delivery state/staus is done. Notify sent or failed.");
        // TODO bug 832140 handle !Components.isSuccessCode(aRv)
        if (!isSentSuccess) {
          if (DEBUG) debug("Send MMS fail. aParams.receivers = " +
                           JSON.stringify(aParams.receivers));
          aRequest.notifySendMessageFailed(aErrorCode);
          Services.obs.notifyObservers(aDomMessage, kSmsFailedObserverTopic, null);
          return;
        }

        if (DEBUG) debug("Send MMS successful. aParams.receivers = " +
                         JSON.stringify(aParams.receivers));

        // Notifying observers the MMS message is sent.
        self.broadcastSentMessageEvent(aDomMessage);

        // Return the request after sending the MMS message successfully.
        aRequest.notifyMessageSent(aDomMessage);
      });
    };

    let savableMessage = this.createSavableFromParams(aParams);
    gMobileMessageDatabaseService
      .saveSendingMessage(savableMessage,
                          function notifySendingResult(aRv, aDomMessage) {
      if (DEBUG) debug("Saving sending message is done. Start to send.");

      // For radio disabled error.
      if (gMmsConnection.radioDisabled) {
        if (DEBUG) debug("Error! Radio is disabled when sending MMS.");
        sendTransactionCb(aDomMessage.id, Ci.nsIMobileMessageCallback.RADIO_DISABLED_ERROR);
        return;
      }

      // For SIM card is not ready.
      if (gRIL.rilContext.cardState != "ready") {
        if (DEBUG) debug("Error! SIM card is not ready when sending MMS.");
        sendTransactionCb(aDomMessage.id, Ci.nsIMobileMessageCallback.NO_SIM_CARD_ERROR);
        return;
      }

      // TODO bug 832140 handle !Components.isSuccessCode(aRv)
      Services.obs.notifyObservers(aDomMessage, kSmsSendingObserverTopic, null);
      let sendTransaction;
      try {
        sendTransaction = new SendTransaction(savableMessage);
      } catch (e) {
        if (DEBUG) debug("Exception: fail to create a SendTransaction instance.");
        sendTransactionCb(aDomMessage.id, Ci.nsIMobileMessageCallback.INTERNAL_ERROR);
        return;
      }
      sendTransaction.run(function callback(aMmsStatus, aMsg) {
        let isSentSuccess = (aMmsStatus == MMS.MMS_PDU_ERROR_OK);
        if (DEBUG) debug("The sending status of sendTransaction.run(): " + aMmsStatus);
        sendTransactionCb(aDomMessage.id, isSentSuccess?
                          Ci.nsIMobileMessageCallback.SUCCESS_NO_ERROR:
                          Ci.nsIMobileMessageCallback.INTERNAL_ERROR);
      });
    });
  },

  retrieve: function retrieve(aMessageId, aRequest) {
    if (DEBUG) debug("Retrieving message with ID " + aMessageId);
    gMobileMessageDatabaseService.getMessageRecordById(aMessageId,
        (function notifyResult(aRv, aMessageRecord, aDomMessage) {
      if (Ci.nsIMobileMessageCallback.SUCCESS_NO_ERROR != aRv) {
        if (DEBUG) debug("Function getMessageRecordById() return error.");
        aRequest.notifyGetMessageFailed(aRv);
        return;
      }
      if ("mms" != aMessageRecord.type) {
        if (DEBUG) debug("Type of message record is not 'mms'.");
        aRequest.notifyGetMessageFailed(Ci.nsIMobileMessageCallback.INTERNAL_ERROR);
        return;
      }
      if (!aMessageRecord.headers) {
        if (DEBUG) debug("Must need the MMS' headers to proceed the retrieve.");
        aRequest.notifyGetMessageFailed(Ci.nsIMobileMessageCallback.INTERNAL_ERROR);
        return;
      }
      if (!aMessageRecord.headers["x-mms-content-location"]) {
        if (DEBUG) debug("Can't find mms content url in database.");
        aRequest.notifyGetMessageFailed(Ci.nsIMobileMessageCallback.INTERNAL_ERROR);
        return;
      }
      if (DELIVERY_NOT_DOWNLOADED != aMessageRecord.delivery) {
        if (DEBUG) debug("Delivery of message record is not 'not-downloaded'.");
        aRequest.notifyGetMessageFailed(Ci.nsIMobileMessageCallback.INTERNAL_ERROR);
        return;
      }
      if (DELIVERY_STATUS_PENDING == aMessageRecord.deliveryStatus) {
        if (DEBUG) debug("Delivery status of message record is 'pending'.");
        aRequest.notifyGetMessageFailed(Ci.nsIMobileMessageCallback.INTERNAL_ERROR);
        return;
      }

      // Cite 6.2 "Multimedia Message Notification" in OMA-TS-MMS_ENC-V1_3-20110913-A:
      // The field has only one format, relative. The recipient client calculates
      // this length of time relative to the time it receives the notification.
      if (aMessageRecord.headers["x-mms-expiry"] != undefined) {
        let expiryDate = aMessageRecord.timestamp +
                         aMessageRecord.headers["x-mms-expiry"] * 1000;
        if (expiryDate < Date.now()) {
          if (DEBUG) debug("The message to be retrieved is expired.");
          aRequest.notifyGetMessageFailed(Ci.nsIMobileMessageCallback.NOT_FOUND_ERROR);
          return;
        }
      }

      let url =  aMessageRecord.headers["x-mms-content-location"].uri;
      // For X-Mms-Report-Allowed
      let wish = aMessageRecord.headers["x-mms-delivery-report"];
      let responseNotify = function responseNotify(mmsStatus, retrievedMsg) {
        // If the mmsStatus is still MMS_PDU_STATUS_DEFERRED after retry,
        // we should not store it into database and update its delivery
        // status to 'error'.
        if (MMS.MMS_PDU_STATUS_RETRIEVED !== mmsStatus) {
          if (DEBUG) debug("RetrieveMessage fail after retry.");
          gMobileMessageDatabaseService.setMessageDelivery(aMessageId,
                                                           null,
                                                           null,
                                                           DELIVERY_STATUS_ERROR,
                                                           function () {
            aRequest.notifyGetMessageFailed(Ci.nsIMobileMessageCallback.INTERNAL_ERROR);
          });
          return;
        }
        // In OMA-TS-MMS_ENC-V1_3, Table 5 in page 25. This header field
        // (x-mms-transaction-id) SHALL be present when the MMS Proxy relay
        // seeks an acknowledgement for the MM delivered though M-Retrieve.conf
        // PDU during deferred retrieval. This transaction ID is used by the MMS
        // Client and MMS Proxy-Relay to provide linkage between the originated
        // M-Retrieve.conf and the response M-Acknowledge.ind PDUs.
        let transactionId = retrievedMsg.headers["x-mms-transaction-id"];

        // The absence of the field does not indicate any default
        // value. So we go checking the same field in retrieved
        // message instead.
        if (wish == null && retrievedMsg) {
          wish = retrievedMsg.headers["x-mms-delivery-report"];
        }
        let reportAllowed = this.getReportAllowed(this.confSendDeliveryReport,
                                                  wish);

        if (DEBUG) debug("retrievedMsg = " + JSON.stringify(retrievedMsg));
        aMessageRecord = this.mergeRetrievalConfirmation(retrievedMsg, aMessageRecord);
        gMobileMessageDatabaseService.saveReceivedMessage(aMessageRecord,
                                                          (function (rv, domMessage) {
          let success = Components.isSuccessCode(rv);
          if (!success) {
            // At this point we could send a message to content to
            // notify the user that storing an incoming MMS failed, most
            // likely due to a full disk.
            if (DEBUG) debug("Could not store MMS " + domMessage.id +
                  ", error code " + rv);
            aRequest.notifyGetMessageFailed(Ci.nsIMobileMessageCallback.INTERNAL_ERROR);
            return;
          }

          // Notifying observers a new MMS message is retrieved.
          this.broadcastReceivedMessageEvent(domMessage);

          // Return the request after retrieving the MMS message successfully.
          aRequest.notifyMessageGot(domMessage);

          // Cite 6.3.1 "Transaction Flow" in OMA-TS-MMS_ENC-V1_3-20110913-A:
          // If an acknowledgement is requested, the MMS Client SHALL respond
          // with an M-Acknowledge.ind PDU to the MMS Proxy-Relay that supports
          // the specific MMS Client. The M-Acknowledge.ind PDU confirms
          // successful message retrieval to the MMS Proxy Relay.
          let transaction = new AcknowledgeTransaction(transactionId, reportAllowed);
          transaction.run();
        }).bind(this));
      };
      // Update the delivery status to pending in DB.
      gMobileMessageDatabaseService
        .setMessageDelivery(aMessageId,
                            null,
                            null,
                            DELIVERY_STATUS_PENDING,
                            this.retrieveMessage(url,
                                                 responseNotify.bind(this),
                                                 aDomMessage));
    }).bind(this));
  },

  // nsIWapPushApplication

  receiveWapPush: function receiveWapPush(array, length, offset, options) {
    let data = {array: array, offset: offset};
    let msg = MMS.PduHelper.parse(data, null);
    if (!msg) {
      return false;
    }
    if (DEBUG) debug("receiveWapPush: msg = " + JSON.stringify(msg));

    switch (msg.type) {
      case MMS.MMS_PDU_TYPE_NOTIFICATION_IND:
        this.handleNotificationIndication(msg);
        break;
      case MMS.MMS_PDU_TYPE_DELIVERY_IND:
        this.handleDeliveryIndication(msg);
        break;
      default:
        if (DEBUG) debug("Unsupported X-MMS-Message-Type: " + msg.type);
        break;
    }
  },
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([MmsService]);

let debug;
if (DEBUG) {
  debug = function (s) {
    dump("-@- MmsService: " + s + "\n");
  };
} else {
  debug = function (s) {};
}

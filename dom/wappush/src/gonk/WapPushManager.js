/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

Cu.import("resource://gre/modules/WspPduHelper.jsm", this);

const DEBUG = false; // set to true to see debug messages

/**
 * WAP Push decoders
 */
XPCOMUtils.defineLazyGetter(this, "SI", function () {
  let SI = {};
  Cu.import("resource://gre/modules/SiPduHelper.jsm", SI);
  return SI;
});

XPCOMUtils.defineLazyGetter(this, "SL", function () {
  let SL = {};
  Cu.import("resource://gre/modules/SlPduHelper.jsm", SL);
  return SL;
});

XPCOMUtils.defineLazyServiceGetter(this, "gSystemMessenger",
                                   "@mozilla.org/system-message-internal;1",
                                   "nsISystemMessagesInternal");

/**
 * Helpers for WAP PDU processing.
 */
this.WapPushManager = {

  /**
   * Parse raw PDU data and deliver to a proper target.
   *
   * @param data
   *        A wrapped object containing raw PDU data.
   * @param options
   *        Extra context for decoding.
   */
  processMessage: function processMessage(data, options) {
    try {
      PduHelper.parse(data, true, options);
      debug("options: " + JSON.stringify(options));
    } catch (ex) {
      debug("Failed to parse sessionless WSP PDU: " + ex.message);
      return;
    }

    let appid = options.headers["x-wap-application-id"];
    if (!appid) {
      debug("Push message doesn't contains X-Wap-Application-Id.");
      return;
    }

    // MMS
    if (appid == "x-wap-application:mms.ua") {
      let mmsService = Cc["@mozilla.org/mms/rilmmsservice;1"]
                       .getService(Ci.nsIMmsService);
      mmsService.QueryInterface(Ci.nsIWapPushApplication)
                .receiveWapPush(data.array, data.array.length, data.offset, options);
      return;
    }

    /**
    * Non-MMS, handled according to content type
    *
    * WAP Type            content-type                              x-wap-application-id
    * MMS                 "application/vnd.wap.mms-message"         "x-wap-application:mms.ua"
    * SI                  "text/vnd.wap.si"                         "x-wap-application:wml.ua"
    * SI(WBXML)           "application/vnd.wap.sic"                 "x-wap-application:wml.ua"
    * SL                  "text/vnd.wap.sl"                         "x-wap-application:wml.ua"
    * SL(WBXML)           "application/vnd.wap.slc"                 "x-wap-application:wml.ua"
    * Provisioning        "text/vnd.wap.connectivity-xml"           "x-wap-application:wml.ua"
    * Provisioning(WBXML) "application/vnd.wap.connectivity-wbxml"  "x-wap-application:wml.ua"
    *
    * @see http://technical.openmobilealliance.org/tech/omna/omna-wsp-content-type.aspx
    */
    let contentType = options.headers["content-type"].media;
    let msg;

    if (contentType === "text/vnd.wap.si" ||
        contentType === "application/vnd.wap.sic") {
      msg = SI.PduHelper.parse(data, contentType);
    } else if (contentType === "text/vnd.wap.sl" ||
               contentType === "application/vnd.wap.slc") {
      msg = SL.PduHelper.parse(data, contentType);
    } else {
      // TODO: Bug 869291 - Support Receiving WAP-Push-CP

      // Unsupported type, provide raw data.
      msg = {
        contentType: contentType,
        content: data.array
      };
    }

    gSystemMessenger.broadcastMessage("wappush-received", {
      contentType:    msg.contentType,
      content:        msg.content
    });
  },

  /**
   * @param array
   *        A Uint8Array or an octet array representing raw PDU data.
   * @param length
   *        Length of the array.
   * @param offset
   *        Offset of the array that a raw PDU data begins.
   * @param options
   *        WDP bearer information.
   */
  receiveWdpPDU: function receiveWdpPDU(array, length, offset, options) {
    if ((options.bearer == null) || !options.sourceAddress
        || (options.sourcePort == null) || !array) {
      debug("Incomplete WDP PDU");
      return;
    }

    if (options.destinationPort != WDP_PORT_PUSH) {
      debug("Not WAP Push port: " + options.destinationPort);
      return;
    }

    this.processMessage({array: array, offset: offset}, options);
  },
};

let debug;
if (DEBUG) {
  debug = function (s) {
    dump("-*- WapPushManager: " + s + "\n");
  };
} else {
  debug = function (s) {};
}

this.EXPORTED_SYMBOLS = ALL_CONST_SYMBOLS.concat([
  "WapPushManager",
]);


// -*- Mode: js2; tab-width: 2; indent-tabs-mode: nil; js2-basic-offset: 2; js2-skip-preprocessor-directives: t; -*-
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

this.EXPORTED_SYMBOLS = ["sendOrderedBroadcast"];

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;

// For adding observers.
Cu.import("resource://gre/modules/Services.jsm");

function sendMessageToJava(message) {
  return Cc["@mozilla.org/android/bridge;1"]
    .getService(Ci.nsIAndroidBridge)
    .handleGeckoMessage(JSON.stringify(message));
}

let _callbackId = 1;

/**
 * Send an ordered broadcast to Java.
 *
 * Internally calls Context.sendOrderedBroadcast.
 *
 * action {String} should be a string with a qualified name (like
 * org.mozilla.gecko.action) that will be broadcast.
 *
 * token {Object} is a piece of arbitrary data that will be given as
 * a parameter to the callback (possibly null).
 *
 * callback {function} should accept three arguments: the data
 * returned from Java as an Object; the specified token; and the
 * specified action.
 *
 * permission {String} should be a string with an Android permission
 * that packages must have to respond to the ordered broadcast, or
 * null to allow all packages to respond.
 */
function sendOrderedBroadcast(action, token, callback, permission) {
  let callbackId = _callbackId++;
  let responseEvent = "OrderedBroadcast:Response:" + callbackId;

  let observer = {
    callbackId: callbackId,
    callback: callback,

    observe: function observe(subject, topic, data) {
      if (topic != responseEvent) {
        return;
      }

      // Unregister observer as soon as possible.
      Services.obs.removeObserver(observer, responseEvent);

      let msg = JSON.parse(data);
      if (!msg.action || !msg.token || !msg.token.callbackId)
        return;

      let theToken = msg.token.data;
      let theAction = msg.action;
      let theData = msg.data ? JSON.parse(msg.data) : null;

      let theCallback = this.callback;
      if (!theCallback)
        return;

      // This is called from within a notified observer, so we don't
      // need to take special pains to catch exceptions.
      theCallback(theData, theToken, theAction);
    },
  };

  Services.obs.addObserver(observer, responseEvent, false);

  sendMessageToJava({
    type: "OrderedBroadcast:Send",
    action: action,
    responseEvent: responseEvent,
    token: { callbackId: callbackId, data: token || null },
    permission: permission || null,
  });
};

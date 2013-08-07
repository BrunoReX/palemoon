/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is RIL JS Worker.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Kyle Machulis <kyle@nonpolynomial.com>
 *   Philipp von Weitershausen <philipp@weitershausen.de>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/**
 * This file implements the RIL worker thread. It communicates with
 * the main thread to provide a high-level API to the phone's RIL
 * stack, and with the RIL IPC thread to communicate with the RIL
 * device itself. These communication channels use message events as
 * known from Web Workers:
 *
 * - postMessage()/"message" events for main thread communication
 *
 * - postRILMessage()/"RILMessageEvent" events for RIL IPC thread
 *   communication.
 *
 * The three objects in this file represent individual parts of this
 * communication chain:
 *
 * - RILMessageEvent -> Buf -> RIL -> Phone -> postMessage()
 * - "message" event -> Phone -> RIL -> Buf -> postRILMessage()
 *
 * Note: The code below is purposely lean on abstractions to be as lean in
 * terms of object allocations. As a result, it may look more like C than
 * JavaScript, and that's intended.
 */

"use strict";

importScripts("ril_consts.js");

const DEBUG = true;

const INT32_MAX   = 2147483647;
const UINT8_SIZE  = 1;
const UINT16_SIZE = 2;
const UINT32_SIZE = 4;
const PARCEL_SIZE_SIZE = UINT32_SIZE;

/**
 * This object contains helpers buffering incoming data & deconstructing it
 * into parcels as well as buffering outgoing data & constructing parcels.
 * For that it maintains two buffers and corresponding uint8 views, indexes.
 *
 * The incoming buffer is a circular buffer where we store incoming data.
 * As soon as a complete parcel is received, it is processed right away, so
 * the buffer only needs to be large enough to hold one parcel.
 *
 * The outgoing buffer is to prepare outgoing parcels. The index is reset
 * every time a parcel is sent.
 */
let Buf = {

  INCOMING_BUFFER_LENGTH: 1024,
  OUTGOING_BUFFER_LENGTH: 1024,

  init: function init() {
    this.incomingBuffer = new ArrayBuffer(this.INCOMING_BUFFER_LENGTH);
    this.outgoingBuffer = new ArrayBuffer(this.OUTGOING_BUFFER_LENGTH);

    this.incomingBytes = new Uint8Array(this.incomingBuffer);
    this.outgoingBytes = new Uint8Array(this.outgoingBuffer);

    // Track where incoming data is read from and written to.
    this.incomingWriteIndex = 0;
    this.incomingReadIndex = 0;

    // Leave room for the parcel size for outgoing parcels.
    this.outgoingIndex = PARCEL_SIZE_SIZE;

    // How many bytes we've read for this parcel so far.
    this.readIncoming = 0;

    // Size of the incoming parcel. If this is zero, we're expecting a new
    // parcel.
    this.currentParcelSize = 0;

    // This gets incremented each time we send out a parcel.
    this.token = 1;

    // Maps tokens we send out with requests to the request type, so that
    // when we get a response parcel back, we know what request it was for.
    this.tokenRequestMap = {};
  },

  /**
   * Grow the incoming buffer.
   *
   * @param min_size
   *        Minimum new size. The actual new size will be the the smallest
   *        power of 2 that's larger than this number.
   */
  growIncomingBuffer: function growIncomingBuffer(min_size) {
    if (DEBUG) {
      debug("Current buffer of " + this.INCOMING_BUFFER_LENGTH +
            " can't handle incoming " + min_size + " bytes.");
    }
    let oldBytes = this.incomingBytes;
    this.INCOMING_BUFFER_LENGTH =
      2 << Math.floor(Math.log(min_size)/Math.log(2));
    if (DEBUG) debug("New incoming buffer size: " + this.INCOMING_BUFFER_LENGTH);
    this.incomingBuffer = new ArrayBuffer(this.INCOMING_BUFFER_LENGTH);
    this.incomingBytes = new Uint8Array(this.incomingBuffer);
    if (this.incomingReadIndex <= this.incomingWriteIndex) {
      // Read and write index are in natural order, so we can just copy
      // the old buffer over to the bigger one without having to worry
      // about the indexes.
      this.incomingBytes.set(oldBytes, 0);
    } else {
      // The write index has wrapped around but the read index hasn't yet.
      // Write whatever the read index has left to read until it would
      // circle around to the beginning of the new buffer, and the rest
      // behind that.
      let head = oldBytes.subarray(this.incomingReadIndex);
      let tail = oldBytes.subarray(0, this.incomingReadIndex);
      this.incomingBytes.set(head, 0);
      this.incomingBytes.set(tail, head.length);
      this.incomingReadIndex = 0;
      this.incomingWriteIndex += head.length;
    }
    if (DEBUG) {
      debug("New incoming buffer size is " + this.INCOMING_BUFFER_LENGTH);
    }
  },

  /**
   * Grow the outgoing buffer.
   *
   * @param min_size
   *        Minimum new size. The actual new size will be the the smallest
   *        power of 2 that's larger than this number.
   */
  growOutgoingBuffer: function growOutgoingBuffer(min_size) {
    if (DEBUG) {
      debug("Current buffer of " + this.OUTGOING_BUFFER_LENGTH +
            " is too small.");
    }
    let oldBytes = this.outgoingBytes;
    this.OUTGOING_BUFFER_LENGTH =
      2 << Math.floor(Math.log(min_size)/Math.log(2));
    this.outgoingBuffer = new ArrayBuffer(this.OUTGOING_BUFFER_LENGTH);
    this.outgoingBytes = new Uint8Array(this.outgoingBuffer);
    this.outgoingBytes.set(oldBytes, 0);
    if (DEBUG) {
      debug("New outgoing buffer size is " + this.OUTGOING_BUFFER_LENGTH);
    }
  },

  /**
   * Functions for reading data from the incoming buffer.
   *
   * These are all little endian, apart from readParcelSize();
   */

  readUint8: function readUint8() {
    let value = this.incomingBytes[this.incomingReadIndex];
    this.incomingReadIndex = (this.incomingReadIndex + 1) %
                             this.INCOMING_BUFFER_LENGTH;
    return value;
  },

  readUint16: function readUint16() {
    return this.readUint8() | this.readUint8() << 8;
  },

  readUint32: function readUint32() {
    return this.readUint8()       | this.readUint8() <<  8 |
           this.readUint8() << 16 | this.readUint8() << 24;
  },

  readUint32List: function readUint32List() {
    let length = this.readUint32();
    let ints = [];
    for (let i = 0; i < length; i++) {
      ints.push(this.readUint32());
    }
    return ints;
  },

  readString: function readString() {
    let string_len = this.readUint32();
    if (string_len < 0 || string_len >= INT32_MAX) {
      return null;
    }
    let s = "";
    for (let i = 0; i < string_len; i++) {
      s += String.fromCharCode(this.readUint16());
    }
    // Strings are \0\0 delimited, but that isn't part of the length. And
    // if the string length is even, the delimiter is two characters wide.
    // It's insane, I know.
    let delimiter = this.readUint16();
    if (!(string_len & 1)) {
      delimiter |= this.readUint16();
    }
    if (DEBUG) {
      if (delimiter != 0) {
        debug("Something's wrong, found string delimiter: " + delimiter);
      }
    }
    return s;
  },

  readStringList: function readStringList() {
    let num_strings = this.readUint32();
    let strings = [];
    for (let i = 0; i < num_strings; i++) {
      strings.push(this.readString());
    }
    return strings;
  },

  readParcelSize: function readParcelSize() {
    return this.readUint8() << 24 | this.readUint8() << 16 |
           this.readUint8() <<  8 | this.readUint8();
  },

  /**
   * Functions for writing data to the outgoing buffer.
   */

  writeUint8: function writeUint8(value) {
    if (this.outgoingIndex >= this.OUTGOING_BUFFER_LENGTH) {
      this.growOutgoingBuffer(this.outgoingIndex + 1);
    }
    this.outgoingBytes[this.outgoingIndex] = value;
    this.outgoingIndex++;
  },

  writeUint16: function writeUint16(value) {
    this.writeUint8(value & 0xff);
    this.writeUint8((value >> 8) & 0xff);
  },

  writeUint32: function writeUint32(value) {
    this.writeUint8(value & 0xff);
    this.writeUint8((value >> 8) & 0xff);
    this.writeUint8((value >> 16) & 0xff);
    this.writeUint8((value >> 24) & 0xff);
  },

  writeString: function writeString(value) {
    if (value == null) {
      this.writeUint32(-1);
      return;
    }
    this.writeUint32(value.length);
    for (let i = 0; i < value.length; i++) {
      this.writeUint16(value.charCodeAt(i));
    }
    // Strings are \0\0 delimited, but that isn't part of the length. And
    // if the string length is even, the delimiter is two characters wide.
    // It's insane, I know.
    this.writeUint16(0);
    if (!(value.length & 1)) {
      this.writeUint16(0);
    }
  },

  writeStringList: function writeStringList(strings) {
    this.writeUint32(strings.length);
    for (let i = 0; i < strings.length; i++) {
      this.writeString(strings[i]);
    }
  },

  writeParcelSize: function writeParcelSize(value) {
    /**
     *  Parcel size will always be the first thing in the parcel byte
     *  array, but the last thing written. Store the current index off
     *  to a temporary to be reset after we write the size.
     */
    let currentIndex = this.outgoingIndex;
    this.outgoingIndex = 0;
    this.writeUint8((value >> 24) & 0xff);
    this.writeUint8((value >> 16) & 0xff);
    this.writeUint8((value >> 8) & 0xff);
    this.writeUint8(value & 0xff);
    this.outgoingIndex = currentIndex;
  },


  /**
   * Parcel management
   */

  /**
   * Write incoming data to the circular buffer.
   *
   * @param incoming
   *        Uint8Array containing the incoming data.
   */
  writeToIncoming: function writeToIncoming(incoming) {
    // We don't have to worry about the head catching the tail since
    // we process any backlog in parcels immediately, before writing
    // new data to the buffer. So the only edge case we need to handle
    // is when the incoming data is larger than the buffer size.
    if (incoming.length > this.INCOMING_BUFFER_LENGTH) {
      this.growIncomingBuffer(incoming.length);
    }

    // We can let the typed arrays do the copying if the incoming data won't
    // wrap around the edges of the circular buffer.
    let remaining = this.INCOMING_BUFFER_LENGTH - this.incomingWriteIndex;
    if (remaining >= incoming.length) {
      this.incomingBytes.set(incoming, this.incomingWriteIndex);
    } else {
      // The incoming data would wrap around it.
      let head = incoming.subarray(0, remaining);
      let tail = incoming.subarray(remaining);
      this.incomingBytes.set(head, this.incomingWriteIndex);
      this.incomingBytes.set(tail, 0);
    }
    this.incomingWriteIndex = (this.incomingWriteIndex + incoming.length) %
                              this.INCOMING_BUFFER_LENGTH;
  },

  /**
   * Process incoming data.
   *
   * @param incoming
   *        Uint8Array containing the incoming data.
   */
  processIncoming: function processIncoming(incoming) {
    if (DEBUG) {
      debug("Received " + incoming.length + " bytes.");
      debug("Already read " + this.readIncoming);
    }

    this.writeToIncoming(incoming);
    this.readIncoming += incoming.length;
    while (true) {
      if (!this.currentParcelSize) {
        // We're expecting a new parcel.
        if (this.readIncoming < PARCEL_SIZE_SIZE) {
          // We don't know how big the next parcel is going to be, need more
          // data.
          if (DEBUG) debug("Next parcel size unknown, going to sleep.");
          return;
        }
        this.currentParcelSize = this.readParcelSize();
        if (DEBUG) debug("New incoming parcel of size " +
                         this.currentParcelSize);
        // The size itself is not included in the size.
        this.readIncoming -= PARCEL_SIZE_SIZE;
      }

      if (this.readIncoming < this.currentParcelSize) {
        // We haven't read enough yet in order to be able to process a parcel.
        if (DEBUG) debug("Read " + this.readIncoming + ", but parcel size is "
                         + this.currentParcelSize + ". Going to sleep.");
        return;
      }

      // Alright, we have enough data to process at least one whole parcel.
      // Let's do that.
      let expectedAfterIndex = (this.incomingReadIndex + this.currentParcelSize)
                               % this.INCOMING_BUFFER_LENGTH;

      if (DEBUG) {
        let parcel;
        if (expectedAfterIndex < this.incomingReadIndex) {
          let head = this.incomingBytes.subarray(this.incomingReadIndex);
          let tail = this.incomingBytes.subarray(0, expectedAfterIndex);
          parcel = Array.slice(head).concat(Array.slice(tail));
        } else {
          parcel = Array.slice(this.incomingBytes.subarray(
            this.incomingReadIndex, expectedAfterIndex));
        }
        debug("Parcel (size " + this.currentParcelSize + "): " + parcel);
      }

      if (DEBUG) debug("We have at least one complete parcel.");
      try {
        this.processParcel();
      } catch (ex) {
        if (DEBUG) debug("Parcel handling threw " + ex);
      }

      // Ensure that the whole parcel was consumed.
      if (this.incomingReadIndex != expectedAfterIndex) {
        if (DEBUG) {
          debug("Parcel handler didn't consume whole parcel, " +
                Math.abs(expectedAfterIndex - this.incomingReadIndex) +
                " bytes left over");
        }
        this.incomingReadIndex = expectedAfterIndex;
      }
      this.readIncoming -= this.currentParcelSize;
      this.currentParcelSize = 0;
    }
  },

  /**
   * Process one parcel.
   */

  processParcel: function processParcel() {
    let response_type = this.readUint32();
    let length = this.readIncoming - UINT32_SIZE;

    let request_type;
    if (response_type == RESPONSE_TYPE_SOLICITED) {
      let token = this.readUint32();
      let error = this.readUint32();
      length -= 2 * UINT32_SIZE;
      request_type = this.tokenRequestMap[token];
      if (error) {
        //TODO
        debug("Received error " + error + " for solicited parcel type " +
              request_type);
        return;
      }
      debug("Solicited response for request type " + request_type +
            ", token " + token);
      delete this.tokenRequestMap[token];
    } else if (response_type == RESPONSE_TYPE_UNSOLICITED) {
      request_type = this.readUint32();
      length -= UINT32_SIZE;
      debug("Unsolicited response for request type " + request_type);
    } else {
      debug("Unknown response type: " + response_type);
      return;
    }

    RIL.handleParcel(request_type, length);
  },

  /**
   * Start a new outgoing parcel.
   *
   * @param type
   *        Integer specifying the request type.
   */
  newParcel: function newParcel(type) {
    // We're going to leave room for the parcel size at the beginning.
    this.outgoingIndex = PARCEL_SIZE_SIZE;
    this.writeUint32(type);
    let token = this.token;
    this.writeUint32(token);
    this.tokenRequestMap[token] = type;
    this.token++;
    return token;
  },

  /**
   * Communication with the RIL IPC thread.
   */

  sendParcel: function sendParcel() {
    // Compute the size of the parcel and write it to the front of the parcel
    // where we left room for it. Note that he parcel size does not include
    // the size itself.
    let parcelSize = this.outgoingIndex - PARCEL_SIZE_SIZE;
    this.writeParcelSize(parcelSize);

    // This assumes that postRILMessage will make a copy of the ArrayBufferView
    // right away!
    let parcel = this.outgoingBytes.subarray(0, this.outgoingIndex);
    debug("Outgoing parcel: " + Array.slice(parcel));
    postRILMessage(parcel);
    this.outgoingIndex = PARCEL_SIZE_SIZE;
  },

  simpleRequest: function simpleRequest(type) {
    this.newParcel(type);
    this.sendParcel();
  }
};


/**
 * Provide a high-level API representing the RIL's capabilities. This is
 * where parcels are sent and received from and translated into API calls.
 * For the most part, this object is pretty boring as it simply translates
 * between method calls and RIL parcels. Somebody's gotta do the job...
 */
let RIL = {

  /**
   * Retrieve the ICC card's status.
   *
   * Response will call Phone.onICCStatus().
   */
  getICCStatus: function getICCStatus() {
    Buf.simpleRequest(REQUEST_GET_SIM_STATUS);
  },

  /**
   * Enter a PIN to unlock the ICC.
   *
   * @param pin
   *        String containing the PIN.
   *
   * Response will call Phone.onEnterSIMPIN().
   */
  enterICCPIN: function enterICCPIN(pin) {
    Buf.newParcel(REQUEST_ENTER_SIM_PIN);
    Buf.writeUint32(1);
    Buf.writeString(pin);
    Buf.sendParcel();
  },

  /**
   * Request the phone's radio power to be switched on or off.
   *
   * @param on
   *        Boolean indicating the desired power state.
   */
  setRadioPower: function setRadioPower(on) {
    Buf.newParcel(REQUEST_RADIO_POWER);
    Buf.writeUint32(1);
    Buf.writeUint32(on ? 1 : 0);
    Buf.sendParcel();
  },

  /**
   * Set screen state.
   *
   * @param on
   *        Boolean indicating whether the screen should be on or off.
   */
  setScreenState: function setScreenState(on) {
    Buf.newParcel(REQUEST_SCREEN_STATE);
    Buf.writeUint32(1);
    Buf.writeUint32(on ? 1 : 0);
    Buf.sendParcel();
  },

  getRegistrationState: function getRegistrationState() {
    Buf.simpleRequest(REQUEST_REGISTRATION_STATE);
  },

  getGPRSRegistrationState: function getGPRSRegistrationState() {
    Buf.simpleRequest(REQUEST_GPRS_REGISTRATION_STATE);
  },

  getOperator: function getOperator() {
    Buf.simpleRequest(REQUEST_OPERATOR);
  },

  getNetworkSelectionMode: function getNetworkSelectionMode() {
    Buf.simpleRequest(REQUEST_QUERY_NETWORK_SELECTION_MODE);
  },

  /**
   * Get current calls.
   */
  getCurrentCalls: function getCurrentCalls() {
    Buf.simpleRequest(REQUEST_GET_CURRENT_CALLS);
  },

  /**
   * Get the signal strength.
   */
  getSignalStrength: function getSignalStrength() {
    Buf.simpleRequest(REQUEST_SIGNAL_STRENGTH);
  },

  getIMEI: function getIMEI() {
    Buf.simpleRequest(REQUEST_GET_IMEI);
  },

  getIMEISV: function getIMEISV() {
    Buf.simpleRequest(REQUEST_GET_IMEISV);
  },

  getDeviceIdentity: function getDeviceIdentity() {
    Buf.simpleRequest(REQUEST_GET_DEVICE_IDENTITY);
  },

  /**
   * Dial the phone.
   *
   * @param address
   *        String containing the address (number) to dial.
   * @param clirMode
   *        Integer doing something XXX TODO
   * @param uusInfo
   *        Integer doing something XXX TODO
   */
  dial: function dial(address, clirMode, uusInfo) {
    let token = Buf.newParcel(REQUEST_DIAL);
    Buf.writeString(address);
    Buf.writeUint32(clirMode || 0);
    Buf.writeUint32(uusInfo || 0);
	// TODO Why do we need this extra 0? It was put it in to make this
	// match the format of the binary message.
	Buf.writeUint32(0);
    Buf.sendParcel();
  },

  /**
   * Hang up the phone.
   *
   * @param index
   *        Call index (1-based) as reported by REQUEST_GET_CURRENT_CALLS.
   */
  hangUp: function hangUp(index) {
    Buf.newParcel(REQUEST_HANGUP);
    Buf.writeUint32(1);
    Buf.writeUint32(index);
    Buf.sendParcel();
  },

  /**
   * Mute or unmute the radio.
   *
   * @param mute
   *        Boolean to indicate whether to mute or unmute the radio.
   */
  setMute: function setMute(mute) {
    Buf.newParcel(REQUEST_SET_MUTE);
    Buf.writeUint32(1);
    Buf.writeUint32(mute ? 1 : 0);
    Buf.sendParcel();
  },

  /**
   * Answer an incoming call.
   */
  answerCall: function answerCall() {
    Buf.simpleRequest(REQUEST_ANSWER);
  },

  /**
   * Reject an incoming call.
   */
  rejectCall: function rejectCall() {
    Buf.simpleRequest(REQUEST_UDUB);
  },

  /**
   * Send an SMS.
   *
   * @param smscPDU
   *        String containing the SMSC PDU in hex format.
   * @param pdu
   *        String containing the PDU in hex format.
   */
  sendSMS: function sendSMS(smscPDU, pdu) {
    let token = Buf.newParcel(REQUEST_SEND_SMS);
    //TODO we want to map token to the input values so that on the
    // response from the RIL device we know which SMS request was successful
    // or not. Maybe we should build that functionality into newParcel() and
    // handle it within tokenRequestMap[].
    Buf.writeUint32(2);
    Buf.writeString(smscPDU);
    Buf.writeString(pdu);
    Buf.sendParcel();
  },

  /**
   * Start a DTMF Tone.
   *
   * @param dtmfChar
   *        DTMF signal to send, 0-9, *, +
   */

  startTone: function startTone(dtmfChar) {
    Buf.newParcel(REQUEST_DTMF_START);
    Buf.writeString(dtmfChar);
    Buf.sendParcel();
  },

  stopTone: function stopTone() {
    Buf.simpleRequest(REQUEST_DTMF_STOP);
  },

  sendTone: function sendTone(dtmfChar) {
    Buf.newParcel(REQUEST_DTMF);
    Buf.writeString(dtmfChar);
    Buf.sendParcel();
  },

  /**
   * Handle incoming requests from the RIL. We find the method that
   * corresponds to the request type. Incidentally, the request type
   * _is_ the method name, so that's easy.
   */

  handleParcel: function handleParcel(request_type, length) {
    let method = this[request_type];
    if (typeof method == "function") {
      debug("Handling parcel as " + method.name);
      method.call(this, length);
    }
  }
};

RIL[REQUEST_GET_SIM_STATUS] = function REQUEST_GET_SIM_STATUS() {
  let iccStatus = {
    cardState:                   Buf.readUint32(), // CARDSTATE_*
    universalPINState:           Buf.readUint32(), // PINSTATE_*
    gsmUmtsSubscriptionAppIndex: Buf.readUint32(),
    setCdmaSubscriptionAppIndex: Buf.readUint32(),
    apps:                        []
  };

  let apps_length = Buf.readUint32();
  if (apps_length > CARD_MAX_APPS) {
    apps_length = CARD_MAX_APPS;
  }

  for (let i = 0 ; i < apps_length ; i++) {
    iccStatus.apps.push({
      app_type:       Buf.readUint32(), // APPTYPE_*
      app_state:      Buf.readUint32(), // APPSTATE_*
      perso_substate: Buf.readUint32(), // PERSOSUBSTATE_*
      aid:            Buf.readString(),
      app_label:      Buf.readString(),
      pin1_replaced:  Buf.readUint32(),
      pin1:           Buf.readUint32(),
      pin2:           Buf.readUint32()
    });
  }
  Phone.onICCStatus(iccStatus);
};
RIL[REQUEST_ENTER_SIM_PIN] = function REQUEST_ENTER_SIM_PIN() {
  let response = Buf.readUint32List();
  Phone.onEnterICCPIN(response);
};
RIL[REQUEST_ENTER_SIM_PUK] = null;
RIL[REQUEST_ENTER_SIM_PIN2] = null;
RIL[REQUEST_ENTER_SIM_PUK2] = null;
RIL[REQUEST_CHANGE_SIM_PIN] = null;
RIL[REQUEST_CHANGE_SIM_PIN2] = null;
RIL[REQUEST_ENTER_NETWORK_DEPERSONALIZATION] = null;
RIL[REQUEST_GET_CURRENT_CALLS] = function REQUEST_GET_CURRENT_CALLS(length) {
  let calls_length = 0;
  // The RIL won't even send us the length integer if there are no active calls.
  // So only read this integer if the parcel actually has it.
  if (length) {
    calls_length = Buf.readUint32();
  }
  if (!calls_length) {
    Phone.onCurrentCalls(null);
    return;
  }

  let calls = {};
  for (let i = 0; i < calls_length; i++) {
    let call = {
      state:              Buf.readUint32(), // CALL_STATE_*
      index:              Buf.readUint32(), // GSM index (1-based)
      toa:                Buf.readUint32(),
      isMpty:             Boolean(Buf.readUint32()),
      isMT:               Boolean(Buf.readUint32()),
      als:                Buf.readUint32(),
      isVoice:            Boolean(Buf.readUint32()),
      isVoicePrivacy:     Boolean(Buf.readUint32()),
      somethingOrOther:   Buf.readUint32(), //XXX TODO whatziz? not in ril.h, but it's in the output...
      number:             Buf.readString(), //TODO munge with TOA
      numberPresentation: Buf.readUint32(), // CALL_PRESENTATION_*
      name:               Buf.readString(),
      namePresentation:   Buf.readUint32(),
      uusInfo:            null
    };
    let uusInfoPresent = Buf.readUint32();
    if (uusInfoPresent == 1) {
      call.uusInfo = {
        type:     Buf.readUint32(),
        dcs:      Buf.readUint32(),
        userData: null //XXX TODO byte array?!?
      };
    }
    calls[call.index] = call;
  }
  Phone.onCurrentCalls(calls);
};
RIL[REQUEST_DIAL] = function REQUEST_DIAL(length) {
  Phone.onDial();
};
RIL[REQUEST_GET_IMSI] = function REQUEST_GET_IMSI(length) {
  let imsi = Buf.readString();
  Phone.onIMSI(imsi);
};
RIL[REQUEST_HANGUP] = function REQUEST_HANGUP(length) {
  Phone.onHangUp();
};
RIL[REQUEST_HANGUP_WAITING_OR_BACKGROUND] = null;
RIL[REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND] = null;
RIL[REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE] = null;
RIL[REQUEST_SWITCH_HOLDING_AND_ACTIVE] = null;
RIL[REQUEST_CONFERENCE] = null;
RIL[REQUEST_UDUB] = function REQUEST_UDUB(length) {
  Phone.onRejectCall();
};
RIL[REQUEST_LAST_CALL_FAIL_CAUSE] = null;
RIL[REQUEST_SIGNAL_STRENGTH] = function REQUEST_SIGNAL_STRENGTH() {
  let strength = {
    // Valid values are (0-31, 99) as defined in TS 27.007 8.5.
    // For some reason we're getting int32s like [99, 4, 0, 0] and [99, 3, 0, 0]
    // here, so let's strip of anything beyond the first byte.
    gsmSignalStrength: Buf.readUint32() & 0xff,
    // GSM bit error rate (0-7, 99) as defined in TS 27.007 8.5.
    gsmBitErrorRate:   Buf.readUint32(),
    // The CDMA RSSI value.
    cdmaDBM:           Buf.readUint32(),
    // The CDMA EC/IO.
    cdmaECIO:          Buf.readUint32(),
    // The EVDO RSSI value.
    evdoDBM:           Buf.readUint32(),
    // The EVDO EC/IO.
    evdoECIO:          Buf.readUint32(),
    // Valid values are 0-8.  8 is the highest signal to noise ratio
    evdoSNR:           Buf.readUint32()
  };
  Phone.onSignalStrength(strength);
};
RIL[REQUEST_REGISTRATION_STATE] = function REQUEST_REGISTRATION_STATE(length) {
  let state = Buf.readStringList();
  Phone.onRegistrationState(state);
};
RIL[REQUEST_GPRS_REGISTRATION_STATE] = function REQUEST_GPRS_REGISTRATION_STATE(length) {
  let state = Buf.readStringList();
  Phone.onGPRSRegistrationState(state);
};
RIL[REQUEST_OPERATOR] = function REQUEST_OPERATOR(length) {
  let operator = Buf.readStringList();
  Phone.onOperator(operator);
};
RIL[REQUEST_RADIO_POWER] = null;
RIL[REQUEST_DTMF] = function REQUEST_DTMF() {
  Phone.onSendTone();
};
RIL[REQUEST_SEND_SMS] = function REQUEST_SEND_SMS() {
  let messageRef = Buf.readUint32();
  let ackPDU = p.readString();
  let errorCode = p.readUint32();
  Phone.onSendSMS(messageRef, ackPDU, errorCode);
};
RIL[REQUEST_SEND_SMS_EXPECT_MORE] = null;
RIL[REQUEST_SETUP_DATA_CALL] = null;
RIL[REQUEST_SIM_IO] = null;
RIL[REQUEST_SEND_USSD] = null;
RIL[REQUEST_CANCEL_USSD] = null;
RIL[REQUEST_GET_CLIR] = null;
RIL[REQUEST_SET_CLIR] = null;
RIL[REQUEST_QUERY_CALL_FORWARD_STATUS] = null;
RIL[REQUEST_SET_CALL_FORWARD] = null;
RIL[REQUEST_QUERY_CALL_WAITING] = null;
RIL[REQUEST_SET_CALL_WAITING] = null;
RIL[REQUEST_SMS_ACKNOWLEDGE] = null;
RIL[REQUEST_GET_IMEI] = function REQUEST_GET_IMEI() {
  let imei = Buf.readString();
  Phone.onIMEI(imei);
};
RIL[REQUEST_GET_IMEISV] = function REQUEST_GET_IMEISV() {
  let imeiSV = Buf.readString();
  Phone.onIMEISV(imeiSV);
};
RIL[REQUEST_ANSWER] = function REQUEST_ANSWER(length) {
  Phone.onAnswerCall();
};
RIL[REQUEST_DEACTIVATE_DATA_CALL] = null;
RIL[REQUEST_QUERY_FACILITY_LOCK] = null;
RIL[REQUEST_SET_FACILITY_LOCK] = null;
RIL[REQUEST_CHANGE_BARRING_PASSWORD] = null;
RIL[REQUEST_QUERY_NETWORK_SELECTION_MODE] = function REQUEST_QUERY_NETWORK_SELECTION_MODE() {
  let response = Buf.readUint32List();
  Phone.onNetworkSelectionMode(response);
};
RIL[REQUEST_SET_NETWORK_SELECTION_AUTOMATIC] = null;
RIL[REQUEST_SET_NETWORK_SELECTION_MANUAL] = null;
RIL[REQUEST_QUERY_AVAILABLE_NETWORKS] = null;
RIL[REQUEST_DTMF_START] = function REQUEST_DTMF_START() {
  Phone.onStartTone();
};
RIL[REQUEST_DTMF_STOP] = function REQUEST_DTMF_STOP() {
  Phone.onStopTone();
};
RIL[REQUEST_BASEBAND_VERSION] = function REQUEST_BASEBAND_VERSION() {
  let version = Buf.readString();
  Phone.onBasebandVersion(version);
},
RIL[REQUEST_SEPARATE_CONNECTION] = null;
RIL[REQUEST_SET_MUTE] = function REQUEST_SET_MUTE(length) {
  Phone.onSetMute();
};
RIL[REQUEST_GET_MUTE] = null;
RIL[REQUEST_QUERY_CLIP] = null;
RIL[REQUEST_LAST_DATA_CALL_FAIL_CAUSE] = null;
RIL[REQUEST_DATA_CALL_LIST] = null;
RIL[REQUEST_RESET_RADIO] = null;
RIL[REQUEST_OEM_HOOK_RAW] = null;
RIL[REQUEST_OEM_HOOK_STRINGS] = null;
RIL[REQUEST_SCREEN_STATE] = null;
RIL[REQUEST_SET_SUPP_SVC_NOTIFICATION] = null;
RIL[REQUEST_WRITE_SMS_TO_SIM] = null;
RIL[REQUEST_DELETE_SMS_ON_SIM] = null;
RIL[REQUEST_SET_BAND_MODE] = null;
RIL[REQUEST_QUERY_AVAILABLE_BAND_MODE] = null;
RIL[REQUEST_STK_GET_PROFILE] = null;
RIL[REQUEST_STK_SET_PROFILE] = null;
RIL[REQUEST_STK_SEND_ENVELOPE_COMMAND] = null;
RIL[REQUEST_STK_SEND_TERMINAL_RESPONSE] = null;
RIL[REQUEST_STK_HANDLE_CALL_SETUP_REQUESTED_FROM_SIM] = null;
RIL[REQUEST_EXPLICIT_CALL_TRANSFER] = null;
RIL[REQUEST_SET_PREFERRED_NETWORK_TYPE] = null;
RIL[REQUEST_GET_PREFERRED_NETWORK_TYPE] = null;
RIL[REQUEST_GET_NEIGHBORING_CELL_IDS] = null;
RIL[REQUEST_SET_LOCATION_UPDATES] = null;
RIL[REQUEST_CDMA_SET_SUBSCRIPTION] = null;
RIL[REQUEST_CDMA_SET_ROAMING_PREFERENCE] = null;
RIL[REQUEST_CDMA_QUERY_ROAMING_PREFERENCE] = null;
RIL[REQUEST_SET_TTY_MODE] = null;
RIL[REQUEST_QUERY_TTY_MODE] = null;
RIL[REQUEST_CDMA_SET_PREFERRED_VOICE_PRIVACY_MODE] = null;
RIL[REQUEST_CDMA_QUERY_PREFERRED_VOICE_PRIVACY_MODE] = null;
RIL[REQUEST_CDMA_FLASH] = null;
RIL[REQUEST_CDMA_BURST_DTMF] = null;
RIL[REQUEST_CDMA_VALIDATE_AND_WRITE_AKEY] = null;
RIL[REQUEST_CDMA_SEND_SMS] = null;
RIL[REQUEST_CDMA_SMS_ACKNOWLEDGE] = null;
RIL[REQUEST_GSM_GET_BROADCAST_SMS_CONFIG] = null;
RIL[REQUEST_GSM_SET_BROADCAST_SMS_CONFIG] = null;
RIL[REQUEST_GSM_SMS_BROADCAST_ACTIVATION] = null;
RIL[REQUEST_CDMA_GET_BROADCAST_SMS_CONFIG] = null;
RIL[REQUEST_CDMA_SET_BROADCAST_SMS_CONFIG] = null;
RIL[REQUEST_CDMA_SMS_BROADCAST_ACTIVATION] = null;
RIL[REQUEST_CDMA_SUBSCRIPTION] = null;
RIL[REQUEST_CDMA_WRITE_SMS_TO_RUIM] = null;
RIL[REQUEST_CDMA_DELETE_SMS_ON_RUIM] = null;
RIL[REQUEST_DEVICE_IDENTITY] = null;
RIL[REQUEST_EXIT_EMERGENCY_CALLBACK_MODE] = null;
RIL[REQUEST_GET_SMSC_ADDRESS] = null;
RIL[REQUEST_SET_SMSC_ADDRESS] = null;
RIL[REQUEST_REPORT_SMS_MEMORY_STATUS] = null;
RIL[REQUEST_REPORT_STK_SERVICE_IS_RUNNING] = null;
RIL[UNSOLICITED_RESPONSE_RADIO_STATE_CHANGED] = function UNSOLICITED_RESPONSE_RADIO_STATE_CHANGED() {
  let newState = Buf.readUint32();
  Phone.onRadioStateChanged(newState);
};
RIL[UNSOLICITED_RESPONSE_CALL_STATE_CHANGED] = function UNSOLICITED_RESPONSE_CALL_STATE_CHANGED() {
  Phone.onCallStateChanged();
};
RIL[UNSOLICITED_RESPONSE_NETWORK_STATE_CHANGED] = function UNSOLICITED_RESPONSE_NETWORK_STATE_CHANGED() {
  Phone.onNetworkStateChanged();
};
RIL[UNSOLICITED_RESPONSE_NEW_SMS] = null;
RIL[UNSOLICITED_RESPONSE_NEW_SMS_STATUS_REPORT] = null;
RIL[UNSOLICITED_RESPONSE_NEW_SMS_ON_SIM] = null;
RIL[UNSOLICITED_ON_USSD] = null;
RIL[UNSOLICITED_ON_USSD_REQUEST] = null;
RIL[UNSOLICITED_NITZ_TIME_RECEIVED] = null;
RIL[UNSOLICITED_SIGNAL_STRENGTH] = function UNSOLICITED_SIGNAL_STRENGTH() {
  this[REQUEST_SIGNAL_STRENGTH]();
};
RIL[UNSOLICITED_DATA_CALL_LIST_CHANGED] = null;
RIL[UNSOLICITED_SUPP_SVC_NOTIFICATION] = null;
RIL[UNSOLICITED_STK_SESSION_END] = null;
RIL[UNSOLICITED_STK_PROACTIVE_COMMAND] = null;
RIL[UNSOLICITED_STK_EVENT_NOTIFY] = null;
RIL[UNSOLICITED_STK_CALL_SETUP] = null;
RIL[UNSOLICITED_SIM_SMS_STORAGE_FULL] = null;
RIL[UNSOLICITED_SIM_REFRESH] = null;
RIL[UNSOLICITED_CALL_RING] = function UNSOLICITED_CALL_RING() {
  let info;
  let isCDMA = false; //XXX TODO hard-code this for now
  if (isCDMA) {
    info = {
      isPresent:  Buf.readUint32(),
      signalType: Buf.readUint32(),
      alertPitch: Buf.readUint32(),
      signal:     Buf.readUint32()
    };
  }
  Phone.onCallRing(info);
};
RIL[UNSOLICITED_RESPONSE_SIM_STATUS_CHANGED] = null;
RIL[UNSOLICITED_RESPONSE_CDMA_NEW_SMS] = null;
RIL[UNSOLICITED_RESPONSE_NEW_BROADCAST_SMS] = null;
RIL[UNSOLICITED_CDMA_RUIM_SMS_STORAGE_FULL] = null;
RIL[UNSOLICITED_RESTRICTED_STATE_CHANGED] = null;
RIL[UNSOLICITED_ENTER_EMERGENCY_CALLBACK_MODE] = null;
RIL[UNSOLICITED_CDMA_CALL_WAITING] = null;
RIL[UNSOLICITED_CDMA_OTA_PROVISION_STATUS] = null;
RIL[UNSOLICITED_CDMA_INFO_REC] = null;
RIL[UNSOLICITED_OEM_HOOK_RAW] = null;
RIL[UNSOLICITED_RINGBACK_TONE] = null;
RIL[UNSOLICITED_RESEND_INCALL_MUTE] = null;


/**
 * This object represents the phone's state and functionality. It is
 * essentially a state machine that's being acted upon from RIL and the
 * mainthread via postMessage communication.
 */
let Phone = {

  //XXX TODO beware, this is just demo code. It's still missing
  // communication with the UI thread.

  /**
   * One of the RADIO_STATE_* constants.
   */
  radioState: RADIO_STATE_UNAVAILABLE,

  /**
   * Strings
   */
  IMEI: null,
  IMEISV: null,
  IMSI: null,

  /**
   * List of strings identifying the network operator.
   */
  operator: null,

  /**
   * String containing the baseband version.
   */
  basebandVersion: null,

  /**
   * Network selection mode. 0 for automatic, 1 for manual selection.
   */
  networkSelectionMode: null,

  /**
   * ICC card status
   */
  iccStatus: null,

  /**
   * Active calls
   */
  currentCalls: {},

  /**
   * Handlers for messages from the RIL. They all begin with on* and are called
   * from RIL object.
   */

  onRadioStateChanged: function onRadioStateChanged(newState) {
    debug("Radio state changed from " + this.radioState + " to " + newState);
    if (this.radioState == newState) {
      // No change in state, return.
      return;
    }

    let gsm = newState == RADIO_STATE_SIM_NOT_READY        ||
              newState == RADIO_STATE_SIM_LOCKED_OR_ABSENT ||
              newState == RADIO_STATE_SIM_READY;
    let cdma = newState == RADIO_STATE_RUIM_NOT_READY       ||
               newState == RADIO_STATE_RUIM_READY            ||
               newState == RADIO_STATE_RUIM_LOCKED_OR_ABSENT ||
               newState == RADIO_STATE_NV_NOT_READY          ||
               newState == RADIO_STATE_NV_READY;

    // Figure out state transitions and send out more RIL requests as necessary
    // as well as events to the main thread.

    if (this.radioState == RADIO_STATE_UNAVAILABLE &&
        newState != RADIO_STATE_UNAVAILABLE) {
      // The radio became available, let's get its info.
      if (gsm) {
        RIL.getIMEI();
        RIL.getIMEISV();
      }
      if (cdma) {
        RIL.getDeviceIdentity();
      }
      Buf.simpleRequest(REQUEST_BASEBAND_VERSION);
      RIL.setScreenState(true);
      this.sendDOMMessage({
        type: "radiostatechange",
        radioState: (newState == RADIO_STATE_OFF) ?
                     DOM_RADIOSTATE_OFF : DOM_RADIOSTATE_READY
      });

      //XXX TODO For now, just turn the radio on if it's off. for the real
      // deal we probably want to do the opposite: start with a known state
      // when we boot up and let the UI layer control the radio power.
      if (newState == RADIO_STATE_OFF) {
        RIL.setRadioPower(true);
      }
    }

    if (newState == RADIO_STATE_UNAVAILABLE) {
      // The radio is no longer available, we need to deal with any
      // remaining pending requests.
      //TODO do that

      this.sendDOMMessage({type: "radiostatechange",
                           radioState: DOM_RADIOSTATE_UNAVAILABLE});
    }

    if (newState == RADIO_STATE_SIM_READY  ||
        newState == RADIO_STATE_RUIM_READY ||
        newState == RADIO_STATE_NV_READY) {
      // The ICC card has become available. Get all the things.
      RIL.getICCStatus();
      this.requestNetworkInfo();
      RIL.getSignalStrength();
      this.sendDOMMessage({type: "cardstatechange",
                           cardState: DOM_CARDSTATE_READY});
    }
    if (newState == RADIO_STATE_SIM_LOCKED_OR_ABSENT  ||
        newState == RADIO_STATE_RUIM_LOCKED_OR_ABSENT) {
      RIL.getICCStatus();
      this.sendDOMMessage({type: "cardstatechange",
                           cardState: DOM_CARDSTATE_UNAVAILABLE});
    }

    let wasOn = this.radioState != RADIO_STATE_OFF &&
                this.radioState != RADIO_STATE_UNAVAILABLE;
    let isOn = newState != RADIO_STATE_OFF &&
               newState != RADIO_STATE_UNAVAILABLE;
    if (!wasOn && isOn) {
      //TODO
    }
    if (wasOn && !isOn) {
      //TODO
    }

    this.radioState = newState;
  },

  onCurrentCalls: function onCurrentCalls(newCalls) {
    // Go through the calls we currently have on file and see if any of them
    // changed state. Remove them from the newCalls map as we deal with them
    // so that only new calls remain in the map after we're done.
    for each (let currentCall in this.currentCalls) {
      let callIndex = currentCall.index;
      let newCall;
      if (newCalls) {
        newCall = newCalls[callIndex];
        delete newCalls[callIndex];
      }

      if (!newCall) {
        // Call is no longer reported by the radio. Send disconnected
        // state change.
        this.sendDOMMessage({type:      "callstatechange",
                             callState:  DOM_CALL_READYSTATE_DISCONNECTED,
                             callIndex:  callIndex,
                             number:     currentCall.number,
                             name:       currentCall.name});
        delete this.currentCalls[callIndex];
        continue;
      }

      if (newCall.state == currentCall.state) {
        continue;
      }

      this._handleChangedCallState(newCall);
    }

    // Go through any remaining calls that are new to us.
    for each (let newCall in newCalls) {
      if (newCall.isVoice) {
        this._handleChangedCallState(newCall);
      }
    }
  },

  _handleChangedCallState: function handleChangedCallState(newCall) {
    // Format international numbers appropriately.
    if (newCall.number &&
        newCall.toa == TOA_INTERNATIONAL &&
        newCall.number[0] != "+") {
      newCall.number = "+" + newCall.number;
    }
    this.currentCalls[newCall.index] = newCall;
    this.sendDOMMessage({type:      "callstatechange",
                         callState: RIL_TO_DOM_CALL_STATE[newCall.state],
                         callIndex: newCall.index,
                         number:    newCall.number,
                         name:      newCall.name});
  },

  onCallStateChanged: function onCallStateChanged() {
    RIL.getCurrentCalls();
  },

  onCallRing: function onCallRing(info) {
    // For now we don't need to do anything here because we'll also get a
    // call state changed notification.
  },

  onNetworkStateChanged: function onNetworkStateChanged() {
    debug("Network state changed, re-requesting phone state.");
    this.requestNetworkInfo();
  },

  onICCStatus: function onICCStatus(iccStatus) {
    debug("SIM card state is " + iccStatus.cardState);
    debug("Universal PIN state is " + iccStatus.universalPINState);
    debug(iccStatus);
    //TODO set to simStatus and figure out state transitions.
    this.iccStatus = iccStatus; //XXX TODO
  },

  onEnterICCPIN: function onEnterICCPIN(response) {
    debug("REQUEST_ENTER_SIM_PIN returned " + response);
    //TODO
  },

  onNetworkSelectionMode: function onNetworkSelectionMode(mode) {
    this.networkSelectionMode = mode[0];
  },

  onBasebandVersion: function onBasebandVersion(version) {
    this.basebandVersion = version;
  },

  onIMSI: function onIMSI(imsi) {
    this.IMSI = imsi;
  },

  onIMEI: function onIMEI(imei) {
    this.IMEI = imei;
  },

  onIMEISV: function onIMEISV(imeiSV) {
    this.IMEISV = imeiSV;
  },

  onRegistrationState: function onRegistrationState(newState) {
    this.registrationState = newState;
  },

  onGPRSRegistrationState: function onGPRSRegistrationState(newState) {
    this.gprsRegistrationState = newState;
  },

  onOperator: function onOperator(operator) {
    if (operator.length < 3) {
      debug("Expected at least 3 strings for operator.");
    }
    if (!this.operator ||
        this.operator.alphaLong  != operator[0] ||
        this.operator.alphaShort != operator[1] ||
        this.operator.numeric    != operator[2]) {
      this.operator = {alphaLong:  operator[0],
                       alphaShort: operator[1],
                       numeric:    operator[2]};
      this.sendDOMMessage({type: "operatorchange",
                           operator: this.operator});
    }
  },

  onSignalStrength: function onSignalStrength(strength) {
    debug("Signal strength " + JSON.stringify(strength));
    this.sendDOMMessage({type: "signalstrengthchange",
                         signalStrength: strength});
  },

  onDial: function onDial() {
  },

  onHangUp: function onHangUp() {
  },

  onAnswerCall: function onAnswerCall() {
  },

  onRejectCall: function onRejectCall() {
  },

  onSetMute: function onSetMute() {
  },

  onSendTone: function onSendTone() {
  },

  onStartTone: function onStartTone() {
  },

  onStopTone: function onStopTone() {
  },

  onSendSMS: function onSendSMS(messageRef, ackPDU, errorCode) {
    //TODO
  },

  /**
   * Outgoing requests to the RIL. These can be triggered from the
   * main thread via messages that look like this:
   *
   *   {type:  "methodName",
   *    extra: "parameters",
   *    go:    "here"}
   *
   * So if one of the following methods takes arguments, it takes only one,
   * an object, which then contains all of the parameters as attributes.
   * The "@param" documentation is to be interpreted accordingly.
   */

  /**
   * Request various states about the network.
   */
  requestNetworkInfo: function requestNetworkInfo() {
    if (DEBUG) debug("Requesting phone state");
    RIL.getRegistrationState();
    RIL.getGPRSRegistrationState(); //TODO only GSM
    RIL.getOperator();
    RIL.getNetworkSelectionMode();
  },

  /**
   * Dial the phone.
   *
   * @param number
   *        String containing the number to dial.
   */
  dial: function dial(options) {
    RIL.dial(options.number, 0, 0);
  },

  /**
   * Send DTMF Tone
   *
   * @param dtmfChar
   *        String containing the DTMF signal to send.
   */
  sendTone: function sendTone(options) {
    RIL.sendTone(options.dtmfChar);
  },

  /**
   * Start DTMF Tone
   *
   * @param dtmfChar
   *        String containing the DTMF signal to send.
   */
  startTone: function startTone(options) {
    RIL.startTone(options.dtmfChar);
  },

  /**
   * Stop DTMF Tone
   */
  stopTone: function stopTone() {
    RIL.stopTone();
  },

  /**
   * Hang up a call.
   *
   * @param callIndex
   *        Call index of the call to hang up.
   */
  hangUp: function hangUp(options) {
    //TODO need to check whether call is holding/waiting/background
    // and then use REQUEST_HANGUP_WAITING_OR_BACKGROUND
    RIL.hangUp(options.callIndex);
  },

  /**
   * Mute or unmute the radio.
   *
   * @param mute
   *        Boolean to indicate whether to mute or unmute the radio.
   */
  setMute: function setMute(options) {
    //TODO need to check whether call is holding/waiting/background
    // and then use REQUEST_HANGUP_WAITING_OR_BACKGROUND
    RIL.setMute(options.mute);
  },

  /**
   * Answer an incoming call.
   */
  answerCall: function answerCall() {
    RIL.answerCall();
  },

  /**
   * Reject an incoming call.
   */
  rejectCall: function rejectCall() {
    RIL.rejectCall();
  },

  /**
   * Send an SMS.
   *
   * @param number
   *        String containing the recipient number.
   * @param message
   *        String containing the message text.
   */
  sendSMS: function sendSMS(options) {
    //TODO munge options.number and options.message into PDU format
    let smscPDU = "";
    let pdu = "";
    RIL.sendSMS(smscPDU, pdu);
  },

  /**
   * Handle incoming messages from the main UI thread.
   *
   * @param message
   *        Object containing the message. Messages are supposed
   */
  handleDOMMessage: function handleMessage(message) {
    if (DEBUG) debug("Received DOM message " + JSON.stringify(message));
    let method = this[message.type];
    if (typeof method != "function") {
      debug("Don't know what to do with message " + JSON.stringify(message));
      return;
    }
    method.call(this, message);
  },

  /**
   * Send messages to the main UI thread.
   */
  sendDOMMessage: function sendDOMMessage(message) {
    postMessage(message, "*");
  }

};


/**
 * Global stuff.
 */

if (!this.debug) {
  // Debugging stub that goes nowhere.
  this.debug = function debug(message) {
    dump("RIL Worker: " + message + "\n");
  };
}

// Initialize buffers. This is a separate function so that unit tests can
// re-initialize the buffers at will.
Buf.init();

function onRILMessage(data) {
  Buf.processIncoming(data);
};

onmessage = function onmessage(event) {
  Phone.handleDOMMessage(event.data);
};

onerror = function onerror(event) {
  debug("RIL Worker error" + event.message + "\n");
};

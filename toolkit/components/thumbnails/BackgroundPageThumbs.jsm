/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const EXPORTED_SYMBOLS = [
  "BackgroundPageThumbs",
];

const DEFAULT_CAPTURE_TIMEOUT = 30000; // ms
const DESTROY_BROWSER_TIMEOUT = 60000; // ms
const FRAME_SCRIPT_URL = "chrome://global/content/backgroundPageThumbsContent.js";

const XUL_NS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
const HTML_NS = "http://www.w3.org/1999/xhtml";

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;

Cu.import("resource://gre/modules/PageThumbs.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/PrivateBrowsingUtils.jsm");

const BackgroundPageThumbs = {

  /**
   * Asynchronously captures a thumbnail of the given URL.
   *
   * The page is loaded anonymously, and plug-ins are disabled.
   *
   * @param url      The URL to capture.
   * @param options  An optional object that configures the capture.  Its
   *                 properties are the following, and all are optional:
   * @opt onDone     A function that will be asynchronously called when the
   *                 capture is complete or times out.  It's called as
   *                   onDone(url),
   *                 where `url` is the captured URL.
   * @opt timeout    The capture will time out after this many milliseconds have
   *                 elapsed after calling this method.  Defaults to 30000 (30
   *                 seconds).
   */
  capture: function (url, options={}) {
    if (isPrivateBrowsingActive()) {
      // There's only one, global private-browsing state shared by all private
      // windows and the thumbnail browser.  Just as if you log into a site in
      // one private window you're logged in in all private windows, you're also
      // logged in in the thumbnail browser.  A crude way to avoid capturing
      // sites in this situation is to refuse to capture at all when any private
      // windows are open.  See bug 870179.
      if (options.onDone)
        Services.tm.mainThread.dispatch(options.onDone.bind(options, url), 0);
      return;
    }
    let cap = new Capture(url, this._onCaptureOrTimeout.bind(this), options);
    this._captureQueue = this._captureQueue || [];
    this._captureQueue.push(cap);
    this._processCaptureQueue();
  },

  /**
   * Ensures that initialization of the thumbnail browser's parent window has
   * begun.
   *
   * @return  True if the parent window is completely initialized and can be
   *          used, and false if initialization has started but not completed.
   */
  _ensureParentWindowReady: function () {
    if (this._parentWin)
      // Already fully initialized.
      return true;
    if (this._startedParentWinInit)
      // Already started initializing.
      return false;

    this._startedParentWinInit = true;

    PrivateBrowsingUtils.whenHiddenPrivateWindowReady(function (parentWin) {
      parentWin.addEventListener("unload", function (event) {
        if (event.target == parentWin.document)
          this._destroy();
      }.bind(this), true);

      if (canHostBrowser(parentWin)) {
        this._parentWin = parentWin;
        this._processCaptureQueue();
        return;
      }

      // Otherwise, create an html:iframe, stick it in the parent document, and
      // use it to host the browser.  about:blank will not have the system
      // principal, so it can't host, but a document with a chrome URI will.
      let iframe = parentWin.document.createElementNS(HTML_NS, "iframe");
      iframe.setAttribute("src", "chrome://global/content/mozilla.xhtml");
      let onLoad = function onLoadFn() {
        iframe.removeEventListener("load", onLoad, true);
        this._parentWin = iframe.contentWindow;
        this._processCaptureQueue();
      }.bind(this);
      iframe.addEventListener("load", onLoad, true);
      parentWin.document.documentElement.appendChild(iframe);
      this._hostIframe = iframe;
    }.bind(this));

    return false;
  },

  /**
   * Destroys the service.  Queued and pending captures will never complete, and
   * their consumer callbacks will never be called.
   */
  _destroy: function () {
    if (this._captureQueue)
      this._captureQueue.forEach(cap => cap.destroy());
    this._destroyBrowser();
    if (this._hostIframe)
      this._hostIframe.remove();
    delete this._captureQueue;
    delete this._hostIframe;
    delete this._startedParentWinInit;
    delete this._parentWin;
  },

  /**
   * Creates the thumbnail browser if it doesn't already exist.
   */
  _ensureBrowser: function () {
    if (this._thumbBrowser)
      return;

    let browser = this._parentWin.document.createElementNS(XUL_NS, "browser");
    browser.setAttribute("type", "content");
    browser.setAttribute("remote", "true");
    browser.setAttribute("privatebrowsing", "true");

    // Size the browser.  Setting the width and height attributes doesn't
    // work -- the resulting thumbnails are blank and transparent -- but
    // setting the style does.
    let width = {};
    let height = {};
    Cc["@mozilla.org/gfx/screenmanager;1"].
      getService(Ci.nsIScreenManager).
      primaryScreen.
      GetRectDisplayPix({}, {}, width, height);
    browser.style.width = width.value + "px";
    browser.style.height = height.value + "px";

    this._parentWin.document.documentElement.appendChild(browser);

    browser.messageManager.loadFrameScript(FRAME_SCRIPT_URL, false);
    this._thumbBrowser = browser;
  },

  _destroyBrowser: function () {
    if (!this._thumbBrowser)
      return;
    this._thumbBrowser.remove();
    delete this._thumbBrowser;
  },

  /**
   * Starts the next capture if the queue is not empty and the service is fully
   * initialized.
   */
  _processCaptureQueue: function () {
    if (!this._captureQueue.length ||
        this._captureQueue[0].pending ||
        !this._ensureParentWindowReady())
      return;

    // Ready to start the first capture in the queue.
    this._ensureBrowser();
    this._captureQueue[0].start(this._thumbBrowser.messageManager);
    if (this._destroyBrowserTimer) {
      this._destroyBrowserTimer.cancel();
      delete this._destroyBrowserTimer;
    }
  },

  /**
   * Called when a capture completes or times out.
   */
  _onCaptureOrTimeout: function (capture) {
    // Since timeouts are configurable per capture, and a capture's timeout
    // timer starts when it's created, it's possible for any capture to time
    // out regardless of its position in the queue.
    let idx = this._captureQueue.indexOf(capture);
    if (idx < 0)
      throw new Error("The capture should be in the queue.");
    this._captureQueue.splice(idx, 1);

    // Start the destroy-browser timer *before* processing the capture queue.
    let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    timer.initWithCallback(this._destroyBrowser.bind(this),
                           this._destroyBrowserTimeout,
                           Ci.nsITimer.TYPE_ONE_SHOT);
    this._destroyBrowserTimer = timer;

    this._processCaptureQueue();
  },

  _destroyBrowserTimeout: DESTROY_BROWSER_TIMEOUT,
};

/**
 * Represents a single capture request in the capture queue.
 *
 * @param url              The URL to capture.
 * @param captureCallback  A function you want called when the capture
 *                         completes.
 * @param options          The capture options.
 */
function Capture(url, captureCallback, options) {
  this.url = url;
  this.captureCallback = captureCallback;
  this.options = options;
  this.id = Capture.nextID++;

  // The timeout starts when the consumer requests the capture, not when the
  // capture is dequeued and started.
  let timeout = typeof(options.timeout) == "number" ? options.timeout :
                DEFAULT_CAPTURE_TIMEOUT;
  this._timeoutTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  this._timeoutTimer.initWithCallback(this, timeout, Ci.nsITimer.TYPE_ONE_SHOT);
}

Capture.prototype = {

  get pending() {
    return !!this._msgMan;
  },

  /**
   * Sends a message to the content script to start the capture.
   *
   * @param messageManager  The nsIMessageSender of the thumbnail browser.
   */
  start: function (messageManager) {
    this._msgMan = messageManager;
    this._msgMan.sendAsyncMessage("BackgroundPageThumbs:capture",
                                  { id: this.id, url: this.url });
    this._msgMan.addMessageListener("BackgroundPageThumbs:didCapture", this);
  },

  /**
   * The only intended external use of this method is by the service when it's
   * uninitializing and doing things like destroying the thumbnail browser.  In
   * that case the consumer's completion callback will never be called.
   *
   * This method is not idempotent.  It's an error to call it more than once on
   * the same capture.
   */
  destroy: function () {
    this._timeoutTimer.cancel();
    delete this._timeoutTimer;
    if (this._msgMan) {
      // The capture may have never started, so _msgMan may be undefined.
      this._msgMan.removeMessageListener("BackgroundPageThumbs:didCapture",
                                         this);
      delete this._msgMan;
    }
    delete this.captureCallback;
  },

  // Called when the didCapture message is received.
  receiveMessage: function (msg) {
    // A different timed-out capture may have finally successfully completed, so
    // discard messages that aren't meant for this capture.
    if (msg.json.id == this.id)
      this._done(msg.json);
  },

  // Called when the timeout timer fires.
  notify: function () {
    this._done(null);
  },

  _done: function (data) {
    // Note that _done will be called only once, by either receiveMessage or
    // notify, since it calls destroy, which cancels the timeout timer and
    // removes the didCapture message listener.

    this.captureCallback(this);
    this.destroy();

    let callOnDone = function callOnDoneFn() {
      if (!("onDone" in this.options))
        return;
      try {
        this.options.onDone(this.url);
      }
      catch (err) {
        Cu.reportError(err);
      }
    }.bind(this);

    if (!data) {
      callOnDone();
      return;
    }
    PageThumbs._store(this.url, data.finalURL, data.imageData).then(callOnDone);
  },
};

Capture.nextID = 0;

/**
 * Returns true if the given window is suitable for hosting our xul:browser.
 *
 * @param win  The window.
 * @return     True if the window can host the browser, false otherwise.
 */
function canHostBrowser(win) {
  // The host document needs to have the system principal since, like all code
  // intended to be used in chrome, the browser binding does lots of things that
  // assume it has it.  The document must also allow XUL children.  So check for
  // both the system principal and the "allowXULXBL" permission.  (It turns out
  // that allowXULXBL is satisfied by the system principal alone, making that
  // check not strictly necessary, but it's here for robustness.)
  let principal = win.document.nodePrincipal;
  if (!Services.scriptSecurityManager.isSystemPrincipal(principal))
    return false;
  let permResult = Services.perms.testPermissionFromPrincipal(principal,
                                                              "allowXULXBL");
  return permResult == Ci.nsIPermissionManager.ALLOW_ACTION;
}

/**
 * Returns true if there are any private windows.
 */
function isPrivateBrowsingActive() {
  let wins = Services.ww.getWindowEnumerator();
  while (wins.hasMoreElements())
    if (PrivateBrowsingUtils.isWindowPrivate(wins.getNext()))
      return true;
  return false;
}

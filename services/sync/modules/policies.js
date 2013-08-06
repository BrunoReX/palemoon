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
 * The Original Code is Firefox Sync.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Marina Samuel <msamuel@mozilla.com>
 *  Philipp von Weitershausen <philipp@weitershausen.de>
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


const EXPORTED_SYMBOLS = ["SyncScheduler"];

const Cu = Components.utils;

Cu.import("resource://services-sync/constants.js");
Cu.import("resource://services-sync/log4moz.js");
Cu.import("resource://services-sync/util.js");
Cu.import("resource://services-sync/engines.js");
Cu.import("resource://services-sync/engines/clients.js");
Cu.import("resource://services-sync/status.js");

Cu.import("resource://services-sync/main.js");    // So we can get to Service for callbacks.

let SyncScheduler = {
  _log: Log4Moz.repository.getLogger("Sync.SyncScheduler"),

  /**
   * The nsITimer object that schedules the next sync. See scheduleNextSync().
   */
  syncTimer: null,

  setDefaults: function setDefaults() {
    this._log.trace("Setting SyncScheduler policy values to defaults.");

    this.singleDeviceInterval = Svc.Prefs.get("scheduler.singleDeviceInterval") * 1000;
    this.idleInterval         = Svc.Prefs.get("scheduler.idleInterval")         * 1000;
    this.activeInterval       = Svc.Prefs.get("scheduler.activeInterval")       * 1000;
    this.immediateInterval    = Svc.Prefs.get("scheduler.immediateInterval")    * 1000;

    // A user is non-idle on startup by default.
    this.idle = false;

    this.hasIncomingItems = false;

    this.clearSyncTriggers();
  },

  // nextSync is in milliseconds, but prefs can't hold that much
  get nextSync() Svc.Prefs.get("nextSync", 0) * 1000,
  set nextSync(value) Svc.Prefs.set("nextSync", Math.floor(value / 1000)),

  get syncInterval() Svc.Prefs.get("syncInterval", this.singleDeviceInterval),
  set syncInterval(value) Svc.Prefs.set("syncInterval", value),

  get syncThreshold() Svc.Prefs.get("syncThreshold", SINGLE_USER_THRESHOLD),
  set syncThreshold(value) Svc.Prefs.set("syncThreshold", value),

  get globalScore() Svc.Prefs.get("globalScore", 0),
  set globalScore(value) Svc.Prefs.set("globalScore", value),

  get numClients() Svc.Prefs.get("numClients", 0),
  set numClients(value) Svc.Prefs.set("numClients", value),

  init: function init() {
    this._log.level = Log4Moz.Level[Svc.Prefs.get("log.logger.service.main")];
    this.setDefaults();
    Svc.Obs.add("weave:engine:score:updated", this);
    Svc.Obs.add("network:offline-status-changed", this);
    Svc.Obs.add("weave:service:sync:start", this);
    Svc.Obs.add("weave:service:sync:finish", this);
    Svc.Obs.add("weave:engine:sync:finish", this);
    Svc.Obs.add("weave:service:login:error", this);
    Svc.Obs.add("weave:service:logout:finish", this);
    Svc.Obs.add("weave:service:sync:error", this);
    Svc.Obs.add("weave:service:backoff:interval", this);
    Svc.Obs.add("weave:service:ready", this);
    Svc.Obs.add("weave:engine:sync:applied", this);
    Svc.Obs.add("weave:service:setup-complete", this);
    Svc.Obs.add("weave:service:start-over", this);
				
    if (Status.checkSetup() == STATUS_OK) {
      Svc.Idle.addIdleObserver(this, Svc.Prefs.get("scheduler.idleTime"));
    }
  },

  observe: function observe(subject, topic, data) {
    switch(topic) {
      case "weave:engine:score:updated":
        Utils.namedTimer(this.calculateScore, SCORE_UPDATE_DELAY, this,
                         "_scoreTimer");
        break;
      case "network:offline-status-changed":
        // Whether online or offline, we'll reschedule syncs
        this._log.trace("Network offline status change: " + data);
        this.checkSyncStatus();
        break;
      case "weave:service:sync:start":
        // Clear out any potentially pending syncs now that we're syncing
        this.clearSyncTriggers();

        // reset backoff info, if the server tells us to continue backing off,
        // we'll handle that later
        Status.resetBackoff();

        this.globalScore = 0;
        break;
      case "weave:service:sync:finish":
        this.nextSync = 0;
        this.adjustSyncInterval();

        let sync_interval;
        this._syncErrors = 0;

        if (Status.sync == NO_SYNC_NODE_FOUND) {
          this._log.trace("Scheduling a sync at interval NO_SYNC_NODE_FOUND.");
          sync_interval = NO_SYNC_NODE_INTERVAL;
        }
        this.scheduleNextSync(sync_interval);
        break;
      case "weave:engine:sync:finish":
        if (data == "clients") {
          // Update the client mode because it might change what we sync.
          this.updateClientMode();
        }
        break;
      case "weave:service:login:error":
        this.clearSyncTriggers();
        
        // Try again later, just as if we threw an error... only without the
        // error count.
        if (Status.login == MASTER_PASSWORD_LOCKED) {
          this._log.debug("Couldn't log in: master password is locked.");
          this._log.trace("Scheduling a sync at MASTER_PASSWORD_LOCKED_RETRY_INTERVAL");
          this.scheduleAtInterval(MASTER_PASSWORD_LOCKED_RETRY_INTERVAL);
        }
        break;
      case "weave:service:logout:finish":
        // Start or cancel the sync timer depending on if
        // logged in or logged out
        this.checkSyncStatus();
        break; 
      case "weave:service:sync:error":
        // There may be multiple clients but if the sync fails, client mode
        // should still be updated so that the next sync has a correct interval.
        this.updateClientMode();
        this.adjustSyncInterval();
        this.nextSync = 0;
        this.handleSyncError();
        break;
      case "weave:service:backoff:interval":
        let requested_interval = subject * 1000;
        // Leave up to 25% more time for the back off.
        let interval = requested_interval * (1 + Math.random() * 0.25);
        Status.backoffInterval = interval;
        Status.minimumNextSync = Date.now() + requested_interval;
        break;
      case "weave:service:ready":
        // Applications can specify this preference if they want autoconnect
        // to happen after a fixed delay.
        let delay = Svc.Prefs.get("autoconnectDelay");
        if (delay) {
          this.delayedAutoConnect(delay);
        }
        break;
      case "weave:engine:sync:applied":
        let numItems = subject.applied;
        this._log.trace("Engine " + data + " applied " + numItems + " items.");
        if (numItems) 
          this.hasIncomingItems = true;
        break;
      case "weave:service:setup-complete":
         Svc.Idle.addIdleObserver(this, Svc.Prefs.get("scheduler.idleTime"));
         break;
      case "weave:service:start-over":
         Svc.Idle.removeIdleObserver(this, Svc.Prefs.get("scheduler.idleTime"));
         SyncScheduler.setDefaults();
         break;
      case "idle":
        this._log.trace("We're idle.");
        this.idle = true;
        // Adjust the interval for future syncs. This won't actually have any
        // effect until the next pending sync (which will happen soon since we
        // were just active.)
        this.adjustSyncInterval();
        break;
      case "back":
        this._log.trace("Received notification that we're back from idle.");
        this.idle = false;
        Utils.namedTimer(function onBack() {
          if (this.idle) {
            this._log.trace("... and we're idle again. " +
                            "Ignoring spurious back notification.");
            return;
          }

          this._log.trace("Genuine return from idle. Syncing.");
          // Trigger a sync if we have multiple clients.
          if (this.numClients > 1) {
            this.scheduleNextSync(0);
          }
        }, IDLE_OBSERVER_BACK_DELAY, this, "idleDebouncerTimer");
        break;
    }
  },

  adjustSyncInterval: function adjustSyncInterval() {
    if (this.numClients <= 1) {
      this._log.trace("Adjusting syncInterval to singleDeviceInterval.");
      this.syncInterval = this.singleDeviceInterval;
      return;
    }
    // Only MULTI_DEVICE clients will enter this if statement
    // since SINGLE_USER clients will be handled above.
    if (this.idle) {
      this._log.trace("Adjusting syncInterval to idleInterval.");
      this.syncInterval = this.idleInterval;
      return;
    }

    if (this.hasIncomingItems) {
      this._log.trace("Adjusting syncInterval to immediateInterval.");
      this.hasIncomingItems = false;
      this.syncInterval = this.immediateInterval;
    } else {
      this._log.trace("Adjusting syncInterval to activeInterval.");
      this.syncInterval = this.activeInterval;
    }
  },

  calculateScore: function calculateScore() {
    var engines = Engines.getEnabled();
    for (let i = 0;i < engines.length;i++) {
      this._log.trace(engines[i].name + ": score: " + engines[i].score);
      this.globalScore += engines[i].score;
      engines[i]._tracker.resetScore();
    }

    this._log.trace("Global score updated: " + this.globalScore);
    this.checkSyncStatus();
  },

  /**
   * Process the locally stored clients list to figure out what mode to be in
   */
  updateClientMode: function updateClientMode() {
    // Nothing to do if it's the same amount
    let {numClients} = Clients.stats;	
    if (this.numClients == numClients)
      return;

    this._log.debug("Client count: " + this.numClients + " -> " + numClients);
    this.numClients = numClients;

    if (numClients <= 1) {
      this._log.trace("Adjusting syncThreshold to SINGLE_USER_THRESHOLD");
      this.syncThreshold = SINGLE_USER_THRESHOLD;
    } else {
      this._log.trace("Adjusting syncThreshold to MULTI_DEVICE_THRESHOLD");
      this.syncThreshold = MULTI_DEVICE_THRESHOLD;
    }
    this.adjustSyncInterval();
  },

  /**
   * Check if we should be syncing and schedule the next sync, if it's not scheduled
   */
  checkSyncStatus: function checkSyncStatus() {
    // Should we be syncing now, if not, cancel any sync timers and return
    // if we're in backoff, we'll schedule the next sync.
    let ignore = [kSyncBackoffNotMet, kSyncMasterPasswordLocked];
    let skip = Weave.Service._checkSync(ignore);
    this._log.trace("_checkSync returned \"" + skip + "\".");
    if (skip) {
      this.clearSyncTriggers();
      return;
    }

    // Only set the wait time to 0 if we need to sync right away
    let wait;
    if (this.globalScore > this.syncThreshold) {
      this._log.debug("Global Score threshold hit, triggering sync.");
      wait = 0;
    }
    this.scheduleNextSync(wait);
  },

  /**
   * Call sync() if Master Password is not locked.
   * 
   * Otherwise, reschedule a sync for later.
   */
  syncIfMPUnlocked: function syncIfMPUnlocked() {
    // No point if we got kicked out by the master password dialog.
    if (Status.login == MASTER_PASSWORD_LOCKED &&
        Utils.mpLocked()) {
      this._log.debug("Not initiating sync: Login status is " + Status.login);

      // If we're not syncing now, we need to schedule the next one.
      this._log.trace("Scheduling a sync at MASTER_PASSWORD_LOCKED_RETRY_INTERVAL");
      this.scheduleAtInterval(MASTER_PASSWORD_LOCKED_RETRY_INTERVAL);
      return;
    }

    Utils.nextTick(Weave.Service.sync, Weave.Service);
  },

  /**
   * Set a timer for the next sync
   */
  scheduleNextSync: function scheduleNextSync(interval) {
    // If no interval was specified, use the current sync interval.
    if (interval == null) {
      interval = this.syncInterval;
    }

    // Ensure the interval is set to no less than the backoff.
    if (Status.backoffInterval && interval < Status.backoffInterval) {
      this._log.trace("Requested interval " + interval +
                      " ms is smaller than the backoff interval. " + 
                      "Using backoff interval " +
                      Status.backoffInterval + " ms instead.");
      interval = Status.backoffInterval;
    }

    if (this.nextSync != 0) {
      // There's already a sync scheduled. Don't reschedule if there's already
      // a timer scheduled for sooner than requested.
      let currentInterval = this.nextSync - Date.now();
      this._log.trace("There's already a sync scheduled in " +
                      currentInterval + " ms.");
      if (currentInterval < interval && this.syncTimer) {
        this._log.trace("Ignoring scheduling request for next sync in " +
                        interval + " ms.");
        return;
      }
    }

    // Start the sync right away if we're already late.
    if (interval <= 0) {
      this._log.trace("Requested sync should happen right away.");
      this.syncIfMPUnlocked();
      return;
    }

    this._log.debug("Next sync in " + interval + " ms.");
    Utils.namedTimer(this.syncIfMPUnlocked, interval, this, "syncTimer");

    // Save the next sync time in-case sync is disabled (logout/offline/etc.)
    this.nextSync = Date.now() + interval;
  },


  /**
   * Incorporates the backoff/retry logic used in error handling and elective
   * non-syncing.
   */
  scheduleAtInterval: function scheduleAtInterval(minimumInterval) {
    let interval = Utils.calculateBackoff(this._syncErrors, MINIMUM_BACKOFF_INTERVAL);
    if (minimumInterval) {
      interval = Math.max(minimumInterval, interval);
    }

    this._log.debug("Starting client-initiated backoff. Next sync in " +
                    interval + " ms.");
    this.scheduleNextSync(interval);
  },

 /**
  * Automatically start syncing after the given delay (in seconds).
  *
  * Applications can define the `services.sync.autoconnectDelay` preference
  * to have this called automatically during start-up with the pref value as
  * the argument. Alternatively, they can call it themselves to control when
  * Sync should first start to sync.
  */
  delayedAutoConnect: function delayedAutoConnect(delay) {
    if (Weave.Service._checkSetup() == STATUS_OK) {
      Utils.namedTimer(this.autoConnect, delay * 1000, this, "_autoTimer");
    }
  },

  autoConnect: function autoConnect() {
    if (Weave.Service._checkSetup() == STATUS_OK && !Weave.Service._checkSync()) {
      // Schedule a sync based on when a previous sync was scheduled.
      // scheduleNextSync() will do the right thing if that time lies in
      // the past.
      this.scheduleNextSync(this.nextSync - Date.now());
    }

    // Once autoConnect is called we no longer need _autoTimer.
    if (this._autoTimer) {
      this._autoTimer.clear();
    }
  },

  _syncErrors: 0,
  /**
   * Deal with sync errors appropriately
   */
  handleSyncError: function handleSyncError() {
    this._syncErrors++;

    // Do nothing on the first couple of failures, if we're not in
    // backoff due to 5xx errors.
    if (!Status.enforceBackoff) {
      if (this._syncErrors < MAX_ERROR_COUNT_BEFORE_BACKOFF) {
        this.scheduleNextSync();
        return;
      }
      Status.enforceBackoff = true;
    }

    this.scheduleAtInterval();
  },


  /**
   * Remove any timers/observers that might trigger a sync
   */
  clearSyncTriggers: function clearSyncTriggers() {
    this._log.debug("Clearing sync triggers.");

    // Clear out any scheduled syncs
    if (this.syncTimer)
      this.syncTimer.clear();
  }

};

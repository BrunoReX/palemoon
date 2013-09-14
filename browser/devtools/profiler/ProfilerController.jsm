/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/devtools/dbg-client.jsm");
Cu.import("resource://gre/modules/devtools/Console.jsm");
Cu.import("resource://gre/modules/AddonManager.jsm");

let EXPORTED_SYMBOLS = ["ProfilerController"];

XPCOMUtils.defineLazyModuleGetter(this, "gDevTools",
  "resource:///modules/devtools/gDevTools.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "DebuggerServer",
  "resource://gre/modules/devtools/dbg-server.jsm");

/**
 * Data structure that contains information that has
 * to be shared between separate ProfilerController
 * instances.
 */
const sharedData = {
  data: new WeakMap(),
  controllers: new WeakMap(),
};

/**
 * Makes a structure representing an individual profile.
 */
function makeProfile(name, def={}) {
  if (def.timeStarted == null)
    def.timeStarted = null;

  if (def.timeEnded == null)
    def.timeEnded = null;

  return {
    name: name,
    timeStarted: def.timeStarted,
    timeEnded: def.timeEnded
  };
}

// Three functions below all operate with sharedData
// structure defined above. They should be self-explanatory.

function addTarget(target) {
  sharedData.data.set(target, new Map());
}

function getProfiles(target) {
  return sharedData.data.get(target);
}

/**
 * Object to control the JavaScript Profiler over the remote
 * debugging protocol.
 *
 * @param Target target
 *        A target object as defined in Target.jsm
 */
function ProfilerController(target) {
  if (sharedData.controllers.has(target)) {
    return sharedData.controllers.get(target);
  }

  this.target = target;
  this.client = target.client;
  this.isConnected = false;
  this.consoleProfiles = [];

  addTarget(target);

  // Chrome debugging targets have already obtained a reference
  // to the profiler actor.
  if (target.chrome) {
    this.isConnected = true;
    this.actor = target.form.profilerActor;
  }

  sharedData.controllers.set(target, this);
};

ProfilerController.prototype = {
  /**
   * Return a map of profile results for the current target.
   *
   * @return Map
   */
  get profiles() {
    return getProfiles(this.target);
  },

  /**
   * Checks whether the profile is currently recording.
   *
   * @param object profile
   *        An object made by calling makeProfile function.
   * @return boolean
   */
  isProfileRecording: function PC_isProfileRecording(profile) {
    return profile.timeStarted !== null && profile.timeEnded === null;
  },

  /**
   * A listener that fires whenever console.profile or console.profileEnd
   * is called.
   *
   * @param string type
   *        Type of a call. Either 'profile' or 'profileEnd'.
   * @param object data
   *        Event data.
   * @param object panel
   *        A reference to the ProfilerPanel in the current tab.
   */
  onConsoleEvent: function (type, data, panel) {
    let name = data.extra.name;

    let profileStart = () => {
      if (name && this.profiles.has(name))
        return;

      // Add profile to the UI (createProfile will return
      // an automatically generated name if 'name' is falsey).
      let profile = panel.createProfile(name);
      profile.start((name, cb) => cb());

      // Add profile structure to shared data.
      this.profiles.set(profile.name, makeProfile(profile.name, {
        timeStarted: data.extra.currentTime
      }));
      this.consoleProfiles.push(profile.name);
    };

    let profileEnd = () => {
      if (!name && !this.consoleProfiles.length)
        return;

      if (!name)
        name = this.consoleProfiles.pop();
      else
        this.consoleProfiles.filter((n) => n !== name);

      if (!this.profiles.has(name))
        return;

      let profile = this.profiles.get(name);
      if (!this.isProfileRecording(profile))
        return;

      let profileData = data.extra.profile;
      profile.timeEnded = data.extra.currentTime;

      profileData.threads = profileData.threads.map((thread) => {
        let samples = thread.samples.filter((sample) => {
          return sample.time >= profile.timeStarted;
        });

        return { samples: samples };
      });

      let ui = panel.getProfileByName(name);
      ui.data = profileData;
      ui.parse(profileData, () => panel.emit("parsed"));
      ui.stop((name, cb) => cb());
    };

    if (type === "profile")
      profileStart();

    if (type === "profileEnd")
      profileEnd();
  },

  /**
   * Connects to the client unless we're already connected.
   *
   * @param function cb
   *        Function to be called once we're connected. If
   *        the controller is already connected, this function
   *        will be called immediately (synchronously).
   */
  connect: function (cb=function(){}) {
    if (this.isConnected) {
      return void cb();
    }

    // Check if we already have a grip to the listTabs response object
    // and, if we do, use it to get to the profilerActor. Otherwise,
    // call listTabs. The problem is that if we call listTabs twice
    // webconsole tests fail (see bug 872826).

    let register = () => {
      let data = { events: ["console-api-profiler"] };

      // Check if Gecko Profiler Addon [1] is installed and, if it is,
      // don't register our own console event listeners. Gecko Profiler
      // Addon takes care of console.profile and console.profileEnd methods
      // and we don't want to break it.
      //
      // [1] - https://github.com/bgirard/Gecko-Profiler-Addon/

      AddonManager.getAddonByID("jid0-edalmuivkozlouyij0lpdx548bc@jetpack", (addon) => {
        if (addon && !addon.userDisabled && !addon.softDisabled)
          return void cb();

        this.request("registerEventNotifications", data, (resp) => {
          this.client.addListener("eventNotification", (type, resp) => {
            let toolbox = gDevTools.getToolbox(this.target);
            if (toolbox == null)
              return;

            let panel = toolbox.getPanel("jsprofiler");
            if (panel)
              return void this.onConsoleEvent(resp.subject.action, resp.data, panel);

            // Can't use a promise here because of a race condition when the promise
            // is resolved only after -ready event is fired when creating a new panel
            // and during the -ready event when waiting for a panel to be created:
            //
            // console.profile();    // creates a new panel, waits for the promise
            // console.profileEnd(); // panel is not created yet but loading
            //
            // -> jsprofiler-ready event is fired which triggers a promise for profileEnd
            // -> a promise for profile is triggered.
            //
            // And it should be the other way around. Hence the event.

            toolbox.once("jsprofiler-ready", (_, panel) => {
              this.onConsoleEvent(resp.subject.action, resp.data, panel);
            });

            toolbox.loadTool("jsprofiler");
          });
        });

        cb();
      });
    };

    if (this.target.root) {
      this.actor = this.target.root.profilerActor;
      this.isConnected = true;
      return void register();
    }

    this.client.listTabs((resp) => {
      this.actor = resp.profilerActor;
      this.isConnected = true;
      register();
    });
  },

  /**
   * Adds actor and type information to data and sends the request over
   * the remote debugging protocol.
   *
   * @param string type
   *        Method to call on the other side
   * @param object data
   *        Data to send with the request
   * @param function cb
   *        A callback function
   */
  request: function (type, data, cb) {
    data.to = this.actor;
    data.type = type;
    this.client.request(data, cb);
  },

  /**
   * Checks whether the profiler is active.
   *
   * @param function cb
   *        Function to be called with a response from the
   *        client. It will be called with two arguments:
   *        an error object (may be null) and a boolean
   *        value indicating if the profiler is active or not.
   */
  isActive: function (cb) {
    this.request("isActive", {}, (resp) => {
      cb(resp.error, resp.isActive, resp.currentTime);
    });
  },

  /**
   * Creates a new profile and starts the profiler, if needed.
   *
   * @param string name
   *        Name of the profile.
   * @param function cb
   *        Function to be called once the profiler is started
   *        or we get an error. It will be called with a single
   *        argument: an error object (may be null).
   */
  start: function PC_start(name, cb) {
    if (this.profiles.has(name)) {
      return;
    }

    let profile = makeProfile(name);
    this.consoleProfiles.push(name);
    this.profiles.set(name, profile);

    // If profile is already running, no need to do anything.
    if (this.isProfileRecording(profile)) {
      return void cb();
    }

    this.isActive((err, isActive, currentTime) => {
      if (isActive) {
        profile.timeStarted = currentTime;
        return void cb();
      }

      let params = {
        entries: 1000000,
        interval: 1,
        features: ["js"],
      };

      this.request("startProfiler", params, (resp) => {
        if (resp.error) {
          return void cb(resp.error);
        }

        profile.timeStarted = 0;
        cb();
      });
    });
  },

  /**
   * Stops the profiler. NOTE, that we don't stop the actual
   * SPS Profiler here. It will be stopped as soon as all
   * clients disconnect from the profiler actor.
   *
   * @param string name
   *        Name of the profile that needs to be stopped.
   * @param function cb
   *        Function to be called once the profiler is stopped
   *        or we get an error. It will be called with a single
   *        argument: an error object (may be null).
   */
  stop: function PC_stop(name, cb) {
    if (!this.profiles.has(name)) {
      return;
    }

    let profile = this.profiles.get(name);
    if (!this.isProfileRecording(profile)) {
      return;
    }

    this.request("getProfile", {}, (resp) => {
      if (resp.error) {
        Cu.reportError("Failed to fetch profile data.");
        return void cb(resp.error, null);
      }

      let data = resp.profile;
      profile.timeEnded = resp.currentTime;

      // Filter out all samples that fall out of current
      // profile's range.

      data.threads = data.threads.map((thread) => {
        let samples = thread.samples.filter((sample) => {
          return sample.time >= profile.timeStarted;
        });

        return { samples: samples };
      });

      cb(null, data);
    });
  },

  /**
   * Cleanup.
   */
  destroy: function PC_destroy() {
    this.client = null;
    this.target = null;
    this.actor = null;
  }
};

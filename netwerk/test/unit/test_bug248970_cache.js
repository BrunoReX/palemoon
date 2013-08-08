/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;

// names for cache devices
const kDiskDevice = "disk";
const kMemoryDevice = "memory";
const kOfflineDevice = "offline";

// the name for our cache session
const kPrivateBrowsing = "PrivateBrowsing";

var _PBSvc;
function get_privatebrowsing_service() {
  if (_PBSvc)
    return _PBSvc;

  try {
    _PBSvc = Cc["@mozilla.org/privatebrowsing;1"].
             getService(Ci.nsIPrivateBrowsingService);
    return _PBSvc;
  } catch (e) {}

  return null;
}

var _CSvc;
function get_cache_service() {
  if (_CSvc)
    return _CSvc;

  return _CSvc = Cc["@mozilla.org/network/cache-service;1"].
                 getService(Ci.nsICacheService);
}







function check_devices_available(devices) {
  var cs = get_cache_service();
  var found_devices = [];

  var visitor = {
    visitDevice: function (deviceID, deviceInfo) {
      found_devices.push(deviceID);
      return false;
    },
    visitEntry: function (deviceID, entryInfo) {
      do_throw("nsICacheVisitor.visitEntry should not be called " +
        "when checking the availability of devices");
    }
  };

  // get the list of active devices
  cs.visitEntries(visitor);

  // see if any of the required devices was missing
  if (devices.sort().toString() != found_devices.sort().toString()) {
    do_throw("Expected to find these devices: \"" + devices.sort().toString() +
      "\", but found these instead: \"" + found_devices.sort().toString() + "\"");
  }

  // see if any extra devices have been found
  if (found_devices.length > devices.length) {
    do_throw("Expected to find these devices: [" + devices.join(", ") +
      "], but instead got: [" + found_devices.join(", ") + "]");
  }
}

function get_device_entry_count(device) {
  var cs = get_cache_service();
  var entry_count = -1;

  var visitor = {
    visitDevice: function (deviceID, deviceInfo) {
      if (device == deviceID)
        entry_count = deviceInfo.entryCount;
      return false;
    },
    visitEntry: function (deviceID, entryInfo) {
      do_throw("nsICacheVisitor.visitEntry should not be called " +
        "when checking the availability of devices");
    }
  };

  // get the device entry count
  cs.visitEntries(visitor);

  return entry_count;
}

function store_in_cache(aKey, aContent, aWhere) {
  var storageFlag, streaming = true;
  if (aWhere == kDiskDevice)
    storageFlag = Ci.nsICache.STORE_ON_DISK;
  else if (aWhere == kOfflineDevice)
    storageFlag = Ci.nsICache.STORE_OFFLINE;
  else if (aWhere == kMemoryDevice)
    storageFlag = Ci.nsICache.STORE_IN_MEMORY;

  var cache = get_cache_service();
  var session = cache.createSession(kPrivateBrowsing, storageFlag, streaming);
  var cacheEntry = session.openCacheEntry(aKey, Ci.nsICache.ACCESS_WRITE, true);

  var oStream = cacheEntry.openOutputStream(0);

  var written = oStream.write(aContent, aContent.length);
  if (written != aContent.length) {
    do_throw("oStream.write has not written all data!\n" +
             "  Expected: " + aContent.length  + "\n" +
             "  Actual: " + written + "\n");
  }
  oStream.close();
  cacheEntry.close();
}

function make_input_stream_scriptable(input) {
  var wrapper = Cc["@mozilla.org/scriptableinputstream;1"].
                createInstance(Ci.nsIScriptableInputStream);
  wrapper.init(input);
  return wrapper;
}

function retrieve_from_cache(aKey, aWhere) {
  var storageFlag, streaming = true;
  if (aWhere == kDiskDevice)
    storageFlag = Ci.nsICache.STORE_ANYWHERE;
  else if (aWhere == kOfflineDevice)
    storageFlag = Ci.nsICache.STORE_OFFLINE;
  else if (aWhere == kMemoryDevice)
    storageFlag = Ci.nsICache.STORE_ANYWHERE;

  var cache = get_cache_service();
  var session = cache.createSession(kPrivateBrowsing, storageFlag, streaming);
  try {
    var cacheEntry = session.openCacheEntry(aKey, Ci.nsICache.ACCESS_READ, true);
  } catch (e) {
    if (e.result == Cr.NS_ERROR_CACHE_KEY_NOT_FOUND ||
        e.result == Cr.NS_ERROR_FAILURE)
      // a key not found error is expected here, so we will simply return null
      // to let the caller know that no data was retrieved.  We also expect
      // a generic failure error in case of the offline cache.
      return null;

    // Throw the textual error description.
    do_throw(e);
  }

  var iStream = make_input_stream_scriptable(cacheEntry.openInputStream(0));

  var read = iStream.read(iStream.available());
  iStream.close();
  cacheEntry.close();

  return read;
}

function run_test() {
  var pb = get_privatebrowsing_service();
  if (pb) { // Private Browsing might not be available
    var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                     getService(Ci.nsIPrefBranch);
    prefBranch.setBoolPref("browser.privatebrowsing.keep_current_session", true);

    const kCacheA = "cache-A",
          kCacheB = "cache-B",
          kCacheC = "cache-C",
          kTestContent = "test content";

    // Simulate a profile dir for xpcshell
    do_get_profile();

    var cs = get_cache_service();

    // Start off with an empty cache
    cs.evictEntries(Ci.nsICache.STORE_ANYWHERE);

    // Store cache-A, cache-B and cache-C
    store_in_cache(kCacheA, kTestContent, kMemoryDevice);
    store_in_cache(kCacheB, kTestContent, kDiskDevice);
    store_in_cache(kCacheC, kTestContent, kOfflineDevice);

    // Make sure all three cache devices are available initially
    check_devices_available([kMemoryDevice, kDiskDevice, kOfflineDevice]);

    // Check if cache-A, cache-B and cache-C are avilable
    do_check_eq(retrieve_from_cache(kCacheA, kMemoryDevice), kTestContent);
    do_check_eq(retrieve_from_cache(kCacheB, kDiskDevice), kTestContent);
    do_check_eq(retrieve_from_cache(kCacheC, kOfflineDevice), kTestContent);

    // Enter private browsing mode
    pb.privateBrowsingEnabled = true;

    // Make sure none of cache-A, cache-B and cache-C are available
    do_check_eq(retrieve_from_cache(kCacheA, kMemoryDevice), null);
    do_check_eq(retrieve_from_cache(kCacheB, kDiskDevice), null);
    do_check_eq(retrieve_from_cache(kCacheC, kOfflineDevice), null);

    // Make sure only the memory device is available
    check_devices_available([kMemoryDevice]);

    // Make sure the memory device is empty
    do_check_eq(get_device_entry_count(kMemoryDevice), 0);

    // Exit private browsing mode
    pb.privateBrowsingEnabled = false;

    // Make sure all three cache devices are available after leaving the private mode
    check_devices_available([kMemoryDevice, kDiskDevice, kOfflineDevice]);

    // Check if cache-A is gone, and cache-B and cache-C are still avilable
    do_check_eq(retrieve_from_cache(kCacheA, kMemoryDevice), null);
    do_check_eq(retrieve_from_cache(kCacheB, kDiskDevice), kTestContent);
    do_check_eq(retrieve_from_cache(kCacheC, kOfflineDevice), kTestContent);

    prefBranch.clearUserPref("browser.privatebrowsing.keep_current_session");
  }
}

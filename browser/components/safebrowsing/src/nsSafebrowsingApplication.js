const Cc = Components.classes;
const Ci = Components.interfaces;

// This is copied from toolkit/components/content/js/lang.js.
// It seems cleaner to copy this rather than #include from so far away.
Function.prototype.inherits = function(parentCtor) {
  var tempCtor = function(){};
  tempCtor.prototype = parentCtor.prototype;
  this.superClass_ = parentCtor.prototype;
  this.prototype = new tempCtor();
}  

#include ../content/application.js
#include ../content/globalstore.js
#include ../content/list-warden.js
#include ../content/phishing-warden.js
#include ../content/malware-warden.js

var modScope = this;
function Init() {
  var jslib = Cc["@mozilla.org/url-classifier/jslib;1"]
              .getService().wrappedJSObject;
  modScope.G_Debug = jslib.G_Debug;
  modScope.G_Assert = jslib.G_Assert;
  modScope.G_Alarm = jslib.G_Alarm;
  modScope.G_ConditionalAlarm = jslib.G_ConditionalAlarm;
  modScope.G_ObserverWrapper = jslib.G_ObserverWrapper;
  modScope.G_Preferences = jslib.G_Preferences;
  modScope.PROT_XMLFetcher = jslib.PROT_XMLFetcher;
  modScope.BindToObject = jslib.BindToObject;
  modScope.G_Protocol4Parser = jslib.G_Protocol4Parser;
  modScope.PROT_UrlCrypto = jslib.PROT_UrlCrypto;
  modScope.RequestBackoff = jslib.RequestBackoff;
  
  // We only need to call Init once
  modScope.Init = function() {};
}

// Module object
function SafebrowsingApplicationMod() {
  this.firstTime = true;
  this.cid = Components.ID("{c64d0bcb-8270-4ca7-a0b3-3380c8ffecb5}");
  this.progid = "@mozilla.org/safebrowsing/application;1";
}

SafebrowsingApplicationMod.prototype.registerSelf = function(compMgr, fileSpec, loc, type) {
  if (this.firstTime) {
    this.firstTime = false;
    throw Components.results.NS_ERROR_FACTORY_REGISTER_AGAIN;
  }
  compMgr = compMgr.QueryInterface(Ci.nsIComponentRegistrar);
  compMgr.registerFactoryLocation(this.cid,
                                  "Safebrowsing Application Module",
                                  this.progid,
                                  fileSpec,
                                  loc,
                                  type);
};

SafebrowsingApplicationMod.prototype.getClassObject = function(compMgr, cid, iid) {  
  if (!cid.equals(this.cid))
    throw Components.results.NS_ERROR_NO_INTERFACE;
  if (!iid.equals(Ci.nsIFactory))
    throw Components.results.NS_ERROR_NOT_IMPLEMENTED;

  return this.factory;
}

SafebrowsingApplicationMod.prototype.canUnload = function(compMgr) {
  return true;
}

SafebrowsingApplicationMod.prototype.factory = {
  createInstance: function(outer, iid) {
    if (outer != null)
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    Init();
    return new PROT_Application();
  }
};

var ApplicationModInst = new SafebrowsingApplicationMod();

function NSGetModule(compMgr, fileSpec) {
  return ApplicationModInst;
}

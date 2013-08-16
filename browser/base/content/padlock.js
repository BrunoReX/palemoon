let Cc = Components.classes;
let Ci = Components.interfaces;
let Cu = Components.utils;
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

var padlock_PadLock =
{
    QueryInterface: XPCOMUtils.generateQI([Ci.nsIWebProgressListener,
                                           Ci.nsISupportsWeakReference]),
  onButtonClick: function(event) {
    event.stopPropagation();
    gIdentityHandler.handleMoreInfoClick(event);
  },
    onStateChange: function() {},
    onProgressChange: function() {},
    onLocationChange: function() {},
    onStatusChange: function() {},
    onSecurityChange: function(aCallerWebProgress, aRequestWithState, aState) {
        // aState is defined as a bitmask that may be extended in the future.
        // We filter out any unknown bits before testing for known values.
        const wpl = Ci.nsIWebProgressListener;
        const wpl_security_bits = wpl.STATE_IS_SECURE |
                                  wpl.STATE_IS_BROKEN |
                                  wpl.STATE_IS_INSECURE |
                                  wpl.STATE_IDENTITY_EV_TOPLEVEL |
                                  wpl.STATE_SECURE_HIGH |
                                  wpl.STATE_SECURE_MED |
                                  wpl.STATE_SECURE_LOW;
        var level;
        var is_insecure;
        var highlight_urlbar = false;

        switch (aState & wpl_security_bits) {
          case wpl.STATE_IS_SECURE | wpl.STATE_SECURE_HIGH | wpl.STATE_IDENTITY_EV_TOPLEVEL:
            level = "ev";
            is_insecure = "";
            highlight_urlbar = true;
            break;
          case wpl.STATE_IS_SECURE | wpl.STATE_SECURE_HIGH:
            level = "high";
            is_insecure = "";
            highlight_urlbar = true;
            break;
          case wpl.STATE_IS_SECURE | wpl.STATE_SECURE_MED:
          case wpl.STATE_IS_SECURE | wpl.STATE_SECURE_LOW:
            level = "low";
            is_insecure = "insecure";
            break;
          case wpl.STATE_IS_BROKEN:
            level = "broken";
            is_insecure = "insecure";
            break;
          default: // should not be reached
            level = null;
            is_insecure = "insecure";
        }

        try {
          var proto = gBrowser.contentWindow.location.protocol;
          if (proto == "about:" || proto == "chrome:" || proto == "file:" ) {
            // do not warn when using local protocols
            is_insecure = false;
          }
        }
        catch (ex) {}
        
        let ub = document.getElementById("urlbar");
        if (highlight_urlbar) {
          ub.setAttribute("security_level", level);
        } else {
          ub.removeAttribute("security_level");
        }

        padlock_PadLock.setPadlockLevel("padlock-ib", level);
        padlock_PadLock.setPadlockLevel("padlock-ib-left", level);
        padlock_PadLock.setPadlockLevel("padlock-ub-right", level);
        padlock_PadLock.setPadlockLevel("padlock-sb", level);
        padlock_PadLock.setPadlockLevel("padlock-tab", level);
    },
  setPadlockLevel: function(item, level) {
    let secbut = document.getElementById(item);

    if (level) {
      secbut.setAttribute("level", level);
      secbut.hidden = false;
    } else {
      secbut.hidden = true;
      secbut.removeAttribute("level");
    }

    secbut.setAttribute("tooltiptext", gBrowser.securityUI.tooltipText);
  },
  prefbranch : null,
  onLoad: function() {
    gBrowser.addProgressListener(padlock_PadLock);
    
    var prefService = Components.classes["@mozilla.org/preferences-service;1"].getService(Components.interfaces.nsIPrefService);
    padlock_PadLock.prefbranch = prefService.getBranch("browser.padlock.");
    padlock_PadLock.prefbranch.QueryInterface(Components.interfaces.nsIPrefBranch2);
    padlock_PadLock.usePrefs();
    padlock_PadLock.prefbranch.addObserver("", padlock_PadLock, false);
  },
  onUnLoad: function() {
    padlock_PadLock.prefbranch.removeObserver("", this);
  },
  observe: function(subject, topic, data)
  {
    if (topic != "nsPref:changed")
      return;
    if (data != "style" && data != "urlbar_background" && data != "shown")
      return;
    padlock_PadLock.usePrefs();
  },
  usePrefs: function() {
    var prefval = padlock_PadLock.prefbranch.getIntPref("style");
    var position;
    var padstyle;
    if (prefval == 2) {
      position = "ib-left";
      padstyle = "modern";
    }
    else if (prefval == 3) {
      position = "ub-right";
      padstyle = "modern";
    }
    else if (prefval == 4) {
      position = "statbar";
      padstyle = "modern";
    }
    else if (prefval == 5) {
      position = "tabs-bar";
      padstyle = "modern";
    }
    else if (prefval == 6) {
      position = "ib-trans-bg";
      padstyle = "classic";
    }
    else if (prefval == 7) {
      position = "ib-left";
      padstyle = "classic";
    }
    else if (prefval == 8) {
      position = "ub-right";
      padstyle = "classic";
    }
    else if (prefval == 9) {
      position = "statbar";
      padstyle = "classic";
    }
    else if (prefval == 10) {
      position = "tabs-bar";
      padstyle = "classic";
    }
    else { // 1 or anything else_ default
      position = "ib-trans-bg";
      padstyle = "modern";
    }

    var colshow;
    var colprefval = padlock_PadLock.prefbranch.getIntPref("urlbar_background");
    if (colprefval == 1) {
      colshow = "y";
    }
    else { // 0 or anything else_ default
      colshow = "";
    }

    var lockenabled;
    var lockenabled = padlock_PadLock.prefbranch.getBoolPref("shown");
    if (lockenabled)
      padshow = position;
    else
      padshow = "";

    document.getElementById("padlock-ib").setAttribute("padshow", padshow);
    document.getElementById("padlock-ib-left").setAttribute("padshow", padshow);
    document.getElementById("padlock-ub-right").setAttribute("padshow", padshow);
    document.getElementById("padlock-sb").setAttribute("padshow", padshow);
    document.getElementById("padlock-tab").setAttribute("padshow", padshow);

    document.getElementById("padlock-ib").setAttribute("padstyle", padstyle);
    document.getElementById("padlock-ib-left").setAttribute("padstyle", padstyle);
    document.getElementById("padlock-ub-right").setAttribute("padstyle", padstyle);
    document.getElementById("padlock-sb").setAttribute("padstyle", padstyle);
    document.getElementById("padlock-tab").setAttribute("padstyle", padstyle);

    document.getElementById("urlbar").setAttribute("https_color", colshow);
  }
};

window.addEventListener("load", padlock_PadLock.onLoad, false );
window.addEventListener("unload", padlock_PadLock.onUnLoad, false );
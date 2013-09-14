/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

'use strict';

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;
const Cr = Components.results;

const INCLUDE_DESC = 0x01;
const INCLUDE_NAME = 0x02;
const INCLUDE_CUSTOM = 0x04;
const NAME_FROM_SUBTREE_RULE = 0x08;

const OUTPUT_DESC_FIRST = 0;
const OUTPUT_DESC_LAST = 1;

Cu.import('resource://gre/modules/XPCOMUtils.jsm');
XPCOMUtils.defineLazyModuleGetter(this, 'Utils',
  'resource://gre/modules/accessibility/Utils.jsm');
XPCOMUtils.defineLazyModuleGetter(this, 'PrefCache',
  'resource://gre/modules/accessibility/Utils.jsm');

let gUtteranceOrder = new PrefCache('accessibility.accessfu.utterance');

var gStringBundle = Cc['@mozilla.org/intl/stringbundle;1'].
  getService(Ci.nsIStringBundleService).
  createBundle('chrome://global/locale/AccessFu.properties');

this.EXPORTED_SYMBOLS = ['UtteranceGenerator', 'BrailleGenerator'];

this.OutputGenerator = {

  /**
   * Generates output for a PivotContext.
   * @param {PivotContext} aContext object that generates and caches
   *    context information for a given accessible and its relationship with
   *    another accessible.
   * @return {Array} An array of strings. Depending on the utterance order,
   *    the strings describe the context for an accessible object either
   *    starting from the accessible's ancestry or accessible's subtree.
   */
  genForContext: function genForContext(aContext) {
    let output = [];
    let self = this;
    let addOutput = function addOutput(aAccessible) {
      output.push.apply(output, self.genForObject(aAccessible));
    };
    let ignoreSubtree = function ignoreSubtree(aAccessible) {
      let roleString = Utils.AccRetrieval.getStringRole(aAccessible.role);
      let nameRule = self.roleRuleMap[roleString] || 0;
      // Ignore subtree if the name is explicit and the role's name rule is the
      // NAME_FROM_SUBTREE_RULE.
      return (nameRule & NAME_FROM_SUBTREE_RULE) &&
        (Utils.getAttributes(aAccessible)['explicit-name'] === 'true');
    };
    let outputOrder = typeof gUtteranceOrder.value == 'number' ?
                      gUtteranceOrder.value : this.defaultOutputOrder;
    let contextStart = this._getContextStart(aContext);

    if (outputOrder === OUTPUT_DESC_FIRST) {
      contextStart.forEach(addOutput);
      addOutput(aContext.accessible);
      [addOutput(node) for
        (node of aContext.subtreeGenerator(true, ignoreSubtree))];
    } else {
      [addOutput(node) for
        (node of aContext.subtreeGenerator(false, ignoreSubtree))];
      addOutput(aContext.accessible);
      contextStart.reverse().forEach(addOutput);
    }

    // Clean up the white space.
    let trimmed;
    output = [trimmed for (word of output) if (trimmed = word.trim())];
    return output;
  },


  /**
   * Generates output for an object.
   * @param {nsIAccessible} aAccessible accessible object to generate utterance
   *    for.
   * @return {Array} Two string array. The first string describes the object
   *    and its states. The second string is the object's name. Whether the
   *    object's description or it's role is included is determined by
   *    {@link roleRuleMap}.
   */
  genForObject: function genForObject(aAccessible) {
    let roleString = Utils.AccRetrieval.getStringRole(aAccessible.role);
    let func = this.objectOutputFunctions[roleString.replace(' ', '')] ||
      this.objectOutputFunctions.defaultFunc;

    let flags = this.roleRuleMap[roleString] || 0;

    if (aAccessible.childCount == 0)
      flags |= INCLUDE_NAME;

    let state = {};
    let extState = {};
    aAccessible.getState(state, extState);
    let states = {base: state.value, ext: extState.value};

    return func.apply(this, [aAccessible, roleString, states, flags]);
  },

  /**
   * Generates output for an action performed.
   * @param {nsIAccessible} aAccessible accessible object that the action was
   *    invoked in.
   * @param {string} aActionName the name of the action, one of the keys in
   *    {@link gActionMap}.
   * @return {Array} A one string array with the action.
   */
  genForAction: function genForAction(aObject, aActionName) {},

  /**
   * Generates output for an announcement. Basically attempts to localize
   * the announcement string.
   * @param {string} aAnnouncement unlocalized announcement.
   * @return {Array} A one string array with the announcement.
   */
  genForAnnouncement: function genForAnnouncement(aAnnouncement) {},

  /**
   * Generates output for a tab state change.
   * @param {nsIAccessible} aAccessible accessible object of the tab's attached
   *    document.
   * @param {string} aTabState the tab state name, see
   *    {@link Presenter.tabStateChanged}.
   * @return {Array} The tab state utterace.
   */
  genForTabStateChange: function genForTabStateChange(aObject, aTabState) {},

  /**
   * Generates output for announcing entering and leaving editing mode.
   * @param {aIsEditing} boolean true if we are in editing mode
   * @return {Array} The mode utterance
   */
  genForEditingMode: function genForEditingMode(aIsEditing) {},

  _getContextStart: function getContextStart(aContext) {},

  _addName: function _addName(aOutput, aAccessible, aFlags) {
    let name;
    if (Utils.getAttributes(aAccessible)['explicit-name'] === 'true' ||
      (aFlags & INCLUDE_NAME)) {
      name = aAccessible.name;
    }

    if (name) {
      let outputOrder = typeof gUtteranceOrder.value == 'number' ?
                        gUtteranceOrder.value : this.defaultOutputOrder;
      aOutput[outputOrder === OUTPUT_DESC_FIRST ?
        'push' : 'unshift'](name);
    }
  },

  _getLocalizedRole: function _getLocalizedRole(aRoleStr) {},

  _getLocalizedStates: function _getLocalizedStates(aStates) {},

  roleRuleMap: {
    'menubar': INCLUDE_DESC,
    'scrollbar': INCLUDE_DESC,
    'grip': INCLUDE_DESC,
    'alert': INCLUDE_DESC | INCLUDE_NAME,
    'menupopup': INCLUDE_DESC,
    'menuitem': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'tooltip': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'columnheader': NAME_FROM_SUBTREE_RULE,
    'rowheader': NAME_FROM_SUBTREE_RULE,
    'column': NAME_FROM_SUBTREE_RULE,
    'row': NAME_FROM_SUBTREE_RULE,
    'application': INCLUDE_NAME,
    'document': INCLUDE_NAME,
    'grouping': INCLUDE_DESC | INCLUDE_NAME,
    'toolbar': INCLUDE_DESC,
    'table': INCLUDE_DESC | INCLUDE_NAME,
    'link': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'helpballoon': NAME_FROM_SUBTREE_RULE,
    'list': INCLUDE_DESC | INCLUDE_NAME,
    'listitem': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'outline': INCLUDE_DESC,
    'outlineitem': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'pagetab': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'graphic': INCLUDE_DESC,
    'pushbutton': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'checkbutton': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'radiobutton': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'buttondropdown': NAME_FROM_SUBTREE_RULE,
    'combobox': INCLUDE_DESC,
    'droplist': INCLUDE_DESC,
    'progressbar': INCLUDE_DESC,
    'slider': INCLUDE_DESC,
    'spinbutton': INCLUDE_DESC,
    'diagram': INCLUDE_DESC,
    'animation': INCLUDE_DESC,
    'equation': INCLUDE_DESC,
    'buttonmenu': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'buttondropdowngrid': NAME_FROM_SUBTREE_RULE,
    'pagetablist': INCLUDE_DESC,
    'canvas': INCLUDE_DESC,
    'check menu item': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'label': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'password text': INCLUDE_DESC,
    'popup menu': INCLUDE_DESC,
    'radio menu item': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'table column header': NAME_FROM_SUBTREE_RULE,
    'table row header': NAME_FROM_SUBTREE_RULE,
    'tear off menu item': NAME_FROM_SUBTREE_RULE,
    'toggle button': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'parent menuitem': NAME_FROM_SUBTREE_RULE,
    'header': INCLUDE_DESC,
    'footer': INCLUDE_DESC,
    'entry': INCLUDE_DESC | INCLUDE_NAME,
    'caption': INCLUDE_DESC,
    'document frame': INCLUDE_DESC,
    'heading': INCLUDE_DESC,
    'calendar': INCLUDE_DESC | INCLUDE_NAME,
    'combobox list': INCLUDE_DESC,
    'combobox option': INCLUDE_DESC | NAME_FROM_SUBTREE_RULE,
    'listbox option': NAME_FROM_SUBTREE_RULE,
    'listbox rich option': NAME_FROM_SUBTREE_RULE,
    'gridcell': NAME_FROM_SUBTREE_RULE,
    'check rich option': NAME_FROM_SUBTREE_RULE,
    'term': NAME_FROM_SUBTREE_RULE,
    'definition': NAME_FROM_SUBTREE_RULE,
    'key': NAME_FROM_SUBTREE_RULE,
    'image map': INCLUDE_DESC,
    'option': INCLUDE_DESC,
    'listbox': INCLUDE_DESC,
    'definitionlist': INCLUDE_DESC | INCLUDE_NAME},

  objectOutputFunctions: {
    _generateBaseOutput: function _generateBaseOutput(aAccessible, aRoleStr, aStates, aFlags) {
      let output = [];

      if (aFlags & INCLUDE_DESC) {
        let desc = this._getLocalizedStates(aStates);
        let roleStr = this._getLocalizedRole(aRoleStr);
        if (roleStr)
          desc.push(roleStr);
        output.push(desc.join(' '));
      }

      this._addName(output, aAccessible, aFlags);

      return output;
    },

    entry: function entry(aAccessible, aRoleStr, aStates, aFlags) {
      let output = [];
      let desc = this._getLocalizedStates(aStates);
      desc.push(this._getLocalizedRole(
                  (aStates.ext & Ci.nsIAccessibleStates.EXT_STATE_MULTI_LINE) ?
                    'textarea' : 'entry'));

      output.push(desc.join(' '));

      this._addName(output, aAccessible, aFlags);

      return output;
    }
  }
};

/**
 * Generates speech utterances from objects, actions and state changes.
 * An utterance is an array of strings.
 *
 * It should not be assumed that flattening an utterance array would create a
 * gramatically correct sentence. For example, {@link genForObject} might
 * return: ['graphic', 'Welcome to my home page'].
 * Each string element in an utterance should be gramatically correct in itself.
 * Another example from {@link genForObject}: ['list item 2 of 5', 'Alabama'].
 *
 * An utterance is ordered from the least to the most important. Speaking the
 * last string usually makes sense, but speaking the first often won't.
 * For example {@link genForAction} might return ['button', 'clicked'] for a
 * clicked event. Speaking only 'clicked' makes sense. Speaking 'button' does
 * not.
 */
this.UtteranceGenerator = {
  __proto__: OutputGenerator,

  defaultOutputOrder: OUTPUT_DESC_FIRST,

  gActionMap: {
    jump: 'jumpAction',
    press: 'pressAction',
    check: 'checkAction',
    uncheck: 'uncheckAction',
    select: 'selectAction',
    open: 'openAction',
    close: 'closeAction',
    switch: 'switchAction',
    click: 'clickAction',
    collapse: 'collapseAction',
    expand: 'expandAction',
    activate: 'activateAction',
    cycle: 'cycleAction'
  },

  //TODO: May become more verbose in the future.
  genForAction: function genForAction(aObject, aActionName) {
    return [gStringBundle.GetStringFromName(this.gActionMap[aActionName])];
  },

  genForAnnouncement: function genForAnnouncement(aAnnouncement) {
    try {
      return [gStringBundle.GetStringFromName(aAnnouncement)];
    } catch (x) {
      return [aAnnouncement];
    }
  },

  genForTabStateChange: function genForTabStateChange(aObject, aTabState) {
    switch (aTabState) {
      case 'newtab':
        return [gStringBundle.GetStringFromName('tabNew')];
      case 'loading':
        return [gStringBundle.GetStringFromName('tabLoading')];
      case 'loaded':
        return [aObject.name || '',
                gStringBundle.GetStringFromName('tabLoaded')];
      case 'loadstopped':
        return [gStringBundle.GetStringFromName('tabLoadStopped')];
      case 'reload':
        return [gStringBundle.GetStringFromName('tabReload')];
      default:
        return [];
    }
  },

  genForEditingMode: function genForEditingMode(aIsEditing) {
    return [gStringBundle.GetStringFromName(
              aIsEditing ? 'editingMode' : 'navigationMode')];
  },

  objectOutputFunctions: {
    defaultFunc: function defaultFunc(aAccessible, aRoleStr, aStates, aFlags) {
      return OutputGenerator.objectOutputFunctions._generateBaseOutput.apply(this, arguments);
    },

    entry: function entry(aAccessible, aRoleStr, aStates, aFlags) {
      return OutputGenerator.objectOutputFunctions.entry.apply(this, arguments);
    },

    heading: function heading(aAccessible, aRoleStr, aStates, aFlags) {
      let level = {};
      aAccessible.groupPosition(level, {}, {});
      let utterance =
        [gStringBundle.formatStringFromName('headingLevel', [level.value], 1)];

      this._addName(utterance, aAccessible, aFlags);

      return utterance;
    },

    listitem: function listitem(aAccessible, aRoleStr, aStates, aFlags) {
      let itemno = {};
      let itemof = {};
      aAccessible.groupPosition({}, itemof, itemno);
      let utterance = [];
      if (itemno.value == 1) // Start of list
        utterance.push(gStringBundle.GetStringFromName('listStart'));
      else if (itemno.value == itemof.value) // last item
        utterance.push(gStringBundle.GetStringFromName('listEnd'));

      this._addName(utterance, aAccessible, aFlags);

      return utterance;
    },

    list: function list(aAccessible, aRoleStr, aStates, aFlags) {
      return this._getListUtterance
        (aAccessible, aRoleStr, aFlags, aAccessible.childCount);
    },

    definitionlist: function definitionlist(aAccessible, aRoleStr, aStates, aFlags) {
      return this._getListUtterance
        (aAccessible, aRoleStr, aFlags, aAccessible.childCount / 2);
    },

    application: function application(aAccessible, aRoleStr, aStates, aFlags) {
      // Don't utter location of applications, it gets tiring.
      if (aAccessible.name != aAccessible.DOMNode.location)
        return this.objectOutputFunctions.defaultFunc.apply(this,
          [aAccessible, aRoleStr, aStates, aFlags]);

      return [];
    }
  },

  _getContextStart: function _getContextStart(aContext) {
    return aContext.newAncestry;
  },

  _getLocalizedRole: function _getLocalizedRole(aRoleStr) {
    try {
      return gStringBundle.GetStringFromName(aRoleStr.replace(' ', ''));
    } catch (x) {
      return '';
    }
  },

  _getLocalizedStates: function _getLocalizedStates(aStates) {
    let stateUtterances = [];

    if (aStates.base & Ci.nsIAccessibleStates.STATE_UNAVAILABLE) {
      stateUtterances.push(gStringBundle.GetStringFromName('stateUnavailable'));
    }

    // Don't utter this in Jelly Bean, we let TalkBack do it for us there.
    // This is because we expose the checked information on the node itself.
    // XXX: this means the checked state is always appended to the end, regardless
    // of the utterance ordering preference.
    if (Utils.AndroidSdkVersion < 16 && aStates.base & Ci.nsIAccessibleStates.STATE_CHECKABLE) {
      let stateStr = (aStates.base & Ci.nsIAccessibleStates.STATE_CHECKED) ?
        'stateChecked' : 'stateNotChecked';
      stateUtterances.push(gStringBundle.GetStringFromName(stateStr));
    }

    if (aStates.ext & Ci.nsIAccessibleStates.EXT_STATE_EXPANDABLE) {
      let stateStr = (aStates.base & Ci.nsIAccessibleStates.STATE_EXPANDED) ?
        'stateExpanded' : 'stateCollapsed';
      stateUtterances.push(gStringBundle.GetStringFromName(stateStr));
    }

    if (aStates.base & Ci.nsIAccessibleStates.STATE_REQUIRED) {
      stateUtterances.push(gStringBundle.GetStringFromName('stateRequired'));
    }

    if (aStates.base & Ci.nsIAccessibleStates.STATE_TRAVERSED) {
      stateUtterances.push(gStringBundle.GetStringFromName('stateTraversed'));
    }

    if (aStates.base & Ci.nsIAccessibleStates.STATE_HASPOPUP) {
      stateUtterances.push(gStringBundle.GetStringFromName('stateHasPopup'));
    }

    return stateUtterances;
  },

  _getListUtterance: function _getListUtterance(aAccessible, aRoleStr, aFlags, aItemCount) {
    let desc = [];
    let roleStr = this._getLocalizedRole(aRoleStr);
    if (roleStr)
      desc.push(roleStr);
    desc.push
      (gStringBundle.formatStringFromName('listItemCount', [aItemCount], 1));
    let utterance = [desc.join(' ')];

    this._addName(utterance, aAccessible, aFlags);

    return utterance;
  }
};


this.BrailleGenerator = {
  __proto__: OutputGenerator,

  defaultOutputOrder: OUTPUT_DESC_LAST,

  objectOutputFunctions: {
    defaultFunc: function defaultFunc(aAccessible, aRoleStr, aStates, aFlags) {
      let braille = OutputGenerator.objectOutputFunctions._generateBaseOutput.apply(this, arguments);

      if (aAccessible.indexInParent === 1 &&
          aAccessible.parent.role == Ci.nsIAccessibleRole.ROLE_LISTITEM &&
          aAccessible.previousSibling.role == Ci.nsIAccessibleRole.ROLE_STATICTEXT) {
        if (aAccessible.parent.parent && aAccessible.parent.parent.DOMNode &&
            aAccessible.parent.parent.DOMNode.nodeName == 'UL') {
          braille.unshift('*');
        } else {
          braille.unshift(aAccessible.previousSibling.name);
        }
      }

      return braille;
    },

    listitem: function listitem(aAccessible, aRoleStr, aStates, aFlags) {
      let braille = [];

      this._addName(braille, aAccessible, aFlags);

      return braille;
    },

    statictext: function statictext(aAccessible, aRoleStr, aStates, aFlags) {
      // Since we customize the list bullet's output, we add the static
      // text from the first node in each listitem, so skip it here.
      if (aAccessible.parent.role == Ci.nsIAccessibleRole.ROLE_LISTITEM) {
        return [];
      }

      return this.objectOutputFunctions._useStateNotRole.apply(this, arguments);
    },

    _useStateNotRole: function _useStateNotRole(aAccessible, aRoleStr, aStates, aFlags) {
      let braille = [];

      let desc = this._getLocalizedStates(aStates);
      braille.push(desc.join(' '));

      this._addName(braille, aAccessible, aFlags);

      return braille;
    },

    checkbutton: function checkbutton(aAccessible, aRoleStr, aStates, aFlags) {
      return this.objectOutputFunctions._useStateNotRole.apply(this, arguments);
    },

    radiobutton: function radiobutton(aAccessible, aRoleStr, aStates, aFlags) {
      return this.objectOutputFunctions._useStateNotRole.apply(this, arguments);
    },

    togglebutton: function radiobutton(aAccessible, aRoleStr, aStates, aFlags) {
      return this.objectOutputFunctions._useStateNotRole.apply(this, arguments);
    },

    entry: function entry(aAccessible, aRoleStr, aStates, aFlags) {
      return OutputGenerator.objectOutputFunctions.entry.apply(this, arguments);
    }
  },

  _getContextStart: function _getContextStart(aContext) {
    if (aContext.accessible.parent.role == Ci.nsIAccessibleRole.ROLE_LINK) {
      return [aContext.accessible.parent];
    }

    return [];
  },

  _getLocalizedRole: function _getLocalizedRole(aRoleStr) {
    try {
      return gStringBundle.GetStringFromName(aRoleStr.replace(' ', '') + 'Abbr');
    } catch (x) {
      try {
        return gStringBundle.GetStringFromName(aRoleStr.replace(' ', ''));
      } catch (y) {
        return '';
      }
    }
  },

  _getLocalizedStates: function _getLocalizedStates(aStates) {
    let stateBraille = [];

    let getCheckedState = function getCheckedState() {
      let resultMarker = [];
      let state = aStates.base;
      let fill = !!(state & Ci.nsIAccessibleStates.STATE_CHECKED) ||
                 !!(state & Ci.nsIAccessibleStates.STATE_PRESSED);

      resultMarker.push('(');
      resultMarker.push(fill ? 'x' : ' ');
      resultMarker.push(')');

      return resultMarker.join('');
    };

    if (aStates.base & Ci.nsIAccessibleStates.STATE_CHECKABLE) {
      stateBraille.push(getCheckedState());
    }

    return stateBraille;
  }

};

/*
 * Copyright 2009-2011 Mozilla Foundation and contributors
 * Licensed under the New BSD license. See LICENSE.txt or:
 * http://opensource.org/licenses/BSD-3-Clause
 */

// define(function(require, exports, module) {

// <INJECTED SOURCE:START>

// THIS FILE IS GENERATED FROM SOURCE IN THE GCLI PROJECT
// DO NOT EDIT IT DIRECTLY

var exports = {};

const TEST_URI = "data:text/html;charset=utf-8,<p id='gcli-input'>gcli-testIncomplete.js</p>";

function test() {
  helpers.addTabWithToolbar(TEST_URI, function(options) {
    return helpers.runTests(options, exports);
  }).then(finish);
}

// <INJECTED SOURCE:END>

'use strict';

// var assert = require('test/assert');
// var helpers = require('gclitest/helpers');
// var mockCommands = require('gclitest/mockCommands');

exports.setup = function(options) {
  mockCommands.setup();
};

exports.shutdown = function(options) {
  mockCommands.shutdown();
};

exports.testBasic = function(options) {
  return helpers.audit(options, [
    {
      setup: 'tsu 2 extra',
      check: {
        args: {
          num: { value: 2, type: 'Argument' }
        }
      },
      post: function() {
        var requisition = options.display.requisition;

        assert.is(requisition._unassigned.length,
                  1,
                  'single unassigned: tsu 2 extra');
        assert.is(requisition._unassigned[0].param.type.isIncompleteName,
                  false,
                  'unassigned.isIncompleteName: tsu 2 extra');
      }
    },
    {
      setup: 'tsu',
      check: {
        args: {
          num: { value: undefined, type: 'BlankArgument' }
        }
      }
    },
    {
      setup: 'tsg',
      check: {
        args: {
          solo: { type: 'BlankArgument' },
          txt1: { type: 'BlankArgument' },
          bool: { type: 'BlankArgument' },
          txt2: { type: 'BlankArgument' },
          num: { type: 'BlankArgument' }
        }
      }
    },
  ]);
};

exports.testCompleted = function(options) {
  return helpers.audit(options, [
    {
      setup: 'tsela<TAB>',
      check: {
        args: {
          command: { name: 'tselarr', type: 'Argument' },
          num: { type: 'BlankArgument' },
          arr: { type: 'ArrayArgument' }
        }
      }
    },
    {
      setup:    'tsn dif ',
      check: {
        input:  'tsn dif ',
        hints:          '<text>',
        markup: 'VVVVVVVV',
        cursor: 8,
        status: 'ERROR',
        args: {
          command: { name: 'tsn dif', type: 'MergedArgument' },
          text: { type: 'BlankArgument', status: 'INCOMPLETE' }
        }
      }
    },
    {
      setup:    'tsn di<TAB>',
      check: {
        input:  'tsn dif ',
        hints:          '<text>',
        markup: 'VVVVVVVV',
        cursor: 8,
        status: 'ERROR',
        args: {
          command: { name: 'tsn dif', type: 'Argument' },
          text: { type: 'BlankArgument', status: 'INCOMPLETE' }
        }
      }
    },
    // The above 2 tests take different routes to 'tsn dif '.
    // The results should be similar. The difference is in args.command.type.
    {
      setup:    'tsg -',
      check: {
        input:  'tsg -',
        hints:       '-txt1 <solo> [options]',
        markup: 'VVVVI',
        cursor: 5,
        status: 'ERROR',
        args: {
          solo: { value: undefined, status: 'INCOMPLETE' },
          txt1: { value: undefined, status: 'VALID' },
          bool: { value: false, status: 'VALID' },
          txt2: { value: undefined, status: 'VALID' },
          num: { value: undefined, status: 'VALID' }
        }
      }
    },
    {
      skipIf: options.isJsdom,
      setup:    'tsg -<TAB>',
      check: {
        input:  'tsg --txt1 ',
        hints:             '<string> <solo> [options]',
        markup: 'VVVVIIIIIIV',
        cursor: 11,
        status: 'ERROR',
        args: {
          solo: { value: undefined, status: 'INCOMPLETE' },
          txt1: { value: undefined, status: 'INCOMPLETE' },
          bool: { value: false, status: 'VALID' },
          txt2: { value: undefined, status: 'VALID' },
          num: { value: undefined, status: 'VALID' }
        }
      }
    },
    {
      setup:    'tsg --txt1 fred',
      check: {
        input:  'tsg --txt1 fred',
        hints:                 ' <solo> [options]',
        markup: 'VVVVVVVVVVVVVVV',
        status: 'ERROR',
        args: {
          solo: { value: undefined, status: 'INCOMPLETE' },
          txt1: { value: 'fred', status: 'VALID' },
          bool: { value: false, status: 'VALID' },
          txt2: { value: undefined, status: 'VALID' },
          num: { value: undefined, status: 'VALID' }
        }
      }
    },
    {
      setup:    'tscook key value --path path --',
      check: {
        input:  'tscook key value --path path --',
        hints:                                 'domain [options]',
        markup: 'VVVVVVVVVVVVVVVVVVVVVVVVVVVVVII',
        status: 'ERROR',
        args: {
          key: { value: 'key', status: 'VALID' },
          value: { value: 'value', status: 'VALID' },
          path: { value: 'path', status: 'VALID' },
          domain: { value: undefined, status: 'VALID' },
          secure: { value: false, status: 'VALID' }
        }
      }
    },
    {
      setup:    'tscook key value --path path --domain domain --',
      check: {
        input:  'tscook key value --path path --domain domain --',
        hints:                                                 'secure [options]',
        markup: 'VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVII',
        status: 'ERROR',
        args: {
          key: { value: 'key', status: 'VALID' },
          value: { value: 'value', status: 'VALID' },
          path: { value: 'path', status: 'VALID' },
          domain: { value: 'domain', status: 'VALID' },
          secure: { value: false, status: 'VALID' }
        }
      }
    }
  ]);

};

exports.testCase = function(options) {
  return helpers.audit(options, [
    {
      setup:    'tsg AA',
      check: {
        input:  'tsg AA',
        hints:        ' [options] -> aaa',
        markup: 'VVVVII',
        status: 'ERROR',
        args: {
          solo: { value: undefined, text: 'AA', status: 'INCOMPLETE' },
          txt1: { value: undefined, status: 'VALID' },
          bool: { value: false, status: 'VALID' },
          txt2: { value: undefined, status: 'VALID' },
          num: { value: undefined, status: 'VALID' }
        }
      }
    },
  ]);
};

exports.testIncomplete = function(options) {
  return helpers.audit(options, [
    {
      setup:    'tsm a a -',
      check: {
        args: {
          abc: { value: 'a', type: 'Argument' },
          txt: { value: 'a', type: 'Argument' },
          num: { value: undefined, arg: ' -', type: 'Argument', status: 'INCOMPLETE' }
        }
      }
    },
    {
      setup:    'tsg -',
      check: {
        args: {
          solo: { type: 'BlankArgument' },
          txt1: { type: 'BlankArgument' },
          bool: { type: 'BlankArgument' },
          txt2: { type: 'BlankArgument' },
          num: { type: 'BlankArgument' }
        }
      },
      post: function() {
        var requisition = options.display.requisition;

        assert.is(requisition._unassigned[0],
                  requisition.getAssignmentAt(5),
                  'unassigned -');
        assert.is(requisition._unassigned.length,
                  1,
                  'single unassigned - tsg -');
        assert.is(requisition._unassigned[0].param.type.isIncompleteName,
                  true,
                  'unassigned.isIncompleteName: tsg -');
      }
    },
  ]);
};

exports.testHidden = function(options) {
  return helpers.audit(options, [
    {
      setup:    'tshidde',
      check: {
        input:  'tshidde',
        hints:         ' -> tse',
        status: 'ERROR'
      }
    },
    {
      setup:    'tshidden',
      check: {
        input:  'tshidden',
        hints:          ' [options]',
        markup: 'VVVVVVVV',
        status: 'VALID',
        args: {
          visible: { value: undefined, status: 'VALID' },
          invisiblestring: { value: undefined, status: 'VALID' },
          invisibleboolean: { value: false, status: 'VALID' }
        }
      }
    },
    {
      setup:    'tshidden --vis',
      check: {
        input:  'tshidden --vis',
        hints:                'ible [options]',
        markup: 'VVVVVVVVVIIIII',
        status: 'ERROR',
        args: {
          visible: { value: undefined, status: 'VALID' },
          invisiblestring: { value: undefined, status: 'VALID' },
          invisibleboolean: { value: false, status: 'VALID' }
        }
      }
    },
    {
      setup:    'tshidden --invisiblestrin',
      check: {
        input:  'tshidden --invisiblestrin',
        hints:                           ' [options]',
        markup: 'VVVVVVVVVEEEEEEEEEEEEEEEE',
        status: 'ERROR',
        args: {
          visible: { value: undefined, status: 'VALID' },
          invisiblestring: { value: undefined, status: 'VALID' },
          invisibleboolean: { value: false, status: 'VALID' }
        }
      }
    },
    {
      setup:    'tshidden --invisiblestring',
      check: {
        input:  'tshidden --invisiblestring',
        hints:                            ' <string> [options]',
        markup: 'VVVVVVVVVIIIIIIIIIIIIIIIII',
        status: 'ERROR',
        args: {
          visible: { value: undefined, status: 'VALID' },
          invisiblestring: { value: undefined, status: 'INCOMPLETE' },
          invisibleboolean: { value: false, status: 'VALID' }
        }
      }
    },
    {
      setup:    'tshidden --invisiblestring x',
      check: {
        input:  'tshidden --invisiblestring x',
        hints:                              ' [options]',
        markup: 'VVVVVVVVVVVVVVVVVVVVVVVVVVVV',
        status: 'VALID',
        args: {
          visible: { value: undefined, status: 'VALID' },
          invisiblestring: { value: 'x', status: 'VALID' },
          invisibleboolean: { value: false, status: 'VALID' }
        }
      }
    },
    {
      setup:    'tshidden --invisibleboolea',
      check: {
        input:  'tshidden --invisibleboolea',
        hints:                            ' [options]',
        markup: 'VVVVVVVVVEEEEEEEEEEEEEEEEE',
        status: 'ERROR',
        args: {
          visible: { value: undefined, status: 'VALID' },
          invisiblestring: { value: undefined, status: 'VALID' },
          invisibleboolean: { value: false, status: 'VALID' }
        }
      }
    },
    {
      setup:    'tshidden --invisibleboolean',
      check: {
        input:  'tshidden --invisibleboolean',
        hints:                             ' [options]',
        markup: 'VVVVVVVVVVVVVVVVVVVVVVVVVVV',
        status: 'VALID',
        args: {
          visible: { value: undefined, status: 'VALID' },
          invisiblestring: { value: undefined, status: 'VALID' },
          invisibleboolean: { value: true, status: 'VALID' }
        }
      }
    },
    {
      setup:    'tshidden --visible xxx',
      check: {
        input:  'tshidden --visible xxx',
        markup: 'VVVVVVVVVVVVVVVVVVVVVV',
        status: 'VALID',
        hints:  '',
        args: {
          visible: { value: 'xxx', status: 'VALID' },
          invisiblestring: { value: undefined, status: 'VALID' },
          invisibleboolean: { value: false, status: 'VALID' }
        }
      }
    },
  ]);
};

// });

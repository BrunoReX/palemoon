/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 30000;

SpecialPowers.addPermission("mobileconnection", true, document);

let icc = navigator.mozIccManager;
ok(icc instanceof MozIccManager, "icc is instanceof " + icc.constructor);

function testSetupMenu(command, expect) {
  log("STK CMD " + JSON.stringify(command));
  is(command.typeOfCommand, icc.STK_CMD_SET_UP_MENU, expect.name);
  is(command.commandQualifier, expect.commandQualifier, expect.name);
  is(command.options.title, expect.title, expect.name);
  for (let index in command.options.items) {
    is(command.options.items[index].identifier, expect.items[index].identifier, expect.name);
    is(command.options.items[index].text, expect.items[index].text, expect.name);
  }

  runNextTest();
}

let tests = [
  {command: "d03b810301250082028182850c546f6f6c6b6974204d656e758f07014974656d20318f07024974656d20328f07034974656d20338f07044974656d2034",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_1",
            commandQualifier: 0x00,
            title: "Toolkit Menu",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}, {identifier: 4, text: "Item 4"}]}},
  {command: "d023810301250082028182850c546f6f6c6b6974204d656e758f04114f6e658f041254776f",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_2",
            commandQualifier: 0x00,
            title: "Toolkit Menu",
            items: [{identifier: 17, text: "One"}, {identifier: 18, text: "Two"}]}},
  {command: "d081fc810301250082028182850a4c617267654d656e75318f05505a65726f8f044f4f6e658f044e54776f8f064d54687265658f054c466f75728f054b466976658f044a5369788f0649536576656e8f064845696768748f05474e696e658f0646416c7068618f0645427261766f8f0844436861726c69658f064344656c74618f05424563686f8f0941466f782d74726f748f0640426c61636b8f063f42726f776e8f043e5265648f073d4f72616e67658f073c59656c6c6f778f063b477265656e8f053a426c75658f073956696f6c65748f0538477265798f063757686974658f06366d696c6c698f06356d6963726f8f05346e616e6f8f05337069636f",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_3",
            commandQualifier: 0x00,
            title: "LargeMenu1",
            items: [{identifier: 80, text: "Zero"}, {identifier: 79, text: "One"}, {identifier: 78, text: "Two"}, {identifier: 77, text: "Three"}, {identifier: 76, text: "Four"}, {identifier: 75, text: "Five"}, {identifier: 74, text: "Six"}, {identifier: 73, text: "Seven"}, {identifier: 72, text: "Eight"}, {identifier: 71, text: "Nine"}, {identifier: 70, text: "Alpha"}, {identifier: 69, text: "Bravo"}, {identifier: 68, text: "Charlie"}, {identifier: 67, text: "Delta"}, {identifier: 66, text: "Echo"}, {identifier: 65, text: "Fox-trot"}, {identifier: 64, text: "Black"}, {identifier: 63, text: "Brown"}, {identifier: 62, text: "Red"}, {identifier: 61, text: "Orange"}, {identifier: 60, text: "Yellow"}, {identifier: 59, text: "Green"}, {identifier: 58, text: "Blue"}, {identifier: 57, text: "Violet"}, {identifier: 56, text: "Grey"}, {identifier: 55, text: "White"}, {identifier: 54, text: "milli"}, {identifier: 53, text: "micro"}, {identifier: 52, text: "nano"}, {identifier: 51, text: "pico"}]}},
  {command: "d081f3810301250082028182850a4c617267654d656e75328f1dff312043616c6c20466f727761726420556e636f6e646974696f6e616c8f1cfe322043616c6c20466f7277617264204f6e205573657220427573798f1bfd332043616c6c20466f7277617264204f6e204e6f205265706c798f25fc342043616c6c20466f7277617264204f6e2055736572204e6f7420526561636861626c658f20fb352042617272696e67204f6620416c6c204f7574676f696e672043616c6c738f24fa362042617272696e67204f6620416c6c204f7574676f696e6720496e742043616c6c738f13f93720434c492050726573656e746174696f6e",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_4",
            commandQualifier: 0x00,
            title: "LargeMenu2",
            items: [{identifier: 255, text: "1 Call Forward Unconditional"}, {identifier: 254, text: "2 Call Forward On User Busy"}, {identifier: 253, text: "3 Call Forward On No Reply"}, {identifier: 252, text: "4 Call Forward On User Not Reachable"}, {identifier: 251, text: "5 Barring Of All Outgoing Calls"}, {identifier: 250, text: "6 Barring Of All Outgoing Int Calls"}, {identifier: 249, text: "7 CLI Presentation"}]}},
  {command: "d081fc8103012500820281828581ec5468652053494d207368616c6c20737570706c79206120736574206f66206d656e75206974656d732c207768696368207368616c6c20626520696e7465677261746564207769746820746865206d656e752073797374656d20286f72206f74686572204d4d4920666163696c6974792920696e206f7264657220746f206769766520746865207573657220746865206f70706f7274756e69747920746f2063686f6f7365206f6e65206f66207468657365206d656e75206974656d7320617420686973206f776e2064697363726574696f6e2e2045616368206974656d20636f6d70726973657320612073688f020159",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_5",
            commandQualifier: 0x00,
            title: "The SIM shall supply a set of menu items, which shall be integrated with the menu system (or other MMI facility) in order to give the user the opportunity to choose one of these menu items at his own discretion. Each item comprises a sh",
            items: [{identifier: 1, text: "Y"}]}},
  {command: "d03b810301258082028182850c546f6f6c6b6974204d656e758f07014974656d20318f07024974656d20328f07034974656d20338f07044974656d2034",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_6",
            commandQualifier: 0x80,
            title: "Toolkit Menu",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}, {identifier: 4, text: "Item 4"}]}},
  {command: "d041810301250082028182850c546f6f6c6b6974204d656e758f07014974656d20318f07024974656d20328f07034974656d20338f07044974656d2034180413101526",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_7",
            commandQualifier: 0x00,
            title: "Toolkit Menu",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}, {identifier: 4, text: "Item 4"}]}},
  {command: "d03c810301250082028182850c546f6f6c6b6974204d656e758f07014974656d20318f07024974656d20328f07034974656d20339e0201019f0401050505",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_8",
            commandQualifier: 0x00,
            title: "Toolkit Menu",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d03c810301250082028182850c546f6f6c6b6974204d656e758f07014974656d20318f07024974656d20328f07034974656d20339e0200019f0400050505",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_9",
            commandQualifier: 0x00,
            title: "Toolkit Menu",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d029810301250182028182850c546f6f6c6b6974204d656e758f07014974656d20318f07024974656d2032",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_10",
            commandQualifier: 0x01,
            title: "Toolkit Menu",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}]}},
  {command: "d048810301250082028182850e546f6f6c6b6974204d656e7520318f07014974656d20318f07024974656d20328f07034974656d2033d004000e00b4d10c000600b4000600b4000600b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_11",
            commandQualifier: 0x00,
            title: "Toolkit Menu 1",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d034810301250082028182850e546f6f6c6b6974204d656e7520328f07044974656d20348f07054974656d20358f07064974656d2036",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_12",
            commandQualifier: 0x00,
            title: "Toolkit Menu 2",
            items: [{identifier: 4, text: "Item 4"}, {identifier: 5, text: "Item 5"}, {identifier: 6, text: "Item 6"}]}},
  {command: "d048810301250082028182850e546f6f6c6b6974204d656e7520318f07014974656d20318f07024974656d20328f07034974656d2033d004000e01b4d10c000601b4000601b4000601b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_13",
            commandQualifier: 0x00,
            title: "Toolkit Menu 1",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d034810301250082028182850e546f6f6c6b6974204d656e7520328f07044974656d20348f07054974656d20358f07064974656d2036",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_14",
            commandQualifier: 0x00,
            title: "Toolkit Menu 2",
            items: [{identifier: 4, text: "Item 4"}, {identifier: 5, text: "Item 5"}, {identifier: 6, text: "Item 6"}]}},
  {command: "d048810301250082028182850e546f6f6c6b6974204d656e7520318f07014974656d20318f07024974656d20328f07034974656d2033d004000e02b4d10c000602b4000602b4000602b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_15",
            commandQualifier: 0x00,
            title: "Toolkit Menu 1",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d034810301250082028182850e546f6f6c6b6974204d656e7520328f07044974656d20348f07054974656d20358f07064974656d2036",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_16",
            commandQualifier: 0x00,
            title: "Toolkit Menu 2",
            items: [{identifier: 4, text: "Item 4"}, {identifier: 5, text: "Item 5"}, {identifier: 6, text: "Item 6"}]}},
  {command: "d048810301250082028182850e546f6f6c6b6974204d656e7520318f07014974656d20318f07024974656d20328f07034974656d2033d004000e04b4d10c000604b4000604b4000604b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_17",
            commandQualifier: 0x00,
            title: "Toolkit Menu 1",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d048810301250082028182850e546f6f6c6b6974204d656e7520328f07044974656d20348f07054974656d20358f07064974656d2036d004000e00b4d10c000600b4000600b4000600b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_18",
            commandQualifier: 0x00,
            title: "Toolkit Menu 2",
            items: [{identifier: 4, text: "Item 4"}, {identifier: 5, text: "Item 5"}, {identifier: 6, text: "Item 6"}]}},
  {command: "d034810301250082028182850e546f6f6c6b6974204d656e7520338f07074974656d20378f07084974656d20388f07094974656d2039",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_19",
            commandQualifier: 0x00,
            title: "Toolkit Menu 3",
            items: [{identifier: 7, text: "Item 7"}, {identifier: 8, text: "Item 8"}, {identifier: 9, text: "Item 9"}]}},
  {command: "d048810301250082028182850e546f6f6c6b6974204d656e7520318f07014974656d20318f07024974656d20328f07034974656d2033d004000e08b4d10c000608b4000608b4000608b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_20",
            commandQualifier: 0x00,
            title: "Toolkit Menu 1",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d048810301250082028182850e546f6f6c6b6974204d656e7520318f07014974656d20318f07024974656d20328f07034974656d2033d004000e10b4d10c000610b4000610b4000610b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_21",
            commandQualifier: 0x00,
            title: "Toolkit Menu 1",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d048810301250082028182850e546f6f6c6b6974204d656e7520318f07014974656d20318f07024974656d20328f07034974656d2033d004000e20b4d10c000620b4000620b4000620b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_22",
            commandQualifier: 0x00,
            title: "Toolkit Menu 1",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d048810301250082028182850e546f6f6c6b6974204d656e7520318f07014974656d20318f07024974656d20328f07034974656d2033d004000e40b4d10c000640b4000640b4000640b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_23",
            commandQualifier: 0x00,
            title: "Toolkit Menu 1",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d048810301250082028182850e546f6f6c6b6974204d656e7520318f07014974656d20318f07024974656d20328f07034974656d2033d004000e80b4d10c000680b4000680b4000680b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_24",
            commandQualifier: 0x00,
            title: "Toolkit Menu 1",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d046810301250082028182850c546f6f6c6b6974204d656e758f07014974656d20318f07024974656d20328f07034974656d2033d004000c00b4d10c000600b4000600b4000600b4",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_25",
            commandQualifier: 0x00,
            title: "Toolkit Menu",
            items: [{identifier: 1, text: "Item 1"}, {identifier: 2, text: "Item 2"}, {identifier: 3, text: "Item 3"}]}},
  {command: "d0819c8103012500820281828519800417041404200410041204210422041204230419042204158f1c018004170414042004100412042104220412042304190422041500318f1c028004170414042004100412042104220412042304190422041500328f1c038004170414042004100412042104220412042304190422041500338f1c04800417041404200410041204210422041204230419042204150034",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_26",
            commandQualifier: 0x00,
            title: "ЗДРАВСТВУЙТЕ",
            items: [{identifier: 1, text: "ЗДРАВСТВУЙТЕ1"}, {identifier: 2, text: "ЗДРАВСТВУЙТЕ2"}, {identifier: 3, text: "ЗДРАВСТВУЙТЕ3"}, {identifier: 4, text: "ЗДРАВСТВУЙТЕ4"}]}},
  {command: "d0608103012500820281828519800417041404200410041204210422041204230419042204158f1c118004170414042004100412042104220412042304190422041500358f1c12800417041404200410041204210422041204230419042204150036",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_27",
            commandQualifier: 0x00,
            title: "ЗДРАВСТВУЙТЕ",
            items: [{identifier: 17, text: "ЗДРАВСТВУЙТЕ5"}, {identifier: 18, text: "ЗДРАВСТВУЙТЕ6"}]}},
  {command: "d03c8103012500820281828509805de551777bb153558f080180987976ee4e008f080280987976ee4e8c8f080380987976ee4e098f080480987976ee56db",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_28",
            commandQualifier: 0x00,
            title: "工具箱单",
            items: [{identifier: 1, text: "项目一"}, {identifier: 2, text: "项目二"}, {identifier: 3, text: "项目三"}, {identifier: 4, text: "项目四"}]}},
  {command: "d0208103012500820281828509805de551777bb153558f0411804e008f0412804e8c",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_29",
            commandQualifier: 0x00,
            title: "工具箱单",
            items: [{identifier: 17, text: "一"}, {identifier: 18, text: "二"}]}},
  {command: "d0448103012500820281828509800038003030eb00308f0a01800038003030eb00318f0a02800038003030eb00328f0a03800038003030eb00338f0a04800038003030eb0034",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_30",
            commandQualifier: 0x00,
            title: "80ル0",
            items: [{identifier: 1, text: "80ル1"}, {identifier: 2, text: "80ル2"}, {identifier: 3, text: "80ル3"}, {identifier: 4, text: "80ル4"}]}},
  {command: "d02c8103012500820281828509800038003030eb00308f0a11800038003030eb00358f0a12800038003030eb0036",
   func: testSetupMenu,
   expect: {name: "setup_menu_cmd_31",
            commandQualifier: 0x00,
            title: "80ル0",
            items: [{identifier: 17, text: "80ル5"}, {identifier: 18, text: "80ル6"}]}}
];

let pendingEmulatorCmdCount = 0;
function sendStkPduToEmulator(command, func, expect) {
  ++pendingEmulatorCmdCount;

  runEmulatorCmd(command, function (result) {
    --pendingEmulatorCmdCount;
    is(result[0], "OK");
  });

  icc.onstkcommand = function (evt) {
    func(evt.command, expect);
  }
}

function runNextTest() {
  let test = tests.pop();
  if (!test) {
    cleanUp();
    return;
  }

  let command = "stk pdu " + test.command;
  sendStkPduToEmulator(command, test.func, test.expect)
}

function cleanUp() {
  if (pendingEmulatorCmdCount) {
    window.setTimeout(cleanUp, 100);
    return;
  }

  SpecialPowers.removePermission("mobileconnection", document);
  finish();
}

runNextTest();

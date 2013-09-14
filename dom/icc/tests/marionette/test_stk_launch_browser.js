/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 30000;

SpecialPowers.addPermission("mobileconnection", true, document);

let icc = navigator.mozIccManager;
ok(icc instanceof MozIccManager, "icc is instanceof " + icc.constructor);

function testLaunchBrowser(command, expect) {
  log("STK CMD " + JSON.stringify(command));
  is(command.typeOfCommand, icc.STK_CMD_LAUNCH_BROWSER, expect.name);
  is(command.commandQualifier, expect.commandQualifier, expect.name);
  is(command.options.url, expect.url, expect.name);
  if (command.options.confirmMessage) {
    is(command.options.confirmMessage, expect.text, expect.name);
  }

  runNextTest();
}

let tests = [
  {command: "d0188103011500820281823100050b44656661756c742055524c",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_1",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL"}},
  {command: "d01f8103011500820281823112687474703a2f2f7878782e7979792e7a7a7a0500",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_2",
            commandQualifier: 0x00,
            url: "http://xxx.yyy.zzz",
            text: ""}},
  {command: "d00e8103011500820281823001003100",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_3",
            commandQualifier: 0x00,
            url: "",
            text: ""}},
  {command: "d02081030115008202818231003201030d10046162632e6465662e6768692e6a6b6c",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_4",
            commandQualifier: 0x00,
            url: "",
            text: ""}},
  {command: "d0188103011502820281823100050b44656661756c742055524c",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_5",
            commandQualifier: 0x02,
            url: "",
            text: "Default URL"}},
  {command: "d0188103011503820281823100050b44656661756c742055524c",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_6",
            commandQualifier: 0x03,
            url: "",
            text: "Default URL"}},
  {command: "d00b8103011500820281823100",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_7",
            commandQualifier: 0x00,
            url: "",
            text: ""}},
  {command: "d0268103011502820281823100051980041704140420041004120421042204120423041904220415",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_8",
            commandQualifier: 0x02,
            url: "",
            text: "ЗДРАВСТВУЙТЕ"}},
  {command: "d021810301150282028182310005104e6f742073656c66206578706c616e2e1e020101",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_9",
            commandQualifier: 0x02,
            url: "",
            text: "Not self explan."}},
  {command: "d01d8103011502820281823100050c53656c66206578706c616e2e1e020001",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_10",
            commandQualifier: 0x02,
            url: "",
            text: "Self explan."}},
  {command: "d0208103011500820281823100050d44656661756c742055524c2031d004000d00b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_11",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 1"}},
  {command: "d01a8103011500820281823100050d44656661756c742055524c2032",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_12",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 2"}},
  {command: "d0208103011500820281823100050d44656661756c742055524c2031d004000d01b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_13",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 1"}},
  {command: "d01a8103011500820281823100050d44656661756c742055524c2032",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_14",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 2"}},
  {command: "d0208103011500820281823100050d44656661756c742055524c2031d004000d02b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_15",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 1"}},
  {command: "d01a8103011500820281823100050d44656661756c742055524c2032",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_16",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 2"}},
   {command: "d0208103011500820281823100050d44656661756c742055524c2031d004000d04b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_17",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 1"}},
  {command: "d0208103011500820281823100050d44656661756c742055524c2032d004000d00b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_18",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 2"}},
  {command: "d01a8103011500820281823100050d44656661756c742055524c2033",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_19",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 3"}},
   {command: "d0208103011500820281823100050d44656661756c742055524c2031d004000d08b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_20",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 1"}},
  {command: "d0208103011500820281823100050d44656661756c742055524c2032d004000d00b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_21",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 2"}},
  {command: "d01a8103011500820281823100050d44656661756c742055524c2033",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_22",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 3"}},
   {command: "d0208103011500820281823100050d44656661756c742055524c2031d004000d10b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_23",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 1"}},
  {command: "d0208103011500820281823100050d44656661756c742055524c2032d004000d00b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_24",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 2"}},
  {command: "d01a8103011500820281823100050d44656661756c742055524c2033",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_25",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 3"}},
   {command: "d0208103011500820281823100050d44656661756c742055524c2031d004000d20b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_26",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 1"}},
  {command: "d0208103011500820281823100050d44656661756c742055524c2032d004000d00b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_27",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 2"}},
  {command: "d01a8103011500820281823100050d44656661756c742055524c2033",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_28",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 3"}},
   {command: "d0208103011500820281823100050d44656661756c742055524c2031d004000d40b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_29",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 1"}},
  {command: "d0208103011500820281823100050d44656661756c742055524c2032d004000d00b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_30",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 2"}},
  {command: "d01a8103011500820281823100050d44656661756c742055524c2033",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_31",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 3"}},
   {command: "d0208103011500820281823100050d44656661756c742055524c2031d004000d80b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_32",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 1"}},
  {command: "d0208103011500820281823100050d44656661756c742055524c2032d004000d00b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_33",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 2"}},
  {command: "d01a8103011500820281823100050d44656661756c742055524c2033",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_34",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 3"}},
   {command: "d0208103011500820281823100050d44656661756c742055524c2031d004000d00b4",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_35",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 1"}},
  {command: "d01a8103011500820281823100050d44656661756c742055524c2032",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_36",
            commandQualifier: 0x00,
            url: "",
            text: "Default URL 2"}},
   {command: "d01281030115028202818231000505804f60597d",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_37",
            commandQualifier: 0x02,
            url: "",
            text: "你好"}},
  {command: "d010810301150282028182310005038030eb",
   func: testLaunchBrowser,
   expect: {name: "launch_browser_cmd_38",
            commandQualifier: 0x02,
            url: "",
            text: "ル"}}
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

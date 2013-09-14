/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 30000;

SpecialPowers.addPermission("mobileconnection", true, document);

let icc = navigator.mozIccManager;
ok(icc instanceof MozIccManager, "icc is instanceof " + icc.constructor);

function testSendSMS(command, expect) {
  log("STK CMD " + JSON.stringify(command));
  is(command.typeOfCommand, icc.STK_CMD_SEND_SMS, expect.name);
  is(command.commandQualifier, expect.commandQualifier, expect.name);
  if (command.options.text) {
    is(command.options.text, expect.title, expect.name);
  }

  runNextTest();
}

let tests = [
  {command: "d037810301130082028183850753656e6420534d86099111223344556677f88b180100099110325476f840f40c54657374204d657373616765",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_1",
            commandQualifier: 0x00,
            title: "Send SM"}},
  {command: "d032810301130182028183850753656e6420534d86099111223344556677f88b130100099110325476f840f40753656e6420534d",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_2",
            commandQualifier: 0x01,
            title: "Send SM"}},
  {command: "d03d810301130082028183850d53686f7274204d65737361676586099111223344556677f88b180100099110325476f840f00d53f45b4e0735cbf379f85c06",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_3",
            commandQualifier: 0x00,
            title: "Short Message"}},
  {command: "d081fd810301130182028183853854686520616464726573732064617461206f626a65637420686f6c6473207468652052501144657374696e6174696f6e114164647265737386099111223344556677f88b81ac0100099110325476f840f4a054776f2074797065732061726520646566696e65643a202d20412073686f7274206d65737361676520746f2062652073656e7420746f20746865206e6574776f726b20696e20616e20534d532d5355424d4954206d6573736167652c206f7220616e20534d532d434f4d4d414e44206d6573736167652c20776865726520746865207573657220646174612063616e20626520706173736564207472616e7370",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_4",
            commandQualifier: 0x01,
            title: "The address data object holds the RP_Destination_Address"}},
  {command: "d081e9810301130082028183853854686520616464726573732064617461206f626a65637420686f6c6473207468652052502044657374696e6174696f6e204164647265737386099111223344556677f88b81980100099110325476f840f0a0d4fb1b44cfc3cb7350585e0691cbe6b4bb4cd6815aa020688e7ecbe9a076793e0f9fcb20fa1b242e83e665371d447f83e8e832c85da6dfdff23528ed0685dda06973da9a5685cd2415d42ecfe7e17399057acb41613768da9cb686cf6633e82482dae5f93c7c2eb3407774595e06d1d165507d5e9683c8617a18340ebb41e232081e9ecfcb64105d1e76cfe1",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_5",
            commandQualifier: 0x00,
            title: "The address data object holds the RP Destination Address"}},
  {command: "d081fd8103011300820281838581e654776f2074797065732061726520646566696e65643a202d20412073686f7274206d65737361676520746f2062652073656e7420746f20746865206e6574776f726b20696e20616e20534d532d5355424d4954206d6573736167652c206f7220616e20534d532d434f4d4d414e44206d6573736167652c20776865726520746865207573657220646174612063616e20626520706173736564207472616e73706172656e746c793b202d20412073686f7274206d65737361676520746f2062652073656e7420746f20746865206e6574776f726b20696e20616e20534d532d5355424d4954208b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_6",
            commandQualifier: 0x00,
            title: "Two types are defined: - A short message to be sent to the network in an SMS-SUBMIT message, or an SMS-COMMAND message, where the user data can be passed transparently; - A short message to be sent to the network in an SMS-SUBMIT "}},
  {command: "d030810301130082028183850086099111223344556677f88b180100099110325476f840f40c54657374204d657373616765",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_7",
            commandQualifier: 0x00,
            title: ""}},
  {command: "d05581030113008202818385198004170414042004100412042104220412042304190422041586099111223344556677f88b240100099110325476f8400818041704140420041004120421042204120423041904220415",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_8",
            commandQualifier: 0x00,
            title: "ЗДРАВСТВУЙТЕ"}},
  {command: "d04b810301130082028183850f810c089794a09092a1a292a399a29586099111223344556677f88b240100099110325476f8400818041704140420041004120421042204120423041904220415",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_9",
            commandQualifier: 0x00,
            title: "ЗДРАВСТВУЙТЕ"}},
  {command: "d04c8103011300820281838510820c041087849080829192829389928586099111223344556677f88b240100099110325476f8400818041704140420041004120421042204120423041904220415",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_9",
            commandQualifier: 0x00,
            title: "ЗДРАВСТВУЙТЕ"}},
  {command: "d03b81030113008202818385074e4f2049434f4e86099111223344556677f88b180100099110325476f840f40c54657374204d6573736167659e020001",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_10",
            commandQualifier: 0x00,
            title: "NO ICON"}},
  {command: "d03b810301130082028183850753656e6420534d86099111223344556677f88b180100099110325476f840f40c54657374204d6573736167651e020101",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_11",
            commandQualifier: 0x00,
            title: "Send SM"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520318b09010002911040f00120d004001000b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_12",
            commandQualifier: 0x00,
            title: "Text Attribute 1"}},
  {command: "d0268103011300820281838510546578742041747472696275746520328b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_13",
            commandQualifier: 0x00,
            title: "Text Attribute 2"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520318b09010002911040f00120d004001001b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_14",
            commandQualifier: 0x00,
            title: "Text Attribute 1"}},
  {command: "d0268103011300820281838510546578742041747472696275746520328b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_15",
            commandQualifier: 0x00,
            title: "Text Attribute 2"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520318b09010002911040f00120d004001002b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_16",
            commandQualifier: 0x00,
            title: "Text Attribute 1"}},
  {command: "d0268103011300820281838510546578742041747472696275746520328b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_17",
            commandQualifier: 0x00,
            title: "Text Attribute 2"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520318b09010002911040f00120d004001004b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_18",
            commandQualifier: 0x00,
            title: "Text Attribute 1"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520328b09010002911040f00120d004001000b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_19",
            commandQualifier: 0x00,
            title: "Text Attribute 2"}},
  {command: "d0268103011300820281838510546578742041747472696275746520338b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_20",
            commandQualifier: 0x00,
            title: "Text Attribute 3"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520318b09010002911040f00120d004001008b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_21",
            commandQualifier: 0x00,
            title: "Text Attribute 1"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520328b09010002911040f00120d004001000b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_22",
            commandQualifier: 0x00,
            title: "Text Attribute 2"}},
  {command: "d0268103011300820281838510546578742041747472696275746520338b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_23",
            commandQualifier: 0x00,
            title: "Text Attribute 3"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520318b09010002911040f00120d004001010b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_24",
            commandQualifier: 0x00,
            title: "Text Attribute 1"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520328b09010002911040f00120d004001000b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_25",
            commandQualifier: 0x00,
            title: "Text Attribute 2"}},
  {command: "d0268103011300820281838510546578742041747472696275746520338b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_26",
            commandQualifier: 0x00,
            title: "Text Attribute 3"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520318b09010002911040f00120d004001020b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_27",
            commandQualifier: 0x00,
            title: "Text Attribute 1"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520328b09010002911040f00120d004001000b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_28",
            commandQualifier: 0x00,
            title: "Text Attribute 2"}},
  {command: "d0268103011300820281838510546578742041747472696275746520338b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_29",
            commandQualifier: 0x00,
            title: "Text Attribute 3"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520318b09010002911040f00120d004001040b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_30",
            commandQualifier: 0x00,
            title: "Text Attribute 1"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520328b09010002911040f00120d004001000b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_31",
            commandQualifier: 0x00,
            title: "Text Attribute 2"}},
  {command: "d0268103011300820281838510546578742041747472696275746520338b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_32",
            commandQualifier: 0x00,
            title: "Text Attribute 3"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520318b09010002911040f00120d004001080b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_33",
            commandQualifier: 0x00,
            title: "Text Attribute 1"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520328b09010002911040f00120d004001000b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_34",
            commandQualifier: 0x00,
            title: "Text Attribute 2"}},
  {command: "d0268103011300820281838510546578742041747472696275746520338b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_35",
            commandQualifier: 0x00,
            title: "Text Attribute 3"}},
  {command: "d02c8103011300820281838510546578742041747472696275746520318b09010002911040f00120d004001000b4",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_36",
            commandQualifier: 0x00,
            title: "Text Attribute 1"}},
  {command: "d0268103011300820281838510546578742041747472696275746520328b09010002911040f00120",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_37",
            commandQualifier: 0x00,
            title: "Text Attribute 2"}},
  {command: "d02d8103011300820281838505804e2d4e0086099111223344556677f88b100100099110325476f84008044e2d4e00",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_38",
            commandQualifier: 0x00,
            title: "中一"}},
  {command: "d02d810301130082028183850581029cad8086099111223344556677f88b100100099110325476f84008044e2d4e00",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_39",
            commandQualifier: 0x00,
            title: "中一"}},
  {command: "d02e810301130082028183850682024e00ad8086099111223344556677f88b100100099110325476f84008044e2d4e00",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_40",
            commandQualifier: 0x00,
            title: "中一"}},
  {command: "d0358103011300820281838509800038003030eb003086099111223344556677f88b140100099110325476f84008080038003030eb0031",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_41",
            commandQualifier: 0x00,
            title: "80ル0"}},
  {command: "d03381030113008202818385078104613831eb3186099111223344556677f88b140100099110325476f84008080038003030eb0032",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_42",
            commandQualifier: 0x00,
            title: "81ル1"}},
  {command: "d0348103011300820281838508820430a03832cb3286099111223344556677f88b140100099110325476f84008080038003030eb0033",
   func: testSendSMS,
   expect: {name: "send_sms_cmd_43",
            commandQualifier: 0x00,
            title: "82ル2"}}
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

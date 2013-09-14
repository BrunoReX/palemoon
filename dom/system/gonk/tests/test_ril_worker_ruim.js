/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

subscriptLoader.loadSubScript("resource://gre/modules/ril_consts.js", this);

function run_test() {
  run_next_test();
}

/**
 * Helper function.
 */
function newUint8Worker() {
  let worker = newWorker();
  let index = 0; // index for read
  let buf = [];

  worker.Buf.writeUint8 = function (value) {
    buf.push(value);
  };

  worker.Buf.readUint8 = function () {
    return buf[index++];
  };

  worker.Buf.seekIncoming = function (offset) {
    index += offset;
  };

  worker.debug = do_print;

  return worker;
}

/**
 * Verify RUIM Service.
 */
add_test(function test_is_ruim_service_available() {
  let worker = newWorker();
  worker.RIL._isCdma = true;
  worker.RIL.appType = CARD_APPTYPE_RUIM;

  function test_table(cst, geckoService, enabled) {
    worker.RIL.iccInfoPrivate.cst = cst;
    do_check_eq(worker.ICCUtilsHelper.isICCServiceAvailable(geckoService),
                enabled);
  }

  test_table([0x0, 0x0, 0x0, 0x0, 0x03], "SPN", true);
  test_table([0x0, 0x0, 0x0, 0x03, 0x0], "SPN", false);

  run_next_test();
});

/**
 * Verify EF_PATH for RUIM file.
 */
add_test(function test_ruim_file_path_id() {
  let worker = newWorker();
  let RIL = worker.RIL;
  let ICCFileHelper = worker.ICCFileHelper;

  RIL.appType = CARD_APPTYPE_RUIM;
  do_check_eq(ICCFileHelper.getEFPath(ICC_EF_CSIM_CST),
              EF_PATH_MF_SIM + EF_PATH_DF_CDMA);

  run_next_test();
});

/**
 * Verify RuimRecordHelper.readCDMAHome
 */
add_test(function test_read_cdmahome() {
  let worker = newUint8Worker();
  let helper = worker.GsmPDUHelper;
  let buf    = worker.Buf;
  let io     = worker.ICCIOHelper;

  io.loadLinearFixedEF = function fakeLoadLinearFixedEF(options)  {
    let cdmaHome = [0xc1, 0x34, 0xff, 0xff, 0x00];

    // Write data size
    buf.writeUint32(cdmaHome.length * 2);

    // Write cdma home file.
    for (let i = 0; i < cdmaHome.length; i++) {
      helper.writeHexOctet(cdmaHome[i]);
    }

    // Write string delimiter
    buf.writeStringDelimiter(cdmaHome.length * 2);

    // We just have 1 test record.

    options.totalRecords = 1;
    if (options.callback) {
      options.callback(options);
    }
  };

  function testCdmaHome(expectedSystemIds, expectedNetworkIds) {
    worker.RuimRecordHelper.readCDMAHome();
    let cdmaHome = worker.RIL.cdmaHome;
    for (let i = 0; i < expectedSystemIds.length; i++) {
      do_check_eq(cdmaHome.systemId[i], expectedSystemIds[i]);
      do_check_eq(cdmaHome.networkId[i], expectedNetworkIds[i]);
    }
    do_check_eq(cdmaHome.systemId.length, expectedSystemIds.length);
    do_check_eq(cdmaHome.networkId.length, expectedNetworkIds.length);
  }

  testCdmaHome([13505], [65535]);

  run_next_test();
});

/**
 * Verify reading CDMA EF_SPN
 */
add_test(function test_read_cdmaspn() {
  let worker = newUint8Worker();
  let helper = worker.GsmPDUHelper;
  let buf    = worker.Buf;
  let io     = worker.ICCIOHelper;

  function testReadSpn(file, expectedSpn, expectedDisplayCondition) {
    io.loadTransparentEF = function fakeLoadTransparentEF(options)  {
      // Write data size
      buf.writeUint32(file.length * 2);

      // Write file.
      for (let i = 0; i < file.length; i++) {
        helper.writeHexOctet(file[i]);
      }

      // Write string delimiter
      buf.writeStringDelimiter(file.length * 2);

      if (options.callback) {
        options.callback(options);
      }
    };

    worker.RuimRecordHelper.readSPN();
    do_check_eq(worker.RIL.iccInfo.spn, expectedSpn);
    do_check_eq(worker.RIL.iccInfoPrivate.spnDisplayCondition,
                expectedDisplayCondition);
  }

  testReadSpn([0x01, 0x04, 0x06, 0x4e, 0x9e, 0x59, 0x2a, 0x96,
               0xfb, 0x4f, 0xe1, 0xff, 0xff, 0xff, 0xff, 0xff,
               0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
               0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
               0xff, 0xff, 0xff],
              String.fromCharCode(0x4e9e) +
              String.fromCharCode(0x592a) +
              String.fromCharCode(0x96fb) +
              String.fromCharCode(0x4fe1),
              0x1);

  // Test when there's no tailing 0xff in spn string.
  testReadSpn([0x01, 0x04, 0x06, 0x4e, 0x9e, 0x59, 0x2a, 0x96,
               0xfb, 0x4f, 0xe1],
              String.fromCharCode(0x4e9e) +
              String.fromCharCode(0x592a) +
              String.fromCharCode(0x96fb) +
              String.fromCharCode(0x4fe1),
              0x1);

  run_next_test();
});

/**
 * Verify display condition for CDMA.
 */
add_test(function test_cdma_spn_display_condition() {
  let worker = newWorker({
    postRILMessage: function fakePostRILMessage(data) {
      // Do nothing
    },
    postMessage: function fakePostMessage(message) {
      // Do nothing
    }
  });
  let RIL = worker.RIL;
  let ICCUtilsHelper = worker.ICCUtilsHelper;

  // Set cdma.
  RIL._isCdma = true;

  // Test updateDisplayCondition runs before any of SIM file is ready.
  do_check_eq(ICCUtilsHelper.updateDisplayCondition(), true);
  do_check_eq(RIL.iccInfo.isDisplayNetworkNameRequired, true);
  do_check_eq(RIL.iccInfo.isDisplaySpnRequired, false);

  // Test with value.
  function testDisplayCondition(ruimDisplayCondition,
                                homeSystemIds, homeNetworkIds,
                                currentSystemId, currentNetworkId,
                                expectUpdateDisplayCondition,
                                expectIsDisplaySPNRequired) {
    RIL.iccInfoPrivate.spnDisplayCondition = ruimDisplayCondition;
    RIL.cdmaHome = {
      systemId: homeSystemIds,
      networkId: homeNetworkIds
    };
    RIL.cdmaSubscription = {
      systemId: currentSystemId,
      networkId: currentNetworkId
    };

    do_check_eq(ICCUtilsHelper.updateDisplayCondition(), expectUpdateDisplayCondition);
    do_check_eq(RIL.iccInfo.isDisplayNetworkNameRequired, false);
    do_check_eq(RIL.iccInfo.isDisplaySpnRequired, expectIsDisplaySPNRequired);
  };

  // SPN is not required when ruimDisplayCondition is false.
  testDisplayCondition(0x0, [123], [345], 123, 345, true, false);

  // System id and network id are all match.
  testDisplayCondition(0x1, [123], [345], 123, 345, true, true);

  // Network is 65535, we should only need to match system id.
  testDisplayCondition(0x1, [123], [65535], 123, 345, false, true);

  // Not match.
  testDisplayCondition(0x1, [123], [456], 123, 345, true, false);

  run_next_test();
});

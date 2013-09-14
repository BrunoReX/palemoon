/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CSFLog.h"

#include <string>

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

#include "nspr.h"
#include "nss.h"

#include "FakeMediaStreams.h"
#include "FakeMediaStreamsImpl.h"
#include "PeerConnectionImpl.h"
#include "PeerConnectionCtx.h"

#include "mtransport_test_utils.h"
MtransportTestUtils *test_utils;
nsCOMPtr<nsIThread> gThread;

extern "C" {
#include "sdp.h"
#include "sdp_private.h"
}

namespace test {

static bool SetupGlobalThread() {
  if (!gThread) {
    nsIThread *thread;

    nsresult rv = NS_NewNamedThread("pseudo-main",&thread);
    if (NS_FAILED(rv))
      return false;

    gThread = thread;
    sipcc::PeerConnectionCtx::InitializeGlobal(gThread);
  }
  return true;
}

class SdpTest : public ::testing::Test {
  public:
    SdpTest() : sdp_ptr_(nullptr) {
      sdp_media_e supported_media[] = {
        SDP_MEDIA_AUDIO,
        SDP_MEDIA_VIDEO,
        SDP_MEDIA_APPLICATION,
        SDP_MEDIA_DATA,
        SDP_MEDIA_CONTROL,
        SDP_MEDIA_NAS_RADIUS,
        SDP_MEDIA_NAS_TACACS,
        SDP_MEDIA_NAS_DIAMETER,
        SDP_MEDIA_NAS_L2TP,
        SDP_MEDIA_NAS_LOGIN,
        SDP_MEDIA_NAS_NONE,
        SDP_MEDIA_IMAGE,
      };

      config_p_ = sdp_init_config();
      unsigned int i;
      for (i = 0; i < sizeof(supported_media) / sizeof(sdp_media_e); i++) {
        sdp_media_supported(config_p_, supported_media[i], true);
      }
      sdp_nettype_supported(config_p_, SDP_NT_INTERNET, true);
      sdp_addrtype_supported(config_p_, SDP_AT_IP4, true);
      sdp_addrtype_supported(config_p_, SDP_AT_IP6, true);
      sdp_transport_supported(config_p_, SDP_TRANSPORT_RTPSAVPF, true);
      sdp_transport_supported(config_p_, SDP_TRANSPORT_UDPTL, true);
      sdp_require_session_name(config_p_, false);
    }

    static void SetUpTestCase() {
      ASSERT_TRUE(SetupGlobalThread());
    }

    void SetUp() {
      final_level_ = 0;
      sdp_ptr_ = nullptr;
    }

    static void TearDownTestCase() {
      gThread = nullptr;
    }

    void ResetSdp() {
      if (!sdp_ptr_) {
        sdp_free_description(sdp_ptr_);
      }
      sdp_ptr_ = sdp_init_description("BogusPeerConnectionId", config_p_);
    }

    void ParseSdp(const std::string &sdp_str) {
      char *bufp = const_cast<char *>(sdp_str.data());
      ResetSdp();
      ASSERT_EQ(sdp_parse(sdp_ptr_, &bufp, sdp_str.size()), SDP_SUCCESS);
    }

    void InitLocalSdp() {
      ResetSdp();
      ASSERT_EQ(sdp_set_version(sdp_ptr_, 0), SDP_SUCCESS);
      ASSERT_EQ(sdp_set_owner_username(sdp_ptr_, "-"), SDP_SUCCESS);
      ASSERT_EQ(sdp_set_owner_sessionid(sdp_ptr_, "132954853"), SDP_SUCCESS);
      ASSERT_EQ(sdp_set_owner_version(sdp_ptr_, "0"), SDP_SUCCESS);
      ASSERT_EQ(sdp_set_owner_network_type(sdp_ptr_, SDP_NT_INTERNET),
                SDP_SUCCESS);
      ASSERT_EQ(sdp_set_owner_address_type(sdp_ptr_, SDP_AT_IP4), SDP_SUCCESS);
      ASSERT_EQ(sdp_set_owner_address(sdp_ptr_, "198.51.100.7"), SDP_SUCCESS);
      ASSERT_EQ(sdp_set_session_name(sdp_ptr_, "SDP Unit Test"), SDP_SUCCESS);
      ASSERT_EQ(sdp_set_time_start(sdp_ptr_, "0"), SDP_SUCCESS);
      ASSERT_EQ(sdp_set_time_stop(sdp_ptr_, "0"), SDP_SUCCESS);
    }

    std::string SerializeSdp() {
      flex_string fs;
      flex_string_init(&fs);
      EXPECT_EQ(sdp_build(sdp_ptr_, &fs), SDP_SUCCESS);
      std::string body(fs.buffer);
      flex_string_free(&fs);
      return body;
    }

    // Returns "level" for new media section
    int AddNewMedia(sdp_media_e type) {
      final_level_++;
      EXPECT_EQ(sdp_insert_media_line(sdp_ptr_, final_level_), SDP_SUCCESS);
      EXPECT_EQ(sdp_set_conn_nettype(sdp_ptr_, final_level_, SDP_NT_INTERNET),
                SDP_SUCCESS);
      EXPECT_EQ(sdp_set_conn_addrtype(sdp_ptr_, final_level_, SDP_AT_IP4),
                SDP_SUCCESS);
      EXPECT_EQ(sdp_set_conn_address(sdp_ptr_, final_level_, "198.51.100.7"),
                SDP_SUCCESS);
      EXPECT_EQ(sdp_set_media_type(sdp_ptr_, final_level_, SDP_MEDIA_VIDEO),
                SDP_SUCCESS);
      EXPECT_EQ(sdp_set_media_transport(sdp_ptr_, final_level_,
                                        SDP_TRANSPORT_RTPAVP),
                SDP_SUCCESS);
      EXPECT_EQ(sdp_set_media_portnum(sdp_ptr_, final_level_, 12345, 0),
                SDP_SUCCESS);
      EXPECT_EQ(sdp_add_media_payload_type(sdp_ptr_, final_level_, 120,
                                           SDP_PAYLOAD_NUMERIC),
                SDP_SUCCESS);
      return final_level_;
    }

    u16 AddNewRtcpFbAck(int level, sdp_rtcp_fb_ack_type_e type,
                         u16 payload = SDP_ALL_PAYLOADS) {
      u16 inst_num = 0;
      EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_RTCP_FB,
                                 &inst_num), SDP_SUCCESS);
      EXPECT_EQ(sdp_attr_set_rtcp_fb_ack(sdp_ptr_, level, payload, inst_num,
                                         type), SDP_SUCCESS);
      return inst_num;
    }

    u16 AddNewRtcpFbNack(int level, sdp_rtcp_fb_nack_type_e type,
                         u16 payload = SDP_ALL_PAYLOADS) {
      u16 inst_num = 0;
      EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_RTCP_FB,
                                 &inst_num), SDP_SUCCESS);
      EXPECT_EQ(sdp_attr_set_rtcp_fb_nack(sdp_ptr_, level, payload, inst_num,
                                          type), SDP_SUCCESS);
      return inst_num;
    }

    u16 AddNewRtcpTrrInt(int level, u32 interval,
                         u16 payload = SDP_ALL_PAYLOADS) {
      u16 inst_num = 0;
      EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_RTCP_FB,
                                 &inst_num), SDP_SUCCESS);
      EXPECT_EQ(sdp_attr_set_rtcp_fb_trr_int(sdp_ptr_, level, payload, inst_num,
                                             interval), SDP_SUCCESS);
      return inst_num;
    }

    u16 AddNewRtcpFbCcm(int level, sdp_rtcp_fb_ccm_type_e type,
                         u16 payload = SDP_ALL_PAYLOADS) {
      u16 inst_num = 0;
      EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_RTCP_FB,
                                 &inst_num), SDP_SUCCESS);
      EXPECT_EQ(sdp_attr_set_rtcp_fb_ccm(sdp_ptr_, level, payload, inst_num,
                                         type), SDP_SUCCESS);
      return inst_num;
    }

  protected:
    int final_level_;
    void *config_p_;
    sdp_t *sdp_ptr_;
};

static const std::string kVideoSdp =
  "v=0\r\n"
  "o=- 137331303 2 IN IP4 127.0.0.1\r\n"
  "s=SIP Call\r\n"
  "t=0 0\r\n"
  "m=video 56436 RTP/SAVPF 120\r\n"
  "c=IN IP4 198.51.100.7\r\n"
  "a=rtpmap:120 VP8/90000\r\n";

TEST_F(SdpTest, parseRtcpFbAckRpsi) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ack rpsi\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_ACK_RPSI);
}

TEST_F(SdpTest, parseRtcpFbAckApp) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ack app\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1), SDP_RTCP_FB_ACK_APP);
}

TEST_F(SdpTest, parseRtcpFbAckAppFoo) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ack app foo\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1), SDP_RTCP_FB_ACK_APP);
}

TEST_F(SdpTest, parseRtcpFbAckFooBar) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ack foo bar\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_ACK_UNKNOWN);
}

TEST_F(SdpTest, parseRtcpFbAckFooBarBaz) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ack foo bar baz\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_ACK_UNKNOWN);
}

TEST_F(SdpTest, parseRtcpFbNack) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_UNSPECIFIED);
}

TEST_F(SdpTest, parseRtcpFbNackPli) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack pli\r\n");
}

TEST_F(SdpTest, parseRtcpFbNackSli) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack sli\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_SLI);
}

TEST_F(SdpTest, parseRtcpFbNackRpsi) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack rpsi\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_RPSI);
}

TEST_F(SdpTest, parseRtcpFbNackApp) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack app\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_APP);
}

TEST_F(SdpTest, parseRtcpFbNackAppFoo) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack app foo\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_APP);
}

TEST_F(SdpTest, parseRtcpFbNackAppFooBar) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack app foo bar\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_APP);
}

TEST_F(SdpTest, parseRtcpFbNackFooBarBaz) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack foo bar baz\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_UNKNOWN);
}

TEST_F(SdpTest, parseRtcpFbTrrInt0) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 trr-int 0\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_trr_int(sdp_ptr_, 1, 120, 1), 0U);
}

TEST_F(SdpTest, parseRtcpFbTrrInt123) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 trr-int 123\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_trr_int(sdp_ptr_, 1, 120, 1), 123U);
}

TEST_F(SdpTest, parseRtcpFbCcmFir) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm fir\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1), SDP_RTCP_FB_CCM_FIR);
}

TEST_F(SdpTest, parseRtcpFbCcmTmmbr) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm tmmbr\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_TMMBR);
}

TEST_F(SdpTest, parseRtcpFbCcmTmmbrSmaxpr) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm tmmbr smaxpr=456\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_TMMBR);
}

TEST_F(SdpTest, parseRtcpFbCcmTstr) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm tstr\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_TSTR);
}

TEST_F(SdpTest, parseRtcpFbCcmVbcm) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm vbcm 123 456 789\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
                                     SDP_RTCP_FB_CCM_VBCM);
  // We don't currently parse out VBCM submessage types, since we don't have
  // any use for them.
}

TEST_F(SdpTest, parseRtcpFbCcmFoo) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm foo\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_UNKNOWN);
}

TEST_F(SdpTest, parseRtcpFbCcmFooBarBaz) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm foo bar baz\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_UNKNOWN);
}

TEST_F(SdpTest, parseRtcpFbFoo) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 foo\r\n");
}

TEST_F(SdpTest, parseRtcpFbFooBar) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 foo bar\r\n");
}

TEST_F(SdpTest, parseRtcpFbFooBarBaz) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 foo bar baz\r\n");
}


TEST_F(SdpTest, parseRtcpFbKitchenSink) {
  ParseSdp(kVideoSdp +
    "a=rtcp-fb:120 ack rpsi\r\n"
    "a=rtcp-fb:120 ack app\r\n"
    "a=rtcp-fb:120 ack app foo\r\n"
    "a=rtcp-fb:120 ack foo bar\r\n"
    "a=rtcp-fb:120 ack foo bar baz\r\n"
    "a=rtcp-fb:120 nack\r\n"
    "a=rtcp-fb:120 nack pli\r\n"
    "a=rtcp-fb:120 nack sli\r\n"
    "a=rtcp-fb:120 nack rpsi\r\n"
    "a=rtcp-fb:120 nack app\r\n"
    "a=rtcp-fb:120 nack app foo\r\n"
    "a=rtcp-fb:120 nack app foo bar\r\n"
    "a=rtcp-fb:120 nack foo bar baz\r\n"
    "a=rtcp-fb:120 trr-int 0\r\n"
    "a=rtcp-fb:120 trr-int 123\r\n"
    "a=rtcp-fb:120 ccm fir\r\n"
    "a=rtcp-fb:120 ccm tmmbr\r\n"
    "a=rtcp-fb:120 ccm tmmbr smaxpr=456\r\n"
    "a=rtcp-fb:120 ccm tstr\r\n"
    "a=rtcp-fb:120 ccm vbcm 123 456 789\r\n"
    "a=rtcp-fb:120 ccm foo\r\n"
    "a=rtcp-fb:120 ccm foo bar baz\r\n"
    "a=rtcp-fb:120 foo\r\n"
    "a=rtcp-fb:120 foo bar\r\n"
    "a=rtcp-fb:120 foo bar baz\r\n");

  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1), SDP_RTCP_FB_ACK_RPSI);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 2), SDP_RTCP_FB_ACK_APP);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 3), SDP_RTCP_FB_ACK_APP);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 4),
            SDP_RTCP_FB_ACK_UNKNOWN);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 5),
            SDP_RTCP_FB_ACK_UNKNOWN);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 6),
            SDP_RTCP_FB_ACK_NOT_FOUND);

  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_UNSPECIFIED);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 2),
            SDP_RTCP_FB_NACK_PLI);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 3),
            SDP_RTCP_FB_NACK_SLI);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 4),
            SDP_RTCP_FB_NACK_RPSI);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 5),
            SDP_RTCP_FB_NACK_APP);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 6),
            SDP_RTCP_FB_NACK_APP);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 7),
            SDP_RTCP_FB_NACK_APP);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 8),
            SDP_RTCP_FB_NACK_UNKNOWN);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 9),
            SDP_RTCP_FB_NACK_NOT_FOUND);

  ASSERT_EQ(sdp_attr_get_rtcp_fb_trr_int(sdp_ptr_, 1, 120, 1), 0U);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_trr_int(sdp_ptr_, 1, 120, 2), 123U);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_trr_int(sdp_ptr_, 1, 120, 3), 0xFFFFFFFF);

  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1), SDP_RTCP_FB_CCM_FIR);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 2),
            SDP_RTCP_FB_CCM_TMMBR);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 3),
            SDP_RTCP_FB_CCM_TMMBR);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 4),
            SDP_RTCP_FB_CCM_TSTR);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 5),
            SDP_RTCP_FB_CCM_VBCM);
  // We don't currently parse out VBCM submessage types, since we don't have
  // any use for them.
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 6),
            SDP_RTCP_FB_CCM_UNKNOWN);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 7),
            SDP_RTCP_FB_CCM_UNKNOWN);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 8),
            SDP_RTCP_FB_CCM_NOT_FOUND);
}


/* TODO (abr@mozilla.com) These attribute adding test cases definitely need
   beefing up; for now, I'm testing the two use cases that we know
   we need right now.  An exhaustive check of the various permutations
   will look similar to the parsing tests, above */

TEST_F(SdpTest, addRtcpFbNack) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_UNSPECIFIED, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 nack\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_UNSPECIFIED);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* nack\r\n"), std::string::npos);
}


TEST_F(SdpTest, addRtcpFbCcmFir) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbCcm(level, SDP_RTCP_FB_CCM_FIR, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 ccm fir\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbCcmFirAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbCcm(level, SDP_RTCP_FB_CCM_FIR);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* ccm fir\r\n"), std::string::npos);
}

/* TODO We need to test the pt=* use cases. */

} // End namespace test.

int main(int argc, char **argv) {
  test_utils = new MtransportTestUtils();
  NSS_NoDB_Init(NULL);
  NSS_SetDomesticPolicy();

  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();

  sipcc::PeerConnectionCtx::Destroy();
  delete test_utils;

  return result;
}

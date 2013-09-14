/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _PEER_CONNECTION_IMPL_H_
#define _PEER_CONNECTION_IMPL_H_

#include <string>
#include <vector>
#include <map>
#include <cmath>

#include "prlock.h"
#include "mozilla/RefPtr.h"
#include "nsWeakPtr.h"
#include "nsIWeakReferenceUtils.h" // for the definition of nsWeakPtr
#include "IPeerConnection.h"
#include "nsComponentManagerUtils.h"
#include "nsPIDOMWindow.h"

#include "dtlsidentity.h"

#include "peer_connection_types.h"
#include "CallControlManager.h"
#include "CC_Device.h"
#include "CC_Call.h"
#include "CC_Observer.h"
#include "MediaPipeline.h"
#include "PeerConnectionMedia.h"

#ifdef MOZILLA_INTERNAL_API
#include "mozilla/net/DataChannel.h"
#include "Layers.h"
#include "VideoUtils.h"
#include "ImageLayers.h"
#include "VideoSegment.h"
#include "nsNSSShutDown.h"
#else
namespace mozilla {
  class DataChannel;
}
#endif

using namespace mozilla;

namespace sipcc {

class PeerConnectionWrapper;

struct ConstraintInfo
{
  std::string  value;
  bool         mandatory;
};
typedef std::map<std::string, ConstraintInfo> constraints_map;

class MediaConstraints
{
public:
  void setBooleanConstraint(const std::string& constraint, bool enabled, bool mandatory);

  void buildArray(cc_media_constraints_t** constraintarray);

private:
  constraints_map  mConstraints;
};

class IceConfiguration
{
public:
  bool addStunServer(const std::string& addr, uint16_t port)
  {
    NrIceStunServer* server(NrIceStunServer::Create(addr, port));
    if (!server) {
      return false;
    }
    addStunServer(*server);
    return true;
  }
  bool addTurnServer(const std::string& addr, uint16_t port,
                     const std::string& username,
                     const std::string& pwd)
  {
    // TODO(ekr@rtfm.com): Need support for SASLprep for
    // username and password. Bug # ???
    std::vector<unsigned char> password(pwd.begin(), pwd.end());

    NrIceTurnServer* server(NrIceTurnServer::Create(addr, port, username, password));
    if (!server) {
      return false;
    }
    addTurnServer(*server);
    return true;
  }
  void addStunServer(const NrIceStunServer& server) { mStunServers.push_back (server); }
  void addTurnServer(const NrIceTurnServer& server) { mTurnServers.push_back (server); }
  const std::vector<NrIceStunServer>& getStunServers() const { return mStunServers; }
  const std::vector<NrIceTurnServer>& getTurnServers() const { return mTurnServers; }
private:
  std::vector<NrIceStunServer> mStunServers;
  std::vector<NrIceTurnServer> mTurnServers;
};

class PeerConnectionWrapper;

// Enter an API call and check that the state is OK,
// the PC isn't closed, etc.
#define PC_AUTO_ENTER_API_CALL(assert_ice_ready) \
    do { \
      /* do/while prevents res from conflicting with locals */    \
      nsresult res = CheckApiState(assert_ice_ready);             \
      if (NS_FAILED(res)) return res; \
    } while(0)
#define PC_AUTO_ENTER_API_CALL_NO_CHECK() CheckThread()


class PeerConnectionImpl MOZ_FINAL : public IPeerConnection,
#ifdef MOZILLA_INTERNAL_API
                                     public mozilla::DataChannelConnection::DataConnectionListener,
                                     public nsNSSShutDownObject,
#endif
                                     public sigslot::has_slots<>
{
public:
  PeerConnectionImpl();
  ~PeerConnectionImpl();

  enum ReadyState {
    kNew,
    kNegotiating,
    kActive,
    kClosing,
    kClosed
  };

  /* Must match constants in IPeerConnection.idl */
  /* Must also be int the same order as in fsmdef_states.h */
  enum SignalingState {
    kSignalingInvalid            = 0,
    kSignalingStable             = 1,
    kSignalingHaveLocalOffer     = 2,
    kSignalingHaveRemoteOffer    = 3,
    kSignalingHaveLocalPranswer  = 4,
    kSignalingHaveRemotePranswer = 5,
    kSignalingClosed             = 6
  };

  enum SipccState {
    kIdle,
    kStarting,
    kStarted
  };

  // TODO(ekr@rtfm.com): make this conform to the specifications
  enum IceState {
    kIceGathering,
    kIceWaiting,
    kIceChecking,
    kIceConnected,
    kIceFailed
  };

  enum Role {
    kRoleUnknown,
    kRoleOfferer,
    kRoleAnswerer
  };

  enum Error {
    kNoError                          = 0,
    kInvalidConstraintsType           = 1,
    kInvalidCandidateType             = 2,
    kInvalidMediastreamTrack          = 3,
    kInvalidState                     = 4,
    kInvalidSessionDescription        = 5,
    kIncompatibleSessionDescription   = 6,
    kIncompatibleConstraints          = 7,
    kIncompatibleMediaStreamTrack     = 8,
    kInternalError                    = 9
  };

  NS_DECL_ISUPPORTS
  NS_DECL_IPEERCONNECTION

  static PeerConnectionImpl* CreatePeerConnection();
  static nsresult ConvertRTCConfiguration(const JS::Value& aSrc,
    IceConfiguration *aDst, JSContext* aCx);
  static nsresult ConvertConstraints(
    const JS::Value& aConstraints, MediaConstraints* aObj, JSContext* aCx);
  static already_AddRefed<DOMMediaStream> MakeMediaStream(nsPIDOMWindow* aWindow,
                                                          uint32_t aHint);

  Role GetRole() const {
    PC_AUTO_ENTER_API_CALL_NO_CHECK();
    return mRole;
  }

  nsresult CreateRemoteSourceStreamInfo(nsRefPtr<RemoteSourceStreamInfo>* aInfo);

  // Implementation of the only observer we need
  virtual void onCallEvent(
    ccapi_call_event_e aCallEvent,
    CSF::CC_CallInfoPtr aInfo
  );

  // DataConnection observers
  void NotifyConnection();
  void NotifyClosedConnection();
  void NotifyDataChannel(already_AddRefed<mozilla::DataChannel> aChannel);

  // Get the media object
  const nsRefPtr<PeerConnectionMedia>& media() const {
    PC_AUTO_ENTER_API_CALL_NO_CHECK();
    return mMedia;
  }

  // Handle system to allow weak references to be passed through C code
  virtual const std::string& GetHandle();

  // ICE events
  void IceGatheringCompleted(NrIceCtx *aCtx);
  void IceCompleted(NrIceCtx *aCtx);
  void IceFailed(NrIceCtx *aCtx);
  void IceStreamReady(NrIceMediaStream *aStream);

  static void ListenThread(void *aData);
  static void ConnectThread(void *aData);

  // Get the main thread
  nsCOMPtr<nsIThread> GetMainThread() {
    PC_AUTO_ENTER_API_CALL_NO_CHECK();
    return mThread;
  }

  // Get the STS thread
  nsCOMPtr<nsIEventTarget> GetSTSThread() {
    PC_AUTO_ENTER_API_CALL_NO_CHECK();
    return mSTSThread;
  }

  // Get the DTLS identity
  mozilla::RefPtr<DtlsIdentity> const GetIdentity() {
    PC_AUTO_ENTER_API_CALL_NO_CHECK();
    return mIdentity;
  }

  // Create a fake media stream
  nsresult CreateFakeMediaStream(uint32_t hint, nsIDOMMediaStream** retval);

  nsPIDOMWindow* GetWindow() const {
    PC_AUTO_ENTER_API_CALL_NO_CHECK();
    return mWindow;
  }

  // Initialize PeerConnection from an IceConfiguration object.
  nsresult Initialize(IPeerConnectionObserver* aObserver,
                      nsIDOMWindow* aWindow,
                      const IceConfiguration& aConfiguration,
                      nsIThread* aThread) {
    return Initialize(aObserver, aWindow, &aConfiguration, nullptr, aThread, nullptr);
  }

  // Validate constraints and construct a MediaConstraints object
  // from a JS::Value.
  NS_IMETHODIMP CreateOffer(MediaConstraints& aConstraints);
  NS_IMETHODIMP CreateAnswer(MediaConstraints& aConstraints);

  nsresult InitializeDataChannel(int track_id, uint16_t aLocalport,
                                 uint16_t aRemoteport, uint16_t aNumstreams);

  // Called whenever something is unrecognized by the parser
  // May be called more than once and does not necessarily mean
  // that parsing was stopped, only that something was unrecognized.
  void OnSdpParseError(const char* errorMessage);

  // Called when OnLocal/RemoteDescriptionSuccess/Error
  // is called to start the list over.
  void ClearSdpParseErrorMessages();

  // Called to retreive the list of parsing errors.
  const std::vector<std::string> &GetSdpParseErrors();

  // Sets the RTC Signaling State
  void SetSignalingState_m(SignalingState aSignalingState);

private:
  PeerConnectionImpl(const PeerConnectionImpl&rhs);
  PeerConnectionImpl& operator=(PeerConnectionImpl);
  nsresult Initialize(IPeerConnectionObserver* aObserver,
                      nsIDOMWindow* aWindow,
                      const IceConfiguration* aConfiguration,
                      const JS::Value* aRTCConfiguration,
                      nsIThread* aThread,
                      JSContext* aCx);
  NS_IMETHODIMP CreateOfferInt(MediaConstraints& constraints);
  NS_IMETHODIMP CreateAnswerInt(MediaConstraints& constraints);
  NS_IMETHODIMP EnsureDataConnection(uint16_t aNumstreams);

  nsresult CloseInt();
  void ChangeReadyState(ReadyState aReadyState);
  nsresult CheckApiState(bool assert_ice_ready) const;
  void CheckThread() const {
    NS_ABORT_IF_FALSE(CheckThreadInt(), "Wrong thread");
  }
  bool CheckThreadInt() const {
#ifdef MOZILLA_INTERNAL_API
    // Thread assertions are disabled in the C++ unit tests because those
    // make API calls off the main thread.
    // TODO(ekr@rtfm.com): Fix the unit tests so they don't do that.
    bool on;
    NS_ENSURE_SUCCESS(mThread->IsOnCurrentThread(&on), false);
    NS_ENSURE_TRUE(on, false);
#endif
    return true;
  }

#ifdef MOZILLA_INTERNAL_API
  void virtualDestroyNSSReference() MOZ_FINAL;
#endif

  // Shut down media - called on main thread only
  void ShutdownMedia();

  // ICE callbacks run on the right thread.
  nsresult IceStateChange_m(IceState aState);

  // The role we are adopting
  Role mRole;

  // The call
  CSF::CC_CallPtr mCall;
  ReadyState mReadyState;
  SignalingState mSignalingState;

  // ICE State
  IceState mIceState;

  nsCOMPtr<nsIThread> mThread;
  // Weak pointer to IPeerConnectionObserver
  // This is only safe to use on the main thread
  nsWeakPtr mPCObserver;
  nsCOMPtr<nsPIDOMWindow> mWindow;

  // The SDP sent in from JS - here for debugging.
  std::string mLocalRequestedSDP;
  std::string mRemoteRequestedSDP;
  // The SDP we are using.
  std::string mLocalSDP;
  std::string mRemoteSDP;

  // DTLS fingerprint
  std::string mFingerprint;
  std::string mRemoteFingerprint;

  // The DTLS identity
  mozilla::RefPtr<DtlsIdentity> mIdentity;

  // A handle to refer to this PC with
  std::string mHandle;

  // The target to run stuff on
  nsCOMPtr<nsIEventTarget> mSTSThread;

#ifdef MOZILLA_INTERNAL_API
  // DataConnection that's used to get all the DataChannels
	nsRefPtr<mozilla::DataChannelConnection> mDataConnection;
#endif

  nsRefPtr<PeerConnectionMedia> mMedia;

  // Temporary: used to prevent multiple audio streams or multiple video streams
  // in a single PC. This is tied up in the IETF discussion around proper
  // representation of multiple streams in SDP, and strongly related to
  // Bug 840728.
  int mNumAudioStreams;
  int mNumVideoStreams;

  bool mHaveDataStream;

  // Holder for error messages from parsing SDP
  std::vector<std::string> mSDPParseErrorMessages;

public:
  //these are temporary until the DataChannel Listen/Connect API is removed
  unsigned short listenPort;
  unsigned short connectPort;
  char *connectStr; // XXX ownership/free
};

// This is what is returned when you acquire on a handle
class PeerConnectionWrapper
{
 public:
  PeerConnectionWrapper(const std::string& handle);

  PeerConnectionImpl *impl() { return impl_; }

 private:
  nsRefPtr<PeerConnectionImpl> impl_;
};

}  // end sipcc namespace

#endif  // _PEER_CONNECTION_IMPL_H_

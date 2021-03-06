/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef tls_agent_h_
#define tls_agent_h_

#include "prio.h"
#include "ssl.h"

#include <iostream>

#include "test_io.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"

namespace nss_test {

#define LOG(msg) std::cerr << name_ << ": " << msg << std::endl

enum SessionResumptionMode {
  RESUME_NONE = 0,
  RESUME_SESSIONID = 1,
  RESUME_TICKET = 2,
  RESUME_BOTH = RESUME_SESSIONID | RESUME_TICKET
};

class TlsAgent : public PollTarget {
 public:
  enum Role { CLIENT, SERVER };
  enum State { INIT, CONNECTING, CONNECTED, ERROR };

  TlsAgent(const std::string& name, Role role, Mode mode, SSLKEAType kea);
  virtual ~TlsAgent();

  bool Init() {
    pr_fd_ = DummyPrSocket::CreateFD(name_, mode_);
    if (!pr_fd_) return false;

    adapter_ = DummyPrSocket::GetAdapter(pr_fd_);
    if (!adapter_) return false;

    return true;
  }

  void SetPeer(TlsAgent* peer) { adapter_->SetPeer(peer->adapter_); }

  void SetPacketFilter(PacketFilter* filter) {
    adapter_->SetPacketFilter(filter);
  }


  void StartConnect();
  void CheckKEAType(SSLKEAType type) const;
  void CheckAuthType(SSLAuthType type) const;

  void Handshake();
  // Marks the internal state as CONNECTING in anticipation of renegotiation.
  void PrepareForRenegotiate();
  // Prepares for renegotiation, then actually triggers it.
  void StartRenegotiate();
  void EnableSomeEcdheCiphers();
  void DisableDheCiphers();
  bool EnsureTlsSetup();

  void ConfigureSessionCache(SessionResumptionMode mode);
  void SetSessionTicketsEnabled(bool en);
  void SetSessionCacheEnabled(bool en);
  void SetVersionRange(uint16_t minver, uint16_t maxver);
  void CheckPreliminaryInfo();
  void SetExpectedVersion(uint16_t version);
  void EnableFalseStart();
  void ExpectResumption();
  void EnableAlpn(const uint8_t* val, size_t len);
  void CheckAlpn(SSLNextProtoState expected_state,
                 const std::string& expected) const;
  void EnableSrtp();
  void CheckSrtp() const;
  void CheckErrorCode(int32_t expected) const;

  State state() const { return state_; }

  const char* state_str() const { return state_str(state()); }

  const char* state_str(State state) const { return states[state]; }

  PRFileDesc* ssl_fd() { return ssl_fd_; }

  uint16_t min_version() const { return vrange_.min; }
  uint16_t max_version() const { return vrange_.max; }
  uint16_t version() const {
    EXPECT_EQ(CONNECTED, state_);
    return info_.protocolVersion;
  }

  bool cipher_suite(int16_t* cipher_suite) const {
    if (state_ != CONNECTED) return false;

    *cipher_suite = info_.cipherSuite;
    return true;
  }

  std::string cipher_suite_name() const {
    if (state_ != CONNECTED) return "UNKNOWN";

    return csinfo_.cipherSuiteName;
  }

  std::vector<uint8_t> session_id() const {
    return std::vector<uint8_t>(info_.sessionID,
                                info_.sessionID + info_.sessionIDLength);
  }

 private:
  const static char* states[];

  void SetState(State state) {
    if (state_ == state) return;

    LOG("Changing state from " << state_str(state_) << " to "
                               << state_str(state));
    state_ = state;
  }

  // Dummy auth certificate hook.
  static SECStatus AuthCertificateHook(void* arg, PRFileDesc* fd,
                                       PRBool checksig, PRBool isServer) {
    TlsAgent* agent = reinterpret_cast<TlsAgent*>(arg);
    agent->CheckPreliminaryInfo();
    agent->auth_certificate_hook_called_ = true;
    return SECSuccess;
  }

  static void ReadableCallback(PollTarget* self, Event event) {
    TlsAgent* agent = static_cast<TlsAgent*>(self);
    agent->ReadableCallback_int();
  }

  void ReadableCallback_int() {
    LOG("Readable");
    Handshake();
  }

  static PRInt32 SniHook(PRFileDesc *fd, const SECItem *srvNameArr,
                         PRUint32 srvNameArrSize,
                         void *arg) {
    TlsAgent* agent = reinterpret_cast<TlsAgent*>(arg);
    agent->CheckPreliminaryInfo();
    agent->sni_hook_called_ = true;
    return SSL_SNI_CURRENT_CONFIG_IS_USED;
  }

  static SECStatus CanFalseStartCallback(PRFileDesc *fd, void *arg,
                                         PRBool *canFalseStart) {
    TlsAgent* agent = reinterpret_cast<TlsAgent*>(arg);
    agent->CheckPreliminaryInfo();
    EXPECT_TRUE(agent->falsestart_enabled_);
    agent->can_falsestart_hook_called_ = true;
    *canFalseStart = true;
    return SECSuccess;
  }

  static void HandshakeCallback(PRFileDesc *fd, void *arg) {
    TlsAgent* agent = reinterpret_cast<TlsAgent*>(arg);
    agent->CheckPreliminaryInfo();
    agent->handshake_callback_called_ = true;
  }

  void CheckCallbacks() const;
  void Connected();

  const std::string name_;
  Mode mode_;
  SSLKEAType kea_;
  PRFileDesc* pr_fd_;
  DummyPrSocket* adapter_;
  PRFileDesc* ssl_fd_;
  Role role_;
  State state_;
  bool falsestart_enabled_;
  uint16_t expected_version_;
  uint16_t expected_cipher_suite_;
  bool expect_resumption_;
  bool can_falsestart_hook_called_;
  bool sni_hook_called_;
  bool auth_certificate_hook_called_;
  bool handshake_callback_called_;
  SSLChannelInfo info_;
  SSLCipherSuiteInfo csinfo_;
  SSLVersionRange vrange_;
  int32_t error_code_;
};

}  // namespace nss_test

#endif

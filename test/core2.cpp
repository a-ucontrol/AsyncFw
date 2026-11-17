// Core2 test suite: TlsContext & AbstractTlsSocket (RSA, ECDSA, Ed25519, Ed448).
//
// Groups:
//   A. TlsContext PKI — key generation, infoKey(), PEM roundtrip, self-signed
//      certificates, CSR generation & CA signing for RSA, ECDSA (prime256v1,
//      secp384r1) and EdDSA (ED25519, ED448).
//   B. TLS Handshake  — end-to-end AbstractTlsSocket + ListenSocket handshakes
//      and bidirectional encrypted I/O for ECDSA, Ed25519, mixed CA/leaf chains,
//      and negative verification failure cases.

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <AsyncFw/AbstractTlsSocket>
#include <AsyncFw/DataArray>
#include <AsyncFw/FunctionConnector>
#include <AsyncFw/ListenSocket>
#include <AsyncFw/LogStream>
#include <AsyncFw/Thread>
#include <AsyncFw/Timer>
#include <AsyncFw/TlsContext>

using namespace AsyncFw;

namespace {

void log_output(const LogStream::Message &message, uint8_t flags) {
  if ((message.type & 0x07) > 5) return;
  LogStream::console_output(message, flags);
}

std::atomic<int> g_failed {0};
std::atomic<int> g_total {0};

#define EXPECT(cond)                                                                        \
  do {                                                                                      \
    ++g_total;                                                                              \
    if (!(cond)) {                                                                          \
      ++g_failed;                                                                           \
      lsError() << LogStream::Color::Red << "FAIL" << __FILE__ << ':' << __LINE__ << #cond; \
    }                                                                                       \
  } while (0)

#define EXPECT_MSG(cond, msg)                                                                                 \
  do {                                                                                                        \
    ++g_total;                                                                                                \
    if (!(cond)) {                                                                                            \
      ++g_failed;                                                                                             \
      lsError() << LogStream::Color::Red << "FAIL" << __FILE__ << ':' << __LINE__ << #cond << " — " << (msg); \
    }                                                                                                         \
  } while (0)

// ============================================================================
// Helper socket for in-thread TLS handshake & echo tests
// ============================================================================

class TlsPeerSocket : public AbstractTlsSocket {
public:
  TlsPeerSocket() = default;
  ~TlsPeerSocket() override = default;

  FunctionConnector<>::Protected<TlsPeerSocket> activated;
  FunctionConnector<const DataArray &>::Protected<TlsPeerSocket> received;
  FunctionConnector<>::Protected<TlsPeerSocket> disconnected;

  bool reachedActive = false;
  bool reachedUnconnected = false;
  Error lastError = None;
  std::string lastErrorString;
  DataArray rxBuffer;

protected:
  void stateEvent() override {
    if (state_ == Active) {
      reachedActive = true;
      activated();
    } else if (state_ == Unconnected) {
      reachedUnconnected = true;
      lastError = error();
      lastErrorString = errorString();
      disconnected();
    }
  }

  void readEvent() override {
    DataArray chunk = read();
    if (!chunk.empty()) {
      rxBuffer += chunk;
      received(chunk);
    }
  }
};

struct PkiBundle {
  TlsContext ca;
  TlsContext server;
  TlsContext client;
};

template <typename CaKeyGen, typename SrvKeyGen>
bool buildPkiBundle(PkiBundle &out, CaKeyGen &&genCaKey, SrvKeyGen &&genSrvKey, const std::string &cn = "localhost", const std::string &san = "DNS:localhost,IP:127.0.0.1") {
  if (!genCaKey(out.ca)) return false;
  if (!out.ca.generateCertificate({{"CN", "AsyncFw-Test-CA"}})) return false;
  if (!out.ca.verifyCertificate()) return false;

  if (!genSrvKey(out.server)) return false;
  DataArray csr = out.server.generateRequest({{"CN", cn}}, san);
  if (csr.empty()) return false;

  DataArray signedCert = out.ca.signRequest(csr);
  if (signedCert.empty()) return false;
  if (!out.server.setCertificate(signedCert)) return false;
  if (!out.server.verifyCertificate()) return false;
  out.server.setVerifyPeer(false);

  if (!out.client.appendTrusted(out.ca.certificate())) return false;
  out.client.setVerifyPeer(true);
  out.client.setVerifyName(cn);
  return true;
}

// ============================================================================
// Group A. TlsContext key generation, infoKey(), CSR & certificate signing
// ============================================================================

void test_tls_context_key_types() {
  lsNotice() << "=== Group A: TlsContext key types (RSA, ECDSA, Ed25519, Ed448) ===";

  // 1. RSA baseline
  {
    TlsContext ctx;
    EXPECT(ctx.empty());
    EXPECT(ctx.generateKey(2048));
    EXPECT(!ctx.empty());
    EXPECT(!ctx.key().empty());

    std::string kinfo = ctx.infoKey();
    EXPECT_MSG(!kinfo.empty(), "RSA infoKey() must not be empty");
    EXPECT_MSG(kinfo.find("2048 bit") != std::string::npos, "RSA infoKey() missing bit length: " + kinfo);
    EXPECT_MSG(TlsContext::infoKey(ctx.key()) == kinfo, "static infoKey(DataArray) mismatch for RSA");

    EXPECT(ctx.generateCertificate({{"CN", "RSA-Test"}}));
    EXPECT(ctx.verifyCertificate());
    EXPECT(ctx.commonName() == "RSA-Test");
    EXPECT(!ctx.infoCertificate().empty());
  }

  // 2. ECDSA curves (prime256v1 / P-256, secp384r1 / P-384)
  for (const std::string &curve : {"prime256v1", "secp384r1"}) {
    TlsContext ca;
    EXPECT_MSG(ca.generateKey(curve), "generateKey failed for " + curve);
    EXPECT(!ca.key().empty());

    std::string kinfo = ca.infoKey();
    EXPECT_MSG(!kinfo.empty(), "ECDSA infoKey() empty for " + curve);
    EXPECT_MSG(kinfo.find(curve) != std::string::npos, "ECDSA infoKey() missing curve name '" + curve + "': " + kinfo);
    EXPECT_MSG(TlsContext::infoKey(ca.key()) == kinfo, "static infoKey(DataArray) mismatch for " + curve);

    EXPECT(ca.generateCertificate({{"CN", "CA-" + curve}}));
    EXPECT(ca.verifyCertificate());

    // Leaf CSR + CA signRequest + PEM reload roundtrip
    TlsContext leaf;
    EXPECT(leaf.generateKey(curve));
    DataArray csr = leaf.generateRequest({{"CN", "leaf-" + curve}}, "DNS:localhost,IP:127.0.0.1");
    EXPECT(!csr.empty());
    EXPECT(!TlsContext::infoRequest(csr).empty());

    DataArray cert = ca.signRequest(csr);
    EXPECT(!cert.empty());
    EXPECT(leaf.setCertificate(cert));
    EXPECT(leaf.verifyCertificate());
    EXPECT(leaf.commonName() == "leaf-" + curve);

    TlsContext reloaded;
    EXPECT(reloaded.setKey(leaf.key()));
    EXPECT(reloaded.setCertificate(leaf.certificate()));
    EXPECT(reloaded.verifyCertificate());
    EXPECT(reloaded.infoKey() == leaf.infoKey());
  }

  // 3. EdDSA (ED25519, ED448) — exercises signDigest() returning nullptr
  for (const std::string &alg : {"ED25519", "ED448"}) {
    TlsContext ca;
    EXPECT_MSG(ca.generateKey(alg), "generateKey failed for " + alg);
    EXPECT(!ca.key().empty());

    std::string kinfo = ca.infoKey();
    EXPECT_MSG(!kinfo.empty(), "EdDSA infoKey() empty for " + alg);
    EXPECT_MSG(kinfo.find(alg) != std::string::npos, "EdDSA infoKey() missing algorithm '" + alg + "': " + kinfo);
    EXPECT_MSG(TlsContext::infoKey(ca.key()) == kinfo, "static infoKey(DataArray) mismatch for " + alg);

    EXPECT_MSG(ca.generateCertificate({{"CN", "CA-" + alg}}), "self-signed cert failed for " + alg);
    EXPECT(ca.verifyCertificate());

    TlsContext leaf;
    EXPECT(leaf.generateKey(alg));
    DataArray csr = leaf.generateRequest({{"CN", "leaf-" + alg}}, "DNS:localhost,IP:127.0.0.1");
    EXPECT_MSG(!csr.empty(), "generateRequest failed for " + alg);
    EXPECT(!TlsContext::infoRequest(csr).empty());

    DataArray cert = ca.signRequest(csr);
    EXPECT_MSG(!cert.empty(), "signRequest failed for " + alg);
    EXPECT(leaf.setCertificate(cert));
    EXPECT(leaf.verifyCertificate());
    EXPECT(leaf.commonName() == "leaf-" + alg);

    TlsContext reloaded;
    EXPECT(reloaded.setKey(leaf.key()));
    EXPECT(reloaded.setCertificate(leaf.certificate()));
    EXPECT(reloaded.verifyCertificate());
    EXPECT(reloaded.infoKey() == leaf.infoKey());
  }

  // 4. Negative: unknown curve/algorithm name
  {
    TlsContext bad;
    EXPECT(!bad.generateKey("not-a-valid-curve-name"));
    EXPECT(bad.empty());
  }
}

// ============================================================================
// Group B. End-to-end TLS Handshakes & Encrypted I/O
// ============================================================================

void runHandshakeRoundtrip(const std::string &label, PkiBundle &pki) {
  lsNotice() << "=== TLS Handshake roundtrip:" << label << "===";

  ListenSocket listener;
  std::unique_ptr<TlsPeerSocket> serverSocket;
  std::unique_ptr<TlsPeerSocket> clientSocket = std::make_unique<TlsPeerSocket>();

  AbstractThread::Waiter waiter;
  bool timedOut = false;
  bool roundtripOk = false;

  const DataArray pingPayload("ping-" + label);
  const DataArray pongPayload("pong-" + label);

  FunctionConnectionGuardList guards;

  guards += listener.incoming.connect([&](int fd, const std::string &, bool *accept) {
    serverSocket = std::make_unique<TlsPeerSocket>();
    serverSocket->setContext(pki.server);
    guards += serverSocket->received.connect([&](const DataArray &) {
      if (serverSocket->rxBuffer == pingPayload) { serverSocket->write(pongPayload); }
    });
    serverSocket->setDescriptor(fd);
    *accept = true;
  });

  EXPECT_MSG(listener.listen("127.0.0.1", 0), label + ": listener.listen failed");
  uint16_t port = listener.port();
  EXPECT(port > 0);
  clientSocket->setContext(pki.client);
  guards += clientSocket->activated.connect([&] { clientSocket->write(pingPayload); });
  guards += clientSocket->received.connect([&](const DataArray &) {
    if (clientSocket->rxBuffer == pongPayload) {
      roundtripOk = true;
      clientSocket->disconnect();
    }
  });
  guards += clientSocket->disconnected.connect([&] {
    if (waiter.waiting()) waiter.complete();
  });
  int timeoutId = AbstractThread::current()->appendTimerTask(3000, [&] {
    timedOut = true;
    if (waiter.waiting()) waiter.complete();
  });
  EXPECT_MSG(clientSocket->connect("127.0.0.1", port), label + ": client.connect failed");
  waiter.wait();
  AbstractThread::current()->removeTimer(timeoutId);
  guards.clear();
  if (serverSocket) serverSocket->close();
  clientSocket->close();
  listener.close();
  EXPECT_MSG(!timedOut, label + ": handshake/roundtrip timed out");
  EXPECT_MSG(clientSocket->reachedActive, label + ": client never reached Active (error: " + clientSocket->lastErrorString + ")");
  EXPECT_MSG(serverSocket && serverSocket->reachedActive, label + ": server never reached Active");
  EXPECT_MSG(roundtripOk, label + ": decrypted echo payload mismatch");
}
void test_tls_handshake_ecdsa_and_eddsa() {
  // 1. Pure ECDSA P-256 chain
  {
    PkiBundle pki;
    EXPECT(buildPkiBundle(pki, [](TlsContext &c) { return c.generateKey("prime256v1"); }, [](TlsContext &c) { return c.generateKey("prime256v1"); }));
    runHandshakeRoundtrip("ECDSA-prime256v1", pki);
  }
  // 2. Pure ECDSA P-384 chain
  {
    PkiBundle pki;
    EXPECT(buildPkiBundle(pki, [](TlsContext &c) { return c.generateKey("secp384r1"); }, [](TlsContext &c) { return c.generateKey("secp384r1"); }));
    runHandshakeRoundtrip("ECDSA-secp384r1", pki);
  }
  // 3. Pure Ed25519 chain
  {
    PkiBundle pki;
    EXPECT(buildPkiBundle(pki, [](TlsContext &c) { return c.generateKey("ED25519"); }, [](TlsContext &c) { return c.generateKey("ED25519"); }));
    runHandshakeRoundtrip("Ed25519", pki);
  }
  // 4. Pure Ed448 chain
  {
    PkiBundle pki;
    EXPECT(buildPkiBundle(pki, [](TlsContext &c) { return c.generateKey("ED448"); }, [](TlsContext &c) { return c.generateKey("ED448"); }));
    runHandshakeRoundtrip("Ed448", pki);
  }
  // 5. Mixed PKI: Ed25519 Root CA signing ECDSA P-256 Server certificate
  {
    PkiBundle pki;
    EXPECT(buildPkiBundle(pki, [](TlsContext &c) { return c.generateKey("ED25519"); }, [](TlsContext &c) { return c.generateKey("prime256v1"); }));
    runHandshakeRoundtrip("Mixed-Ed25519CA-ECDSAServer", pki);
  }
}
void test_tls_handshake_verification_failure() {
  lsNotice() << "=== TLS Handshake negative: hostname & CA mismatch (Ed25519) ===";
  PkiBundle pki;
  EXPECT(buildPkiBundle(pki, [](TlsContext &c) { return c.generateKey("ED25519"); }, [](TlsContext &c) { return c.generateKey("ED25519"); }, "localhost", "DNS:localhost"));
  // Expect a different hostname so peer verification must reject the handshake
  pki.client.setVerifyName("wrong.example.org");
  ListenSocket listener;
  std::unique_ptr<TlsPeerSocket> serverSocket;
  std::unique_ptr<TlsPeerSocket> clientSocket = std::make_unique<TlsPeerSocket>();
  AbstractThread::Waiter waiter;
  bool timedOut = false;
  FunctionConnectionGuardList guards;
  guards += listener.incoming.connect([&](int fd, const std::string &, bool *accept) {
    serverSocket = std::make_unique<TlsPeerSocket>();
    serverSocket->setContext(pki.server);
    serverSocket->setDescriptor(fd);
    *accept = true;
  });
  EXPECT(listener.listen("127.0.0.1", 0));
  uint16_t port = listener.port();
  clientSocket->setContext(pki.client);
  guards += clientSocket->disconnected.connect([&] {
    if (waiter.waiting()) waiter.complete();
  });
  int timeoutId = AbstractThread::current()->appendTimerTask(3000, [&] {
    timedOut = true;
    if (waiter.waiting()) waiter.complete();
  });
  EXPECT(clientSocket->connect("127.0.0.1", port));
  waiter.wait();
  AbstractThread::current()->removeTimer(timeoutId);
  guards.clear();
  if (serverSocket) serverSocket->close();
  clientSocket->close();
  listener.close();
  EXPECT_MSG(!timedOut, "negative handshake timed out");
  EXPECT_MSG(!clientSocket->reachedActive, "client must NOT reach Active on verifyName mismatch");
  EXPECT_MSG(clientSocket->reachedUnconnected, "client must settle to Unconnected");
  EXPECT_MSG(clientSocket->lastError == AbstractSocket::Activate, "expected Activate error on TLS verification failure");
}
}  // namespace
// ============================================================================
// main
// ============================================================================
int main(int, char **) {
  LogStream::setCompleted(&log_output);
  Thread tst_thread("Core2-test");
  tst_thread.invoke([&] {
    test_tls_context_key_types();
    test_tls_handshake_ecdsa_and_eddsa();
    test_tls_handshake_verification_failure();
    tst_thread.quit();
  });
  tst_thread.start();
  tst_thread.waitFinished();
  if (g_failed.load()) {
    lsError() << LogStream::Color::Red << "core2: " << g_failed.load() << '/' << g_total.load() << " checks failed";
    return 1;
  }
  lsNotice() << "core2: " << g_total.load() << " checks passed";
  return 0;
}

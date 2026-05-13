/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/TlsContext>
#include <AsyncFw/LogStream>

#define KEY_BITS 2048

int main(int argc, char *argv[]) {
  AsyncFw::TlsContext _ctx;

  AsyncFw::TlsContext _root_ca;
  _root_ca.generateKey(KEY_BITS);
  _root_ca.generateCertificate();

  AsyncFw::TlsContext _ca;
  _ca.generateKey(KEY_BITS);
  AsyncFw::DataArray req = _ca.generateRequest({{"C", "RU"}, {"CN", "Test-CA"}}, "", "CA:TRUE,pathlen:0");
  _ca.setCertificate(_root_ca.signRequest(req));

  AsyncFw::TlsContext _cert1;
  _cert1.generateKey(KEY_BITS);
  req = _cert1.generateRequest({{"C", "RU"}, {"ST", "RU-region"}, {"CN", "common_name.org.ru"}, {"L", "RU-l"}, {"O", "Org"}, {"OU", "Org-unit"}, {"serialNumber", "device-000-00-00"}, {"emailAddress", "ssl@org.email.ru"}, {"dnQualifier", "device"}}, "IP:192.168.100.101,IP:192.168.100.102,IP:192.168.100.103");
  _cert1.setCertificate(_ca.signRequest(req));

  _cert1.appendTrusted(_ca.certificate());
  _cert1.setVerifyName("VerifyName");
  lsInfoCyan() << _cert1;

  AsyncFw::TlsContext _cert2;
  _cert2.generateKey(KEY_BITS);
  req = _cert1.generateRequest({{"C", "RU"}, {"ST", "RU-region"}, {"CN", "common_name.org.ru"}, {"L", "RU-l"}, {"O", "Org"}, {"OU", "Org-unit"}, {"serialNumber", "device-000-00-00"}, {"emailAddress", "ssl@org.email.ru"}, {"dnQualifier", "device"}}, "IP:192.168.100.101,IP:192.168.100.102,IP:192.168.100.103");
  _cert2.setCertificate(_ca.signRequest(req));

  _cert2.appendTrusted(_ca.certificate());
  _cert2.setVerifyName("VerifyName");
  lsInfoMagenta() << _cert2;

  return 0;
}
//! [snippet]

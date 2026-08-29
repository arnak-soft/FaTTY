#include "tests/test.hpp"
#include "core/placeholders.hpp"
#include "core/store.hpp"

#include <map>
#include <string>

using namespace fatty;

namespace fatty::test {

void test_placeholders() {
  expect(percent_encode("abc") == "abc", "plain encode");
  expect(percent_encode("p@ss:w rd") == "p%40ss%3Aw%20rd", "special encode");
  expect(percent_encode("ю") == "%D1%8E", "utf8 encode");

  std::map<std::string, std::string> vars{{"host", "10.0.0.1"}, {"user", "root"}, {"port", "22"}};
  expect(expand_placeholders("{user}@{host}:{port}", vars) == "root@10.0.0.1:22", "expand known");
  expect(expand_placeholders("keep {unknown}", vars) == "keep {unknown}", "unknown kept");
  expect(expand_placeholders("pre{user}post", vars) == "prerootpost", "adjacent");
  expect(expand_placeholders("no braces", vars) == "no braces", "plain text");
  expect(expand_placeholders("{", vars) == "{", "dangling brace");

  expect(make_sftp_url("root", "p@ss", "example.com", 22) == "sftp://root:p%40ss@example.com:22/",
         "sftp url password");
  expect(make_sftp_url("u", "", "1.2.3.4", 2222) == "sftp://u@1.2.3.4:2222/", "sftp url no password");
  expect(make_sftp_url("u", "x", "2001:db8::1", 22) == "sftp://u:x@[2001:db8::1]:22/", "sftp ipv6");

  Server s;
  s.name = "alpha";
  s.host = "10.0.0.1";
  s.port = 2222;
  s.username = "deploy";
  s.password = "secret";
  s.key_path = "C:\\keys\\id_ed25519";
  auto ph = program_placeholders(s, "C:\\cache\\key.ppk");
  expect(ph["host"] == "10.0.0.1", "ph host");
  expect(ph["port"] == "2222", "ph port");
  expect(ph["user"] == "deploy", "ph user");
  expect(ph["username"] == "deploy", "ph username");
  expect(ph["password"] == "secret", "ph password");
  expect(ph["name"] == "alpha", "ph name");
  expect(ph["key"] == "C:\\keys\\id_ed25519", "ph key");
  expect(ph["ppk"] == "C:\\cache\\key.ppk", "ph ppk");
  expect(ph["ssh_target"] == "deploy@10.0.0.1", "ph ssh_target");
  expect(ph["sftp_url"] == "sftp://deploy:secret@10.0.0.1:2222/", "ph sftp_url");
}

}  // namespace fatty::test

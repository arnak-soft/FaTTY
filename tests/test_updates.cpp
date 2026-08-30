#include "tests/test.hpp"
#include "net/updates.hpp"

#include <algorithm>
#include <string>

using namespace fatty;

namespace fatty::test {

void test_updates() {
  const char* dotted = R"([
    {"name":"FaTTY.1.6.6.OneFile.exe","label":"FaTTY 1.6.6 OneFile.exe",
     "browser_download_url":"https://github.com/arnak-soft/FaTTY/releases/download/v1.6.6/FaTTY.1.6.6.OneFile.exe"},
    {"name":"FaTTY.1.6.6.Portable.zip","label":"FaTTY 1.6.6 Portable.zip",
     "browser_download_url":"https://github.com/arnak-soft/FaTTY/releases/download/v1.6.6/FaTTY.1.6.6.Portable.zip"},
    {"name":"FaTTY.1.6.6.Setup.exe","label":"FaTTY 1.6.6 Setup.exe",
     "browser_download_url":"https://github.com/arnak-soft/FaTTY/releases/download/v1.6.6/FaTTY.1.6.6.Setup.exe"}
  ])";
  auto setup = pick_github_setup_url(dotted);
  expect(setup && *setup ==
                     "https://github.com/arnak-soft/FaTTY/releases/download/v1.6.6/FaTTY.1.6.6.Setup.exe",
         "dotted setup.exe wins over OneFile");

  const char* spaced = R"([
    {"name":"FaTTY 1.6.0 Setup.exe","browser_download_url":"https://example/FaTTY%201.6.0%20Setup.exe"},
    {"name":"FaTTY 1.6.0 OneFile.exe","browser_download_url":"https://example/onefile.exe"}
  ])";
  setup = pick_github_setup_url(spaced);
  expect(setup && setup->find("Setup.exe") != std::string::npos, "spaces in name still match setup");

  const char* label_only = R"([
    {"name":"FaTTY.1.6.6.Setup.exe","label":"FaTTY 1.6.6 Setup.exe",
     "browser_download_url":"https://github.com/x/y/releases/download/v1.6.6/FaTTY.1.6.6.Setup.exe"}
  ])";
  setup = pick_github_setup_url(label_only);
  expect(setup && setup->find("Setup.exe") != std::string::npos, "label with Setup.exe");

  const char* release_obj = R"({"tag_name":"v1.6.6","html_url":"https://github.com/arnak-soft/FaTTY/releases/tag/v1.6.6",
    "assets":[{"name":"FaTTY.1.6.6.Setup.exe","browser_download_url":"https://github.com/arnak-soft/FaTTY/releases/download/v1.6.6/FaTTY.1.6.6.Setup.exe"}]})";
  setup = pick_github_setup_url(release_obj);
  expect(setup && setup->find("/releases/download/") != std::string::npos, "full release object");

  expect(!pick_github_setup_url("not json"), "bad json");
  expect(!pick_github_setup_url("[]"), "empty assets");

  auto urls = installer_download_urls("v1.6.6", std::nullopt);
  expect(!urls.empty(), "constructed urls");
  expect(std::find(urls.begin(), urls.end(),
                     "https://github.com/arnak-soft/FaTTY/releases/download/v1.6.6/FaTTY.1.6.6.Setup.exe") !=
             urls.end(),
         "dotted constructed url");

  auto page = std::string("https://github.com/arnak-soft/FaTTY/releases/tag/v1.6.6");
  urls = installer_download_urls("1.6.6", page);
  expect(std::find(urls.begin(), urls.end(), page) == urls.end(), "release page is not a download url");

  auto asset = std::string(
      "https://github.com/arnak-soft/FaTTY/releases/download/v1.6.6/FaTTY.1.6.6.Setup.exe");
  urls = installer_download_urls("1.6.6", asset);
  expect(!urls.empty() && urls[0] == asset, "preferred asset first");
}

}  // namespace fatty::test

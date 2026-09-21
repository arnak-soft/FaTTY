#include "tests/test.hpp"
#include "app/whats_new.hpp"

using namespace fatty;

namespace fatty::test {

void test_whats_new() {
  expect(release_version_id("") == "", "empty version id");
  expect(release_version_id("v1.2.3") == "1.2.3", "strip v prefix");
  expect(release_version_id("1.2.3-5-gabc1234") == "1.2.3", "ignore git suffix");
  expect(release_version_id("1.2.3-dirty") == "1.2.3", "ignore dirty");

  expect(!should_show_whats_new("1.2.3", "1.2.3", "• note"), "same release");
  expect(!should_show_whats_new("v1.2.3", "1.2.3-4-gdef", "• note"), "same x.y.z");
  expect(should_show_whats_new("1.2.3", "1.2.4", "• note"), "newer patch");
  expect(should_show_whats_new("", "1.0.0", "• note"), "first launch");
  expect(!should_show_whats_new("", "1.0.0", "  "), "empty notes");
  expect(!should_show_whats_new("1.0.0", "1.0.1", ""), "no notes on upgrade");
}

}  // namespace fatty::test

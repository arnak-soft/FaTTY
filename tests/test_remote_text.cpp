#include "tests/test.hpp"
#include "core/remote_text.hpp"

using namespace fatty;

namespace fatty::test {

void test_remote_text() {
  {
    const auto d = decode_remote_text("hello\nworld\n");
    expect(d.ok, "lf ok");
    expect(d.view.eol == TextEolKind::Lf, "lf kind");
    expect(d.view.display_utf8 == "hello\nworld\n", "lf display");
    expect(encode_remote_text(d.view.display_utf8, TextEolKind::Lf) == "hello\nworld\n", "lf encode");
  }
  {
    const auto d = decode_remote_text("hello\r\nworld\r\n");
    expect(d.ok, "crlf ok");
    expect(d.view.eol == TextEolKind::Crlf, "crlf kind");
    expect(d.view.display_utf8 == "hello\nworld\n", "crlf display normalized");
    expect(encode_remote_text(d.view.display_utf8, TextEolKind::Crlf) == "hello\r\nworld\r\n", "crlf encode");
  }
  {
    const auto d = decode_remote_text("a\rb\r");
    expect(d.ok, "cr ok");
    expect(d.view.eol == TextEolKind::Cr, "cr kind");
    expect(encode_remote_text(d.view.display_utf8, TextEolKind::Cr) == "a\rb\r", "cr encode");
  }
  {
    const auto d = decode_remote_text("mix\r\nand\n");
    expect(!d.ok, "mixed eol rejected");
  }
  {
    std::string bin = "abc";
    bin.push_back('\0');
    bin += "def";
    const auto d = decode_remote_text(bin);
    expect(!d.ok, "nul rejected");
  }
  {
    // invalid UTF-8: lone continuation
    const char bad[] = {'a', static_cast<char>(0x80), 'b', 0};
    const auto d = decode_remote_text(std::string_view(bad, 3));
    expect(!d.ok, "bad utf8 rejected");
  }
  {
    std::string huge(kRemoteTextMaxBytes + 1, 'x');
    const auto d = decode_remote_text(huge);
    expect(!d.ok, "oversize rejected");
  }
  {
    // wx-like GetValue with CRLF → normalize then encode back to LF file
    const auto norm = normalize_editor_newlines("line1\r\nline2\r\n");
    expect(norm == "line1\nline2\n", "normalize editor");
    expect(encode_remote_text(norm, TextEolKind::Lf) == "line1\nline2\n", "roundtrip lf after wx");
    expect(encode_remote_text(norm, TextEolKind::Crlf) == "line1\r\nline2\r\n", "roundtrip crlf after wx");
  }
  {
    const auto d = decode_remote_text("");
    expect(d.ok, "empty ok");
    expect(d.view.eol == TextEolKind::Lf, "empty default lf");
    expect(encode_remote_text("", TextEolKind::Lf).empty(), "empty encode");
  }
  {
    const std::string hi = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82\n";  // привет\n
    const auto d = decode_remote_text(hi);
    expect(d.ok, "unicode utf8 ok");
    expect(encode_remote_text(d.view.display_utf8, TextEolKind::Lf) == hi, "unicode roundtrip");
  }
}

}  // namespace fatty::test

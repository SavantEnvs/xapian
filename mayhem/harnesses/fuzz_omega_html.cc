// Fuzz omindex's HTML parser (xapian-omega/htmlparser.cc, built on xmlparser.cc), which runs over
// arbitrary documents. The calls mirror omindex's own (index_file.cc, text/html): parse as
// iso-8859-1 and, when the document names another charset in a meta tag, reset and parse again.
#include "htmlparser.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // omindex passes a std::string (index_file.cc: file_to_string()), so the text is NUL-terminated;
    // XmlParser::parse() reads one byte past a final '<' relying on that. Do the same here.
    const std::string text(reinterpret_cast<const char*>(data), size);
    HtmlParser p;
    try {
        p.parse(text, "iso-8859-1", false);
    } catch (const std::string& newcharset) {
        p.reset();
        p.parse(text, newcharset, true);
    }
    return 0;
}

// Fuzz omega's XML parsers over untrusted documents: AtomParser (omindex, application/atom+xml) and
// GenericXmlParser (scriptindex), both on top of XmlParser (xapian-omega/xmlparser.cc).
#include "atomparser.h"
#include "genericxmlparser.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // The callers (index_file.cc, scriptindex.cc) pass a std::string, so the text is NUL-terminated;
    // XmlParser::parse() reads one byte past a final '<' relying on that. Do the same here.
    const std::string text(reinterpret_cast<const char*>(data), size);
    {
        AtomParser p;
        p.parse(text);
    }
    {
        GenericXmlParser p;
        p.parse(text);
    }
    return 0;
}

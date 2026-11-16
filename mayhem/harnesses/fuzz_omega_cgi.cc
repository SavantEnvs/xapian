// Fuzz omega's CGI parameter decoding (xapian-omega/cgiparam.cc): the input is the QUERY_STRING a web
// client sends, decoded by decode_get() as the omega CGI does. decode_get() clears cgi_params first,
// and QUERY_STRING is set on every call, so nothing carries over between inputs.
#include "cgiparam.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // An environment value is a C string, so the query ends at the first NUL, as it would for a real CGI.
    std::string query(reinterpret_cast<const char*>(data), size);
    setenv("QUERY_STRING", query.c_str(), 1);
    decode_get();
    return 0;
}

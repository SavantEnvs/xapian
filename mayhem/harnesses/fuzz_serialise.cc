// Fuzz the public unserialise entry points that read untrusted bytes in a real deployment
// (remote protocol, stored queries, replicated documents): Query, Document and every built-in
// Weight scheme. Byte 0 selects the decoder, the rest is the serialised form.
#include <xapian.h>

#include <cstdint>
#include <memory>
#include <string>

static const char* const weight_names[] = {
    "bool", "tfidf", "bm25", "bm25+", "inl2", "ifb2", "ineb2", "bb2", "dlh", "pl2", "pl2+", "dph", "lmjm", "lmdirichlet", "lmabsdiscount", "lm2stage", "dice", "coord",
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1) return -1;
    unsigned which = data[0] % (2 + sizeof(weight_names) / sizeof(weight_names[0]));
    std::string s(reinterpret_cast<const char*>(data + 1), size - 1);
    try {
        if (which == 0) {
            Xapian::Registry reg;
            Xapian::Query q = Xapian::Query::unserialise(s, reg);
            std::string d = q.get_description();
            std::string again = q.serialise();
            (void)d; (void)again;
            (void)q.get_length();
            for (Xapian::TermIterator t = q.get_terms_begin(); t != q.get_terms_end(); ++t) {}
        } else if (which == 1) {
            Xapian::Document doc = Xapian::Document::unserialise(s);
            (void)doc.get_description();
            (void)doc.get_data();
            for (Xapian::TermIterator t = doc.termlist_begin(); t != doc.termlist_end(); ++t) {
                for (Xapian::PositionIterator p = t.positionlist_begin(); p != t.positionlist_end(); ++p) {}
            }
            for (Xapian::ValueIterator v = doc.values_begin(); v != doc.values_end(); ++v) {}
            (void)doc.serialise();
        } else {
            Xapian::Registry reg;
            const Xapian::Weight* proto = reg.get_weighting_scheme(weight_names[which - 2]);
            if (!proto) return -1;
            std::unique_ptr<Xapian::Weight> w(proto->unserialise(s));
            (void)w->name();
            (void)w->serialise();
        }
    } catch (const Xapian::Error&) {
        return -1;
    }
    return 0;
}

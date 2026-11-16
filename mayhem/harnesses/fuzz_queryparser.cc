// Fuzz Xapian::QueryParser. Input: 3 control bytes (flags, strategy, default operator) then the
// query string. The database is built once in LLVMFuzzerInitialize and only read afterwards.
#include <xapian.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

static Xapian::Database* db;
static Xapian::Stem* stemmer;
static Xapian::SimpleStopper* stopper;
static Xapian::NumberRangeProcessor* number_range;
static Xapian::DateRangeProcessor* date_range;
static std::string db_dir;
static const std::string group("XG2");
static const std::string date_grouping("date:");

static void remove_db_dir() {
    std::error_code ec;
    std::filesystem::remove_all(db_dir, ec);
}

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    const char* tmp = getenv("TMPDIR");
    std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") + "/qp-db-XXXXXX";
    if (!mkdtemp(tmpl.data())) return 1;
    db_dir = tmpl;
    atexit(remove_db_dir);

    Xapian::WritableDatabase w(db_dir + "/db",
                               Xapian::DB_CREATE_OR_OVERWRITE | Xapian::DB_BACKEND_GLASS);
    Xapian::Stem en("en");
    Xapian::TermGenerator tg;
    tg.set_stemmer(en);
    tg.set_database(w);
    tg.set_flags(Xapian::TermGenerator::FLAG_SPELLING);
    static const char* const texts[] = {
        "the quick brown fox jumps over the lazy dog",
        "xapian is an open source probabilistic information retrieval library",
        "searching indexing stemming wildcard phrase proximity boolean filters",
        "database glass honey inmemory remote backend compaction",
    };
    for (const char* t : texts) {
        Xapian::Document doc;
        tg.set_document(doc);
        tg.index_text(t);
        doc.add_boolean_term("Hexample.org");
        doc.add_value(0, Xapian::sortable_serialise(42.5));
        doc.add_value(1, "20240131");
        w.add_document(doc);
    }
    w.add_synonym("fast", "quick");
    w.add_synonym("quick fox", "red fox");
    w.commit();
    db = new Xapian::Database(db_dir + "/db");

    stemmer = new Xapian::Stem("en");
    static const char* const stop[] = {"the", "is", "an", "of"};
    stopper = new Xapian::SimpleStopper(stop, stop + 4);
    number_range = new Xapian::NumberRangeProcessor(0, "$");
    date_range = new Xapian::DateRangeProcessor(1);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 3) return -1;
    unsigned flags = 0;
    static const unsigned bits[] = {
        Xapian::QueryParser::FLAG_BOOLEAN,
        Xapian::QueryParser::FLAG_PHRASE,
        Xapian::QueryParser::FLAG_LOVEHATE,
        Xapian::QueryParser::FLAG_BOOLEAN_ANY_CASE,
        Xapian::QueryParser::FLAG_WILDCARD,
        Xapian::QueryParser::FLAG_PURE_NOT,
        Xapian::QueryParser::FLAG_PARTIAL,
        Xapian::QueryParser::FLAG_SPELLING_CORRECTION,
        Xapian::QueryParser::FLAG_SYNONYM,
        Xapian::QueryParser::FLAG_AUTO_SYNONYMS,
        Xapian::QueryParser::FLAG_AUTO_MULTIWORD_SYNONYMS,
        Xapian::QueryParser::FLAG_NGRAMS,
        Xapian::QueryParser::FLAG_WORD_BREAKS,
        Xapian::QueryParser::FLAG_WILDCARD_MULTI,
        Xapian::QueryParser::FLAG_WILDCARD_SINGLE,
        Xapian::QueryParser::FLAG_FUZZY,
    };
    unsigned sel = data[0] | (data[1] << 8);
    for (unsigned i = 0; i < 16; ++i)
        if (sel & (1u << i)) flags |= bits[i];
    unsigned ctl = data[2];
    std::string query(reinterpret_cast<const char*>(data + 3), size - 3);

    try {
        Xapian::QueryParser qp;
        qp.set_database(*db);
        qp.set_stemmer(*stemmer);
        qp.set_stemming_strategy(Xapian::QueryParser::stem_strategy(ctl % 5));
        if (ctl & 0x08) qp.set_stopper(stopper);
        qp.set_default_op((ctl & 0x10) ? Xapian::Query::OP_AND : Xapian::Query::OP_OR);
        if (ctl & 0x20) {
            qp.add_prefix("title", "S");
            qp.add_prefix("body", "XB");
            qp.add_prefix("", "XD");
            qp.add_boolean_prefix("site", "H");
            qp.add_boolean_prefix("tag", "XT", true);
            qp.add_boolean_prefix("grp", "XG", &group);
        }
        if (ctl & 0x40) {
            qp.add_rangeprocessor(number_range);
            qp.add_rangeprocessor(date_range, &date_grouping);
        }
        Xapian::Query q = qp.parse_query(query, flags);
        (void)q.get_description();
        (void)q.serialise();
        (void)qp.get_corrected_query_string();
        for (Xapian::TermIterator t = qp.unstem_begin("quick"); t != qp.unstem_end("quick"); ++t) {}
        for (Xapian::TermIterator t = qp.stoplist_begin(); t != qp.stoplist_end(); ++t) {}
    } catch (const Xapian::Error&) {
        return -1;
    }
    return 0;
}

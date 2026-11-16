// Fuzz the text-analysis side of Xapian: TermGenerator (tokeniser, stemmers for every language,
// stopwords, n-grams / word-breaks, spelling data), then QueryParser + Enquire over the resulting
// in-memory database and MSet::snippet() on the same text. Input: 4 control bytes then the text;
// a NUL byte in the text separates it from a free-form query string.
#include <xapian.h>

#include <cstdint>
#include <string>
#include <vector>

static std::vector<std::string> languages;

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    std::string all = Xapian::Stem::get_available_languages();
    size_t pos = 0;
    while (pos < all.size()) {
        size_t sp = all.find(' ', pos);
        if (sp == std::string::npos) sp = all.size();
        if (sp > pos) languages.push_back(all.substr(pos, sp - pos));
        pos = sp + 1;
    }
    return languages.empty() ? 1 : 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4) return -1;
    const std::string& lang = languages[data[0] % languages.size()];
    unsigned strategy = data[1] % 5;
    unsigned tgflags = (data[2] & 1 ? Xapian::TermGenerator::FLAG_SPELLING : 0) |
                       (data[2] & 2 ? Xapian::TermGenerator::FLAG_NGRAMS : 0) |
                       (data[2] & 4 ? Xapian::TermGenerator::FLAG_WORD_BREAKS : 0);
    unsigned qpflags = Xapian::QueryParser::FLAG_DEFAULT |
                       (data[3] & 1 ? Xapian::QueryParser::FLAG_PARTIAL : 0) |
                       (data[3] & 2 ? Xapian::QueryParser::FLAG_WILDCARD : 0) |
                       (data[3] & 4 ? Xapian::QueryParser::FLAG_NGRAMS : 0) |
                       (data[3] & 8 ? Xapian::QueryParser::FLAG_WORD_BREAKS : 0);
    unsigned snflags = (data[3] & 16 ? Xapian::MSet::SNIPPET_BACKGROUND_MODEL : 0) |
                       (data[3] & 32 ? Xapian::MSet::SNIPPET_EXHAUSTIVE : 0) |
                       (data[3] & 64 ? Xapian::MSet::SNIPPET_EMPTY_WITHOUT_MATCH : 0) |
                       (data[3] & 128 ? Xapian::MSet::SNIPPET_NGRAMS : 0);
    std::string rest(reinterpret_cast<const char*>(data + 4), size - 4);
    std::string text = rest, qstr;
    size_t nul = rest.find('\0');
    if (nul != std::string::npos) {
        text = rest.substr(0, nul);
        qstr = rest.substr(nul + 1);
    } else {
        qstr = rest.substr(0, 64);
    }

    try {
        Xapian::WritableDatabase db("", Xapian::DB_BACKEND_INMEMORY);
        Xapian::Stem stem(lang);
        Xapian::TermGenerator tg;
        tg.set_database(db);
        tg.set_stemmer(stem);
        tg.set_stemming_strategy(Xapian::TermGenerator::stem_strategy(strategy));
        tg.set_flags(Xapian::TermGenerator::flags(tgflags));
        Xapian::Document doc;
        tg.set_document(doc);
        tg.index_text(text);
        tg.increase_termpos();
        tg.index_text(text, 1, "S");
        db.add_document(doc);

        Xapian::QueryParser qp;
        qp.set_database(db);
        qp.set_stemmer(stem);
        qp.set_stemming_strategy(Xapian::QueryParser::stem_strategy(strategy));
        Xapian::Query q = qp.parse_query(qstr, qpflags);
        if (q.empty()) q = Xapian::Query(Xapian::Query::OP_OR, q, Xapian::Query("a"));
        Xapian::Enquire enq(db);
        enq.set_query(q);
        Xapian::MSet mset = enq.get_mset(0, 10);
        (void)mset.snippet(text, 100, stem, snflags);
        (void)mset.snippet(text, 20, stem, snflags, "", "", "");
    } catch (const Xapian::Error&) {
        return -1;
    }
    return 0;
}

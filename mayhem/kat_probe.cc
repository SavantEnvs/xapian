// Known-answer probe: prints values that follow from Xapian's documented behaviour
// (Snowball stemming, query grammar, BM25 ranking, serialisation round trips).
#include <xapian.h>
#include <iostream>
#include <string>

using std::cout;

int main() {
    Xapian::Stem en("english");
    for (const char* w : {"running", "fishing", "generously", "happiness", "caresses", "ponies"})
        cout << "stem-en " << w << " " << en(w) << "\n";
    Xapian::Stem fr("french");
    cout << "stem-fr continuellement " << fr("continuellement") << "\n";
    Xapian::Stem de("german");
    cout << "stem-de aufeinanderfolgenden " << de("aufeinanderfolgenden") << "\n";

    Xapian::QueryParser qp;
    qp.set_stemmer(en);
    qp.set_stemming_strategy(Xapian::QueryParser::STEM_SOME);
    qp.add_prefix("title", "S");
    qp.add_boolean_prefix("site", "H");
    for (const char* q : {"hello world", "cats AND dogs", "title:fun \"exact phrase\" -bad",
                          "site:example.org apples", "a OR b NOT c"}) {
        Xapian::Query parsed = qp.parse_query(q, Xapian::QueryParser::FLAG_DEFAULT |
                                                 Xapian::QueryParser::FLAG_LOVEHATE);
        cout << "query [" << q << "] " << parsed.get_description() << "\n";
        Xapian::Query again = Xapian::Query::unserialise(parsed.serialise());
        cout << "roundtrip " << (again.get_description() == parsed.get_description() ? "ok" : "FAIL") << "\n";
    }

    Xapian::WritableDatabase db("", Xapian::DB_BACKEND_INMEMORY);
    Xapian::TermGenerator tg;
    tg.set_stemmer(en);
    const char* texts[] = {
        "the quick brown fox jumps over the lazy dog",
        "a quick brown dog outpaces a quick red fox",
        "lazy cats sleep all day",
        "xapian is a probabilistic information retrieval library",
    };
    for (const char* t : texts) {
        Xapian::Document d;
        d.set_data(t);
        tg.set_document(d);
        tg.index_text(t);
        db.add_document(d);
    }
    db.commit();
    cout << "doccount " << db.get_doccount() << " lastdocid " << db.get_lastdocid()
         << " avlength " << db.get_avlength() << "\n";
    cout << "termfreq Zfox " << db.get_termfreq("Zfox") << " Zlazi " << db.get_termfreq("Zlazi")
         << " Zdog " << db.get_termfreq("Zdog") << "\n";

    Xapian::Enquire enq(db);
    enq.set_query(qp.parse_query("quick fox"));
    Xapian::MSet ms = enq.get_mset(0, 10);
    cout << "matches " << ms.get_matches_estimated() << "\n";
    for (Xapian::MSetIterator m = ms.begin(); m != ms.end(); ++m)
        cout << "hit docid=" << *m << " rank=" << m.get_rank() << " pct=" << m.get_percent()
             << " data=" << m.get_document().get_data() << "\n";

    enq.set_query(Xapian::Query(Xapian::Query::OP_AND_NOT, Xapian::Query("Zdog"), Xapian::Query("Zlazi")));
    ms = enq.get_mset(0, 10);
    cout << "andnot";
    for (Xapian::MSetIterator m = ms.begin(); m != ms.end(); ++m) cout << " " << *m;
    cout << "\n";

    cout << "snippet " << ms.snippet("a quick brown dog outpaces a quick red fox", 40, en) << "\n";
    return 0;
}

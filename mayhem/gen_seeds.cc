// One-off seed generator (not part of build.sh): gen_seeds <outdir>
// Writes serialise seeds to <outdir>/serialise and small database directories to <outdir>/dbs/glass.
// gen_seeds.sh then compacts that database (glass and, since honey is write-once, honey) into the single-file seeds for fuzz_db_file.
#include <xapian.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <sys/stat.h>

static void put(const std::string& path, const std::string& bytes) {
    std::ofstream(path, std::ios::binary) << bytes;
}

static int run(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string out = argv[1];
    mkdir(out.c_str(), 0777);
    mkdir((out + "/serialise").c_str(), 0777);
    mkdir((out + "/dbs").c_str(), 0777);

    Xapian::Query q1("hello");
    Xapian::Query q2(Xapian::Query::OP_AND, Xapian::Query("a"), Xapian::Query("b"));
    Xapian::Query q3(Xapian::Query::OP_NEAR, Xapian::Query("x"), Xapian::Query("y"));
    Xapian::Query q4(Xapian::Query::OP_OR,
                     Xapian::Query(Xapian::Query::OP_PHRASE, Xapian::Query("quick"), Xapian::Query("fox")),
                     Xapian::Query(Xapian::Query::OP_SYNONYM, Xapian::Query("dog"), Xapian::Query("hound")));
    Xapian::Query q5(Xapian::Query::OP_VALUE_RANGE, 3, "aaa", "zzz");
    Xapian::Query q6(Xapian::Query::OP_SCALE_WEIGHT, Xapian::Query("scaled"), 2.5);
    std::vector<std::string> el = {"e1", "e2", "e3"};
    Xapian::Query q7(Xapian::Query::OP_ELITE_SET, el.begin(), el.end(), 2);
    Xapian::Query qs[] = {q1, q2, q3, q4, q5, q6, q7};
    int i = 0;
    for (auto& q : qs) put(out + "/serialise/query" + std::to_string(i++), std::string(1, '\0') + q.serialise());

    Xapian::Document d;
    d.set_data("payload data");
    d.add_term("alpha", 2);
    d.add_posting("beta", 3);
    d.add_posting("beta", 9);
    d.add_boolean_term("Qtag");
    d.add_value(0, "v0");
    d.add_value(5, Xapian::sortable_serialise(3.25));
    put(out + "/serialise/doc0", std::string(1, '\x01') + d.serialise());

    static const char* const names[] = {"bool", "tfidf", "bm25", "bm25+", "inl2", "ifb2", "ineb2", "bb2", "dlh", "pl2", "pl2+", "dph", "lmjm", "lmdirichlet", "lmabsdiscount", "lm2stage", "dice", "coord"};
    Xapian::Registry reg;
    for (int k = 0; k < 18; ++k) {
        const Xapian::Weight* proto = reg.get_weighting_scheme(names[k]);
        Xapian::Weight* w = proto->clone();
        put(out + "/serialise/weight_" + names[k], std::string(1, char(2 + k)) + w->serialise());
        delete w;
    }
    Xapian::BM25Weight bm(1.5, 0.5, 0.9, 0.3, 0.7);
    put(out + "/serialise/weight_bm25_params", std::string(1, '\x04') + bm.serialise());

    static const char* const texts[] = {
        "the quick brown fox jumps over the lazy dog",
        "xapian is a probabilistic information retrieval library",
        "a quick brown dog outpaces a quick red fox",
        "searching and indexing text with stemming and positions",
        "lazy cats sleep all day while the dogs run in the park",
    };
    for (const char* be : {"glass"}) {
        std::string dir = out + "/dbs/" + be;
        Xapian::WritableDatabase db(dir, Xapian::DB_CREATE_OR_OVERWRITE | Xapian::DB_BACKEND_GLASS);
        Xapian::TermGenerator tg;
        tg.set_stemmer(Xapian::Stem("english"));
        tg.set_database(db);
        tg.set_flags(Xapian::TermGenerator::FLAG_SPELLING);
        int n = 0;
        for (const char* t : texts) {
            Xapian::Document doc;
            doc.set_data(t);
            doc.add_value(0, std::to_string(n));
            doc.add_value(1, Xapian::sortable_serialise(n * 1.5));
            tg.set_document(doc);
            tg.index_text(t);
            db.add_document(doc);
            ++n;
        }
        db.add_synonym("quick", "fast");
        db.set_metadata("title", "seed");
        db.commit();
    }
    return 0;
}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const Xapian::Error& e) {
        fprintf(stderr, "%s\n", e.get_description().c_str());
        return 1;
    }
}

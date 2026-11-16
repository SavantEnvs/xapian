// One-off seed generator (not part of build.sh): gen_remote_seeds <outdir> <scratchdir> <recorder.so> <progsrv>
// Talks to a real xapian-progsrv (<progsrv>) with mayhem/rec_read.c preloaded (<recorder.so>), which
// records the client-to-server byte stream, and leaves one recorded conversation per scenario in <outdir>.
#include <xapian.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

static const char* const words[][3] = {{"hello", "world", "quick"}, {"quick", "brown", "fox"}, {"hello", "fox", "dog"}};

static void make_db(const std::string& dir) {
    Xapian::WritableDatabase wdb(dir, Xapian::DB_CREATE_OR_OVERWRITE | Xapian::DB_BACKEND_GLASS | Xapian::DB_NO_SYNC);
    for (const auto& w : words) {
        Xapian::Document d;
        d.set_data(std::string(w[0]) + " " + w[1] + " " + w[2]);
        Xapian::termpos pos = 1;
        for (const char* t : w) d.add_posting(t, pos++);
        d.add_value(0, w[0]);
        wdb.add_document(d);
    }
    wdb.set_metadata("key", "value");
    wdb.commit();
}

static std::string out, scratch, recorder, progsrv;

static void record(const std::string& name, bool writable, void (*scenario)(Xapian::Database&, Xapian::WritableDatabase*)) {
    std::string db = scratch + "/db-" + name;
    make_db(db);
    std::string args = "LD_PRELOAD=" + recorder + " REC=" + out + "/" + name + " " + progsrv + (writable ? " --writable " : " ") + db;
    if (writable) {
        Xapian::WritableDatabase w = Xapian::Remote::open_writable("/usr/bin/env", args);
        scenario(w, &w);
    } else {
        Xapian::Database d = Xapian::Remote::open("/usr/bin/env", args);
        scenario(d, nullptr);
    }
}

static void read_all(Xapian::Database& db, Xapian::WritableDatabase*) {
    (void)db.get_doccount(); (void)db.get_avlength(); (void)db.get_lastdocid();
    (void)db.get_termfreq("hello"); (void)db.get_collection_freq("fox"); (void)db.term_exists("dog");
    (void)db.get_doclength(2); (void)db.get_value_freq(0); (void)db.get_metadata("key");
    for (auto t = db.allterms_begin(); t != db.allterms_end(); ++t) {}
    for (auto p = db.postlist_begin("quick"); p != db.postlist_end("quick"); ++p)
        for (auto q = p.positionlist_begin(); q != p.positionlist_end(); ++q) {}
    Xapian::Document d = db.get_document(2);
    (void)d.get_data();
    for (auto t = d.termlist_begin(); t != d.termlist_end(); ++t) {}
    for (auto v = db.valuestream_begin(0); v != db.valuestream_end(0); ++v) {}
}

static void search(Xapian::Database& db, Xapian::WritableDatabase*) {
    Xapian::Enquire e(db);
    e.set_query(Xapian::Query(Xapian::Query::OP_OR, Xapian::Query("hello"),
                              Xapian::Query(Xapian::Query::OP_PHRASE, Xapian::Query("quick"), Xapian::Query("brown"))));
    Xapian::MSet m = e.get_mset(0, 10);
    for (auto i = m.begin(); i != m.end(); ++i) (void)i.get_document().get_data();
    e.set_sort_by_value(0, true);
    (void)e.get_mset(0, 10);
    Xapian::RSet rs;
    rs.add_document(1);
    (void)e.get_eset(5, rs);
}

static void update(Xapian::Database&, Xapian::WritableDatabase* w) {
    Xapian::Document d;
    d.set_data("added");
    d.add_term("added", 2);
    d.add_value(1, "v");
    Xapian::docid id = w->add_document(d);
    d.add_term("more");
    w->replace_document(id, d);
    w->replace_document("Qadded-unique", d);
    w->delete_document(1);
    w->set_metadata("k2", "v2");
    w->add_spelling("hello", 2);
    w->add_synonym("quick", "fast");
    w->commit();
    w->delete_document("Qadded-unique");
    w->commit();
}

// The client always opens with MSG_WRITEACCESS and an empty body; give it the DB_NO_SYNC flag so that
// replays of the update conversation do not fsync on every commit (the server honours client flags).
static void no_sync(const std::string& path) {
    std::string bytes;
    { std::ifstream in(path, std::ios::binary); bytes.assign(std::istreambuf_iterator<char>(in), {}); }
    const std::string head("\x15\x00", 2);
    if (bytes.compare(0, 2, head) != 0) return;
    bytes.replace(0, 2, std::string("\x15\x01\x04", 3));
    std::ofstream(path, std::ios::binary) << bytes;
}

int main(int argc, char** argv) {
    if (argc < 5) return 2;
    out = argv[1]; scratch = argv[2]; recorder = argv[3]; progsrv = argv[4];
    try {
        record("read_all", false, read_all);
        record("search", false, search);
        record("update", true, update);
        no_sync(out + "/update");
    } catch (const Xapian::Error& e) {
        fprintf(stderr, "%s\n", e.get_description().c_str());
        return 1;
    }
    return 0;
}

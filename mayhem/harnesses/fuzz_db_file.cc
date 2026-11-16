// Fuzz Xapian's on-disk database readers with an arbitrary single-file database (glass or honey).
// The input is written to an unlinked scratch file; it is opened through Database(fd) and walked
// (terms, postings, positions, documents, values, spelling, synonyms, metadata, a match), and a
// second copy goes through the integrity checker behind xapian-check.
#include <xapian.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <unistd.h>

static std::string scratch_template() {
    const char* tmp = getenv("TMPDIR");
    return std::string(tmp && *tmp ? tmp : "/tmp") + "/dbfile-XXXXXX";
}

static void walk(Xapian::Database& db) {
    (void)db.get_doccount();
    (void)db.get_lastdocid();
    (void)db.get_avlength();
    (void)db.get_total_length();
    (void)db.get_uuid();
    (void)db.has_positions();
    (void)db.get_value_lower_bound(0);
    (void)db.get_value_upper_bound(0);
    (void)db.get_wdf_upper_bound("a");
    for (Xapian::TermIterator k = db.metadata_keys_begin(); k != db.metadata_keys_end(); ++k)
        (void)db.get_metadata(*k);
    for (Xapian::TermIterator s = db.spellings_begin(); s != db.spellings_end(); ++s)
        (void)s.get_termfreq();
    for (Xapian::TermIterator s = db.synonym_keys_begin(); s != db.synonym_keys_end(); ++s)
        for (Xapian::TermIterator y = db.synonyms_begin(*s); y != db.synonyms_end(*s); ++y) {}

    std::string first_terms[3];
    int n = 0;
    for (Xapian::TermIterator t = db.allterms_begin(); t != db.allterms_end(); ++t) {
        (void)t.get_termfreq();
        if (n < 3) first_terms[n++] = *t;
        for (Xapian::PostingIterator p = db.postlist_begin(*t); p != db.postlist_end(*t); ++p) {
            (void)p.get_doclength();
            (void)p.get_unique_terms();
            for (Xapian::PositionIterator pos = p.positionlist_begin(); pos != p.positionlist_end(); ++pos) {}
        }
    }
    for (Xapian::PostingIterator p = db.postlist_begin(std::string()); p != db.postlist_end(std::string()); ++p) {}

    for (Xapian::valueno slot = 0; slot < 3; ++slot) {
        (void)db.get_value_freq(slot);
        for (Xapian::ValueIterator v = db.valuestream_begin(slot); v != db.valuestream_end(slot); ++v) {}
    }

    Xapian::docid last = db.get_lastdocid();
    unsigned seen = 0;
    for (Xapian::PostingIterator d = db.postlist_begin(std::string()); d != db.postlist_end(std::string()) && seen < 64; ++d, ++seen) {
        Xapian::Document doc = db.get_document(*d);
        (void)doc.get_data();
        for (Xapian::ValueIterator v = doc.values_begin(); v != doc.values_end(); ++v) {}
        for (Xapian::TermIterator t = doc.termlist_begin(); t != doc.termlist_end(); ++t) {
            (void)t.get_wdf();
            for (Xapian::PositionIterator pos = t.positionlist_begin(); pos != t.positionlist_end(); ++pos) {}
        }
    }
    if (last > 0) (void)db.get_doclength(last);

    if (n > 0) {
        Xapian::Query q(first_terms[0]);
        for (int i = 1; i < n; ++i)
            q = Xapian::Query(Xapian::Query::OP_OR, q, Xapian::Query(first_terms[i]));
        Xapian::Enquire enq(db);
        enq.set_query(q);
        Xapian::MSet m = enq.get_mset(0, 10);
        for (Xapian::MSetIterator i = m.begin(); i != m.end(); ++i)
            (void)i.get_document().get_data();
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string path = scratch_template();
    int wfd = mkstemp(path.data());
    if (wfd < 0) return 0;
    size_t off = 0;
    while (off < size) {
        ssize_t r = write(wfd, data + off, size - off);
        if (r <= 0) break;
        off += size_t(r);
    }
    close(wfd);
    int fd1 = open(path.c_str(), O_RDONLY);
    int fd2 = open(path.c_str(), O_RDONLY);
    unlink(path.c_str());
    if (fd1 < 0 || fd2 < 0) {
        if (fd1 >= 0) close(fd1);
        if (fd2 >= 0) close(fd2);
        return 0;
    }

    int rc = 0;
    try {
        Xapian::Database db(fd1);
        try {
            walk(db);
        } catch (const Xapian::Error&) {
        }
    } catch (const Xapian::Error&) {
        rc = -1;
    }
    try {
        (void)Xapian::Database::check(fd2, Xapian::DBCHECK_FULL_TREE);
    } catch (const Xapian::Error&) {
    }
    return rc;
}

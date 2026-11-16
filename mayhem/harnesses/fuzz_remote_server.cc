// Fuzz Xapian's remote-backend server, the code behind xapian-tcpsrv and xapian-progsrv, which parses
// messages from an untrusted client. The input is the whole client-to-server byte stream, delivered
// through an unlinked file, and the server's replies go to /dev/null. Each input gets its own small
// writable glass database (the server upgrades to write access when the client asks), so the write
// messages are reachable and no state carries over between inputs.
#include <xapian.h>

#include "remoteserver.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;

static void make_db(const std::string& dir) {
    Xapian::WritableDatabase wdb(dir, Xapian::DB_CREATE_OR_OVERWRITE | Xapian::DB_BACKEND_GLASS | Xapian::DB_NO_SYNC);
    static const char* const words[][3] = {{"hello", "world", "quick"}, {"quick", "brown", "fox"}, {"hello", "fox", "dog"}};
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

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const char* tmp = getenv("TMPDIR");
    std::string base = std::string(tmp && *tmp ? tmp : "/tmp") + "/remsrv-XXXXXX";
    if (!mkdtemp(base.data())) return 0;

    std::string in_path = base + "/in";
    int fdin = open(in_path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fdin < 0) { fs::remove_all(base); return 0; }
    unlink(in_path.c_str());
    size_t off = 0;
    while (off < size) {
        ssize_t r = write(fdin, data + off, size - off);
        if (r <= 0) break;
        off += size_t(r);
    }
    lseek(fdin, 0, SEEK_SET);
    int fdout = open("/dev/null", O_WRONLY);
    if (fdout < 0) { close(fdin); fs::remove_all(base); return 0; }

    // RemoteServer does not own or close the descriptors (its callers exit or close them), so
    // we close both below on every path, including when make_db() or the constructor throws.
    std::string dbdir = base + "/db";
    try {
        make_db(dbdir);
        RemoteServer server(std::vector<std::string>{dbdir}, fdin, fdout, 60.0, 60.0, true);
        server.run();
    } catch (...) {
        // The real callers (net/tcpserver.cc, bin/xapian-progsrv.cc) catch everything, log, and
        // close the connection, so an exception ending the session is not a crash.
    }
    close(fdin);
    close(fdout);
    std::error_code ec;
    fs::remove_all(base, ec);
    return 0;
}

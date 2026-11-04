#include "TestSignals.h"

#include "engine/util/Sha256.h"

#include <catch2/catch_test_macros.hpp>

using jaudios::util::Sha256;

TEST_CASE ("SHA-256 FIPS 180-4 test vectors", "[util][sha256]")
{
    auto digest = [] (const std::string& s) { Sha256 h; h.update (s.data(), s.size()); return h.hexDigest(); };
    CHECK (digest ("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK (digest ("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK (digest ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
           == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    Sha256 million;
    const std::string a (1000, 'a');
    for (int i = 0; i < 1000; ++i)
        million.update (a.data(), a.size()); // split updates across block boundaries
    CHECK (million.hexDigest() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE ("SHA-256 of a file matches the in-memory digest", "[util][sha256]")
{
    jaudios::test::TempDir tmp;
    const auto f = tmp.dir.getChildFile ("x.bin");
    f.replaceWithText ("abc");
    CHECK (Sha256::ofFile (f.getFullPathName().toStdString()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK (Sha256::ofFile ("/nonexistent/file").empty());
}

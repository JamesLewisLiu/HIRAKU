// SPDX-License-Identifier: GPL-3.0-or-later
// Mods' [[text]] (runtime/host/mods.h, find_text): a string found in the game's memory and the
// patches that replace it, in made-up memory.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "host/mods.h"
#include "shell/core.h"

// the shell asks the core for memory elsewhere; find_text doesn't
hk::Core &hk::core() { abort(); }

using namespace hk::mods;

static int failed = 0;
#define CHECK(ok, ...) do { printf("%s", (ok) ? "ok    " : "FAIL  "); printf(__VA_ARGS__); printf("\n"); failed += !(ok); } while (0)

static std::vector<uint8_t> bytes(const char *s) { return std::vector<uint8_t>(s, s + strlen(s)); }

static TextPatch text(const char *find, const char *replace, bool every = false) {
    TextPatch t;
    t.mod = "test", t.line = "[[text]] 1", t.label = find;
    t.find = bytes(find), t.replace = bytes(replace), t.every = every;
    return t;
}

int main() {
    // strings as a program keeps them: each ends with a zero, some with zeros after it
    std::vector<uint8_t> mem(64, 0);
    const char *at[] = {"HELLO", "WORLD", "AGAIN", "HELLO2", "AGAIN"};
    const size_t where[] = {1, 8, 16, 24, 40};
    for (int k = 0; k < 5; k++) memcpy(&mem[where[k]], at[k], strlen(at[k]));
    const uint32_t base = 0x80010000;
    std::string why;

    auto p = find_text(text("HELLO", "HEY"), mem.data(), mem.size(), base, &why);
    CHECK(p.size() == 1 && p[0].address == base + 1, "HELLO found once (not inside HELLO2): %zu", p.size());
    CHECK(!p.empty() && p[0].original == bytes("HELLO") && p[0].replace == std::vector<uint8_t>({'H', 'E', 'Y', 0, 0}),
          "its patch: HELLO -> HEY and zeros");
    CHECK(why.empty(), "nothing to say");

    why.clear();
    p = find_text(text("WORLD", "EVERYONE!"), mem.data(), mem.size(), base, &why);
    CHECK(p.empty() && why.find("has room for 7 bytes, the replacement is 9") != std::string::npos,
          "WORLD -> EVERYONE! too long for its room: %s", why.c_str());

    why.clear();
    p = find_text(text("WORLD", "PLANET"), mem.data(), mem.size(), base, &why);
    CHECK(p.size() == 1 && p[0].replace.size() == 7 && p[0].original.size() == 7, "WORLD -> PLANET into the zeros after it");

    why.clear();
    p = find_text(text("AGAIN", "ONCE"), mem.data(), mem.size(), base, &why);
    CHECK(p.empty() && why.find("found in 2 places") != std::string::npos, "AGAIN twice, not every: %s", why.c_str());

    why.clear();
    p = find_text(text("AGAIN", "ONCE", true), mem.data(), mem.size(), base, &why);
    CHECK(p.size() == 2 && p[0].address == base + 16 && p[1].address == base + 40, "every = true: both places");

    printf("test_find_text: %s\n", failed ? "FAILED" : "all passed");
    return failed ? 1 : 0;
}

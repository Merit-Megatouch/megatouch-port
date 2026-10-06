// Settings lookup shared by the launcher, host shim and backend.
// MEGA_<NAME> is the current spelling; TRIX_<NAME> (the first port) is still accepted.
#pragma once
#include <cstdlib>
#include <string>

inline const char* menv(const char* name) {
    std::string a = std::string("MEGA_") + name;
    if (const char* v = getenv(a.c_str())) return v;
    std::string b = std::string("TRIX_") + name;
    return getenv(b.c_str());
}

inline int menv_int(const char* name, int fallback) {
    const char* v = menv(name);
    return v && *v ? atoi(v) : fallback;
}

#pragma once

#include <cstdio>
#include <cstdlib>

#define WIREMIC_CHECK(condition)                                        \
  do {                                                                   \
    if (!(condition)) {                                                  \
      std::fprintf(stderr, "CHECK FAILED at %s:%d: %s\n", __FILE__,      \
                    __LINE__, #condition);                               \
      std::abort();                                                      \
    }                                                                    \
  } while (0)

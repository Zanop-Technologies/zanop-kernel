#ifndef POSIX_SELFTEST_HPP
#define POSIX_SELFTEST_HPP

// Boot-time self-test, compiled only by `make selftest` (ZANOP_SELFTEST=1).
// Results go to the serial port; see docs/posix.md.
namespace posix {
void selftest();
}

#endif

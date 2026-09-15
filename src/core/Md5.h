// MD5, as RFC 1321 defines it. The freedesktop.org thumbnail cache names a
// thumbnail by the MD5 of its photo's URI. Not for anything that needs security.
#pragma once

#include <string>

namespace Md5 {

// The digest of text as 32 lower case hex digits.
std::string hex(const std::string &text);

}  // namespace Md5

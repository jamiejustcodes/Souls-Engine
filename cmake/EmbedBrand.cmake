file(READ "${INPUT}" data HEX)
string(REGEX REPLACE "(..)" "0x\\1," values "${data}")
file(WRITE "${OUTPUT}" "#pragma once\nnamespace souls::brand::embedded { inline constexpr unsigned char logo[]{${values}}; }\n")

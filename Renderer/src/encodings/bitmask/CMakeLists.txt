thesis_add_library(encoding_bitmask
    SOURCES
        BitmaskEncoding.cpp
    PUBLIC_DEPS
        thesis::encoding
        thesis::shared
    FOLDER "encodings")
thesis_register_plugin(SOCKET ENCODING TARGET thesis_encoding_bitmask FUNCTION registerBitmaskEncoding)

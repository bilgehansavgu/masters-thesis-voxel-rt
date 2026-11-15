thesis_add_library(encoding_brick2012
    SOURCES
        Brick2012Encoding.cpp
        ColumnBrick.cpp
    PUBLIC_DEPS
        thesis::encoding
        thesis::shared
    FOLDER "encodings")
thesis_register_plugin(SOCKET ENCODING TARGET thesis_encoding_brick2012 FUNCTION registerBrick2012Encodings)

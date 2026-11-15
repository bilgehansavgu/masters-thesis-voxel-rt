# Encoding plugins. To add one: create a folder with its C++, add_subdirectory() it here, call
# thesis_register_plugin(SOCKET ENCODING ...) in its CMakeLists.txt, and add a primitive kind
# with the same name in shaders/primitives/ and in THESIS_PRIMITIVE_KINDS.
add_subdirectory(brick2012)

# thesis::encoding_plugins exposes registerBuiltInEncodings(), generated from the registrations above.
thesis_add_library(encoding_plugins
    SOURCES
        include/thesis/encodings/BuiltInEncodings.h
    PUBLIC_DEPS
        thesis::encoding
    FOLDER "encodings")
thesis_generate_plugin_registration(
    SOCKET ENCODING
    TARGET thesis_encoding_plugins
    REGISTRY_HEADER "thesis/encoding/Encoding.h"
    REGISTRY_TYPE "encoding::EncodingRegistry"
    ENTRY registerBuiltInEncodings)

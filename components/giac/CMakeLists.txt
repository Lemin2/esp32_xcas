set(GIAC_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/src")

set(GIAC_SOURCES
    "${GIAC_SRC_DIR}/alg_ext.cc"
    "${GIAC_SRC_DIR}/cocoa_stub.cc"
    "${GIAC_SRC_DIR}/csturm.cc"
    "${GIAC_SRC_DIR}/derive.cc"
    "${GIAC_SRC_DIR}/desolve.cc"
    "${GIAC_SRC_DIR}/ezgcd.cc"
    "${GIAC_SRC_DIR}/first.cc"
    "${GIAC_SRC_DIR}/gauss.cc"
    "${GIAC_SRC_DIR}/gausspol.cc"
    "${GIAC_SRC_DIR}/cutils.c"
    "${GIAC_SRC_DIR}/gen.cc"
    "${GIAC_SRC_DIR}/global.cc"
    "${GIAC_SRC_DIR}/graph_stubs.cc"
    "${GIAC_SRC_DIR}/help.cc"
    "${GIAC_SRC_DIR}/identificateur.cc"
    "${GIAC_SRC_DIR}/ifactor.cc"
    "${GIAC_SRC_DIR}/index.cc"
    "${GIAC_SRC_DIR}/input_lexer.cc"
    "${GIAC_SRC_DIR}/input_parser.cc"
    "${GIAC_SRC_DIR}/intg.cc"
    "${GIAC_SRC_DIR}/intgab.cc"
    "${GIAC_SRC_DIR}/isom.cc"
    "${GIAC_SRC_DIR}/lin.cc"
    "${GIAC_SRC_DIR}/libbf.c"
    "${GIAC_SRC_DIR}/maple.cc"
    "${GIAC_SRC_DIR}/mathml.cc"
    "${GIAC_SRC_DIR}/misc.cc"
    "${GIAC_SRC_DIR}/modfactor.cc"
    "${GIAC_SRC_DIR}/modpoly.cc"
    "${GIAC_SRC_DIR}/moyal.cc"
    "${GIAC_SRC_DIR}/pari.cc"
    "${GIAC_SRC_DIR}/permu.cc"
    "${GIAC_SRC_DIR}/plot.cc"
    "${GIAC_SRC_DIR}/plot3d.cc"
    "${GIAC_SRC_DIR}/prog.cc"
    "${GIAC_SRC_DIR}/quater.cc"
    "${GIAC_SRC_DIR}/risch.cc"
    "${GIAC_SRC_DIR}/rpn.cc"
    "${GIAC_SRC_DIR}/series.cc"
    "${GIAC_SRC_DIR}/signalprocessing.cc"
    "${GIAC_SRC_DIR}/solve.cc"
    "${GIAC_SRC_DIR}/sparse.cc"
    "${GIAC_SRC_DIR}/subst.cc"
    "${GIAC_SRC_DIR}/sym2poly.cc"
    "${GIAC_SRC_DIR}/symbolic.cc"
    "${GIAC_SRC_DIR}/tex.cc"
    "${GIAC_SRC_DIR}/threaded.cc"
    "${GIAC_SRC_DIR}/ti89.cc"
    "${GIAC_SRC_DIR}/tinymt32.cc"
    "${GIAC_SRC_DIR}/unary.cc"
    "${GIAC_SRC_DIR}/usual.cc"
    "${GIAC_SRC_DIR}/vecteur.cc"
)

idf_component_register(
    SRCS ${GIAC_SOURCES}
    INCLUDE_DIRS "src"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    HAVE_CONFIG_H
    IN_GIAC
    BF2GMP
    TIMEOUT
    NO_STDEXCEPT
    GIAC_BINARY_ARCHIVE
    NO_UNARY_FUNCTION_COMPOSE
    NO_TEMPLATE_MULTGCD
    GIAC_NO_OPTIMIZATIONS
    STATIC_BUILTIN_LEXER_FUNCTIONS
    HAVE_NO_PWD_H
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++17
    -frtti
    -fno-threadsafe-statics
    -fno-strict-aliasing
    -fno-exceptions
    -Os
    -Wno-error
    -Wno-error=unknown-pragmas
)

if(CONFIG_IDF_TARGET_ARCH_XTENSA)
    target_compile_options(${COMPONENT_LIB} PRIVATE -mauto-litpools)
endif()

# A handful of files have try/catch blocks not guarded by NO_STDEXCEPT.
# Compile just those with exceptions enabled (their own unwind tables are
# negligible) while the rest of giac uses -fno-exceptions for size.
set_source_files_properties(
    "${GIAC_SRC_DIR}/signalprocessing.cc"
    "${GIAC_SRC_DIR}/maple.cc"
    "${GIAC_SRC_DIR}/help.cc"
    "${GIAC_SRC_DIR}/vecteur.cc"
    PROPERTIES COMPILE_OPTIONS "-fexceptions"
)
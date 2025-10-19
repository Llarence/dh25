set(UI_DIR ./ui)
file(GLOB_RECURSE UI_SOURCES ${UI_DIR}/*.c)

set(INTERNAL_DIR ./internal)
file(GLOB_RECURSE INTERNAL_SOURCES ${INTERNAL_DIR}/*.c)

idf_component_register(
    SRCS "main.c" "lv_port.c" ${UI_SOURCES} ${INTERNAL_SOURCES}
    INCLUDE_DIRS "." ${UI_DIR} ${INTERNAL_DIR})

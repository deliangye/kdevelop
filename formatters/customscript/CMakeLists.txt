########### indent ###############
set(kdevcustomscript_PART_SRCS
    customscript_plugin.cpp
)

ecm_qt_declare_logging_category(kdevcustomscript_PART_SRCS
    HEADER debug.h
    IDENTIFIER CUSTOMSCRIPT
    CATEGORY_NAME "kdevelop.formatters.customscript"
)
kdevplatform_add_plugin(kdevcustomscript JSON kdevcustomscript.json SOURCES ${kdevcustomscript_PART_SRCS})
target_link_libraries(kdevcustomscript
    KDev::Interfaces
    KDev::Util)

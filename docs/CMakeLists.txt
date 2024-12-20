find_package(Doxygen)

if(DOXYGEN_FOUND)
  message(STATUS "Doxygen found: ${DOXYGEN_EXECUTABLE}")

  FetchContent_Declare(
    doxygen-awesome-css
    GIT_REPOSITORY https://github.com/jothepro/doxygen-awesome-css.git
    GIT_TAG        v2.3.4
  )

  FetchContent_MakeAvailable(doxygen-awesome-css)

  # Halide-specific Doxygen options
  set(DOXYGEN_ALPHABETICAL_INDEX NO)
  set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
  set(DOXYGEN_CASE_SENSE_NAMES NO)
  set(DOXYGEN_CLASS_DIAGRAMS NO)
  set(DOXYGEN_EXCLUDE bin)
  set(DOXYGEN_EXTRACT_ALL NO)
  set(DOXYGEN_EXTRACT_LOCAL_CLASSES NO)
  set(DOXYGEN_FILE_PATTERNS *.hpp)
  set(DOXYGEN_GENERATE_TREEVIEW YES)
  set(DOXYGEN_HIDE_UNDOC_CLASSES YES)
  set(DOXYGEN_HIDE_FRIEND_COMPOUNDS YES)
  set(DOXYGEN_HIDE_IN_BODY_DOCS YES)
  set(DOXYGEN_MARKDOWN_ID_STYLE GITHUB)
  set(DOXYGEN_QT_AUTOBRIEF YES)
  set(DOXYGEN_QUIET YES)
  set(DOXYGEN_RECURSIVE YES)
  set(DOXYGEN_REFERENCED_BY_RELATION YES)
  set(DOXYGEN_REFERENCES_RELATION YES)
  set(DOXYGEN_SORT_BY_SCOPE_NAME YES)
  set(DOXYGEN_SORT_MEMBER_DOCS NO)
  set(DOXYGEN_SOURCE_BROWSER YES)
  set(DOXYGEN_STRIP_CODE_COMMENTS NO)
  set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${PROJECT_SOURCE_DIR}/README.md")
  set(DOXYGEN_HTML_EXTRA_STYLESHEET "${CMAKE_BINARY_DIR}/_deps/doxygen-awesome-css-src/doxygen-awesome.css")
  set(DOXYGEN_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}")
  set(DOXYGEN_EXCLUDE_SYMBOLS "detail,detail::*,std,std::*,benchmark,test")
  set(DOXYGEN_CASE_SENSE_NAMES YES)
  set(DOXYGEN_CREATE_SUBDIRS YES)

  # TODO: get include paths correct, e.g. <Lightweight/SqlConnection.hpp> rather than <SqlConnection.hpp>
  set(DOXYGEN_FULL_PATH_NAMES YES)
  set(DOXYGEN_STRIP_FROM_PATH "${PROJECT_SOURCE_DIR}/src")

  set(DOCS_SOURCES
    "${PROJECT_SOURCE_DIR}/src";
    "${PROJECT_SOURCE_DIR}/README.md";
    "${PROJECT_SOURCE_DIR}/docs/usage.md";
    "${PROJECT_SOURCE_DIR}/docs/best-practices.md";
  )
  doxygen_add_docs(doc
    ${DOCS_SOURCES}
    #"${PROJECT_SOURCE_DIR}/src;${PROJECT_SOURCE_DIR}/README.md;${PROJECT_SOURCE_DIR}/docs/usage.md;${PROJECT_SOURCE_DIR}/docs/best-practices.md"
    ALL
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generate HTML documentation"
  )

else()
    message(WARNING "Doxygen not found, not generating documentation")
endif()

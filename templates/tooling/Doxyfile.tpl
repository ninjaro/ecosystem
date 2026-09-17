PROJECT_NAME           = {{project_name}}
PROJECT_NUMBER         = {{project_version}}
PROJECT_BRIEF          = "Generated API and source reference."

INPUT                  = {{input_files}}
RECURSIVE              = NO
EXTENSION_MAPPING      = tpp=C++
MARKDOWN_SUPPORT       = YES

GENERATE_HTML          = YES
HTML_OUTPUT            = html
GENERATE_LATEX         = NO
OUTPUT_DIRECTORY       = {{output_dir}}
CREATE_SUBDIRS         = YES
TIMESTAMP              = YES

GENERATE_TREEVIEW      = YES
FULL_SIDEBAR           = NO
SEARCHENGINE           = YES
SERVER_BASED_SEARCH    = NO
HTML_COPY_CLIPBOARD    = YES
HTML_CODE_FOLDING      = YES

EXTRACT_ALL            = YES
EXTRACT_PRIVATE        = YES
EXTRACT_STATIC         = YES
EXTRACT_LOCAL_CLASSES  = YES
EXTRACT_LOCAL_METHODS  = YES
EXTRACT_ANON_NSPACES   = YES
HIDE_SCOPE_NAMES       = NO
SHOW_INCLUDE_FILES     = YES
SOURCE_BROWSER         = YES
INLINE_SOURCES         = YES
REFERENCED_BY_RELATION = YES
REFERENCES_RELATION    = YES
REFERENCES_LINK_SOURCE = YES
OPTIMIZE_OUTPUT_FOR_C  = NO

HAVE_DOT               = YES
INTERACTIVE_SVG        = YES
DOT_NUM_THREADS        = 2
DOT_GRAPH_MAX_NODES    = 75
CLASS_GRAPH            = YES
COLLABORATION_GRAPH    = YES
INCLUDE_GRAPH          = YES
INCLUDED_BY_GRAPH      = YES
CALL_GRAPH             = YES
CALLER_GRAPH           = YES
UML_LOOK               = NO
DOT_UML_DETAILS        = NO

ENABLE_PREPROCESSING   = YES
# Basic docs use Doxygen's parser without requiring a configured C++ build.
CLANG_ASSISTED_PARSING = NO
CLANG_ADD_INC_PATHS    = YES
CLANG_DATABASE_PATH    = {{clang_database}}

FULL_PATH_NAMES        = NO
STRIP_FROM_PATH        = .
STRIP_FROM_INC_PATH    = .

WARN_IF_UNDOCUMENTED   = YES
WARN_IF_DOC_ERROR      = YES
WARN_NO_PARAMDOC       = YES
WARN_AS_ERROR          = NO
WARN_FORMAT            = "$file:$line: $text"
WARN_LOGFILE           = {{warning_log}}

NUM_PROC_THREADS       = 2

SET(MSSQL_SRCS
  qgsmssqlprovider.cpp
  qgsmssqlgeometryparser.cpp
  qgsmssqlsourceselect.cpp
  qgsmssqltablemodel.cpp
  qgsmssqlnewconnection.cpp
  qgsmssqldataitems.cpp
  qgsmssqlexpressioncompiler.cpp
  qgsmssqlfeatureiterator.cpp
)

SET(MSSQL_MOC_HDRS
  qgsmssqlprovider.h
  qgsmssqlsourceselect.h
  qgsmssqltablemodel.h
  qgsmssqlnewconnection.h
  qgsmssqldataitems.h
)

SET(MSSQL_HDRS
  qgsmssqlexpressioncompiler.h
)

########################################################
# Build

QT5_WRAP_CPP(MSSQL_MOC_SRCS ${MSSQL_MOC_HDRS})

INCLUDE_DIRECTORIES(
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/core/expression
  ${CMAKE_SOURCE_DIR}/src/core/geometry
  ${CMAKE_SOURCE_DIR}/src/core/metadata
  ${CMAKE_SOURCE_DIR}/src/gui

  ${CMAKE_BINARY_DIR}/src/core
  ${CMAKE_BINARY_DIR}/src/gui
  ${CMAKE_BINARY_DIR}/src/ui
)
INCLUDE_DIRECTORIES(SYSTEM
  ${GDAL_INCLUDE_DIR}
  ${GEOS_INCLUDE_DIR}
  ${QT_INCLUDE_DIR}
  ${QSCINTILLA_INCLUDE_DIR}
  ${PROJ_INCLUDE_DIR}
)


ADD_LIBRARY(mssqlprovider MODULE ${MSSQL_SRCS} ${MSSQL_MOC_SRCS} ${MSSQL_HDRS})

TARGET_LINK_LIBRARIES(mssqlprovider
  qgis_core
  qgis_gui
  ${QT_QTSQL_LIBRARY}
)
########################################################
# Install

INSTALL (TARGETS mssqlprovider
  RUNTIME DESTINATION ${QGIS_PLUGIN_DIR}
  LIBRARY DESTINATION ${QGIS_PLUGIN_DIR})


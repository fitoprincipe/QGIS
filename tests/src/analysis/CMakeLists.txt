# Standard includes and utils to compile into all tests.
SET (util_SRCS)


#####################################################
# Don't forget to include output directory, otherwise
# the UI file won't be wrapped!
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/core/auth
  ${CMAKE_SOURCE_DIR}/src/core/expression
  ${CMAKE_SOURCE_DIR}/src/core/geometry
  ${CMAKE_SOURCE_DIR}/src/core/metadata
  ${CMAKE_SOURCE_DIR}/src/core/raster
  ${CMAKE_SOURCE_DIR}/src/core/symbology-ng
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/vector
  ${CMAKE_SOURCE_DIR}/src/analysis/raster
  ${CMAKE_SOURCE_DIR}/src/test

  ${CMAKE_BINARY_DIR}/src/core
  ${CMAKE_BINARY_DIR}/src/analysis
)
INCLUDE_DIRECTORIES(SYSTEM
  ${QT_INCLUDE_DIR}
  ${GDAL_INCLUDE_DIR}
  ${PROJ_INCLUDE_DIR}
  ${GEOS_INCLUDE_DIR}
  ${SQLITE3_INCLUDE_DIR}
)

#note for tests we should not include the moc of our
#qtests in the executable file list as the moc is
#directly included in the sources
#and should not be compiled twice. Trying to include
#them in will cause an error at build time

#No relinking and full RPATH for the install tree
#See: http://www.cmake.org/Wiki/CMake_RPATH_handling#No_relinking_and_full_RPATH_for_the_install_tree

MACRO (ADD_QGIS_TEST TESTSRC)
  SET (TESTNAME  ${TESTSRC})
  STRING(REPLACE "test" "" TESTNAME ${TESTNAME})
  STRING(REPLACE "qgs" "" TESTNAME ${TESTNAME})
  STRING(REPLACE ".cpp" "" TESTNAME ${TESTNAME})
  SET (TESTNAME  "qgis_${TESTNAME}test")

  SET(${TESTNAME}_SRCS ${TESTSRC} ${util_SRCS})
  SET(${TESTNAME}_MOC_CPPS ${TESTSRC})
  ADD_EXECUTABLE(${TESTNAME} ${${TESTNAME}_SRCS})
  SET_TARGET_PROPERTIES(${TESTNAME} PROPERTIES AUTOMOC TRUE)
  TARGET_LINK_LIBRARIES(${TESTNAME}
    ${QT_QTCORE_LIBRARY}
    ${QT_QTTEST_LIBRARY}
    qgis_analysis)
  ADD_TEST(${TESTNAME} ${CMAKE_BINARY_DIR}/output/bin/${TESTNAME} -maxwarnings 10000)
  #SET_TARGET_PROPERTIES(qgis_${testname} PROPERTIES
  #  INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/${QGIS_LIB_DIR}
  #  INSTALL_RPATH_USE_LINK_PATH true )
ENDMACRO (ADD_QGIS_TEST)

#############################################################
# Tests:
SET(TESTS
 testqgsvectoranalyzer.cpp
 testqgsgeometrysnapper.cpp
 testopenstreetmap.cpp
 testqgszonalstatistics.cpp
 testqgsrastercalculator.cpp
 testqgsalignraster.cpp
    )

FOREACH(TESTSRC ${TESTS})
    ADD_QGIS_TEST(${TESTSRC})
ENDFOREACH(TESTSRC)

# CMake generated Testfile for 
# Source directory: D:/Tmap/_lab/_kursor/FaTTY
# Build directory: D:/Tmap/_lab/_kursor/FaTTY/build-mingw
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[fatty_tests]=] "D:/Tmap/_lab/_kursor/FaTTY/build-mingw/fatty_tests.exe")
set_tests_properties([=[fatty_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Tmap/_lab/_kursor/FaTTY/CMakeLists.txt;183;add_test;D:/Tmap/_lab/_kursor/FaTTY/CMakeLists.txt;0;")
add_test([=[fatty_smoke]=] "D:/Tmap/_lab/_kursor/FaTTY/build-mingw/FaTTY.exe" "--smoke-test")
set_tests_properties([=[fatty_smoke]=] PROPERTIES  RUN_SERIAL "TRUE" _BACKTRACE_TRIPLES "D:/Tmap/_lab/_kursor/FaTTY/CMakeLists.txt;184;add_test;D:/Tmap/_lab/_kursor/FaTTY/CMakeLists.txt;0;")

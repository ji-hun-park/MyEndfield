# CMake generated Testfile for 
# Source directory: D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore
# Build directory: D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(NativeCoreTests "D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/build/Debug/NativeCoreTests.exe")
  set_tests_properties(NativeCoreTests PROPERTIES  _BACKTRACE_TRIPLES "D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/CMakeLists.txt;90;add_test;D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(NativeCoreTests "D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/build/Release/NativeCoreTests.exe")
  set_tests_properties(NativeCoreTests PROPERTIES  _BACKTRACE_TRIPLES "D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/CMakeLists.txt;90;add_test;D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(NativeCoreTests "D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/build/MinSizeRel/NativeCoreTests.exe")
  set_tests_properties(NativeCoreTests PROPERTIES  _BACKTRACE_TRIPLES "D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/CMakeLists.txt;90;add_test;D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(NativeCoreTests "D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/build/RelWithDebInfo/NativeCoreTests.exe")
  set_tests_properties(NativeCoreTests PROPERTIES  _BACKTRACE_TRIPLES "D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/CMakeLists.txt;90;add_test;D:/ArrFolder/WorkSpaces/UnityWorkSpace/MyEndfield/NativeCore/CMakeLists.txt;0;")
else()
  add_test(NativeCoreTests NOT_AVAILABLE)
endif()
subdirs("_deps/googletest-build")

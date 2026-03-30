# CMake generated Testfile for 
# Source directory: C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server
# Build directory: C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/cmake-build-debug
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(UnitTests "C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/cmake-build-debug/bin/test_genie.exe")
set_tests_properties(UnitTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;247;add_test;C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;252;add_genie_test;C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;0;")
add_test(IntegrationTests "C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/cmake-build-debug/bin/test_integration.exe")
set_tests_properties(IntegrationTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;247;add_test;C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;253;add_genie_test;C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;0;")
add_test(Tier3Tests "C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/cmake-build-debug/bin/test_tier3.exe")
set_tests_properties(Tier3Tests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;247;add_test;C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;254;add_genie_test;C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;0;")
add_test(RestEndpointTests "C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/cmake-build-debug/bin/test_rest_endpoints.exe")
set_tests_properties(RestEndpointTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;247;add_test;C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;255;add_genie_test;C:/Users/Benni/CLionProjects/Metis_Genie_Platform/server/CMakeLists.txt;0;")

cmake_minimum_required(VERSION 3.24)
get_filename_component(source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(revision 7954e2dbb49c469ea286070bf46cdd71aeb29e4b)
file(MAKE_DIRECTORY "${source_dir}/refs")
file(DOWNLOAD
  "https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/${revision}/bin_files/6502_functional_test.bin"
  "${source_dir}/refs/6502_functional_test.bin"
  EXPECTED_HASH SHA256=fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd
  TLS_VERIFY ON SHOW_PROGRESS)
message(STATUS "Verified Klaus functional test in ${source_dir}/refs/6502_functional_test.bin")
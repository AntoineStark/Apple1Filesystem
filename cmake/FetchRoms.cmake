cmake_minimum_required(VERSION 3.24)
get_filename_component(source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(revision 3f69e3f35f19078f9437be7338d6b582c8d1e4e5)
set(base_url "https://raw.githubusercontent.com/fstark/apple1loader/${revision}")
file(MAKE_DIRECTORY "${source_dir}/roms")
file(DOWNLOAD "${base_url}/32KA1COMPIL.BIN" "${source_dir}/roms/32KA1COMPIL.BIN"
  EXPECTED_HASH SHA256=12be503a010d4302bce2b9c24e806bb29267795504bcbfe235ca46ac6a175547
  TLS_VERIFY ON SHOW_PROGRESS)
file(DOWNLOAD "${base_url}/software/WOZMON" "${source_dir}/roms/wozmon.rom"
  EXPECTED_HASH SHA256=e5af0d1c4057bd8e0ef5cb069c208ff7cc0984a7dff53b12c5cf119de8cb5c25
  TLS_VERIFY ON SHOW_PROGRESS)
message(STATUS "Verified expansion and WozMon ROM images in ${source_dir}/roms")
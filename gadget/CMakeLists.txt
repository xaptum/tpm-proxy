cmake_minimum_required(VERSION 3.1 FATAL_ERROR)

project(tpm_proxy
  VERSION 0.1.0
  LANGUAGES C
)

include(GNUInstallDirs)


set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

find_library(LIBRT rt)

add_executable(tpm_gadget
  src/tpm_gadget_main.c
  src/usbg_service.c
  src/usbstring.c
  src/tpm_proxy.c
)

target_link_libraries(tpm_gadget
  Threads::Threads
)
                                                                                                                                                                                                                                                                                     
if(LIBRT)
  target_link_libraries(tpm_gadget ${LIBRT})
endif()

set_target_properties(tpm_gadget
  PROPERTIES
  C_STANDARD 99
)

install(
  TARGETS tpm_gadget
  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

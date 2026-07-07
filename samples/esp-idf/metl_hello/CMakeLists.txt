# ESP-IDF sample application that consumes metl as a component.
#
# Standard ESP-IDF top-level project boilerplate. Before pulling in the IDF
# project machinery we add metl's in-tree components/ directory to the build via
# EXTRA_COMPONENT_DIRS, so IDF discovers the `metl` component (this repo's
# components/metl/) without publishing it to the component registry.
#
#   samples/esp-idf/metl_hello/CMakeLists.txt  ->  ../../../components
#
# The app's `main` component then declares `REQUIRES metl` (see
# main/CMakeLists.txt) to get <metl/...> on its include path.
#
# See: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html
cmake_minimum_required(VERSION 3.16)

# Add metl's component directory (repo-root/components) to the component search
# path. Absolute path so the sample builds regardless of the CWD idf.py runs in.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(metl_hello)

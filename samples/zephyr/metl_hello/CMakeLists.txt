# Zephyr sample application that consumes metl as a module.
#
# Standard Zephyr application boilerplate: locate the Zephyr build system, then
# declare the app. metl's headers are made available by the metl Zephyr module
# (see <metl-root>/zephyr/) when CONFIG_METL=y (set in prj.conf) and the module
# is on Zephyr's module path (EXTRA_ZEPHYR_MODULES=<metl-root>).

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(metl_hello LANGUAGES C CXX)

target_sources(app PRIVATE src/main.cpp)

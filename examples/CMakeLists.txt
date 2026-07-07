# examples/CMakeLists.txt
#
# Each example is a self-contained main() exercising one or more METL
# modules. Examples link only against metl::metl — they do not pull in
# the test warning/sanitizer options.

metl_cc_binary(
  NAME metl_example_blinky_fsm
  SRCS blinky_fsm.cpp
  DEPS metl::metl
)

metl_cc_binary(
  NAME metl_example_sensor_pipeline
  SRCS sensor_pipeline.cpp
  DEPS metl::metl
)

metl_cc_binary(
  NAME metl_example_can_frame_parser
  SRCS can_frame_parser.cpp
  DEPS metl::metl
)

# Threaded example needs the system thread library on POSIX.
find_package(Threads QUIET)
if(TARGET Threads::Threads)
  target_link_libraries(metl_example_sensor_pipeline PRIVATE Threads::Threads)
endif()

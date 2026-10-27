# CoCIS Smart Control & Monitoring System - ESP-IDF project root.
# Every folder under components/ is picked up automatically by the IDF build.
cmake_minimum_required(VERSION 3.22)

# Build only what main (transitively) needs, not every IDF component.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cocis_smart_control)

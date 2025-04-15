#!/bin/bash
mkdir -p build
cd build
cmake -D CMAKE_BUILD_TYPE=Debug .. || { echo "ERROR: cmake failed"; exit 1; }
make -j8 || { echo "ERROR: make failed"; exit 2; }


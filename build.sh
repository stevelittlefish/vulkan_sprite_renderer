#!/bin/bash
mkdir -p build

./update_file_list.sh

cd build
cmake -D CMAKE_BUILD_TYPE=Debug .. || { echo "ERROR: cmake failed"; exit 1; }
make -j8 || { echo "ERROR: make failed"; exit 2; }


#!/bin/bash
rm sourcelist.cmake
echo "set(source_list" > sourcelist.cmake
find src -name "*.c" >> sourcelist.cmake
echo ")" >> sourcelist.cmake

echo "set(header_list" >> sourcelist.cmake
find include -name "*.h" >> sourcelist.cmake
echo ")" >> sourcelist.cmake


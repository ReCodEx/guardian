#!/bin/sh
cgdelete -r memory:container_instance && cmake --build . && ./src/container --yaml=../temp/bsearch.yaml
# 이 파일은 예제 디렉토리의 루트에 위치합니다.
# 현재 예제는 'main' 컴포넌트 하나만 가집니다.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS ../components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(example_project)
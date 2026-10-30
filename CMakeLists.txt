cmake_minimum_required(VERSION 3.15)
project(TopoPartitioner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件列表
set(TOPO_SOURCES
    main.cpp
    Partitioner.cpp
    TopologyGraph.cpp
    Netlist.cpp
)

set(BASELINE_SOURCES
    solution.cpp
    Graph.cpp
    Node.cpp
    Net.cpp
)

# 主目标：拓扑+基线统一入口
add_executable(TopoPartitioner ${TOPO_SOURCES} ${BASELINE_SOURCES})

# 编译优化：MSVC 用 /O2，其他编译器（g++/MinGW）用 -O2
if(MSVC)
    target_compile_options(TopoPartitioner PRIVATE /O2)
else()
    target_compile_options(TopoPartitioner PRIVATE -O2)
endif()

# 线程支持
find_package(Threads REQUIRED)
target_link_libraries(TopoPartitioner Threads::Threads)

# MinGW 需要静态链接 winpthread 以避免运行时依赖
if(MINGW)
    target_link_options(TopoPartitioner PRIVATE -static -static-libgcc -static-libstdc++)
endif()

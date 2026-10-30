.RECIPEPREFIX := >

CXX = g++
CXXFLAGS = -O2 -std=c++17 -Wall -fopenmp

TARGET = TopoPartitioner

TOPO_OBJS = main.o Partitioner.o TopologyGraph.o Netlist.o
BASELINE_OBJS = solution.o Graph.o Node.o Net.o

OBJS = $(TOPO_OBJS) $(BASELINE_OBJS)

all: $(TARGET)

$(TARGET): $(OBJS)
>$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) -lpthread

main.o: main.cpp Partitioner.hpp TopologyGraph.hpp Netlist.hpp CandidatePropagator.hpp solution.h Types.hpp
>$(CXX) $(CXXFLAGS) -c main.cpp

Partitioner.o: Partitioner.cpp Partitioner.hpp TopologyGraph.hpp Netlist.hpp CandidatePropagator.hpp Types.hpp
>$(CXX) $(CXXFLAGS) -c Partitioner.cpp

TopologyGraph.o: TopologyGraph.cpp TopologyGraph.hpp Types.hpp
>$(CXX) $(CXXFLAGS) -c TopologyGraph.cpp

Netlist.o: Netlist.cpp Netlist.hpp Types.hpp
>$(CXX) $(CXXFLAGS) -c Netlist.cpp

solution.o: solution.cpp solution.h Graph.h Node.h Net.h
>$(CXX) $(CXXFLAGS) -c solution.cpp

Graph.o: Graph.cpp Graph.h Node.h Net.h
>$(CXX) $(CXXFLAGS) -c Graph.cpp

Node.o: Node.cpp Node.h Net.h
>$(CXX) $(CXXFLAGS) -c Node.cpp

Net.o: Net.cpp Net.h Node.h
>$(CXX) $(CXXFLAGS) -c Net.cpp

clean:
>rm -f *.o $(TARGET)

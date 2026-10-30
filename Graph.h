#ifndef GRAPH_H
#define GRAPH_H

#include <vector>
#include "Node.h"
#include "Net.h"

class Graph {
private:
    std::vector<Node *> nodes_;
    std::vector<Net *> nets_;

public:
    Graph();

    ~Graph();

    void clear();

    Node *add_node(int index);

    Net *add_net(int index);

    Node *node_at(int zero_based_index) const;

    Net *net_at(int zero_based_index) const;

    std::vector<Node *> get_nodes() const;

    std::vector<Net *> get_nets() const;

    const std::vector<Node *> &nodes_ref() const;

    const std::vector<Net *> &nets_ref() const;
};

#endif
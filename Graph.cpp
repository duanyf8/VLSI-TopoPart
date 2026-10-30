#include "Graph.h"

Graph::Graph() {}

Graph::~Graph() {
    clear();
}

void Graph::clear() {
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        delete nodes_[i];
    }

    for (std::size_t i = 0; i < nets_.size(); ++i) {
        delete nets_[i];
    }

    nodes_.clear();
    nets_.clear();
}

Node *Graph::add_node(int index) {
    Node *node = new Node(index);
    nodes_.push_back(node);
    return node;
}

Net *Graph::add_net(int index) {
    Net *net = new Net(index);
    nets_.push_back(net);
    return net;
}

Node *Graph::node_at(int zero_based_index) const {
    return nodes_.at(zero_based_index);
}

Net *Graph::net_at(int zero_based_index) const {
    return nets_.at(zero_based_index);
}

std::vector<Node *> Graph::get_nodes() const {
    return nodes_;
}

std::vector<Net *> Graph::get_nets() const {
    return nets_;
}

const std::vector<Node *> &Graph::nodes_ref() const {
    return nodes_;
}

const std::vector<Net *> &Graph::nets_ref() const {
    return nets_;
}
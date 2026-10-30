#include "Node.h"
#include "Net.h"

Node::Node(int index) : index_(index) {}

int Node::get_index() const {
    return index_;
}

const std::vector<Net *> &Node::get_nets() const {
    return nets_;
}

void Node::add_net(Net *net) {
    nets_.push_back(net);
}
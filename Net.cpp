#include "Net.h"
#include "Node.h"

Net::Net(int index) : index_(index) {}

int Net::get_index() const {
    return index_;
}

const std::vector<Node *> &Net::get_nodes() const {
    return nodes_;
}

void Net::add_node(Node *node) {
    nodes_.push_back(node);
}
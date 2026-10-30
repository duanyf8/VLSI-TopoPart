#ifndef NET_H
#define NET_H

#include <vector>

class Node;

class Net {
private:
    int index_;
    std::vector<Node *> nodes_;

public:
    explicit Net(int index);

    int get_index() const;

    const std::vector<Node *> &get_nodes() const;

    void add_node(Node *node);
};

#endif
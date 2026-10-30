#ifndef NODE_H
#define NODE_H

#include <vector>

class Net;

class Node {
private:
    int index_;
    std::vector<Net *> nets_;

public:
    explicit Node(int index);

    int get_index() const;

    const std::vector<Net *> &get_nets() const;

    void add_net(Net *net);
};

#endif
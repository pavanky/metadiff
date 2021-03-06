//
// Created by alex on 13/12/15.
//

#ifndef METADIFF_OPERATORS_BASE_H
#define METADIFF_OPERATORS_BASE_H

namespace metadiff {

    // Helper function for boilerplate code
    void update_grad_name(std::shared_ptr<NodeInternal> my_grad, size_t current){
        if(my_grad->name == "Derived Node" or my_grad->name == ""){
            my_grad->name = "Grad of " + std::to_string(current);
        } else {
            my_grad->name += "|Grad of " + std::to_string(current);
        }
    };

    // Helper function for boilerplate code
    void send_grad_message(std::shared_ptr<GraphInternal> graph,
                           size_t target_id, size_t msg_id,
                           std::unordered_map<size_t, size_t> &messages);

    // Helper function to validate axes
    bool validate_axes(std::vector<size_t> axes){
        if(axes.size() > 4){
            return false;
        }
        bool checks[4] {false, false, false, false};
        for(int i=0;i<axes.size();i++){
            if(axes[i] > 3){
                return false;
            }
            if(checks[axes[i]]){
                return false;
            }
            checks[axes[i]] = true;
        }
        return true;
    }

    // Helper function to verify shapes of elementwise operators
    Shape verify_elementwise_shapes(std::string name, NodeInVec node_ptrs){
        Shape max_shape  = node_ptrs[0].lock()->shape;
        for(int i=1; i<node_ptrs.size();i++){
            auto node_shape = node_ptrs[i].lock()->shape;
            bool max = false;
            for(int j=0;j<4;j++){
                if(node_shape[j] != 1 and max_shape[j] == 1){
                    max = true;
                    break;
                }
            }
            if(max){
                for(int j=0;j<4;j++) {
                    if(node_shape[j] == 1 and max_shape[j] != 1){
                        throw IncompatibleShapes(name, node_ptrs);
                    } else if(node_shape[j] != 1 and max_shape[j] != 1 and node_shape[j] != max_shape[j]){
                        throw IncompatibleShapes(name, node_ptrs);
                    }
                }
                max_shape = node_shape;
            }
        }
        return max_shape;
    }

    // Helper function to get all elements
    SymInt number_of_elements(Shape shape){
        return shape[0] * shape[1] * shape[2] * shape[3];
    };

    class Broadcast : public Operator {
    public:
        NodeInPtr parent;
        Shape to_shape;
        Broadcast(GraphInPtr graph,
                  NodeInPtr parent,
                  Shape to_shape):
                Operator("Broadcast", graph),
                parent(parent),
                to_shape(to_shape){
            auto parent_shape = parent.lock()->shape;
            for(int i=0;i<4;i++){
                if(parent_shape[i] != 1 and parent_shape[i] != to_shape[i]){
                    throw IncompatibleShapes(name, {parent.lock()->id}, {parent_shape, to_shape});
                }
            }
        }

        ad_value_type get_value_type(){
            return parent.lock()->v_type;
        }

        Shape get_shape(){
            return to_shape;
        }

        ad_node_type get_node_type(){
            auto parent_type = parent.lock()->type;
            if(parent_type == INPUT
               or parent_type == SHARED_INPUT
               or parent_type == INPUT_DERIVED){
                return INPUT_DERIVED;
            } else if (parent_type == CONSTANT_DERIVED or parent_type == SYMBOLIC_INTEGER){
                return CONSTANT_DERIVED;
            } else {
                return CONSTANT;
            }
        };

        unsigned short get_gradient_level(){
            return parent.lock()->grad_level;
        }

        NodeInVec get_parents(){
            return {parent};
        }

        NodeInVec get_arguments(){
            return {};
        }

        std::vector<size_t> get_axes(){
            std::vector<size_t> axes;
            auto p1_shape = this->parent.lock()->shape;
            for(int i=0;i<4;i++){
                if(p1_shape[i] != to_shape[i]){
                    axes.push_back(i);
                }
            }
            return axes;
        }

        void generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages);
    };

    Node Node::broadcast(Shape shape) {
        auto graph = this->graph.lock();
        auto op = std::make_shared<Broadcast>(this->graph, graph->nodes[this->id], shape);
        return Node(this->graph, graph->derived_node(op).lock()->id);
    }

    Node Node::broadcast_to(Node other) {
        return broadcast(graph.lock()->nodes[other.id]->shape);
    }

    Node broadcast(Node node, Shape shape){
        return node.broadcast(shape);
    }

    Node broadcast_to(Node node, Node other) {
        return node.broadcast_to(other);
    }

    class Sum : public Operator {
    public:
        NodeInPtr parent;
        std::vector<size_t> axes;

        Sum(GraphInPtr graph,
            NodeInPtr parent,
            std::vector<size_t> axes):
                Operator("Sum", graph),
                parent(parent),
                axes(axes)
        {
            if(not validate_axes(axes)){
                std::string axes_str;
                for(int i=0;i<axes.size();i++){
                    axes_str += std::to_string(axes[i]);
                    if(i < axes.size()-1){
                        axes_str += ", ";
                    }
                }
                if(axes.size() == 0){
                    axes_str = "NULL";
                }
                throw InvalidArguments(name, {parent}, axes_str);
            }
        }

        ad_value_type get_value_type(){
            return parent.lock()->v_type;
        }

        ad_node_type get_node_type(){
            auto parent_type = parent.lock()->type;
            switch (parent_type) {
                case INPUT: return INPUT_DERIVED;
                case SHARED_INPUT: return INPUT_DERIVED;
                case SYMBOLIC_INTEGER: return CONSTANT;
                default: return parent_type;
            }
        };

        Shape get_shape(){
            auto p_shape = parent.lock()->shape;
            for(int i=0;i<axes.size();i++){
                p_shape[axes[i]] = 1;
            }
            return p_shape;
        }

        unsigned short get_gradient_level(){
            return parent.lock()->grad_level;
        }

        NodeInVec get_parents(){
            return {parent};
        }

        NodeInVec get_arguments(){
            return {};
        }

        void generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages);
    };

    Node Node::sum(std::vector<size_t> axes) {
        auto graph = this->graph.lock();
        auto op = std::make_shared<Sum>(this->graph, graph->nodes[this->id], axes);
        return Node(this->graph, graph->derived_node(op).lock()->id);
    }

    Node sum(Node node, std::vector<size_t> axes={0, 1, 2, 3}){
        return node.sum(axes);
    }

    class NaryOperator: public Operator{
    public:
        NodeInVec parents;
        Shape shape;
        NaryOperator(std::string const name,
                     GraphInPtr graph,
                     NodeInVec parents) :
                Operator(name, graph),
                parents(parents)
        {
            if(parents.size() < 2){
                throw InvalidArguments(name, parents, "Need atleast 2 parents");
            }
        };

        NodeInVec get_parents() {
            return parents;
        };

        ad_value_type get_value_type(){
            auto top_type = BOOLEAN;
            for(int i=0;i<parents.size();i++){
                auto v_type = parents[i].lock()->v_type;
                if(v_type == FLOAT){
                    return FLOAT;
                }
                if(v_type == INTEGER){
                    top_type = INTEGER;
                }
            }
            return top_type;
        };

        ad_node_type get_node_type(){
            bool constant_derived = false;
            for(int i=0;i<parents.size();i++){
                auto parent_type = parents[i].lock()->type;
                if(parent_type == INPUT
                   or parent_type == INPUT_DERIVED
                   or parent_type == SHARED_INPUT){
                    return INPUT_DERIVED;
                }
                if(parent_type == CONSTANT_DERIVED or parent_type == SYMBOLIC_INTEGER){
                    constant_derived = true;
                }
            }
            if(constant_derived){
                return CONSTANT_DERIVED;
            } else {
                return CONSTANT;
            }
        };

        std::array<SymInt,4> get_shape(){
            return shape;
        }

        unsigned short get_gradient_level(){
            unsigned short max_grad_level = 0;
            for(int i=0;i<parents.size();i++){
                auto grad_level = parents[i].lock()->grad_level;
                if(grad_level > max_grad_level){
                    max_grad_level = grad_level;
                }
            }
            return max_grad_level;
        };

        NodeInVec get_arguments() {
            return NodeInVec {};
        }

        bool all_parent_const(){
            for(int i=0;i<parents.size();i++){
                if(not parents[i].lock()->is_constant()){
                    return false;
                }
            }
            return true;
        }

        void throw_grad_type_error(){
            std::string type_str;
            for(int i=0;i<parents.size();i++){
                type_str += to_string(parents[i].lock()->type);
                if(i < parents.size() - 1){
                    type_str += ", ";
                }
            }
            throw UnknownError(parents, "Gradient message present, but parents are " + type_str);
        }
    };

    class BinaryOperator : public Operator{
    public:
        NodeInPtr parent1;
        NodeInPtr parent2;
        Shape shape;

        BinaryOperator(std::string const name,
                       GraphInPtr graph,
                       NodeInPtr parent1,
                       NodeInPtr parent2) :
                Operator(name, graph),
                parent1(parent1),
                parent2(parent2)
        {}

        NodeInVec get_parents() {
            return {parent1, parent2};
        };

        ad_value_type get_value_type(){
            auto parent1_v_type = parent1.lock()->v_type;
            auto parent2_v_type = parent2.lock()->v_type;
            if(parent1_v_type == FLOAT or parent2_v_type == FLOAT){
                return FLOAT;
            } else if(parent1_v_type == INTEGER or parent2_v_type == INTEGER) {
                return INTEGER;
            } else {
                return BOOLEAN;
            }
        };

        ad_node_type get_node_type(){
            auto parent1_type = parent1.lock()->type;
            auto parent2_type = parent1.lock()->type;
            if(parent1_type == INPUT
               or parent1_type == SHARED_INPUT
               or parent1_type == INPUT_DERIVED
               or parent2_type == INPUT
               or parent2_type == SHARED_INPUT
               or parent2_type == INPUT_DERIVED){
                return INPUT_DERIVED;
            }
            if(parent1_type == CONSTANT_DERIVED
               or parent1_type == SYMBOLIC_INTEGER
               or parent2_type == CONSTANT_DERIVED
               or parent2_type == SYMBOLIC_INTEGER){
                return CONSTANT_DERIVED;
            } else {
                return CONSTANT;
            }
        };

        std::array<SymInt,4> get_shape(){
            return shape;
        }

        unsigned short get_gradient_level(){
            auto parent1_grad_level = parent1.lock()->grad_level;
            auto parent2_grad_level = parent2.lock()->grad_level;
            return parent1_grad_level > parent2_grad_level ? parent1_grad_level : parent2_grad_level;
        };

        NodeInVec get_arguments() {
            return NodeInVec {};
        }

        void throw_grad_type_error(){
            throw UnknownError({parent1, parent2},
                               "Gradient message present, but parents are " +
                               to_string(parent1.lock()->type) + ", " +
                               to_string(parent2.lock()->type));
        }
    };

    class UnaryOperator : public Operator{
    public:
        NodeInPtr parent;
        UnaryOperator(std::string const name,
                      GraphInPtr graph,
                      NodeInPtr parent):
                Operator(name, graph),
                parent(parent)
        {};

        NodeInVec get_parents() {
            return {parent};
        };

        ad_value_type get_value_type(){
            return parent.lock()->v_type;
        };

        ad_node_type get_node_type(){
            auto parent_type = parent.lock()->type;
            if(parent_type == INPUT
               or parent_type == SHARED_INPUT
               or parent_type == INPUT_DERIVED){
                return INPUT_DERIVED;
            } else if (parent_type == CONSTANT_DERIVED or parent_type == SYMBOLIC_INTEGER){
                return CONSTANT_DERIVED;
            } else {
                return CONSTANT;
            }
        };

        Shape get_shape(){
            return parent.lock()->shape;
        }

        unsigned short get_gradient_level(){
            return parent.lock()->grad_level;
        };

        NodeInVec get_arguments() {
            return NodeInVec {};
        }

        void throw_grad_type_error(){
            throw UnknownError({parent},
                               "Gradient message present, but parent is " + to_string(parent.lock()->type));
        }
    };

    class ElementwiseNary : public NaryOperator{
    public:
        ElementwiseNary(std::string const name,
                        GraphInPtr graph,
                        NodeInVec parents) :
                NaryOperator(name, graph, parents){
            this->parents.clear();
            shape = verify_elementwise_shapes(name, parents);
            for(int i=0;i<parents.size();i++){
                auto parent = parents[i].lock();
                if(parent->shape == shape or parent->is_scalar()){
                    this->parents.push_back(parents[i]);
                } else if(graph.lock()->broadcast == ad_implicit_broadcast::RAISE){
                    throw ImplicitBroadcast(name, parents);
                } else{
                    if(graph.lock()->broadcast == ad_implicit_broadcast::WARN){
                        auto msg = ImplicitBroadcast(name, parents);
                        std::cout << "WARNING:" << msg.get_message() << std::endl;
                    }
                    auto  op = std::make_shared<Broadcast>(this->graph, parents[i], shape);
                    this->parents.push_back(graph.lock()->derived_node(op));
                }
            }
        };
    };

    class ElementwiseBinary : public BinaryOperator{
    public:
        ElementwiseBinary(std::string const name,
                          GraphInPtr graph,
                          NodeInPtr parent1,
                          NodeInPtr parent2) :
                BinaryOperator(name, graph, parent1, parent2)
        {
            NodeInVec parents = get_parents();
            shape = verify_elementwise_shapes(name, {parents});
            for(int i=0;i<2;i++){
                auto parent = parents[i].lock();
                if(parent->shape == shape or parent->is_scalar()){
                    continue;
                } else if(graph.lock()->broadcast == ad_implicit_broadcast::RAISE){
                    throw ImplicitBroadcast(name, parents);
                } else{
                    if(graph.lock()->broadcast == ad_implicit_broadcast::WARN){
                        auto msg = ImplicitBroadcast(name, parents);
                        std::cout << "WARNING:" << msg.get_message() << std::endl;
                    }
                    auto  op = std::make_shared<Broadcast>(this->graph, parents[i], shape);
                    if(i == 0){
                        this->parent1 = graph.lock()->derived_node(op);
                    } else {
                        this->parent2 = graph.lock()->derived_node(op);
                    }
                }
            }
        }
    };

    class Add : public ElementwiseNary {
    public:
        Add(GraphInPtr graph, NodeInVec parents) :
                ElementwiseNary("Add", graph, parents)
        {}

        Add(GraphInPtr graph, NodeInPtr parent1, NodeInPtr parent2) :
                Add(graph, {parent1, parent2})
        {}

        void generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages) {
            auto graph = this->graph.lock();

            // Check for any incoming messages
            if(messages.find(current) == messages.end()){
                return;
            }

            // Get the gradient with respect to this node
            auto my_grad = graph->nodes[messages[current]];
            update_grad_name(my_grad, current);

            // Check for any surprises
            if(all_parent_const()){
                throw_grad_type_error();
            }

            for(int i=0;i<parents.size();i++){
                auto parent = parents[i].lock();
                if (not parent->is_constant()){
                    // Node computes f = p_1 + p_2 + ... + p_n
                    // => dE/dp_i = dE/df
                    auto parent_grad = my_grad;
                    send_grad_message(graph, parent->id, parent_grad->id, messages);
                }
            }
        };
    };

    Node add(std::vector<Node> nodes){
        auto graph = nodes[0].graph.lock();
        NodeInVec nodes_in;
        for(int i=0;i<nodes.size();i++){
            nodes_in.push_back(graph->nodes[nodes[i].id]);
        }
        auto op = std::make_shared<Add>(graph, nodes_in);
        return Node(graph, graph->derived_node(op).lock()->id);
    };

    Node add(Node node1, Node node2){
        return add({node1, node2});
    };

    Node operator+(Node node1, Node node2){
        return add({node1, node2});
    };

    void Broadcast::generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages) {
        auto graph = this->graph.lock();

        // Check for any incoming messages
        if(messages.find(current) == messages.end()){
            return;
        }

        // Get the gradient with respect to this node
        auto my_grad = graph->nodes[messages[current]];
        update_grad_name(my_grad, current);

        // Check for any surprises
        auto parent = this->parent.lock();
        if(parent->is_constant()){
            throw UnknownError({parent},
                               "Gradient message present, but parents are " +
                               to_string(parent->type));
        }

        // Node computes f = broadcast(p, shape)
        // => dE/dp = dE/df.sum(broadcasted axes)
        auto op = std::make_shared<Sum>(graph, my_grad, this->get_axes());
        auto parent_grad = graph->derived_node(op).lock();
        parent_grad->name = "Grad msg " + std::to_string(current) + " -> " + std::to_string(parent->id);
        send_grad_message(graph, parent->id, parent_grad->id, messages);
    }

    void Sum::generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages) {
        auto graph = this->graph.lock();

        // Check for any incoming messages
        if(messages.find(current) == messages.end()){
            return;
        }

        // Get the gradient with respect to this node
        auto my_grad = graph->nodes[messages[current]];
        update_grad_name(my_grad, current);

        // Check for any surprises
        auto parent = this->parent.lock();
        if(parent->is_constant()){
            throw UnknownError({parent},
                               "Gradient message present, but parents are " +
                               to_string(parent->type));
        }

        // Node computes f = p.sum(axes)
        // => dE/dp = broadcast(dE/df, p.shape)
        auto op = std::make_shared<Broadcast>(this->graph, my_grad, parent->shape);
        auto parent_grad = graph->derived_node(op).lock();
        parent_grad->name = "Grad msg " + std::to_string(current) + " -> " + std::to_string(parent->id);
        send_grad_message(graph, parent->id, parent_grad->id, messages);
    }

    class Neg : public UnaryOperator {
    public:
        Neg(GraphInPtr graph, NodeInPtr parent) :
                UnaryOperator("Neg", graph, parent)
        {};

        void generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages) {
            auto graph = this->graph.lock();

            // Get the gradient with respect to this node
            auto my_grad = graph->nodes[messages[current]];
            update_grad_name(my_grad, current);

            // Check for any surprises
            auto parent = this->parent.lock();
            if(parent->is_constant()){
                throw UnknownError({parent},
                                   "Gradient message present, but parents are " +
                                   to_string(parent->type));
            }

            // Node computes f = -p
            // => dE/dp = - dE/df
            auto op = std::make_shared<Neg>(graph, my_grad);
            auto parent_grad = graph->derived_node(op).lock();
            parent_grad->name = "Grad msg " + std::to_string(current) + " -> " + std::to_string(parent->id);
            send_grad_message(graph, parent->id, parent_grad->id, messages);
        };
    };

    Node neg(Node node){
        auto graph = node.graph.lock();
        auto arg = graph->nodes[node.id];
        auto op = std::make_shared<Neg>(node.graph, arg);
        return Node(node.graph, graph->derived_node(op).lock()->id);
    }

    Node operator-(Node node){
        return neg(node);
    }

    Node operator-(Node node1, Node node2){
        return add({node1, neg(node2)});
    }

    class Mul : public ElementwiseNary {
    public:
        Mul(GraphInPtr graph, NodeInVec parents) :
                ElementwiseNary("Mul", graph, parents)
        {};

        Mul(GraphInPtr graph, NodeInPtr p1, NodeInPtr p2) :
                ElementwiseNary("Mul", graph, {p1, p2})
        {};

        void generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages);
    };

    Node mul(std::vector<Node> nodes){
        auto graph = nodes[0].graph.lock();
        NodeInVec nodes_in;
        for(int i=0;i<nodes.size();i++){
            nodes_in.push_back(graph->nodes[nodes[i].id]);
        }
        auto op = std::make_shared<Mul>(graph, nodes_in);
        return Node(graph, graph->derived_node(op).lock()->id);
    };

    Node mul(Node node1, Node node2){
        return mul({node1, node2});
    }

    Node operator*(Node node1, Node node2){
        return mul({node1, node2});
    };

    class Div : public UnaryOperator {
    public:
        Div(GraphInPtr graph, NodeInPtr parent) :
                UnaryOperator("Div", graph, parent)
        {};

        void generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages);
    };

    Node div(Node node1, Node node2){
        auto graph = node2.graph.lock();
        auto op = std::make_shared<Div>(node2.graph, graph->nodes[node2.id]);
        auto node2_div = graph->derived_node(op);
        return mul({node1, Node(graph, node2_div.lock()->id)});
    }

    Node operator/(Node node1, Node node2){
        return div(node1, node2);
    };

    class Square : public UnaryOperator {
    public:
        Square(GraphInPtr graph, NodeInPtr parent) :
                UnaryOperator("Square", graph, parent)
        {};

        void generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages);
    };

    Node square(Node node){
        auto graph = node.graph.lock();
        auto op = std::make_shared<Square>(node.graph, graph->nodes[node.id]);
        return Node(node.graph, graph->derived_node(op).lock()->id);
    }

    void Mul::generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages) {
        auto graph = this->graph.lock();

        // Check for any incoming messages
        if(messages.find(current) == messages.end()){
            return;
        }

        // Get the gradient with respect to this node
        auto my_grad = graph->nodes[messages[current]];
        update_grad_name(my_grad, current);

        // Check for any surprises
        if(all_parent_const()){
            throw_grad_type_error();
        }

        if(parents.size() == 2){
            // Special case when only two parents
            for(int i=0;i<2;i++){
                auto parent = parents[i].lock();
                auto other_parent = parents[1-i].lock();
                if(not parent->is_constant()) {
                    // Node computes f = p_1 * p_2
                    // => dE/dp_i = dE/df * p_{1-i}
                    auto op = std::make_shared<Mul>(graph, my_grad, other_parent);
                    auto parent_grad = graph->derived_node(op).lock();
                    if(parent_grad->type == CONSTANT){
                        parent_grad->value = my_grad->value * other_parent->value;
                    }
                    send_grad_message(graph, parent->id, parent_grad->id, messages);
                }
            }
        } else {
            auto this_node = graph->nodes[current];
            std::shared_ptr<Operator> op = std::make_shared<Mul>(graph, my_grad, my_grad);
            auto this_node_times_grad = graph->derived_node(op);
            for(int i=0;i<parents.size();i++){
                auto parent = parents[i].lock();
                if(not parent->is_constant()) {
                    // Node computes f = p_1 * p_2 * p_3 .. * p_n
                    // => dE/dp_i = dE/df * f / p_i
                    std::shared_ptr<Operator> op = std::make_shared<Div>(graph, parent);
                    auto parent_inv = graph->derived_node(op);
                    op = std::make_shared<Mul>(graph, this_node_times_grad, parent_inv);
                    auto parent_grad = graph->derived_node(op).lock();
                    send_grad_message(graph, parent->id, parent_grad->id, messages);
                }
            }
        }
    }

    void Div::generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages) {
        auto graph = this->graph.lock();

        // Check for any incoming messages
        if(messages.find(current) == messages.end()){
            return;
        }

        // Get the gradient with respect to this node
        auto my_grad = graph->nodes[messages[current]];
        update_grad_name(my_grad, current);

        // Check for any surprises
        auto parent = this->parent.lock();
        if(parent->is_constant()) {
            throw_grad_type_error();
        }

        // Node computes f = p^(-1)
        // => dE/dp = - dE * (p^2)^-1
        auto parent_node = graph->nodes[parent->id];
        std::shared_ptr<Operator> op = std::make_shared<Square>(graph, parent);
        auto parent_sqr = graph->derived_node(op);
        op = std::make_shared<Div>(graph, parent_sqr);
        auto parent_sqr_inv = graph->derived_node(op);
        op = std::make_shared<Mul>(graph, my_grad, parent_sqr_inv);
        auto times_grad = graph->derived_node(op);
        op = std::make_shared<Neg>(graph, times_grad);
        auto parent_grad = graph->derived_node(op).lock();
        parent_grad->name = "Grad msg " + std::to_string(current) + " -> " + std::to_string(parent->id);
        send_grad_message(graph, parent->id, parent_grad->id, messages);
    }

    void Square::generate_gradients(size_t current, std::unordered_map<size_t, size_t> &messages) {
        auto graph = this->graph.lock();

        // Check for any incoming messages
        if(messages.find(current) == messages.end()){
            return;
        }

        // Get the gradient with respect to this node
        auto my_grad = graph->nodes[messages[current]];
        update_grad_name(my_grad, current);

        // Check for any surprises
        auto parent = this->parent.lock();
        if(parent->is_constant()) {
            throw_grad_type_error();
        }

        // Node computes f = p^2
        // => dE/dp = 2 * dE * p
        auto parent_node = graph->nodes[parent->id];
        auto two = graph->nodes[graph->constant_node(af::constant(2.0, 1)).id];
        two->grad_level = my_grad->grad_level;
        auto op = std::make_shared<Mul>(graph, NodeInVec {my_grad, two, parent});
        auto parent_grad = graph->derived_node(op).lock();
        parent_grad->name = "Grad msg " + std::to_string(current) + " -> " + std::to_string(parent->id);
        send_grad_message(graph, parent->id, parent_grad->id, messages);
    }

    void send_grad_message(std::shared_ptr<GraphInternal> graph,
                           size_t target_id, size_t msg_id,
                           std::unordered_map<size_t, size_t> &messages){
        // Add it to the already existing message to the parent on make this the first
        if (messages.find(target_id) != messages.end()) {
            auto prev_msg = graph->nodes[messages[target_id]];
            auto op = std::make_shared<Add>(graph, prev_msg, graph->nodes[msg_id]);
            messages[target_id] = graph->derived_node(op).lock()->id;
        } else {
            messages[target_id] = msg_id;
        }
    }
}

#endif //METADIFF_OPERATORS_BASE_H

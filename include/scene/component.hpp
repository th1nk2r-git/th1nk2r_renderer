#ifndef COMPONENT_HPP
#define COMPONENT_HPP

class Component {
public:
    Component(const Component&) = delete;
    auto operator=(const Component&) -> Component& = delete;
    Component(Component&&) = delete;
    auto operator=(Component&&) -> Component& = delete;

    virtual ~Component() = 0;

protected:
    Component() = default;
};

#endif

#ifndef PRIMITIVE_HPP
#define PRIMITIVE_HPP

#include "resource/gpu/resource_id.hpp"

class Material;
class Mesh;

struct Primitive {
    ResourceId<Mesh> mesh;
    ResourceId<Material> material;
};

#endif

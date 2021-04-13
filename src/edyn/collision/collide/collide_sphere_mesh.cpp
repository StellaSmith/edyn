#include "edyn/collision/collide.hpp"
#include "edyn/util/aabb_util.hpp"

namespace edyn {

collision_result collide(const sphere_shape &shA, const mesh_shape &shB, 
                         const collision_context &ctx) {
    auto result = collision_result{};

    // Sphere position in mesh's space.
    auto posA_in_B = rotate(conjugate(ctx.ornB), ctx.posA - ctx.posB);
    auto ornA_in_B = conjugate(ctx.ornB) * ctx.ornA;

    auto aabb = shape_aabb(shA, posA_in_B, ornA_in_B);
    shB.trimesh->visit(aabb, [&] (size_t tri_idx, const triangle_vertices &vertices) {
        if (result.num_points == max_contacts) {
            return;
        }

        auto tri = triangle_shape{};
        tri.vertices = vertices;

        for (int i = 0; i < 3; ++i) {
            tri.is_concave_edge[i] = shB.trimesh->is_concave_edge[tri_idx * 3 + i];
            tri.cos_angles[i] = shB.trimesh->cos_angles[tri_idx * 3 + i];
        }

        tri.update_computed_properties();

        collide_sphere_triangle(shA, posA_in_B, ornA_in_B, tri, ctx.threshold, result);
    });

    return result;
}

collision_result collide(const mesh_shape &shA, const sphere_shape &shB,
                         const collision_context &ctx) {
    return swap_collide(shA, shB, ctx);
}

}

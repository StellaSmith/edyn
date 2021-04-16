#include "edyn/dynamics/solver.hpp"
#include "edyn/constraints/contact_constraint.hpp"
#include "edyn/dynamics/row_cache.hpp"
#include "edyn/sys/integrate_linacc.hpp"
#include "edyn/sys/integrate_linvel.hpp"
#include "edyn/sys/integrate_angvel.hpp"
#include "edyn/sys/apply_gravity.hpp"
#include "edyn/sys/update_aabbs.hpp"
#include "edyn/sys/update_rotated_meshes.hpp"
#include "edyn/comp/orientation.hpp"
#include "edyn/constraints/constraint.hpp"
#include "edyn/comp/constraint_row.hpp"
#include "edyn/comp/mass.hpp"
#include "edyn/comp/inertia.hpp"
#include "edyn/comp/linvel.hpp"
#include "edyn/comp/angvel.hpp"
#include "edyn/comp/delta_linvel.hpp"
#include "edyn/comp/delta_angvel.hpp"
#include "edyn/util/constraint_util.hpp"
#include <entt/entity/fwd.hpp>
#include <entt/entt.hpp>
#include <type_traits>

namespace edyn {

static
scalar solve(constraint_row &row) {
    auto delta_relvel = dot(row.J[0], *row.dvA) + 
                        dot(row.J[1], *row.dwA) +
                        dot(row.J[2], *row.dvB) +
                        dot(row.J[3], *row.dwB);
    auto delta_impulse = (row.rhs - delta_relvel) * row.eff_mass;
    auto impulse = row.impulse + delta_impulse;

    if (impulse < row.lower_limit) {
        delta_impulse = row.lower_limit - row.impulse;
        row.impulse = row.lower_limit;
    } else if (impulse > row.upper_limit) {
        delta_impulse = row.upper_limit - row.impulse;
        row.impulse = row.upper_limit;
    } else {
        row.impulse = impulse;
    }

    return delta_impulse;
}

void update_inertia(entt::registry &registry) {
    auto view = registry.view<orientation, inertia_inv, inertia_world_inv, dynamic_tag>();
    view.each([] (orientation& orn, inertia_inv &inv_I, inertia_world_inv &inv_IW) {
        auto basis = to_matrix3x3(orn);
        inv_IW = basis * inv_I * transpose(basis);
    });
}

template<typename C>
void update_impulse(entt::registry &registry, row_cache &cache, size_t &con_idx, size_t &row_idx) {
    auto con_view = registry.view<C, constraint_impulse>();
    con_view.each([&] (entt::entity entity, C &con, constraint_impulse &imp) {
        auto num_rows = cache.con_num_rows[con_idx];
        for (size_t i = 0; i < num_rows; ++i) {
            imp.values[i] = cache.con_rows[row_idx + i].impulse;
        }

        row_idx += num_rows;
        ++con_idx;
    });
}

template<typename... Cs>
void update_impulse_tuple(std::tuple<Cs...>, entt::registry &registry, row_cache &cache, size_t &con_idx, size_t &row_idx) {
    (update_impulse<Cs>(registry, cache, con_idx, row_idx), ...);
}

void update_impulses(entt::registry &registry, row_cache &cache) {
    size_t con_idx = 0;
    size_t row_idx = 0;
    update_impulse_tuple(constraints_tuple_t{}, registry, cache, con_idx, row_idx);
}

solver::solver(entt::registry &registry) 
    : m_registry(&registry)
{
    registry.on_construct<linvel>().connect<&entt::registry::emplace<delta_linvel>>();
    registry.on_construct<angvel>().connect<&entt::registry::emplace<delta_angvel>>();
}

solver::~solver() = default;

void solver::update(scalar dt) {
    auto &registry = *m_registry;

    m_row_cache.clear();

    // Apply forces and acceleration.
    integrate_linacc(registry, dt);
    apply_gravity(registry, dt);

    // Setup constraints.
    prepare_constraints(registry, m_row_cache, dt);

    EDYN_ASSERT(m_row_cache.con_rows.size() == m_row_cache.con_rows.size());

    // Solve constraints.
    for (uint32_t i = 0; i < iterations; ++i) {
        // Prepare constraints for iteration.
        iterate_constraints(registry, m_row_cache, dt);

        // Solve rows.
        for (auto &row : m_row_cache.con_rows) {
            auto delta_impulse = solve(row);
            apply_impulse(delta_impulse, row);
        }
    }

    // Apply constraint velocity correction.
    auto vel_view = registry.view<linvel, angvel, delta_linvel, delta_angvel, dynamic_tag>();
    vel_view.each([] (linvel &v, angvel &w, delta_linvel &dv, delta_angvel &dw) {
        v += dv;
        w += dw;
        dv = vector3_zero;
        dw = vector3_zero;
    });

    // Assign applied impulses.
    update_impulses(registry, m_row_cache);

    // Integrate velocities to obtain new transforms.
    integrate_linvel(registry, dt);
    integrate_angvel(registry, dt);

    // Update AABBs after transforms change.
    update_aabbs(registry);
    
    // Update rotated vertices of convex meshes after rotations change.
    update_rotated_meshes(registry);

    // Update world-space moment of inertia.
    update_inertia(registry);
}

}

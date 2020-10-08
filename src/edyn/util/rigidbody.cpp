#include <entt/entt.hpp>
#include "edyn/util/rigidbody.hpp"
#include "edyn/comp/tag.hpp"
#include "edyn/comp/position.hpp"
#include "edyn/comp/orientation.hpp"
#include "edyn/comp/linvel.hpp"
#include "edyn/comp/angvel.hpp"
#include "edyn/comp/linacc.hpp"
#include "edyn/comp/mass.hpp"
#include "edyn/comp/inertia.hpp"
#include "edyn/comp/material.hpp"
#include "edyn/comp/present_position.hpp"
#include "edyn/comp/present_orientation.hpp"
#include "edyn/comp/collision_filter.hpp"

namespace edyn {

void rigidbody_def::update_inertia() {
    std::visit([&] (auto &&s) {
        inertia = s.inertia(mass);
    }, *shape_opt);
}

void make_rigidbody(entt::entity entity, entt::registry &registry, const rigidbody_def &def) {    
    registry.assign<position>(entity, def.position);
    registry.assign<orientation>(entity, def.orientation);

    if (def.kind == rigidbody_kind::rb_dynamic) {
        EDYN_ASSERT(def.mass > 0);
        registry.assign<mass>(entity, def.mass);
        registry.assign<mass_inv>(entity, 1 / def.mass);
        registry.assign<inertia>(entity, def.inertia);
        EDYN_ASSERT(def.inertia > vector3_zero);
        auto &invI = registry.assign<inertia_inv>(entity, 1 / def.inertia);
        registry.assign<inertia_world_inv>(entity, diagonal(invI));
    } else {
        registry.assign<mass>(entity, EDYN_SCALAR_MAX);
        registry.assign<mass_inv>(entity, 0);
        registry.assign<inertia>(entity, vector3_max);
        registry.assign<inertia_inv>(entity, vector3_zero);
        registry.assign<inertia_world_inv>(entity, matrix3x3_zero);
    }

    if (def.kind == rigidbody_kind::rb_static) {
        registry.assign<linvel>(entity, vector3_zero);
        registry.assign<angvel>(entity, vector3_zero);
    } else {
        registry.assign<linvel>(entity, def.linvel);
        registry.assign<angvel>(entity, def.angvel);
    }

    if (def.kind == rigidbody_kind::rb_dynamic && def.gravity != vector3_zero) {
        registry.assign<linacc>(entity, def.gravity);
    }

    if (!def.sensor) {
        registry.assign<material>(entity, def.restitution, def.friction,
                                  def.stiffness, def.damping);
    }

    if (def.presentation) {
        registry.assign<present_position>(entity, def.position);
        registry.assign<present_orientation>(entity, def.orientation);
    }

    if (auto opt = def.shape_opt) {
        registry.assign<shape>(entity, *opt);
        registry.assign<AABB>(entity);
        auto &filter = registry.assign<collision_filter>(entity);
        filter.group = def.collision_group;
        filter.mask = def.collision_mask;
    }

    switch (def.kind) {
    case rigidbody_kind::rb_dynamic:
        registry.assign<dynamic_tag>(entity);
        break;
    case rigidbody_kind::rb_kinematic:
        registry.assign<kinematic_tag>(entity);
        break;
    case rigidbody_kind::rb_static:
        registry.assign<static_tag>(entity);
        break;
    }
}

entt::entity make_rigidbody(entt::registry &registry, const rigidbody_def &def) {
    auto ent = registry.create();
    make_rigidbody(ent, registry, def);
    return ent;
}


void rigidbody_set_mass(entt::registry &registry, entt::entity entity, scalar mass) {
    registry.replace<edyn::mass>(entity, mass);
    rigidbody_update_inertia(registry, entity);
}

void rigidbody_update_inertia(entt::registry &registry, entt::entity entity) {
    auto &mass = registry.get<edyn::mass>(entity);

    edyn::vector3 inertia;
    auto& shape = registry.get<edyn::shape>(entity);
    std::visit([&] (auto&& s) {
        inertia = s.inertia(mass);
    }, shape.var);
    registry.replace<edyn::inertia>(entity, inertia);
}

void rigidbody_apply_impulse(entt::registry &registry, entt::entity entity, 
                             const vector3 &impulse, const vector3 &rel_location) {
    auto &m_inv = registry.get<const mass_inv>(entity);
    auto &i_inv = registry.get<const inertia_world_inv>(entity);
    registry.get<linvel>(entity) += impulse * m_inv;
    registry.get<angvel>(entity) += i_inv * cross(rel_location, impulse);
}

void update_kinematic_position(entt::registry &registry, entt::entity entity, const vector3 &pos, scalar dt) {
    EDYN_ASSERT(registry.has<kinematic_tag>(entity));
    auto &curpos = registry.get<position>(entity);
    auto &vel = registry.get<linvel>(entity);
    vel = (pos - curpos) / dt;
    curpos = pos;
}

void update_kinematic_orientation(entt::registry &registry, entt::entity entity, const quaternion &orn, scalar dt) {
    EDYN_ASSERT(registry.has<kinematic_tag>(entity));
    auto &curorn = registry.get<orientation>(entity);
    auto q = normalize(conjugate(curorn) * orn);
    auto &vel = registry.get<angvel>(entity);
    vel = (quaternion_axis(q) * quaternion_angle(q)) / dt;
    curorn = orn;
}

void clear_kinematic_velocities(entt::registry &registry) {
    auto view = registry.view<kinematic_tag, linvel, angvel>();
    view.each([] ([[maybe_unused]] auto, linvel &v, angvel &w) {
        v = vector3_zero;
        w = vector3_zero;
    });
}

}

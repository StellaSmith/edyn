#include "edyn/collision/collide.hpp"
#include "edyn/math/math.hpp"
#include "edyn/math/matrix3x3.hpp"
#include "edyn/shapes/triangle_shape.hpp"

namespace edyn {

struct box_tri_separating_axis {
    vector3 dir;
    box_feature featureA;
    triangle_feature featureB;
    size_t feature_indexA;
    size_t feature_indexB;
    scalar distance;
};

void collide_box_triangle(
    const box_shape &box, const vector3 &box_pos, const quaternion &box_orn,
    const std::array<vector3, 3> box_axes, const triangle_shape &tri,
    scalar threshold, collision_result &result) {

    std::array<box_tri_separating_axis, 13> sep_axes;
    size_t axis_idx = 0;

    // Box faces.
    for (size_t i = 0; i < 3; ++i) {
        auto &axisA = box_axes[i];
        auto &axis = sep_axes[axis_idx];
        axis.featureA = box_feature::face;

        // Find which direction gives greatest penetration.
        triangle_feature neg_tri_feature, pos_tri_feature;
        size_t neg_tri_feature_index, pos_tri_feature_index;
        scalar neg_tri_proj, pos_tri_proj;
        get_triangle_support_feature(tri.vertices, box_pos, -axisA, 
                                     neg_tri_feature, neg_tri_feature_index, 
                                     neg_tri_proj, threshold);
        get_triangle_support_feature(tri.vertices, box_pos, axisA, 
                                     pos_tri_feature, pos_tri_feature_index, 
                                     pos_tri_proj, threshold);

        if (neg_tri_proj < pos_tri_proj) {
            axis.dir = -axisA;
            axis.feature_indexA = i * 2;
            axis.featureB = neg_tri_feature;
            axis.feature_indexB = neg_tri_feature_index;
            axis.distance = -(box.half_extents[i] + neg_tri_proj);
        } else {
            axis.dir = axisA;
            axis.feature_indexA = i * 2 + 1;
            axis.featureB = pos_tri_feature;
            axis.feature_indexB = pos_tri_feature_index;
            axis.distance = -(box.half_extents[i] + pos_tri_proj);
        }

        if (!tri.ignore_feature(axis.featureB, axis.feature_indexB, axis.dir)) {
            ++axis_idx;
        }
    }

    // Triangle face normal.
    {
        auto &axis = sep_axes[axis_idx++];
        axis.featureB = triangle_feature::face;
        axis.dir = tri.normal;

        box.support_feature(box_pos, box_orn, 
                            tri.vertices[0], -tri.normal, 
                            axis.featureA, axis.feature_indexA, 
                            axis.distance, threshold);
        // Make distance negative when penetrating.
        axis.distance *= -1;
    }

    // Edges.
    for (size_t i = 0; i < 3; ++i) {
        auto &axisA = box_axes[i];

        for (size_t j = 0; j < 3; ++j) {
            auto &axisB = tri.edges[j];
            auto &axis = sep_axes[axis_idx];
            axis.dir = cross(axisA, axisB);
            auto dir_len_sqr = length_sqr(axis.dir);

            if (dir_len_sqr <= EDYN_EPSILON) {
                continue;
            }

            axis.dir /= std::sqrt(dir_len_sqr);

            if (dot(box_pos - tri.vertices[j], axis.dir) < 0) {
                // Make it point towards A.
                axis.dir *= -1;
            }

            scalar projA, projB;
            box.support_feature(box_pos, box_orn, tri.vertices[j], -axis.dir, 
                                axis.featureA, axis.feature_indexA, 
                                projA, threshold);
            get_triangle_support_feature(tri.vertices, tri.vertices[j], axis.dir, 
                                         axis.featureB, axis.feature_indexB, 
                                         projB, threshold);
            axis.distance = -(projA + projB);

            if (!tri.ignore_feature(axis.featureB, axis.feature_indexB, axis.dir)) {
                ++axis_idx;
            }
        }
    }

    // Get axis with greatest distance.
    auto greatest_distance = -EDYN_SCALAR_MAX;
    size_t sep_axis_idx;

    for (size_t i = 0; i < axis_idx; ++i) {
        auto &sep_axis = sep_axes[i];
        
        if (sep_axis.distance > greatest_distance) {
            greatest_distance = sep_axis.distance;
            sep_axis_idx = i;
        }
    }

    auto &sep_axis = sep_axes[sep_axis_idx];

    // No collision.
    if (sep_axis.distance > threshold) {
        return;
    }

    if (sep_axis.featureA == box_feature::face && sep_axis.featureB == triangle_feature::face) {
        auto face_normal_in_B = box.get_face_normal(sep_axis.feature_indexA, box_orn);
        auto face_vertices = box.get_face(sep_axis.feature_indexA);
        std::array<vector3, 4> face_vertices_in_B;
        for (int i = 0; i < 4; ++i) {
            face_vertices_in_B[i] = box_pos + rotate(box_orn, face_vertices[i]);
        }

        size_t num_tri_vert_in_box_face = 0;

        // Check for triangle vertices inside box face.
        for (int i = 0; i < 3; ++i) {
            // Ignore vertices that are on a concave edge.
            if (tri.is_concave_vertex[i]) {
                continue;
            }

            if (point_in_quad(tri.vertices[i], face_vertices_in_B, face_normal_in_B)) {
                // Triangle vertex is inside box face.
                auto pivot_face = project_plane(tri.vertices[i], face_vertices_in_B[0], sep_axis.dir);
                auto pivotA = to_object_space(pivot_face, box_pos, box_orn);
                auto pivotB = tri.vertices[i];
                result.maybe_add_point({pivotA, pivotB, sep_axis.dir, sep_axis.distance});
                ++num_tri_vert_in_box_face;
            }
        }

        // Continue if not all triangle vertices are contained in face.
        size_t num_box_vert_in_tri_face = 0;

        if (num_tri_vert_in_box_face < 3) {
            // Look for box face vertices inside triangle face.
            for (int i = 0; i < 4; ++i) {
                if (point_in_triangle(tri.vertices, tri.normal, face_vertices_in_B[i])) {
                    auto pivotA = face_vertices[i];
                    auto pivotB = project_plane(face_vertices_in_B[i], tri.vertices[0], sep_axis.dir);
                    result.maybe_add_point({pivotA, pivotB, sep_axis.dir, sep_axis.distance});
                    ++num_box_vert_in_tri_face;
                }
            }
        }

        // Continue if not all box's face vertices are contained in the triangle.
        // Perform edge intersection tests.
        if (num_box_vert_in_tri_face < 4) {
            for (int i = 0; i < 4; ++i) {                    
                auto &a0 = face_vertices[i];
                auto &a1 = face_vertices[(i + 1) % 4];

                for (int j = 0; j < 3; ++j) {
                    // Ignore concave edges.
                    if (tri.is_concave_edge[j]) {
                        continue;
                    }

                    auto &b0 = tri.vertices[j];
                    auto &b1 = tri.vertices[(j + 1) % 3];

                    // Convert this into a 2D segment intersection problem in the box' space.
                    auto b0_in_A = to_object_space(b0, box_pos, box_orn);
                    auto b1_in_A = to_object_space(b1, box_pos, box_orn);
                    
                    vector2 p0, p1, q0, q1;

                    if (sep_axis.feature_indexA == 0 || sep_axis.feature_indexA == 1) { // X face
                        p0 = {a0.z, a0.y}; p1 = {a1.z, a1.y};
                        q0 = {b0_in_A.z, b0_in_A.y}; q1 = {b1_in_A.z, b1_in_A.y};
                    } else if (sep_axis.feature_indexA == 2 || sep_axis.feature_indexA == 3) { // Y face
                        p0 = {a0.x, a0.z}; p1 = {a1.x, a1.z};
                        q0 = {b0_in_A.x, b0_in_A.z}; q1 = {b1_in_A.x, b1_in_A.z};
                    } else { // if (sep_axis.feature_indexA == 4 || sep_axis.feature_indexA == 5) { // Z face
                        p0 = {a0.x, a0.y}; p1 = {a1.x, a1.y};
                        q0 = {b0_in_A.x, b0_in_A.y}; q1 = {b1_in_A.x, b1_in_A.y};
                    }

                    scalar s[2], t[2];
                    auto num_points = intersect_segments(p0, p1, q0, q1, s[0], t[0], s[1], t[1]);

                    for (size_t k = 0; k < num_points; ++k) {
                        auto pivotA = lerp(a0, a1, s[k]);
                        auto pivotB = lerp(b0, b1, t[k]);
                        result.maybe_add_point({pivotA, pivotB, sep_axis.dir, sep_axis.distance});
                    }
                }
            }
        }
    } else if (sep_axis.featureA == box_feature::face && sep_axis.featureB == triangle_feature::edge) {
        EDYN_ASSERT(!tri.is_concave_edge[sep_axis.feature_indexB]);
    
        auto face_normal_in_B = box.get_face_normal(sep_axis.feature_indexA, box_orn);
        auto face_vertices = box.get_face(sep_axis.feature_indexA);
        std::array<vector3, 4> face_vertices_in_B;
        for (int i = 0; i < 4; ++i) {
            face_vertices_in_B[i] = box_pos + rotate(box_orn, face_vertices[i]);
        }

        // Check if edge vertices are inside box face.
        vector3 edge_vertices[] = {tri.vertices[sep_axis.feature_indexB],
                                   tri.vertices[(sep_axis.feature_indexB + 1) % 3]};
        size_t num_edge_vert_in_box_face = 0;

        for (int i = 0; i < 2; ++i) {
            if (point_in_quad(edge_vertices[i], face_vertices_in_B, face_normal_in_B)) {
                // Edge's vertex is inside face.
                auto pivot_face = project_plane(edge_vertices[i], face_vertices_in_B[0], face_normal_in_B);
                auto pivotA = to_object_space(pivot_face, box_pos, box_orn);
                auto pivotB = edge_vertices[i];
                result.maybe_add_point({pivotA, pivotB, sep_axis.dir, sep_axis.distance});
                ++num_edge_vert_in_box_face;
            }
        }

        // If both vertices are not inside the face then perform edge intersection tests.
        if (num_edge_vert_in_box_face < 2) {
            for (int i = 0; i < 4; ++i) {
                auto &a0 = face_vertices[i];
                auto &a1 = face_vertices[(i + 1) % 4];
                auto e0_in_A = to_object_space(edge_vertices[0], box_pos, box_orn);
                auto e1_in_A = to_object_space(edge_vertices[1], box_pos, box_orn);
                
                vector2 p0, p1, q0, q1;

                if (sep_axis.feature_indexA == 0 || sep_axis.feature_indexA == 1) { // X face
                    p0 = {a0.z, a0.y}; p1 = {a1.z, a1.y};
                    q0 = {e0_in_A.z, e0_in_A.y}; q1 = {e1_in_A.z, e1_in_A.y};
                } else if (sep_axis.feature_indexA == 2 || sep_axis.feature_indexA == 3) { // Y face
                    p0 = {a0.x, a0.z}; p1 = {a1.x, a1.z};
                    q0 = {e0_in_A.x, e0_in_A.z}; q1 = {e1_in_A.x, e1_in_A.z};
                } else { // if (sep_axis.feature_indexA == 4 || sep_axis.feature_indexA == 5) { // Z face
                    p0 = {a0.x, a0.y}; p1 = {a1.x, a1.y};
                    q0 = {e0_in_A.x, e0_in_A.y}; q1 = {e1_in_A.x, e1_in_A.y};
                }

                scalar s[2], t[2];
                auto num_points = intersect_segments(p0, p1, q0, q1, s[0], t[0], s[1], t[1]);

                for (size_t k = 0; k < num_points; ++k) {
                    auto pivotA = lerp(a0, a1, s[k]);
                    auto pivotB = lerp(edge_vertices[0], edge_vertices[1], t[k]);
                    result.maybe_add_point({pivotA, pivotB, sep_axis.dir, sep_axis.distance});
                }
            }
        }
    } else if (sep_axis.featureA == box_feature::edge && sep_axis.featureB == triangle_feature::face) {
        // Check if edge vertices are inside triangle face.
        auto edge = box.get_edge(sep_axis.feature_indexA);
        auto edge_in_B = edge;
        size_t num_edge_vert_in_tri_face = 0;

        for (size_t i = 0; i < 2; ++i) {
            edge_in_B[i] = box_pos + rotate(box_orn, edge[i]);

            if (point_in_triangle(tri.vertices, tri.normal, edge_in_B[i])) {
                auto pivotA = edge[i];
                auto pivotB = project_plane(edge_in_B[i], tri.vertices[0], sep_axis.dir);
                result.maybe_add_point({pivotA, pivotB, sep_axis.dir, sep_axis.distance});
                num_edge_vert_in_tri_face = 0;
            }
        }

        // If both vertices are not inside the face then perform segment intersections.
        if (num_edge_vert_in_tri_face < 2) {
            auto &tri_origin = tri.vertices[0];
            auto tangent = normalize(tri.vertices[1] - tri.vertices[0]);
            auto bitangent = cross(tri.normal, tangent);
            auto tri_basis = matrix3x3_columns(tangent, tri.normal, bitangent);

            auto e0_in_tri = (edge_in_B[0] - tri_origin) * tri_basis;
            auto e1_in_tri = (edge_in_B[1] - tri_origin) * tri_basis;
            auto p0 = vector2{e0_in_tri.x, e0_in_tri.z};
            auto p1 = vector2{e1_in_tri.x, e1_in_tri.z};

            for (int i = 0; i < 3; ++i) {
                // Ignore concave edges.
                if (tri.is_concave_edge[i]) {
                    continue;
                }
                
                auto &v0 = tri.vertices[i];
                auto &v1 = tri.vertices[(i + 1) % 3];

                auto v0_in_tri = (v0 - tri_origin) * tri_basis; // multiply by transpose.
                auto v1_in_tri = (v1 - tri_origin) * tri_basis;

                auto q0 = vector2{v0_in_tri.x, v0_in_tri.z};
                auto q1 = vector2{v1_in_tri.x, v1_in_tri.z};

                scalar s[2], t[2];
                auto num_points = intersect_segments(p0, p1, q0, q1, s[0], t[0], s[1], t[1]);

                for (size_t k = 0; k < num_points; ++k) {
                    auto pivotA = lerp(edge[0], edge[1], s[k]);
                    auto pivotB = lerp(v0, v1, t[k]);
                    result.maybe_add_point({pivotA, pivotB, sep_axis.dir, sep_axis.distance});
                }
            }
        }
    } else if (sep_axis.featureA == box_feature::edge && sep_axis.featureB == triangle_feature::edge) {
        EDYN_ASSERT(!tri.is_concave_edge[sep_axis.feature_indexB]);
            
        scalar s[2], t[2];
        vector3 p0[2], p1[2];
        size_t num_points = 0;
        auto edgeA = box.get_edge(sep_axis.feature_indexA, box_pos, box_orn);
        vector3 edgeB[2];
        edgeB[0] = tri.vertices[sep_axis.feature_indexB];
        edgeB[1] = tri.vertices[(sep_axis.feature_indexB + 1) % 3];
        closest_point_segment_segment(edgeA[0], edgeA[1], edgeB[0], edgeB[1], 
                                    s[0], t[0], p0[0], p1[0], &num_points, 
                                    &s[1], &t[1], &p0[1], &p1[1]);

        for (size_t i = 0; i < num_points; ++i) {
            auto pivotA = to_object_space(p0[i], box_pos, box_orn);
            auto pivotB = p1[i]; // We're in the triangle's object space.
            result.maybe_add_point({pivotA, pivotB, sep_axis.dir, sep_axis.distance});
        }
    } else if (sep_axis.featureA == box_feature::face && sep_axis.featureB == triangle_feature::vertex) {
        // Ignore vertices that are on a concave edge.
        EDYN_ASSERT(!tri.is_concave_vertex[sep_axis.feature_indexB]);
        auto vertex = tri.vertices[sep_axis.feature_indexB];
        auto face_normal = box.get_face_normal(sep_axis.feature_indexA, box_orn);
        auto face_vertices = box.get_face(sep_axis.feature_indexA, box_pos, box_orn);

        if (point_in_quad(vertex, face_vertices, face_normal)) {
            auto vertex_proj = vertex - sep_axis.dir * sep_axis.distance;
            auto pivotA = to_object_space(vertex_proj, box_pos, box_orn);
            auto pivotB = vertex;
            result.maybe_add_point({pivotA, pivotB, sep_axis.dir, sep_axis.distance});
        }
    } else if (sep_axis.featureA == box_feature::vertex && sep_axis.featureB == triangle_feature::face) {
        auto pivotA = box.get_vertex(sep_axis.feature_indexA);
        auto pivotB = box_pos + rotate(box_orn, pivotA) - tri.normal * sep_axis.distance;
        if (point_in_triangle(tri.vertices, tri.normal, pivotB)) {
            result.maybe_add_point({pivotA, pivotB, sep_axis.dir, sep_axis.distance});
        }
    }
}

}

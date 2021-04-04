#ifndef EDYN_SHAPES_CONVEX_MESH_HPP
#define EDYN_SHAPES_CONVEX_MESH_HPP

#include <array>
#include <vector>
#include <cstdint>
#include "edyn/math/vector3.hpp"
#include "edyn/config/config.h"

namespace edyn {

struct convex_mesh {
    std::vector<vector3> vertices;

    // Vertex indices of all faces.
    std::vector<uint16_t> indices;

    // Each subsequent pair of integers represents the indices of the two 
    // vertices of an edge in the `vertices` array.
    std::vector<uint16_t> edges;

    // Each subsequent pair of integers represents the index of the first
    // vertex of a face in the `indices` array and the number of vertices
    // in the face.
    std::vector<uint16_t> faces;

    // Face normals.
    std::vector<vector3> normals;

    size_t num_faces() const {
        EDYN_ASSERT(faces.size() % 2 == 0);
        return faces.size() / 2;
    }

    /**
     * @brief Returns the index of the first vertex of a face.
     * @param face_idx Face index.
     * @return Vertex index of the first vertex in the face.
     */
    uint16_t first_vertex_index(size_t face_idx) const {
        auto face_index_idx = face_idx * 2;
        EDYN_ASSERT(face_index_idx < faces.size());
        auto index_idx = faces[face_index_idx];
        EDYN_ASSERT(index_idx < indices.size());
        return indices[index_idx];
    }

    /**
     * @brief Returns the number of vertices on a face.
     * @param face_idx Face index.
     * @return Number of vertices on the face.
     */
    uint16_t vertex_count(size_t face_idx) const {
        auto face_count_idx = face_idx * 2 + 1;
        EDYN_ASSERT(face_count_idx < faces.size());
        return faces[face_count_idx];
    }
    
    void calculate_normals();

    void calculate_edges();
};

}

#endif // EDYN_SHAPES_CONVEX_MESH_HPP
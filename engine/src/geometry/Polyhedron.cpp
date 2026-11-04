#include "woodyn/geometry/Polyhedron.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace woodyn::geometry {

glm::vec3 ConvexPolyhedron::vertexCentroid() const {
    glm::vec3 sum{0.0f};
    for (const auto& v : vertices) {
        sum += v;
    }
    return vertices.empty() ? sum : sum / static_cast<float>(vertices.size());
}

float ConvexPolyhedron::volume() const {
    // Sum of signed tetrahedra from an interior point (faces must be outward CCW).
    const glm::vec3 c = vertexCentroid();
    float total = 0.0f;
    for (const auto& face : faces) {
        for (std::size_t i = 1; i + 1 < face.size(); ++i) {
            const glm::vec3 a = vertices[face[0]] - c;
            const glm::vec3 b = vertices[face[i]] - c;
            const glm::vec3 d = vertices[face[i + 1]] - c;
            total += glm::dot(a, glm::cross(b, d)) / 6.0f;
        }
    }
    return total;
}

ConvexPolyhedron extrudeConvexPolygon(std::span<const glm::vec2> polygon, float depth) {
    if (polygon.size() < 3) {
        throw std::invalid_argument("extrudeConvexPolygon: polygon needs at least 3 points");
    }
    const auto n = static_cast<std::uint32_t>(polygon.size());
    const float hz = depth * 0.5f;

    ConvexPolyhedron poly;
    poly.vertices.reserve(2 * n);
    for (const auto& p : polygon) {
        poly.vertices.emplace_back(p.x, p.y, hz); // front: indices [0, n)
    }
    for (const auto& p : polygon) {
        poly.vertices.emplace_back(p.x, p.y, -hz); // back: indices [n, 2n)
    }

    std::vector<std::uint32_t> front;
    std::vector<std::uint32_t> back;
    for (std::uint32_t i = 0; i < n; ++i) {
        front.push_back(i);
        back.push_back(n + (n - 1 - i));
    }
    poly.faces.push_back(std::move(front));
    poly.faces.push_back(std::move(back));

    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t j = (i + 1) % n;
        poly.faces.push_back({n + i, n + j, j, i});
    }

    orientFacesOutward(poly);
    return poly;
}

ConvexPolyhedron makePyramid(std::span<const glm::vec2> basePolygonXZ, float height) {
    if (basePolygonXZ.size() < 3) {
        throw std::invalid_argument("makePyramid: base needs at least 3 points");
    }
    const auto n = static_cast<std::uint32_t>(basePolygonXZ.size());
    const float hy = height * 0.5f;

    ConvexPolyhedron poly;
    for (const auto& p : basePolygonXZ) {
        poly.vertices.emplace_back(p.x, -hy, p.y);
    }
    const std::uint32_t apex = n;
    poly.vertices.emplace_back(0.0f, hy, 0.0f);

    std::vector<std::uint32_t> base;
    for (std::uint32_t i = 0; i < n; ++i) {
        base.push_back(i);
    }
    poly.faces.push_back(std::move(base));
    for (std::uint32_t i = 0; i < n; ++i) {
        poly.faces.push_back({i, (i + 1) % n, apex});
    }

    orientFacesOutward(poly);
    return poly;
}

std::vector<glm::vec2> circlePolygon(float radius, int segments) {
    std::vector<glm::vec2> pts;
    pts.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        pts.emplace_back(radius * std::cos(a), radius * std::sin(a));
    }
    return pts;
}

void orientFacesOutward(ConvexPolyhedron& poly) {
    const glm::vec3 interior = poly.vertexCentroid();
    for (auto& face : poly.faces) {
        // Newell normal is robust for planar polygons of any size.
        glm::vec3 normal{0.0f};
        glm::vec3 center{0.0f};
        for (std::size_t i = 0; i < face.size(); ++i) {
            const glm::vec3& a = poly.vertices[face[i]];
            const glm::vec3& b = poly.vertices[face[(i + 1) % face.size()]];
            normal.x += (a.y - b.y) * (a.z + b.z);
            normal.y += (a.z - b.z) * (a.x + b.x);
            normal.z += (a.x - b.x) * (a.y + b.y);
            center += a;
        }
        center /= static_cast<float>(face.size());
        if (glm::dot(normal, center - interior) < 0.0f) {
            std::reverse(face.begin(), face.end());
        }
    }
}

void rotateZtoY(ConvexPolyhedron& poly) {
    // (x, y, z) -> (x, z, -y): proper rotation (det = +1), keeps winding valid.
    for (auto& v : poly.vertices) {
        v = glm::vec3(v.x, v.z, -v.y);
    }
}

TriangleMesh triangulate(std::span<const ConvexPolyhedron> parts) {
    TriangleMesh mesh;
    for (const auto& part : parts) {
        const auto base = static_cast<std::uint32_t>(mesh.positions.size());
        mesh.positions.insert(mesh.positions.end(), part.vertices.begin(), part.vertices.end());
        for (const auto& face : part.faces) {
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                mesh.indices.push_back(base + face[0]);
                mesh.indices.push_back(base + face[i]);
                mesh.indices.push_back(base + face[i + 1]);
            }
        }
    }
    return mesh;
}

namespace {

struct FaceKey {
    std::size_t part;
    std::size_t face;
    glm::vec3 center;
    glm::vec3 normal; // unnormalized Newell normal
    float area;
};

FaceKey makeKey(const ConvexPolyhedron& poly, std::size_t partIndex, std::size_t faceIndex) {
    const auto& face = poly.faces[faceIndex];
    glm::vec3 center{0.0f};
    glm::vec3 normal{0.0f};
    for (std::size_t i = 0; i < face.size(); ++i) {
        const glm::vec3& a = poly.vertices[face[i]];
        const glm::vec3& b = poly.vertices[face[(i + 1) % face.size()]];
        normal += glm::cross(a, b);
        center += a;
    }
    center /= static_cast<float>(face.size());
    return {partIndex, faceIndex, center, normal, glm::length(normal) * 0.5f};
}

} // namespace

TriangleMesh triangulateSurface(std::span<const ConvexPolyhedron> parts, float epsilon) {
    std::vector<FaceKey> keys;
    for (std::size_t p = 0; p < parts.size(); ++p) {
        for (std::size_t f = 0; f < parts[p].faces.size(); ++f) {
            keys.push_back(makeKey(parts[p], p, f));
        }
    }
    std::vector<bool> internal(keys.size(), false);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            if (keys[i].part == keys[j].part) {
                continue;
            }
            const bool sameCenter = glm::length(keys[i].center - keys[j].center) < epsilon;
            const bool sameArea = std::abs(keys[i].area - keys[j].area) <
                                  epsilon * std::max(1.0f, keys[i].area) + 1e-12f;
            const bool opposite = glm::dot(keys[i].normal, keys[j].normal) < 0.0f;
            if (sameCenter && sameArea && opposite) {
                internal[i] = true;
                internal[j] = true;
            }
        }
    }

    TriangleMesh mesh;
    std::size_t k = 0;
    for (const auto& part : parts) {
        const auto base = static_cast<std::uint32_t>(mesh.positions.size());
        mesh.positions.insert(mesh.positions.end(), part.vertices.begin(), part.vertices.end());
        for (const auto& face : part.faces) {
            if (!internal[k++]) {
                for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                    mesh.indices.push_back(base + face[0]);
                    mesh.indices.push_back(base + face[i]);
                    mesh.indices.push_back(base + face[i + 1]);
                }
            }
        }
    }
    return mesh;
}

} // namespace woodyn::geometry

#include "woodyn/geometry/RoundedMesh.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace woodyn::geometry {
namespace {

constexpr float kHalfPi = glm::half_pi<float>();

glm::vec2 rotateCCW(const glm::vec2& v, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

glm::vec2 outwardNormal(const glm::vec2& edgeDir) { return {edgeDir.y, -edgeDir.x}; }

int arcSegments(float angle, int segmentsPer90) {
    return std::max(1, static_cast<int>(std::ceil(angle / kHalfPi * static_cast<float>(segmentsPer90))));
}

/// Adds a triangle, flipping it if needed so its winding agrees with the
/// supplied vertex normals (all generators rely on this for orientation).
void addTriangle(RenderMesh& m, const glm::vec3& p0, const glm::vec3& n0, const glm::vec3& p1,
                 const glm::vec3& n1, const glm::vec3& p2, const glm::vec3& n2) {
    const glm::vec3 g = glm::cross(p1 - p0, p2 - p0);
    if (glm::dot(g, g) < 1e-24f) {
        return; // degenerate
    }
    const auto base = static_cast<std::uint32_t>(m.positions.size());
    const bool flip = glm::dot(g, n0 + n1 + n2) < 0.0f;
    m.positions.push_back(p0);
    m.normals.push_back(n0);
    if (flip) {
        m.positions.push_back(p2);
        m.normals.push_back(n2);
        m.positions.push_back(p1);
        m.normals.push_back(n1);
    } else {
        m.positions.push_back(p1);
        m.normals.push_back(n1);
        m.positions.push_back(p2);
        m.normals.push_back(n2);
    }
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2});
}

void addQuad(RenderMesh& m, const glm::vec3& a, const glm::vec3& na, const glm::vec3& b,
             const glm::vec3& nb, const glm::vec3& c, const glm::vec3& nc, const glm::vec3& d,
             const glm::vec3& nd) {
    addTriangle(m, a, na, b, nb, c, nc);
    addTriangle(m, a, na, c, nc, d, nd);
}

glm::vec3 slerpDir(const glm::vec3& a, const glm::vec3& b, float t) {
    const float cosA = std::clamp(glm::dot(a, b), -1.0f, 1.0f);
    const float angle = std::acos(cosA);
    if (angle < 1e-5f) {
        return a;
    }
    const float s = std::sin(angle);
    return glm::normalize(a * (std::sin((1.0f - t) * angle) / s) + b * (std::sin(t * angle) / s));
}

/// Appends the fillet arc that replaces corner p1 (between p0 -> p1 -> p2).
void appendFillet(std::vector<ProfilePoint>& out, const glm::vec2& p0, const glm::vec2& p1,
                  const glm::vec2& p2, float radius, int segmentsPer90) {
    const glm::vec2 e1 = glm::normalize(p1 - p0);
    const glm::vec2 e2 = glm::normalize(p2 - p1);
    const float turn = std::acos(std::clamp(glm::dot(e1, e2), -1.0f, 1.0f));
    const float interior = glm::pi<float>() - turn;
    const float t = radius / std::tan(interior * 0.5f);
    const glm::vec2 n1 = outwardNormal(e1);
    const glm::vec2 center = (p1 - e1 * t) - n1 * radius;
    const int segs = arcSegments(turn, segmentsPer90);
    for (int k = 0; k <= segs; ++k) {
        const glm::vec2 n = rotateCCW(n1, turn * static_cast<float>(k) / static_cast<float>(segs));
        out.push_back({center + n * radius, n, 1.0f});
    }
}

} // namespace

float RenderMesh::volume() const {
    float v = 0.0f;
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const glm::vec3& a = positions[indices[i]];
        const glm::vec3& b = positions[indices[i + 1]];
        const glm::vec3& c = positions[indices[i + 2]];
        v += glm::dot(a, glm::cross(b, c)) / 6.0f;
    }
    return v;
}

void RenderMesh::append(const RenderMesh& other) {
    const auto base = static_cast<std::uint32_t>(positions.size());
    positions.insert(positions.end(), other.positions.begin(), other.positions.end());
    normals.insert(normals.end(), other.normals.begin(), other.normals.end());
    for (const auto i : other.indices) {
        indices.push_back(base + i);
    }
}

std::vector<ProfilePoint> filletProfile(std::span<const glm::vec2> poly,
                                        const RoundingOptions& options) {
    const std::size_t n = poly.size();
    const float smooth = glm::radians(options.smoothAngleDegrees);
    std::vector<ProfilePoint> out;
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2& p0 = poly[(i + n - 1) % n];
        const glm::vec2& p1 = poly[i];
        const glm::vec2& p2 = poly[(i + 1) % n];
        const glm::vec2 e1 = glm::normalize(p1 - p0);
        const glm::vec2 e2 = glm::normalize(p2 - p1);
        const float cross = e1.x * e2.y - e1.y * e2.x;
        const float turn = std::acos(std::clamp(glm::dot(e1, e2), -1.0f, 1.0f));
        if (turn < smooth || options.radius <= 0.0f || cross <= 0.0f) {
            // Smooth (curve) or concave vertex: averaged normal, mitered offset.
            const glm::vec2 n1 = outwardNormal(e1);
            const glm::vec2 nn = glm::normalize(n1 + outwardNormal(e2));
            out.push_back({p1, nn, 1.0f / std::max(glm::dot(nn, n1), 0.2f)});
        } else {
            appendFillet(out, p0, p1, p2, options.radius, options.segmentsPer90);
        }
    }
    return out;
}

RenderMesh roundedExtrusion(std::span<const ProfilePoint> profile, float depth,
                            const RoundingOptions& options) {
    const float r = std::min(options.radius, depth * 0.5f);
    const float hz = depth * 0.5f;
    const int S = std::max(1, options.segmentsPer90);
    const std::size_t n = profile.size();

    struct Ring {
        std::vector<glm::vec3> p;
        std::vector<glm::vec3> nrm;
    };
    auto makeRing = [&](int k, float side) {
        const float phi = kHalfPi * static_cast<float>(k) / static_cast<float>(S);
        const float inset = r * (1.0f - std::cos(phi));
        Ring ring;
        for (const auto& pp : profile) {
            const glm::vec2 q = pp.position - pp.normal * (pp.miter * inset);
            ring.p.emplace_back(q.x, q.y, side * (hz - r + r * std::sin(phi)));
            ring.nrm.push_back(glm::normalize(
                glm::vec3(pp.normal * std::cos(phi), side * std::sin(phi))));
        }
        return ring;
    };

    std::vector<Ring> rings;
    for (int k = S; k >= 0; --k) rings.push_back(makeRing(k, -1.0f));
    for (int k = 0; k <= S; ++k) rings.push_back(makeRing(k, 1.0f));

    RenderMesh mesh;
    for (std::size_t a = 0; a + 1 < rings.size(); ++a) {
        const Ring& A = rings[a];
        const Ring& B = rings[a + 1];
        for (std::size_t j = 0; j < n; ++j) {
            const std::size_t k = (j + 1) % n;
            addQuad(mesh, A.p[j], A.nrm[j], A.p[k], A.nrm[k], B.p[k], B.nrm[k], B.p[j], B.nrm[j]);
        }
    }

    // Flat caps from the fully inset profile, de-duplicating collapsed fillets.
    std::vector<glm::vec2> cap;
    for (const auto& pp : profile) {
        const glm::vec2 q = pp.position - pp.normal * (pp.miter * r);
        if (cap.empty() || glm::length(q - cap.back()) > 1e-7f) {
            cap.push_back(q);
        }
    }
    if (cap.size() > 1 && glm::length(cap.front() - cap.back()) <= 1e-7f) {
        cap.pop_back();
    }
    const auto tris = triangulatePolygon(cap);
    for (const float side : {-1.0f, 1.0f}) {
        const glm::vec3 nz{0.0f, 0.0f, side};
        for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
            const glm::vec2& a = cap[tris[t]];
            const glm::vec2& b = cap[tris[t + 1]];
            const glm::vec2& c = cap[tris[t + 2]];
            addTriangle(mesh, {a, side * hz}, nz, {b, side * hz}, nz, {c, side * hz}, nz);
        }
    }
    return mesh;
}

RenderMesh revolveProfile(std::span<const ProfilePoint> profile, int segments) {
    RenderMesh mesh;
    auto at = [](const ProfilePoint& p, float angle, glm::vec3& pos, glm::vec3& nrm) {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        pos = {p.position.x * c, p.position.y, p.position.x * s};
        nrm = glm::normalize(glm::vec3(p.normal.x * c, p.normal.y, p.normal.x * s));
    };
    for (std::size_t i = 0; i + 1 < profile.size(); ++i) {
        for (int s = 0; s < segments; ++s) {
            const float a0 = glm::two_pi<float>() * static_cast<float>(s) / static_cast<float>(segments);
            const float a1 = glm::two_pi<float>() * static_cast<float>(s + 1) / static_cast<float>(segments);
            glm::vec3 p00, n00, p01, n01, p10, n10, p11, n11;
            at(profile[i], a0, p00, n00);
            at(profile[i], a1, p01, n01);
            at(profile[i + 1], a0, p10, n10);
            at(profile[i + 1], a1, p11, n11);
            addQuad(mesh, p00, n00, p01, n01, p11, n11, p10, n10);
        }
    }
    return mesh;
}

RenderMesh roundedCylinder(float radius, float halfHeight, const RoundingOptions& options,
                           int segments) {
    const float r = std::min({options.radius, radius, halfHeight});
    std::vector<ProfilePoint> prof;
    prof.push_back({{0.0f, -halfHeight}, {0.0f, -1.0f}});
    appendFillet(prof, {0.0f, -halfHeight}, {radius, -halfHeight}, {radius, halfHeight}, r,
                 options.segmentsPer90);
    appendFillet(prof, {radius, -halfHeight}, {radius, halfHeight}, {0.0f, halfHeight}, r,
                 options.segmentsPer90);
    prof.push_back({{0.0f, halfHeight}, {0.0f, 1.0f}});
    return revolveProfile(prof, segments);
}

RenderMesh roundedCone(float radius, float height, const RoundingOptions& options, int segments) {
    const float h2 = height * 0.5f;
    const float r = options.radius;
    std::vector<ProfilePoint> prof;
    prof.push_back({{0.0f, -h2}, {0.0f, -1.0f}});
    appendFillet(prof, {0.0f, -h2}, {radius, -h2}, {0.0f, h2}, r, options.segmentsPer90);

    // Apex: sphere of radius r centered on the axis, tangent to the side.
    const glm::vec2 side = glm::normalize(glm::vec2(-radius, height));
    const glm::vec2 nSide = outwardNormal(side);
    const float yc = h2 - r / nSide.y;
    const float a0 = std::atan2(nSide.y, nSide.x);
    const int segs = arcSegments(kHalfPi - a0, options.segmentsPer90);
    for (int k = 0; k <= segs; ++k) {
        const float a = a0 + (kHalfPi - a0) * static_cast<float>(k) / static_cast<float>(segs);
        const glm::vec2 n{std::cos(a), std::sin(a)};
        glm::vec2 p = glm::vec2(0.0f, yc) + n * r;
        if (k == segs) p.x = 0.0f;
        prof.push_back({p, n});
    }
    return revolveProfile(prof, segments);
}

RenderMesh minkowskiRound(const ConvexPolyhedron& core, float radius, int segmentsPer90) {
    RenderMesh mesh;
    std::vector<glm::vec3> faceNormals;
    for (const auto& face : core.faces) {
        glm::vec3 nrm{0.0f};
        for (std::size_t i = 0; i < face.size(); ++i) {
            nrm += glm::cross(core.vertices[face[i]], core.vertices[face[(i + 1) % face.size()]]);
        }
        faceNormals.push_back(glm::normalize(nrm));
    }

    // Offset faces.
    for (std::size_t f = 0; f < core.faces.size(); ++f) {
        const auto& face = core.faces[f];
        const glm::vec3 off = faceNormals[f] * radius;
        for (std::size_t i = 1; i + 1 < face.size(); ++i) {
            addTriangle(mesh, core.vertices[face[0]] + off, faceNormals[f],
                        core.vertices[face[i]] + off, faceNormals[f],
                        core.vertices[face[i + 1]] + off, faceNormals[f]);
        }
    }

    // Edge strips.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::size_t>> edgeFaces;
    std::vector<std::vector<std::size_t>> vertexFaces(core.vertices.size());
    for (std::size_t f = 0; f < core.faces.size(); ++f) {
        const auto& face = core.faces[f];
        for (std::size_t i = 0; i < face.size(); ++i) {
            const auto a = face[i];
            const auto b = face[(i + 1) % face.size()];
            edgeFaces[{std::min(a, b), std::max(a, b)}].push_back(f);
            vertexFaces[a].push_back(f);
        }
    }
    for (const auto& [edge, faces] : edgeFaces) {
        if (faces.size() != 2) continue;
        const glm::vec3 n1 = faceNormals[faces[0]];
        const glm::vec3 n2 = faceNormals[faces[1]];
        const float angle = std::acos(std::clamp(glm::dot(n1, n2), -1.0f, 1.0f));
        const int segs = arcSegments(angle, segmentsPer90);
        const glm::vec3& va = core.vertices[edge.first];
        const glm::vec3& vb = core.vertices[edge.second];
        for (int k = 0; k < segs; ++k) {
            const glm::vec3 d0 = slerpDir(n1, n2, static_cast<float>(k) / static_cast<float>(segs));
            const glm::vec3 d1 = slerpDir(n1, n2, static_cast<float>(k + 1) / static_cast<float>(segs));
            addQuad(mesh, va + d0 * radius, d0, vb + d0 * radius, d0, vb + d1 * radius, d1,
                    va + d1 * radius, d1);
        }
    }

    // Spherical corner patches.
    for (std::size_t v = 0; v < core.vertices.size(); ++v) {
        auto& faces = vertexFaces[v];
        if (faces.size() < 3) continue;
        glm::vec3 c{0.0f};
        for (const auto f : faces) c += faceNormals[f];
        c = glm::normalize(c);
        const glm::vec3 helper = std::abs(c.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 u = glm::normalize(glm::cross(helper, c));
        const glm::vec3 w = glm::cross(c, u);
        std::sort(faces.begin(), faces.end(), [&](std::size_t a, std::size_t b) {
            return std::atan2(glm::dot(faceNormals[a], w), glm::dot(faceNormals[a], u)) <
                   std::atan2(glm::dot(faceNormals[b], w), glm::dot(faceNormals[b], u));
        });
        const glm::vec3& p = core.vertices[v];
        for (std::size_t i = 0; i < faces.size(); ++i) {
            const glm::vec3 ni = faceNormals[faces[i]];
            const glm::vec3 nj = faceNormals[faces[(i + 1) % faces.size()]];
            const float angle = std::acos(std::clamp(glm::dot(ni, nj), -1.0f, 1.0f));
            const int segs = arcSegments(angle, segmentsPer90);
            for (int k = 0; k < segs; ++k) {
                const glm::vec3 d0 = slerpDir(ni, nj, static_cast<float>(k) / static_cast<float>(segs));
                const glm::vec3 d1 = slerpDir(ni, nj, static_cast<float>(k + 1) / static_cast<float>(segs));
                addTriangle(mesh, p + c * radius, c, p + d0 * radius, d0, p + d1 * radius, d1);
            }
        }
    }
    return mesh;
}

ConvexPolyhedron shrinkTangential(const ConvexPolyhedron& poly, const glm::vec3& incenter,
                                  float inradius, float radius) {
    ConvexPolyhedron out = poly;
    const float k = (inradius - radius) / inradius;
    for (auto& v : out.vertices) {
        v = incenter + (v - incenter) * k;
    }
    return out;
}

std::vector<std::uint32_t> triangulatePolygon(std::span<const glm::vec2> poly) {
    std::vector<std::uint32_t> out;
    const std::size_t n = poly.size();
    if (n < 3) return out;

    float area = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    std::vector<std::uint32_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) {
        idx[i] = static_cast<std::uint32_t>(area > 0.0f ? i : n - 1 - i);
    }

    auto cross2 = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    auto inside = [&](const glm::vec2& p, const glm::vec2& a, const glm::vec2& b,
                      const glm::vec2& c) {
        return cross2(a, b, p) >= 0.0f && cross2(b, c, p) >= 0.0f && cross2(c, a, p) >= 0.0f;
    };

    while (idx.size() > 3) {
        bool clipped = false;
        const std::size_t m = idx.size();
        for (std::size_t i = 0; i < m; ++i) {
            const auto ia = idx[(i + m - 1) % m];
            const auto ib = idx[i];
            const auto ic = idx[(i + 1) % m];
            const glm::vec2& a = poly[ia];
            const glm::vec2& b = poly[ib];
            const glm::vec2& c = poly[ic];
            if (cross2(a, b, c) <= 1e-12f) continue; // reflex or degenerate
            bool ear = true;
            for (const auto j : idx) {
                if (j == ia || j == ib || j == ic) continue;
                if (inside(poly[j], a, b, c)) {
                    ear = false;
                    break;
                }
            }
            if (!ear) continue;
            out.insert(out.end(), {ia, ib, ic});
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            // Numerically degenerate remainder: fall back to a fan.
            for (std::size_t i = 1; i + 1 < idx.size(); ++i) {
                out.insert(out.end(), {idx[0], idx[i], idx[i + 1]});
            }
            return out;
        }
    }
    out.insert(out.end(), {idx[0], idx[1], idx[2]});
    return out;
}

} // namespace woodyn::geometry

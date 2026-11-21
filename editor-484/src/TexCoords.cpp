// =============================================================================
// Texture coordinate generation and sampling
// =============================================================================
// Student work. See include/TexCoords.h for why this is its own translation
// unit and what stays on the given side.

#include "TexCoords.h"
#include "TextureCache.h"

#include <cmath>
#include <algorithm>

namespace TexCoords {

std::vector<glm::vec2> sphere(unsigned latSegments, unsigned lonSegments) {
    // TODO(textures): generate one UV per sphere vertex
    // Walk the same loop bounds Sphere::setupSphere uses, so entry i here
    // matches vertex i there. Remember v runs bottom-up for OpenGL.
   
    std::vector<glm::vec2> uvs;
    uvs.reserve((latSegments + 1) * (lonSegments + 1));

    for (unsigned y = 0; y <= latSegments; ++y) {
        for (unsigned x = 0; x <= lonSegments; ++x) {
            float u = static_cast<float>(x) / lonSegments;
            float v = static_cast<float>(y) / latSegments;
            uvs.push_back(glm::vec2(1-u, 1-v));
        }
    }
    return uvs;
}

std::vector<glm::vec2> cube(const std::vector<glm::vec3>& vertices,
                            const std::vector<std::vector<int> >& faces,
                            const std::vector<glm::vec3>& faceNormals) {
    // TODO(textures): generate one UV per cube triangle corner
    // Three per face, in the order the corners appear in `faces`. Project
    // each vertex onto the two axes the face normal is NOT aligned with.
    // faceNormals is indexed faceNormals[i / 2]: two triangles per face.

    std::vector<glm::vec2> uvs;
    uvs.reserve(faces.size() * 3);

    for (size_t i = 0; i < faces.size(); ++i) {
        const glm::vec3& n = faceNormals[i / 2];

        // Determine the two axes to project onto based on the face normal
        int uAxis, vAxis;
        if (std::fabs(n.z) > 0.5) { uAxis = 0; vAxis = 1; } // Project onto XY plane
        else if (std::fabs(n.x) > 0.5) { uAxis = 2; vAxis = 1; } // Project onto YZ plane
        else { uAxis = 0; vAxis = 2; } // Project onto XZ plane
        
        for (int j = 0; j < 3; ++j) {
            const glm::vec3& p = vertices[faces[i][j]];
    
            uvs.push_back(glm::vec2(p[uAxis] + 0.5f, p[vAxis] + 0.5f)); // Shift to [0, 1] range
        }
    }

    return uvs;
}

// Cylindrical projection, one UV per triangle corner.
//
// For anything shaped roughly like a solid of revolution but WITHOUT a
// generating parameterisation to reuse -- the teapot above all, whose vertex
// data arrives as a bare triangle soup from a table of Bezier patches.
//
// u is the angle round the Y axis, v the height, both normalised by the mesh's
// own bounds.
std::vector<glm::vec2> cylindricalPerCorner(const std::vector<glm::vec3>& vertices,
                                            const std::vector<std::vector<int> >& faces) {
    // TODO(textures): cylindrical projection with the wrap seam repaired
    // Project every triangle corner onto a cylinder about the Y axis: u is
    // the angle round it (atan2(z, x), remapped from -pi..pi into 0..1), v is
    // the height normalised by the mesh's own Y bounds. Return three UVs per
    // face, in corner order.
    //
    // Then repair the wrap seam, which is the part worth the effort. A
    // triangle straddling the back of the mesh gets corners at u = 0.98 and
    // u = 0.02; interpolated across the face that runs the LONG way round and
    // squeezes the whole image, mirrored, into one triangle -- a bright
    // vertical scar down the back. Detect it (the u range across a triangle
    // exceeds half a turn) and push the low corners past 1.0 so the span is
    // short again; wrapping in the sampler puts them back.
    //
    // That repair is only expressible per corner, which is why this returns
    // per-corner UVs: one point in space needs two different u values
    // depending on which triangle is asking.
    std::vector<glm::vec2> uvs;
    if (vertices.empty() || faces.empty()) return uvs;

    float minY = vertices[0].y, maxY = vertices[0].y;
    for (const auto& v : vertices) {
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }

    float spanY = (maxY - minY > 1e-6f) ? (maxY - minY) : 1.0f; // Avoid division by zero
    const float pi = 3.14159265358979323846f;

    uvs.reserve(faces.size() * 3);

    for (size_t f = 0; f < faces.size(); ++f) {
        const auto& face = faces[f];
        glm::vec3 u(0.0f), v(0.0f);
        for (int j = 0; j < 3; ++j) {
            const glm::vec3& p = vertices[face[j]];
            u[j] = (std::atan2(p.z, p.x) + pi) / (2.0f * pi);
            v[j] = (p.y - minY) / spanY;
        }

        const float uMin = std::min(u[0], std::min(u[1], u[2]));
        const float uMax = std::max(u[0], std::max(u[1], u[2]));
        if (uMax - uMin > 0.5f) {
            for (int j = 0; j < 3; ++j) {
                if (u[j] < 0.5f) {
                    u[j] += 1.0f;
                }
            }
        }

        for (int j = 0; j < 3; ++j) {
            uvs.push_back(glm::vec2(u[j], v[j]));
        }
    }

    return uvs;
}

} // namespace TexCoords

glm::vec3 Texture::sample(float u, float v) const {
    // TODO(textures): bilinear sample in normalised coordinates, wrapping outside [0, 1]
    // Wrap u and v into [0, 1), find the four surrounding texels, and
    // blend them. Texel CENTRES sit at (i + 0.5) / size, which is why the
    // -0.5 below matters: without it the image is offset by half a texel.
    
    // 1. Check for valid texture
    if (!valid()) return glm::vec3(1.0f, 0.0f, 1.0f); // Return magenta for invalid texture

    // 2. Wrap u and v into [0, 1)
    u = u - std::floor(u);
    v = v - std::floor(v);

    // 3. Convert to texel space
    float x = u * width - 0.5f;
    float y = v * height - 0.5f;

    // 4. Find the four surrounding texels
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));

    // 5. Fractional  distance
    float fx = x - static_cast<float>(x0);
    float fy = y - static_cast<float>(y0);

    // 6. Calculate the indices needed for our top pixels
    int xs[2] = {
        ((x0 % width) + width) % width, 
        ((x0 + 1) % width + width) % width
    };
    int ys[2] = {
        ((y0 % height) + height) % height, 
        ((y0 + 1) % height + height) % height
    };

    // 7. 2x2 array to hold colors of the four surrounding texels
    glm::vec3 colors[2][2];

    for (int j=0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            int index = (ys[j] * width + xs[i]) * 4; // RGBA8 has 4 channels
            colors[j][i] = glm::vec3(
                pixels[index] / 255.0f,
                pixels[index + 1] / 255.0f,
                pixels[index + 2] / 255.0f
            );
        }
    }   

    // 8. Interpolate horizontally
    glm::vec3 top = colors[0][0] + (colors[0][1] - colors[0][0]) * fx;
    glm::vec3 bottom = colors[1][0] + (colors[1][1] - colors[1][0]) * fx;

    // 9. Interpolate vertically
    return top + (bottom - top) * fy;
}

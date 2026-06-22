// voxelize_multi.cpp
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <array>
#include <string>
#include <tuple>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <filesystem>
#include <random>

using namespace std;

/* ----------------------------- math ----------------------------- */

struct Vec2 { float x, y; };

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }

struct Vec3 { float x, y, z; };

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

Vec3 minv(Vec3 a, Vec3 b) { return {min(a.x, b.x), min(a.y, b.y), min(a.z, b.z)}; }
Vec3 maxv(Vec3 a, Vec3 b) { return {max(a.x, b.x), max(a.y, b.y), max(a.z, b.z)}; }

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalize(Vec3 a) { float l = sqrt(dot(a, a)); return (l == 0) ? Vec3{0, 0, 0} : a * (1.0f / l); }

/* --------------------------- texture ---------------------------- */

struct Image {
    int w = 0, h = 0, channels = 0;
    vector<uint8_t> data;
};

Image loadImage(const string& path) {
    Image img;
    unsigned char* pixels = stbi_load(path.c_str(), &img.w, &img.h, &img.channels, 0); 
    if (!pixels) throw runtime_error("Failed to load texture: " + path);
    img.data.assign(pixels, pixels + img.w * img.h * img.channels);
    stbi_image_free(pixels);
    return img;
}

struct ColorSample {
    Vec3 rgb;
    float alpha;
};

ColorSample sample(const Image& img, Vec2 uv) {
    if (img.data.empty()) {
        return { {1.f, 1.f, 1.f}, 1.f };
    }

    uv.x -= floor(uv.x); uv.y -= floor(uv.y);
    int x = min(img.w - 1, int(uv.x * img.w));
    int y = min(img.h - 1, int((1 - uv.y) * img.h));
    
    int i = (y * img.w + x) * img.channels;
    
    Vec3 rgb = { img.data[i] / 255.f, img.data[i + 1] / 255.f, img.data[i + 2] / 255.f };
    float alpha = 1.0f;
    
    if (img.channels == 4) {
        alpha = img.data[i + 3] / 255.f;
    }
    
    return { rgb, alpha };
}

// Sample a greyscale opacity map (map_d). Returns the red channel as opacity.
float sampleOpacityMap(const Image& img, Vec2 uv) {
    if (img.data.empty()) return 1.0f;
    uv.x -= floor(uv.x); uv.y -= floor(uv.y);
    int x = min(img.w - 1, int(uv.x * img.w));
    int y = min(img.h - 1, int((1 - uv.y) * img.h));
    int i = (y * img.w + x) * img.channels;
    return img.data[i] / 255.f;
}

/* ----------------------------- OBJ ------------------------------ */

struct Material { 
    Image tex; 
    Image opacityTex;
    Vec3 diffuseColor = {1.0f, 1.0f, 1.0f};
    Vec3 fallbackColor = {1.0f, 0.0f, 1.0f};
    float opacity = 1.0f;
};
map<string, Material> materials;

struct Tri {
    Vec3 p[3];
    Vec2 uv[3];
    string material;
};

void loadMTL(const string& mtllib) {
    ifstream m(mtllib);
    if (!m) return;
    string line, matName;
    while (getline(m, line)) {
        stringstream ss(line);
        string t; ss >> t;
        
        if (t == "newmtl") {
            ss >> matName;
            materials[matName] = Material{};
        } else if (matName.empty()) {
            continue;
        }
        
        if (t == "Kd") {
            ss >> materials[matName].diffuseColor.x 
               >> materials[matName].diffuseColor.y 
               >> materials[matName].diffuseColor.z;
        } 
        else if (t == "d") {
            ss >> materials[matName].opacity;
        } 
        else if (t == "Tr") {
            float transparency;
            ss >> transparency;
            materials[matName].opacity = 1.0f - transparency;
        } 
        else if (t == "map_Kd") {
            string texpath; getline(ss, texpath);
            texpath.erase(0, texpath.find_first_not_of(" \t"));
            string fullPath = mtllib;
            size_t lastSlash = fullPath.find_last_of("/\\");
            string dir = (lastSlash != string::npos) ? fullPath.substr(0, lastSlash + 1) : "";
            try { materials[matName].tex = loadImage(dir + texpath); } catch (...) {}
        } 
        else if (t == "map_d") {
            string texpath; getline(ss, texpath);
            texpath.erase(0, texpath.find_first_not_of(" \t"));
            string fullPath = mtllib;
            size_t lastSlash = fullPath.find_last_of("/\\");
            string dir = (lastSlash != string::npos) ? fullPath.substr(0, lastSlash + 1) : "";
            
            try { 
                Image img = loadImage(dir + texpath); 

                // If it's an RGBA image, check if the alpha channel is actually used
                if (img.channels == 4) {
                    bool hasAlphaData = false;
                    
                    // Scan the entire alpha channel
                    for (size_t i = 3; i < img.data.size(); i += 4) {
                        if (img.data[i] < 255) {
                            hasAlphaData = true;
                            break; // Alpha data found! Stop scanning.
                        }
                    }

                    // If alpha data is present, convert the image to a grayscale opacity map
                    // by copying the Alpha channel values into the Red/Green/Blue channels.
                    if (hasAlphaData) {
                        for (size_t i = 0; i < img.data.size(); i += 4) {
                            unsigned char alpha = img.data[i + 3];
                            img.data[i]     = alpha; // R
                            img.data[i + 1] = alpha; // G
                            img.data[i + 2] = alpha; // B
                        }
                    }
                }

                materials[matName].opacityTex = img; 
            } 
            catch (...) {}
        }
    }
}

vector<Tri> loadOBJ(const string& path) {
    ifstream f(path, ios::binary | ios::ate); // Open at the end to get file size
    if (!f) throw runtime_error("OBJ open failed");

    // 1. Get total file size for percentage calculation
    streamsize totalBytes = f.tellg();
    f.seekg(0, ios::beg); // Rewind back to the start


    vector<Vec3> V; vector<Vec2> VT; vector<Tri> tris;
    string mtllib, line, currentMaterial;
    streamsize bytesRead = 0;
    int lastPercent = -1;

    while (getline(f, line)) {

        // Accumulate bytes read (including the newline character)
        bytesRead += line.length() + 1; 

        // 2. Calculate and display percentage
        if (totalBytes > 0) {
            int percent = static_cast<int>((bytesRead * 100) / totalBytes);
            if (percent > 100) percent = 100; // Cap it just in case
            
            // Only print if the percentage actually changed to avoid slowing down IO
            if (percent != lastPercent) {
                cout << "\rLoading OBJ: " << percent << "%" << flush;
                lastPercent = percent;
            }
        }

        stringstream ss(line);
        string t; ss >> t;
        if (t == "mtllib") {
            string rest; getline(ss, rest);
            while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            loadMTL(rest);
        } else if (t == "v") { Vec3 v; ss >> v.x >> v.y >> v.z; V.push_back(v); }
        else if (t == "vt") { Vec2 uv; ss >> uv.x >> uv.y; VT.push_back(uv); }
        else if (t == "usemtl") { ss >> currentMaterial; }
        else if (t == "f") {
            vector<pair<int, int>> verts; string token;
            while (ss >> token) {
                int vi, ti;
                if (sscanf(token.c_str(), "%d/%d", &vi, &ti) == 2) verts.push_back({vi - 1, ti - 1});
                else if (sscanf(token.c_str(), "%d", &vi) == 1) verts.push_back({vi - 1, -1});
            }
            for (size_t i = 1; i + 1 < verts.size(); i++) {
                Tri tr; tr.material = currentMaterial;
                auto v0 = verts[0], v1 = verts[i], v2 = verts[i + 1];
                tr.p[0] = V[v0.first]; tr.uv[0] = v0.second >= 0 ? VT[v0.second] : Vec2{0, 0};
                tr.p[1] = V[v1.first]; tr.uv[1] = v1.second >= 0 ? VT[v1.second] : Vec2{0, 0};
                tr.p[2] = V[v2.first]; tr.uv[2] = v2.second >= 0 ? VT[v2.second] : Vec2{0, 0};
                tris.push_back(tr);
            }
        }
    }
    cout << endl;
    return tris;
}

/* --------------------------- palette ---------------------------- */

struct Color { uint8_t r, g, b, a; };
bool operator<(const Color& a, const Color& b) { return tie(a.r, a.g, a.b, a.a) < tie(b.r, b.g, b.b, b.a); }

struct Palette {
    Color list[256];
    int numGenerated = 0;

    uint8_t get(Vec3 c) const {
        uint8_t best = 0; 
        float bestDist = 1e9f;
        int r = int(c.x * 255.0f), g = int(c.y * 255.0f), b = int(c.z * 255.0f);
        
        bool foundValid = false;

        for (int i = 0; i < numGenerated; i++) {
            //if (list[i].r == 0 && list[i].g == 0 && list[i].b == 0) continue; 

            int dr = r - list[i].r, dg = g - list[i].g, db = b - list[i].b;
            float d = (dr * dr * 0.299f) + (dg * dg * 0.587f) + (db * db * 0.114f);
            
            if (d < bestDist) { 
                bestDist = d; 
                best = (uint8_t)i; 
                foundValid = true;
            }
        }

        if (!foundValid) return 1;
        return (uint8_t)(best + 1);
    }

    void buildFromColors(vector<Vec3>& uniqueColors, int colors, const string& rangeString) {
        struct Range { int start, end; };
        vector<Range> targetRanges;
        stringstream ss(rangeString);
        string token;
        while (getline(ss, token, ',')) {
            token.erase(token.begin(), find_if(token.begin(), token.end(),
                [](unsigned char ch) { return !isspace(ch); }));
            token.erase(find_if(token.rbegin(), token.rend(),
                [](unsigned char ch) { return !isspace(ch); }).base(), token.end());
            size_t dashPos = token.find('-');
            if (dashPos != string::npos) {
                int start = stoi(token.substr(0, dashPos));
                int end = stoi(token.substr(dashPos + 1));
                targetRanges.push_back({start - 1, end - 1});
            }
        }
        vector<int> activeIndices;
        for (auto& r : targetRanges) {
            int s = max(0, r.start);
            int e = min(255, r.end);
            if (s > e) swap(s, e);
            for (int i = s; i <= e; ++i) {
                activeIndices.push_back(i);
            }
        }

        sort(activeIndices.begin(), activeIndices.end());
        activeIndices.erase(unique(activeIndices.begin(), activeIndices.end()), activeIndices.end());

        int targetCount = (int)activeIndices.size();
        if (targetCount == 0 || uniqueColors.empty()) return;

        // Work in perceptual space: weight channels before splitting
        // so distance and axis-selection are consistent
        auto toPerceptual = [](Vec3 c) -> Vec3 {
            return { c.x * 0.577f, c.y * 0.766f, c.z * 0.338f }; 
            // sqrt(0.299), sqrt(0.587), sqrt(0.114) — squaring recovers the weights
        };
        auto fromPerceptual = [](Vec3 c) -> Vec3 {
            return { c.x / 0.577f, c.y / 0.766f, c.z / 0.338f };
        };

        vector<Vec3> pc(uniqueColors.size());
        for (size_t i = 0; i < uniqueColors.size(); i++)
            pc[i] = toPerceptual(uniqueColors[i]);

        struct Box { int start, end; };
        vector<Box> boxes = {{ 0, (int)pc.size() }};

        while ((int)boxes.size() < targetCount) {
            // Pick the box with the largest perceptual variance (not just spread)
            int bestBox = -1;
            float maxSpread = -1;
            for (int i = 0; i < (int)boxes.size(); i++) {
                if (boxes[i].end - boxes[i].start < 2) continue;
                Vec3 bMin{1,1,1}, bMax{0,0,0};
                for (int j = boxes[i].start; j < boxes[i].end; j++) {
                    bMin = minv(bMin, pc[j]); bMax = maxv(bMax, pc[j]);
                }
                // Largest extent in perceptual space — no extra weights needed
                // because pc[] is already in perceptual space
                int count = boxes[i].end - boxes[i].start;
                float spread = count * max({ bMax.x-bMin.x, bMax.y-bMin.y, bMax.z-bMin.z });
                if (spread > maxSpread) { maxSpread = spread; bestBox = i; }
            }
            if (bestBox == -1) break;

            Box& b = boxes[bestBox];
            Vec3 bMin{1,1,1}, bMax{0,0,0};
            for (int j = b.start; j < b.end; j++) {
                bMin = minv(bMin, pc[j]); bMax = maxv(bMax, pc[j]);
            }
            float dx = bMax.x-bMin.x, dy = bMax.y-bMin.y, dz = bMax.z-bMin.z;

            // Split along widest perceptual axis — consistent with selection above
            sort(pc.begin()+b.start, pc.begin()+b.end,
                [&](const Vec3& a, const Vec3& b) {
                    if (dx>=dy && dx>=dz) return a.x < b.x;
                    if (dy>=dx && dy>=dz) return a.y < b.y;
                    return a.z < b.z;
                });
            // Keep uniqueColors in sync
            sort(uniqueColors.begin()+b.start, uniqueColors.begin()+b.end,
                [&](const Vec3& a, const Vec3& b_) {
                    Vec3 pa = toPerceptual(a), pb = toPerceptual(b_);
                    if (dx>=dy && dx>=dz) return pa.x < pb.x;
                    if (dy>=dx && dy>=dz) return pa.y < pb.y;
                    return pa.z < pb.z;
                });

            int mid = (b.start + b.end) / 2;
            int end = b.end;
            b.end = mid;
            boxes.push_back({mid, end});
        }

        // Average each box in original (linear) space
        numGenerated = activeIndices.back() + 1;
        for (int i = 0; i < 256; i++) list[i] = {0, 0, 0, 255};

        for (int i = 0; i < (int)boxes.size() && i < (int)activeIndices.size(); i++) {
            Vec3 avg = {0,0,0};
            int count = boxes[i].end - boxes[i].start;
            if (count == 0) continue;
            for (int j = boxes[i].start; j < boxes[i].end; j++)
                avg = avg + uniqueColors[j];
            avg = avg * (1.0f / count);

            int idx = activeIndices[i];
            // Only avoid true (0,0,0) if it's your reserved null — otherwise clamp to 1
            uint8_t r = (uint8_t)(max(0.0f, min(1.0f, avg.x)) * 255.0f);
            uint8_t g = (uint8_t)(max(0.0f, min(1.0f, avg.y)) * 255.0f);
            uint8_t b = (uint8_t)(max(0.0f, min(1.0f, avg.z)) * 255.0f);
            if (r == 0 && g == 0 && b == 0) r = 1; // only if 0 is truly reserved
            list[idx] = {r, g, b, 255};
        }
    }

    bool loadFromPNG(const string& path) {
        int w, h, channels;
        unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 3);
        if (!pixels) return false;

        int count = min(256, w * h);
        for (int i = 0; i < count; i++) {
            list[i] = { pixels[i * 3], pixels[i * 3 + 1], pixels[i * 3 + 2], 255 };
        }
        for (int i = count; i < 256; i++) {
            list[i] = { 0, 0, 0, 255 };
        }

        numGenerated = count;
        stbi_image_free(pixels);
        cout << "Loaded custom palette from: " << path << " (" << count << " colors)" << endl;
        return true;
    }
};

/* ========================= SOLID FILL SYSTEM ========================= */

enum class VoxelNormal : uint8_t { EMPTY = 0, SURFACE, INTERIOR, SPACE };

struct MaterialTable {
    vector<string> names;
    map<string, uint16_t> index;

    MaterialTable() {
        names.push_back("");
        index[""] = 0;
    }

    uint16_t intern(const string& name) {
        if (name.empty()) return 0;
        auto it = index.find(name);
        if (it != index.end()) return it->second;
        if (names.size() >= 65535) return 0;
        uint16_t id = (uint16_t)names.size();
        names.push_back(name);
        index[name] = id;
        return id;
    }

    const string& lookup(uint16_t id) const {
        if (id < names.size()) return names[id];
        return names[0];
    }
};

static MaterialTable gMatTable;

struct VoxelGrid {
    int sx, sy, sz;
    vector<uint8_t>     color;
    vector<VoxelNormal> kind;
    vector<Vec3>        normal;
    vector<uint16_t>    matId;
    // Per-cell opacity in [0,1]. 1.0 = fully opaque. Stored so solidFill can
    // average opacity across rays the same way it averages color.
    vector<float>       opacity;

    VoxelGrid() : sx(0), sy(0), sz(0) {}
    VoxelGrid(int x, int y, int z) : sx(x), sy(y), sz(z),
        color  (size_t(x)*y*z, 0),
        kind   (size_t(x)*y*z, VoxelNormal::EMPTY),
        normal (size_t(x)*y*z, {0,0,0}),
        matId  (size_t(x)*y*z, 0),
        opacity(size_t(x)*y*z, 1.0f) {}

    inline bool inBounds(int x, int y, int z) const {
        return x >= 0 && y >= 0 && z >= 0 && x < sx && y < sy && z < sz;
    }
    inline size_t idx(int x, int y, int z) const {
        return size_t(x) + size_t(y)*sx + size_t(z)*sx*sy;
    }

    void set(int x, int y, int z, uint8_t c, VoxelNormal k,
             Vec3 n = {0,0,0}, uint16_t mat = 0, float op = 1.0f) {
        size_t i = idx(x,y,z);
        color  [i] = c;
        kind   [i] = k;
        normal [i] = n;
        matId  [i] = mat;
        opacity[i] = op;
    }

    uint8_t     getColor  (int x,int y,int z) const { return color  [idx(x,y,z)]; }
    VoxelNormal getKind   (int x,int y,int z) const { return kind   [idx(x,y,z)]; }
    Vec3        getNormal (int x,int y,int z) const { return normal [idx(x,y,z)]; }
    uint16_t    getMatId  (int x,int y,int z) const { return matId  [idx(x,y,z)]; }
    float       getOpacity(int x,int y,int z) const { return opacity[idx(x,y,z)]; }

    const string& getMaterialName(int x, int y, int z) const {
        return gMatTable.lookup(matId[idx(x,y,z)]);
    }
};

/* ------------------------------------------------------------------ */
/*  Opacity helpers                                                     */
/* ------------------------------------------------------------------ */

// Resolve the opacity for a triangle at given barycentric UV coordinates.
// Priority: map_d texture > base-color alpha channel > scalar Material::opacity.
float triOpacity(const string& matName, Vec2 uv, const map<string, Material>& mats) {
    auto it = mats.find(matName);
    if (it == mats.end()) return 1.0f;
    const Material& mat = it->second;

    // Explicit opacity map (map_d) wins first.
    if (mat.opacityTex.w > 0) {
        return sampleOpacityMap(mat.opacityTex, uv);
    }

    /*// Alpha channel baked into the diffuse texture (RGBA) is next. < ---- Bad. Some PNGs have random shit in the alpha channel for no reason. Better to go off only what is plugged into the map_d or default to d value
    if (mat.tex.w > 0 && mat.tex.channels == 4) {
        ColorSample cs = sample(mat.tex, uv);
        return cs.alpha;
    }*/

    // Fall back to the scalar opacity from the MTL.
    return mat.opacity;
}

/* ------------------------------------------------------------------ */
/*  solidFill                                                           */
/* ------------------------------------------------------------------ */
void solidFill(VoxelGrid& grid, const Palette& pal) {
    const Vec3 rayDirs[6] = {
        { 1, 0, 0}, {-1, 0, 0},
        { 0, 1, 0}, { 0,-1, 0},
        { 0, 0, 1}, { 0, 0,-1}
    };
    const int dx[6] = { 1,-1, 0, 0, 0, 0};
    const int dy[6] = { 0, 0, 1,-1, 0, 0};
    const int dz[6] = { 0, 0, 0, 0, 1,-1};

    cout << "  Building empty-cell list..." << endl;
    vector<int> todo;
    todo.reserve(size_t(grid.sx) * grid.sy * grid.sz / 4);
    for (int z = 0; z < grid.sz; z++)
        for (int y = 0; y < grid.sy; y++)
            for (int x = 0; x < grid.sx; x++)
                if (grid.getKind(x,y,z) == VoxelNormal::EMPTY)
                    todo.push_back((int)grid.idx(x,y,z));

    cout << "  " << todo.size() << " empty cells to classify." << endl;

    mt19937 rng(42);
    int passNum = 0;
    size_t prevSize = todo.size() + 1;
    size_t initialTodoSize = todo.size();
    size_t printInterval = (initialTodoSize >= 100) ? (initialTodoSize / 100) : 1; 
    size_t totalResolvedSoFar = 0;

    while (!todo.empty()) {
        if (todo.size() >= prevSize) {
            cout << "\n  [solidFill] No progress in pass " << passNum
                 << " — forcing " << todo.size()
                 << " remaining cells to SPACE." << endl;
            for (int flatIdx : todo) {
                int fi = flatIdx;
                int x = fi % grid.sx; fi /= grid.sx;
                int y = fi % grid.sy; fi /= grid.sy;
                int z = fi;
                grid.set(x, y, z, 0, VoxelNormal::SPACE);
            }
            break;
        }

        prevSize = todo.size();
        shuffle(todo.begin(), todo.end(), rng);

        vector<int> nextTodo;
        nextTodo.reserve(todo.size());
        size_t resolved = 0;

        for (int flatIdx : todo) {
            int fi = flatIdx;
            int x = fi % grid.sx; fi /= grid.sx;
            int y = fi % grid.sy; fi /= grid.sy;
            int z = fi;

            bool escapes     = false;
            int  insideHits  = 0;
            int  outsideHits = 0;
            Vec3 colorAccum  = {0,0,0};
            int  colorCount  = 0;
            // Accumulated opacity from inside-hit rays.
            float opacityAccum = 0.0f;
            int   opacityCount = 0;

            uint16_t matVotes[6];
            int      matVoteCount = 0;

            for (int d = 0; d < 6; d++) {
                int cx = x + dx[d];
                int cy = y + dy[d];
                int cz = z + dz[d];

                bool hitSomething = false;
                while (grid.inBounds(cx, cy, cz)) {
                    VoxelNormal k = grid.getKind(cx, cy, cz);
                    if (k != VoxelNormal::EMPTY) {
                        hitSomething = true;
                        if (k == VoxelNormal::SPACE) {
                            outsideHits++;
                        } else if (k == VoxelNormal::INTERIOR) {
                            insideHits++;
                            matVotes[matVoteCount++] = grid.getMatId(cx, cy, cz);
                            // Accumulate color from this interior hit.
                            uint8_t ci = grid.getColor(cx, cy, cz);
                            if (ci > 0 && ci <= 255) {
                                const Color& pc = pal.list[ci - 1];
                                colorAccum.x += pc.r / 255.f;
                                colorAccum.y += pc.g / 255.f;
                                colorAccum.z += pc.b / 255.f;
                                colorCount++;
                            }
                            // Accumulate opacity from this interior hit.
                            opacityAccum += grid.getOpacity(cx, cy, cz);
                            opacityCount++;
                        } else {
                            // SURFACE
                            Vec3 fn = grid.getNormal(cx, cy, cz);
                            float d_dot = dot(fn, rayDirs[d]);
                            if (d_dot > 0.0f) {
                                insideHits++;
                                matVotes[matVoteCount++] = grid.getMatId(cx, cy, cz);
                                uint8_t ci = grid.getColor(cx, cy, cz);
                                if (ci > 0 && ci <= 255) {
                                    const Color& pc = pal.list[ci - 1];
                                    colorAccum.x += pc.r / 255.f;
                                    colorAccum.y += pc.g / 255.f;
                                    colorAccum.z += pc.b / 255.f;
                                    colorCount++;
                                }
                                opacityAccum += grid.getOpacity(cx, cy, cz);
                                opacityCount++;
                            } else {
                                outsideHits++;
                            }
                        }
                        break;
                    }
                    cx += dx[d];
                    cy += dy[d];
                    cz += dz[d];
                }
                if (!hitSomething) {
                    escapes = true;
                    break;
                }
            }

            bool cellResolved = false;
            if (escapes) {
                grid.set(x, y, z, 0, VoxelNormal::SPACE);
                resolved++;
                cellResolved = true;
            } else if (insideHits > 2) {
                Vec3 avgColor = (colorCount > 0)
                    ? colorAccum * (1.0f / colorCount)
                    : Vec3{0.5f, 0.5f, 0.5f};
                uint8_t ci = pal.get(avgColor);

                // Average opacity across all inside-hit rays.
                float avgOpacity = (opacityCount > 0)
                    ? opacityAccum / (float)opacityCount
                    : 1.0f;

                // Majority vote for material name.
                uint16_t bestMat = 0;
                if (matVoteCount > 0) {
                    uint16_t ids[6];
                    int      cnts[6];
                    int      unique = 0;
                    for (int v = 0; v < matVoteCount; v++) {
                        uint16_t id = matVotes[v];
                        bool found = false;
                        for (int u = 0; u < unique; u++) {
                            if (ids[u] == id) { cnts[u]++; found = true; break; }
                        }
                        if (!found) { ids[unique] = id; cnts[unique] = 1; unique++; }
                    }
                    int maxCnt = 0;
                    for (int u = 0; u < unique; u++) if (cnts[u] > maxCnt) maxCnt = cnts[u];
                    uint16_t tied[6];
                    int      tieCount = 0;
                    for (int u = 0; u < unique; u++)
                        if (cnts[u] == maxCnt) tied[tieCount++] = ids[u];
                    bestMat = tied[flatIdx % tieCount];
                }

                grid.set(x, y, z, ci, VoxelNormal::INTERIOR, {0,0,0}, bestMat, avgOpacity);
                resolved++;
                cellResolved = true;
            } else if (outsideHits >= 4) {
                grid.set(x, y, z, 0, VoxelNormal::SPACE);
                resolved++;
                cellResolved = true;
            } else {
                nextTodo.push_back(flatIdx);
            }

            if (cellResolved) {
                totalResolvedSoFar++;
                if (totalResolvedSoFar % printInterval == 0 || totalResolvedSoFar == initialTodoSize) {
                    size_t estimatedRemaining = initialTodoSize - totalResolvedSoFar;
                    std::cout << std::fixed << std::setprecision(2);
                    cout << "  solidFill resolved " << totalResolvedSoFar << "/" << initialTodoSize
                        << " total cells. (Remaining: " << estimatedRemaining << ") " << 100 * ((float) totalResolvedSoFar/(float) initialTodoSize) << "% completed    \r" << flush;
                }
            }
        }

        passNum++;

        todo = move(nextTodo);
    }
    cout << "\n  solidFill complete." << endl;
}

/* -------------------- Voxel struct -------------------- */

struct Voxel {
    int x, y, z;
    uint8_t color;
    string material;
    float opacity;   // 0 = fully transparent, 1 = fully opaque
};

/* =================== Multi-VOX writer =================== */

class MultiVoxWriter {
    string baseName;
    bool isSolidFill;
    float scale;
    int fileIdx = 0;
    ofstream f;
    Palette palette;
    bool onFloor;
    struct ChunkMeta { int id, ox, oy, oz, sx, sy, sz; };
    vector<ChunkMeta> chunks;
    long mainSizePos, mainStart;
    int modelCount = 0;
    const long SIZE_THRESHOLD = 1700000000L;

    void writeVoxString(const string& s) {
        int32_t len = (int32_t)s.size(); f.write((char*)&len, 4); if (len > 0) f.write(s.c_str(), len);
    }
    void writeVoxDict(const map<string, string>& dict) {
        int32_t n = (int32_t)dict.size(); f.write((char*)&n, 4);
        for (auto& [k, v] : dict) { writeVoxString(k); writeVoxString(v); }
    }
    long writeChunkHeader(const char* name) {
        f.write(name, 4); long p = f.tellp(); int32_t d = 0; f.write((char*)&d, 4); f.write((char*)&d, 4); return p;
    }
    void fixChunkHeader(long p) {
        long long c = (long long)f.tellp(); int32_t s = (int32_t)(c - p - 8); f.seekp(p); f.write((char*)&s, 4); f.seekp(c);
    }
    std::string formatScale(float scale) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << scale;
        std::string result = stream.str();
        result.erase(result.find_last_not_of('0') + 1, std::string::npos);
        if (!result.empty() && result.back() == '.') result.pop_back();
        return result;
    }
    void startNewFile(bool onFloor) {
        if (f.is_open()) finalize(onFloor);
        string solidIdent = isSolidFill ? "_solid" : "";
        string path = baseName + "_" + to_string(fileIdx++) + "_" + formatScale(scale) + solidIdent + ".vox";
        std::cout << "\nStarting new file: " << path << endl;
        f.open(path, ios::binary);
        chunks.clear();
        modelCount = 0;

        f.write("VOX ", 4); int32_t v = 150; f.write((char*)&v, 4);
        f.write("MAIN", 4); mainSizePos = f.tellp();
        int32_t d = 0; f.write((char*)&d, 4); f.write((char*)&d, 4); mainStart = f.tellp();
    }

public:
    MultiVoxWriter(const string& path, const Palette& pal, const bool onFloor, const bool isSolidFill, const float scale) : baseName(path), palette(pal), onFloor(onFloor), isSolidFill(isSolidFill), scale(scale) {
        if (baseName.find(".vox") != string::npos) baseName = baseName.substr(0, baseName.find(".vox"));
        startNewFile(onFloor);
    }

    void writeChunk(int ox, int oy, int oz, int sx, int sy, int sz, const vector<Voxel>& voxels) {
        if (voxels.empty()) return;

        long long currentPos = (long long)f.tellp();
        long long estSize = (long long)voxels.size() * 4 + 1024;

        if (currentPos + estSize > (long long)SIZE_THRESHOLD) {
            startNewFile(onFloor);
        }

        chunks.push_back({modelCount++, ox, oy, oz, sx, sy, sz});
        long pSize = writeChunkHeader("SIZE");
        f.write((char*)&sx, 4); f.write((char*)&sy, 4); f.write((char*)&sz, 4);
        fixChunkHeader(pSize);

        long pXyzi = writeChunkHeader("XYZI");
        int32_t n = (int32_t)voxels.size(); f.write((char*)&n, 4);
        for (const auto& v : voxels) {
            uint8_t buf[4] = {(uint8_t)(v.x - ox), (uint8_t)(v.y - oy), (uint8_t)(v.z - oz), v.color};
            f.write((char*)buf, 4);
        }
        fixChunkHeader(pXyzi);
    }

    void finalize(bool onFloor) {
        if (!f.is_open()) return;
        int32_t nodeId = 0, rootTrnId = nodeId++, grpId = nodeId++;
        auto W = [&](auto v) { f.write((char*)&v, sizeof(v)); };

        int minX = 1e9, minY = 1e9, minZ = 1e9;
        int maxX = -1e9, maxY = -1e9, maxZ = -1e9;
        for (const auto& c : chunks) {
            if (c.ox < minX) minX = c.ox;
            if (c.oy < minY) minY = c.oy;
            if (c.oz < minZ) minZ = c.oz;
            if (c.ox + c.sx > maxX) maxX = c.ox + c.sx;
            if (c.oy + c.sy > maxY) maxY = c.oy + c.sy;
            if (c.oz + c.sz > maxZ) maxZ = c.oz + c.sz;
        }

        float globalCenterX = (minX + maxX) / 2.0f;
        float globalCenterY = (minY + maxY) / 2.0f;
        float globalCenterZ = (minZ + maxZ) / 2.0f;
        float globalFloorZ  = (float)minZ;

        { long p = writeChunkHeader("nTRN"); W(rootTrnId); W(0); W(grpId); W(-1); W(0); W(1); writeVoxDict({}); fixChunkHeader(p); }
        { long p = writeChunkHeader("nGRP"); W(grpId); writeVoxDict({}); W((int32_t)chunks.size()); for (size_t i = 0; i < chunks.size(); i++) W((int32_t)(2 + i * 2)); fixChunkHeader(p); }

        for (size_t i = 0; i < chunks.size(); i++) {
            const auto& c = chunks[i];
            int32_t transId = nodeId++, shapeId = nodeId++;
            {
                int chunkCenterX = (int)c.ox + ((int)c.sx / 2);
                int chunkCenterY = (int)c.oy + ((int)c.sy / 2);
                int chunkCenterZ = (int)c.oz + ((int)c.sz / 2);
                long p = writeChunkHeader("nTRN");
                W(transId); writeVoxDict({}); W(shapeId); W(-1); W(0); W(1);
                int offsetX = chunkCenterX - (int)std::round(globalCenterX);
                int offsetY = chunkCenterY - (int)std::round(globalCenterY);

                int offsetZ;
                if (onFloor) {
                    offsetZ = chunkCenterZ - (int)std::round(globalFloorZ);
                } else {
                    offsetZ = chunkCenterZ - (int)std::round(globalCenterZ);
                }
                string sOffset = to_string(offsetX) + " " + to_string(offsetY) + " " + to_string(offsetZ);
                writeVoxDict({{"_t", sOffset}}); fixChunkHeader(p);
            }
            { long p = writeChunkHeader("nSHP"); W(shapeId); writeVoxDict({}); W(1); W(c.id); writeVoxDict({}); fixChunkHeader(p); }
        }
        long pPal = writeChunkHeader("RGBA");
        for (int i = 0; i < 256; i++) { Color c = i < 255 ? palette.list[i] : Color{0, 0, 0, 0}; f.write((char*)&c, 4); }
        fixChunkHeader(pPal);

        long long mainEnd = (long long)f.tellp(); int32_t cSize = (int32_t)(mainEnd - mainStart);
        f.seekp(mainSizePos); int32_t z = 0; f.write((char*)&z, 4); f.write((char*)&cSize, 4);
        f.close();
    }
};

/* -------------------- Rasterization Logic -------------------- */


float edge(Vec2 a, Vec2 b, Vec2 c) { return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x); }
bool barycentric(Vec2 p, Vec2 a, Vec2 b, Vec2 c, float& u, float& v, float& w) {
    float area = edge(a, b, c);
    if (fabs(area) < 1e-4f) return false;
    u = edge(b, c, p) / area; v = edge(c, a, p) / area; w = 1.f - u - v;
    return u >= -1e-4f && v >= -1e-4f && w >= -1e-4f;
}

struct VTri { Vec3 p[3]; Vec2 uv[3]; string material; };

static Vec3 triColor(const VTri& t, float u, float v, float w, const map<string, Material>& mats) {
    Vec2 uv = {u * t.uv[0].x + v * t.uv[1].x + w * t.uv[2].x,
               u * t.uv[0].y + v * t.uv[1].y + w * t.uv[2].y};
    auto it = mats.find(t.material);
    if (it != mats.end()) {
        return (it->second.tex.w > 0) ? sample(it->second.tex, uv).rgb : it->second.diffuseColor;
    }
    return {1, 0, 1};
}

// Resolve barycentric UV then delegate to triOpacity().
static float triOpacityBary(const VTri& t, float u, float v, float w,
                             const map<string, Material>& mats) {
    Vec2 uv = {u * t.uv[0].x + v * t.uv[1].x + w * t.uv[2].x,
               u * t.uv[0].y + v * t.uv[1].y + w * t.uv[2].y};
    return triOpacity(t.material, uv, mats);
}

std::string formatScale(float scale) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << scale;
    std::string result = stream.str();
    result.erase(result.find_last_not_of('0') + 1, std::string::npos);
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result;
}

// Rasterize into a VoxelGrid. Fully transparent voxels (opacity == 0) are
// skipped so they never block the solidFill ray-cast.
void rasterizeIntoGrid(VoxelGrid& grid, const vector<VTri>& tris,
                       const map<string, Material>& mats, Palette& pal, const map<string, int>& matPriorities)
{
    for (const auto& t : tris) {
        uint16_t matId = gMatTable.intern(t.material);

        Vec3 e1 = t.p[1] - t.p[0], e2 = t.p[2] - t.p[0];
        Vec3 n = normalize(cross(e1, e2));

        if (fabs(n.z) > 0.7f) {
            int minx = max(0, (int)floor(min({t.p[0].x, t.p[1].x, t.p[2].x})));
            int maxx = min(grid.sx - 1, (int)ceil(max({t.p[0].x, t.p[1].x, t.p[2].x})));
            int miny = max(0, (int)floor(min({t.p[0].y, t.p[1].y, t.p[2].y})));
            int maxy = min(grid.sy - 1, (int)ceil(max({t.p[0].y, t.p[1].y, t.p[2].y})));
            Vec2 p0={t.p[0].x,t.p[0].y}, p1={t.p[1].x,t.p[1].y}, p2={t.p[2].x,t.p[2].y};
            for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
                float u,v,w;
                if (barycentric({x+0.5f,y+0.5f},p0,p1,p2,u,v,w)) {
                    float fz = u*t.p[0].z + v*t.p[1].z + w*t.p[2].z;
                    int iz = (int)floor(fz);
                    if (grid.inBounds(x,y,iz)) {
                        float op = triOpacityBary(t, u, v, w, mats);
                        if (op == 0.0f) continue;   // fully transparent: leave EMPTY

                        // Only overwrite if this material has higher priority (lower rank = smaller area = wins)
                        const string& existingMat = grid.getMaterialName(x, y, iz);
                        if (grid.getKind(x, y, iz) != VoxelNormal::EMPTY) {
                            int existingPriority = matPriorities.count(existingMat) ? matPriorities.at(existingMat) : INT_MAX;
                            int newPriority      = matPriorities.count(t.material)  ? matPriorities.at(t.material)  : INT_MAX;
                            if (newPriority >= existingPriority) continue;  // existing wins, skip
                        }

                        Vec3 col = triColor(t, u, v, w, mats);
                        grid.set(x, y, iz, pal.get(col), VoxelNormal::SURFACE, n, matId, op);
                    }
                }
            }
        } else {
            int minZ = max(0, (int)floor(min({t.p[0].z, t.p[1].z, t.p[2].z})));
            int maxZ = min(grid.sz - 1, (int)ceil(max({t.p[0].z, t.p[1].z, t.p[2].z})));
            for (int z = minZ; z <= maxZ; z++) {
                float zc = z + 0.5f;
                struct ClipVert { Vec3 p; Vec2 uv; };
                vector<ClipVert> poly;
                for (int i = 0; i < 3; i++) {
                    int j = (i+1)%3;
                    Vec3 a=t.p[i], b=t.p[j]; Vec2 uva=t.uv[i], uvb=t.uv[j];
                    if (fabs(a.z-zc) < 1e-6f) poly.push_back({a,uva});
                    if ((a.z-zc)*(b.z-zc) < 0.0f) {
                        float tt=(zc-a.z)/(b.z-a.z);
                        poly.push_back({a+(b-a)*tt, {uva.x+(uvb.x-uva.x)*tt, uva.y+(uvb.y-uva.y)*tt}});
                    }
                }
                if (poly.size() < 2) continue;
                Vec2 boxMin{1e9,1e9}, boxMax{-1e9,-1e9};
                for (auto& v : poly) { boxMin.x=min(boxMin.x,v.p.x); boxMin.y=min(boxMin.y,v.p.y); boxMax.x=max(boxMax.x,v.p.x); boxMax.y=max(boxMax.y,v.p.y); }
                int minx=max(0,(int)floor(boxMin.x)), maxx=min(grid.sx-1,(int)ceil(boxMax.x));
                int miny=max(0,(int)floor(boxMin.y)), maxy=min(grid.sy-1,(int)ceil(boxMax.y));

                if (poly.size() == 2) {
                    Vec2 a={poly[0].p.x,poly[0].p.y}, b={poly[1].p.x,poly[1].p.y};
                    Vec2 ab={b.x-a.x,b.y-a.y};
                    float lenSq=ab.x*ab.x+ab.y*ab.y;
                    for (int y=miny;y<=maxy;y++) for (int x=minx;x<=maxx;x++) {
                        Vec2 p={x+0.5f,y+0.5f};
                        float tseg=lenSq>0?((p.x-a.x)*ab.x+(p.y-a.y)*ab.y)/lenSq:0;
                        if (tseg>=0&&tseg<=1) {
                            Vec2 q={a.x+ab.x*tseg,a.y+ab.y*tseg};
                            if ((p.x-q.x)*(p.x-q.x)+(p.y-q.y)*(p.y-q.y)<=2.0f) {
                                Vec2 uv={poly[0].uv.x*(1-tseg)+poly[1].uv.x*tseg, poly[0].uv.y*(1-tseg)+poly[1].uv.y*tseg};
                                float op = triOpacity(t.material, uv, mats);
                                if (op == 0.0f) continue;
                                auto it=mats.find(t.material);

                                // Only overwrite if this material has higher priority (lower rank = smaller area = wins)
                                const string& existingMat = grid.getMaterialName(x, y, z);
                                if (grid.getKind(x, y, z) != VoxelNormal::EMPTY) {
                                    int existingPriority = matPriorities.count(existingMat) ? matPriorities.at(existingMat) : INT_MAX;
                                    int newPriority      = matPriorities.count(t.material)  ? matPriorities.at(t.material)  : INT_MAX;
                                    if (newPriority >= existingPriority) continue;  // existing wins, skip
                                }

                                Vec3 col=(it!=mats.end())
                                    ? ((it->second.tex.w>0)?sample(it->second.tex,uv).rgb:it->second.diffuseColor)
                                    : Vec3{1,0,1};
                                if (grid.inBounds(x,y,z))
                                    grid.set(x,y,z,pal.get(col),VoxelNormal::SURFACE,n,matId,op);
                            }
                        }
                    }
                } else if (poly.size() >= 3) {
                    Vec2 p0={poly[0].p.x,poly[0].p.y},p1={poly[1].p.x,poly[1].p.y},p2={poly[2].p.x,poly[2].p.y};
                    for (int y=miny;y<=maxy;y++) for (int x=minx;x<=maxx;x++) {
                        float u,v,w;
                        if (barycentric({x+0.5f,y+0.5f},p0,p1,p2,u,v,w)) {
                            Vec2 uv={poly[0].uv.x*u+poly[1].uv.x*v+poly[2].uv.x*w, poly[0].uv.y*u+poly[1].uv.y*v+poly[2].uv.y*w};
                            float op = triOpacity(t.material, uv, mats);
                            if (op == 0.0f) continue;
                            auto it=mats.find(t.material);
                            // Only overwrite if this material has higher priority (lower rank = smaller area = wins)
                            const string& existingMat = grid.getMaterialName(x, y, z);
                            if (grid.getKind(x, y, z) != VoxelNormal::EMPTY) {
                                int existingPriority = matPriorities.count(existingMat) ? matPriorities.at(existingMat) : INT_MAX;
                                int newPriority      = matPriorities.count(t.material)  ? matPriorities.at(t.material)  : INT_MAX;
                                if (newPriority >= existingPriority) continue;  // existing wins, skip
                            }
                            Vec3 col=(it!=mats.end())
                                ? ((it->second.tex.w>0)?sample(it->second.tex,uv).rgb:it->second.diffuseColor)
                                : Vec3{1,0,1};
                            if (grid.inBounds(x,y,z))
                                grid.set(x,y,z,pal.get(col),VoxelNormal::SURFACE,n,matId,op);
                        }
                    }
                }
            }
        }
    }
}

// Shell-only rasterizer: returns Voxel list with opacity set.
// Fully transparent (opacity == 0) voxels are excluded entirely.
vector<Voxel> rasterizeChunk(int ox, int oy, int oz, int sx, int sy, int sz,
                             const vector<VTri>& tris,
                             const map<string, Material>& mats, Palette& pal, const map<string, int>& matPriorities)
{
    map<uint64_t, size_t> bestVoxel;
    vector<Voxel> localVoxels;

    auto tryInsert = [&](int x, int y, int z, uint8_t color, const string& mat, float op) {
        uint64_t key = ((uint64_t)x << 42) | ((uint64_t)y << 21) | (uint64_t)z;
        int newPriority = matPriorities.count(mat) ? matPriorities.at(mat) : INT_MAX;

        auto it = bestVoxel.find(key);
        if (it != bestVoxel.end()) {
            // A voxel already exists here — check priority
            int existingPriority = matPriorities.count(localVoxels[it->second].material)
                                 ? matPriorities.at(localVoxels[it->second].material)
                                 : INT_MAX;
            if (newPriority >= existingPriority) return;  // existing wins
            // Replace
            localVoxels[it->second] = {x, y, z, color, mat, op};
        } else {
            bestVoxel[key] = localVoxels.size();
            localVoxels.push_back({x, y, z, color, mat, op});
        }
    };

    for (const auto& t : tris) {
        Vec3 e1 = t.p[1] - t.p[0], e2 = t.p[2] - t.p[0];
        Vec3 n = normalize(cross(e1, e2));
        if (fabs(n.z) > 0.7f) {
            int minx = max(ox, (int)floor(min({t.p[0].x, t.p[1].x, t.p[2].x}))), maxx = min(ox + sx - 1, (int)ceil(max({t.p[0].x, t.p[1].x, t.p[2].x})));
            int miny = max(oy, (int)floor(min({t.p[0].y, t.p[1].y, t.p[2].y}))), maxy = min(oy + sy - 1, (int)ceil(max({t.p[0].y, t.p[1].y, t.p[2].y})));
            Vec2 p0 = {t.p[0].x, t.p[0].y}, p1 = {t.p[1].x, t.p[1].y}, p2 = {t.p[2].x, t.p[2].y};
            for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
                float u, v, w; if (barycentric({x + 0.5f, y + 0.5f}, p0, p1, p2, u, v, w)) {
                    float z = u * t.p[0].z + v * t.p[1].z + w * t.p[2].z;
                    int iz = (int)floor(z); if (iz >= oz && iz < oz + sz) {
                        Vec2 uv = {u * t.uv[0].x + v * t.uv[1].x + w * t.uv[2].x, u * t.uv[0].y + v * t.uv[1].y + w * t.uv[2].y};
                        float op = triOpacity(t.material, uv, mats);
                        if (op == 0.0f) continue;
                        Vec3 col;
                        auto it = mats.find(t.material);
                        if (it != mats.end()) {
                            if (it->second.tex.w > 0) col = sample(it->second.tex, uv).rgb;
                            else col = it->second.diffuseColor;
                        } else col = Vec3{1, 0, 1};
                        tryInsert(x, y, iz, pal.get(col), t.material, op);
                    }
                }
            }
        } else {
            int minZ = max(oz, (int)floor(min({t.p[0].z, t.p[1].z, t.p[2].z}))), maxZ = min(oz + sz - 1, (int)ceil(max({t.p[0].z, t.p[1].z, t.p[2].z})));
            for (int z = minZ; z <= maxZ; z++) {
                float zc = z + 0.5f; struct ClipVert { Vec3 p; Vec2 uv; }; vector<ClipVert> poly;
                for (int i = 0; i < 3; i++) {
                    int j = (i + 1) % 3; Vec3 a = t.p[i], b = t.p[j]; Vec2 uva = t.uv[i], uvb = t.uv[j];
                    if (fabs(a.z - zc) < 1e-6f) poly.push_back({a, uva});
                    if ((a.z - zc) * (b.z - zc) < 0.0f) {
                        float tt = (zc - a.z) / (b.z - a.z);
                        poly.push_back({a + (b - a) * tt, {uva.x + (uvb.x - uva.x) * tt, uva.y + (uvb.y - uva.y) * tt}});
                    }
                }
                if (poly.size() < 2) continue;
                Vec2 boxMin{1e9, 1e9}, boxMax{-1e9, -1e9};
                for (auto& v : poly) { boxMin.x = min(boxMin.x, v.p.x); boxMin.y = min(boxMin.y, v.p.y); boxMax.x = max(boxMax.x, v.p.x); boxMax.y = max(boxMax.y, v.p.y); }
                int minx = max(ox, (int)floor(boxMin.x)), maxx = min(ox + sx - 1, (int)ceil(boxMax.x)), miny = max(oy, (int)floor(boxMin.y)), maxy = min(oy + sy - 1, (int)ceil(boxMax.y));
                if (poly.size() == 2) {
                    Vec2 a = {poly[0].p.x, poly[0].p.y}, b = {poly[1].p.x, poly[1].p.y}, ab = {b.x - a.x, b.y - a.y};
                    float lenSq = ab.x * ab.x + ab.y * ab.y;
                    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
                        Vec2 p = {x + 0.5f, y + 0.5f}; float tseg = lenSq > 0 ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq : 0;
                        if (tseg >= 0 && tseg <= 1) {
                            Vec2 q = {a.x + ab.x * tseg, a.y + ab.y * tseg};
                            if ((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) <= 2.0f) {
                                Vec2 uv = {poly[0].uv.x * (1 - tseg) + poly[1].uv.x * tseg, poly[0].uv.y * (1 - tseg) + poly[1].uv.y * tseg};
                                float op = triOpacity(t.material, uv, mats);
                                if (op == 0.0f) continue;
                                Vec3 col; auto it = mats.find(t.material);
                                if (it != mats.end()) col = (it->second.tex.w > 0) ? sample(it->second.tex, uv).rgb : it->second.diffuseColor;
                                else col = Vec3{1, 0, 1};
                                tryInsert(x, y, z, pal.get(col), t.material, op);
                            }
                        }
                    }
                } else if (poly.size() >= 3) {
                    Vec2 p0 = {poly[0].p.x, poly[0].p.y}, p1 = {poly[1].p.x, poly[1].p.y}, p2 = {poly[2].p.x, poly[2].p.y};
                    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
                        float u, v, w; if (barycentric({x + 0.5f, y + 0.5f}, p0, p1, p2, u, v, w)) {
                            Vec2 uv = {poly[0].uv.x * u + poly[1].uv.x * v + poly[2].uv.x * w, poly[0].uv.y * u + poly[1].uv.y * v + poly[2].uv.y * w};
                            float op = triOpacity(t.material, uv, mats);
                            if (op == 0.0f) continue;
                            Vec3 col; auto it = mats.find(t.material);
                            if (it != mats.end()) col = (it->second.tex.w > 0) ? sample(it->second.tex, uv).rgb : it->second.diffuseColor;
                            else col = Vec3{1, 0, 1};
                            tryInsert(x, y, z, pal.get(col), t.material, op);
                        }
                    }
                }
            }
        }
    }
    return localVoxels;
}

/* ------------------------------------------------------------------ */
/*  Final opacity pass                                                  */
/* ------------------------------------------------------------------ */
// For every voxel with 0 < opacity < 1, use the opacity value as the
// probability of keeping the voxel.  Fully opaque voxels (opacity >= 1)
// are always kept.  The RNG is seeded with a fixed value so two runs of
// the program with identical input always produce identical output.
// The per-voxel decision is derived from the voxel's own flat grid index
// so that shuffling the input order doesn't change individual outcomes.
void applyOpacityPass(vector<Voxel>& voxels, int gridSx, int gridSy) {
    // We use a single seeded RNG but derive each voxel's threshold from
    // a hash of its grid position so the result is position-stable.
    // A simple multiplicative hash over (x, y, z) mixed with a fixed seed.
    auto cellHash = [](int x, int y, int z) -> uint32_t {
        uint32_t h = 2166136261u;
        h ^= (uint32_t)x; h *= 16777619u;
        h ^= (uint32_t)y; h *= 16777619u;
        h ^= (uint32_t)z; h *= 16777619u;
        // Extra mixing to spread low bits.
        h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16;
        return h;
    };

    // Fixed seed mixed into every hash so two different models with
    // coincidentally identical coordinates still differ.
    const uint32_t SEED = 0xDEADBEEFu;

    vector<Voxel> kept;
    kept.reserve(voxels.size());

    int removed = 0;
    for (const auto& v : voxels) {
        if (v.opacity >= 0.999f) {
            kept.push_back(v);
            continue;
        }
        // Convert hash to a float in [0, 1) and keep if below opacity.
        uint32_t h = cellHash(v.x, v.y, v.z) ^ SEED;
        float threshold = (h & 0xFFFFFFu) / (float)0x1000000u; // 24-bit precision
        if (threshold < pow(v.opacity, 0.3)) {
            kept.push_back(v);
        } else {
            removed++;
        }
    }

    //if (removed > 0)
        //cout << "  Opacity pass: removed " << removed << " of "
        //     << voxels.size() << " voxels." << endl;

    voxels = move(kept);
}

// Grid-based variant used after solidFill.  Marks removed voxels as SPACE
// in place so no extra vector is needed.
void applyOpacityPassGrid(VoxelGrid& grid) {
    auto cellHash = [](int x, int y, int z) -> uint32_t {
        uint32_t h = 2166136261u;
        h ^= (uint32_t)x; h *= 16777619u;
        h ^= (uint32_t)y; h *= 16777619u;
        h ^= (uint32_t)z; h *= 16777619u;
        h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16;
        return h;
    };
    const uint32_t SEED = 0xDEADBEEFu;

    int removed = 0;
    for (int z = 0; z < grid.sz; z++)
    for (int y = 0; y < grid.sy; y++)
    for (int x = 0; x < grid.sx; x++) {
        VoxelNormal k = grid.getKind(x, y, z);
        if (k != VoxelNormal::SURFACE && k != VoxelNormal::INTERIOR) continue;

        float op = pow(grid.getOpacity(x, y, z), 0.3);
        if (op >= 1.0f) continue;

        uint32_t h = cellHash(x, y, z) ^ SEED;
        float threshold = (h & 0xFFFFFFu) / (float)0x1000000u;
        if (threshold >= op) {
            // Remove: mark as SPACE so it is skipped during write-out.
            grid.set(x, y, z, 0, VoxelNormal::SPACE, {0,0,0}, 0, 1.0f);
            removed++;
        }
    }

    //if (removed > 0)
        //cout << "  Opacity pass: removed " << removed << " voxels from grid." << endl;
}

/* -------------------- ChunkEntry -------------------- */

struct ChunkEntry {
    uint64_t key;
    int triIdx;
    bool operator<(const ChunkEntry& other) const { return key < other.key; }
};

#include <string>
#include <algorithm>

bool to_bool(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return (str == "true" || str == "1" || str == "yes");
}

void handleConfig(float &scale, int &maxDim, int &CHUNK_SIZE, bool &onFloor, string &colorRanges, float &voxelsPerUnit, bool &isSolidFill) {
    string configPath = "config.txt";
    
    if (!filesystem::exists(configPath)) {
        ofstream outFile(configPath);
        outFile << "scale=1.0                           # Attempts to multiply the size of the model by this value. \n";
        outFile << "chunksize=128                       # Default 128. Anything greater than 256 will cause integer overflow and corrupt the .vox. Smaller chunks use less RAM during conversion but may take more time and hard drive space\n";
        outFile << "maxDim=1000                         # Maximum dimension size, automatically lowers scale if scale is too big. MagicaVoxel squishes/poorly handles .vox files with dimensions greater than 2000x2000x2000 voxels, but other software (Like Teardown) can handle large dimensions well.\n";
        outFile << "onFloor=True                        # When true, the .vox model will be offset so that the lowest part is at Z = 0. Keep in mind this will cut MagicaVoxel max height in half, but can make using/viewing the model easier\n";
        outFile << "colorRanges=1-184,185-255           # Determines which indexes should be used for colors. Most use cases should just leave at 1-255, but Teardown determines materials based on color index and only uses 1-184. Check Teardown modding wiki for palette details.\n";
        outFile << "voxelsPerUnit=12.0                  # Represents how many voxels is equal to 1 OBJ unit in length (1 OBJ unit = 1 meter in Blender). Is multiplied by scale to determine the final size.  A value of 12 is roughly 1m in Teardown, 1 is 1m in minecraft (personally think 0.914 might scale better for MC)\n";
        outFile << "isSolidFill=False                   # Experimental and very slow for large dimensions, be patient. If false, only creates the surface/shell and leaves the inside empty. If true, fills in most empty voxels that are behind faces and not directly exposed. Best to test this on smaller scales first. If what are supposed to be outer surfaces get covered in a cloud of voxels, you have flipped normals/exposed backfaces. If the inside has a lot of holes, you may have a leak or some intersecting faces causing it.\n";
        outFile.close();
    }

    ifstream inFile(configPath);
    string line;
    while (getline(inFile, line)) {
        size_t commentPos = line.find('#');
        if (commentPos != string::npos) line = line.substr(0, commentPos);
        line.erase(line.find_last_not_of(" \n\r\t") + 1);
        
        size_t delimPos = line.find('=');
        if (delimPos != string::npos) {
            string key = line.substr(0, delimPos);
            string val = line.substr(delimPos + 1);
            
            if (key == "scale") scale = stof(val);
            else if (key == "maxDim") maxDim = stoi(val);
            else if (key == "chunksize") CHUNK_SIZE = stoi(val);
            else if (key == "onFloor") onFloor = to_bool(val);
            else if (key == "colorRanges") colorRanges = val;
            else if (key == "voxelsPerUnit") voxelsPerUnit = stof(val);
            else if (key == "isSolidFill") isSolidFill = to_bool(val);
        }
    }
}


int main(int argc, char** argv) {
    if (argc < 2) { 
        cerr << "usage: " << argv[0] << " in.obj [scale] [maxDim] [chunkSize] [isSolidFill] [out.vox]\n"; 
        return 1; 
    }

    float scale = 1.0f;
    int maxDim = 1;
    int CHUNK_SIZE = 128;
    bool onFloor = true;
    int colors = 255;
    string colorRanges = "1-255";
    float voxelsPerUnit = 1.0f;
    bool isSolidFill = false;
    
    string inPath = argv[1];
    string outPath = filesystem::path(inPath).replace_extension(".vox").string();

    handleConfig(scale, maxDim, CHUNK_SIZE, onFloor, colorRanges, voxelsPerUnit, isSolidFill);

    if (argc > 2) scale = atof(argv[2]);
    if (argc > 3) maxDim = atoi(argv[3]);
    if (argc > 4) CHUNK_SIZE = atoi(argv[4]);
    if (argc > 5) isSolidFill = to_bool(argv[5]);
    if (argc > 6) outPath = argv[6]; 

    cout << "Running with: Scale = " << scale << ", Max Dim = " << maxDim 
         << ", Chunk Size = " << CHUNK_SIZE << ", Is Solid Fill = " << isSolidFill 
         << ", Output = " << outPath << endl;

    //std::cout << "Loading OBJ..." << endl;
    auto tris = loadOBJ(argv[1]);

    std::cout << "Correcting rotation..." << endl;
    for (auto& t : tris) {
        for (int i = 0; i < 3; i++) {
            float oldY = t.p[i].y;
            float oldZ = t.p[i].z;
            t.p[i].y = -oldZ;
            t.p[i].z = oldY;
        }
    }

    Vec3 mn{1e9, 1e9, 1e9}, mx{-1e9, -1e9, -1e9};
    std::cout << "Determining size..." << endl;
    for (auto& t : tris) for (int i = 0; i < 3; i++) { mn = minv(mn, t.p[i]); mx = maxv(mx, t.p[i]); }
    
    Vec3 size = mx - mn;
    scale = voxelsPerUnit * scale;

    cout << setprecision(4) << "OBJ original size: " << size.x << "x" << size.y << "x" << size.z << " Units (Blender Meters)" << endl;
    float largestDim = max({size.x, size.y, size.z});
    
    cout << "Largest Dimension: " << round(scale * largestDim) << " Voxels" << endl;
    float maxScale = (float)maxDim / largestDim;
    if (maxScale < scale)
    {
        scale = maxScale;
        cout << "Specified scale would exceed max dimension of " << maxDim << ", setting scale to " << scale/voxelsPerUnit << endl;
    }

    int sx = round(size.x * scale), sy = round(size.y * scale), sz = round(size.z * scale);
    cout << "Voxel Grid Dimensions: " << sx << "x" << sy << "x" << sz << endl;

    vector<VTri> vtris; vtris.reserve(tris.size());
    for (auto& t : tris) {
        VTri vt; vt.material = t.material;
        for (int i = 0; i < 3; i++) { 
            vt.p[i] = (t.p[i] - mn) * scale; 
            vt.uv[i] = t.uv[i]; 
        }
        vtris.push_back(vt);
    }
    tris.clear(); tris.shrink_to_fit();

    // --- Palette ---
    string objPath = argv[1];
    string pngPath = objPath;
    size_t lastDot = pngPath.find_last_of(".");
    if (lastDot != string::npos) pngPath = pngPath.substr(0, lastDot);
    pngPath += ".png";
    Palette pal;
    if (!pal.loadFromPNG(pngPath)) {
        cout << "Generating palette..." << endl;
        
        vector<Vec3> allTextureColors;
        const int MAX_TOTAL_SAMPLES = 2000000;
        allTextureColors.reserve(MAX_TOTAL_SAMPLES);
        
        // Pass 1: compute how many samples each triangle gets
        struct TriSample { const VTri* tri; const Material* mat; int numSamples; };
        vector<TriSample> plan;
        long long totalPlanned = 0;

        for (const auto& t : vtris) {
            auto it = materials.find(t.material);
            if (it == materials.end()) continue;

            Vec3 e1 = t.p[1] - t.p[0], e2 = t.p[2] - t.p[0];
            float worldArea = 0.5f * sqrt(max(0.0f, dot(cross(e1, e2), cross(e1, e2))));
            float rawSamples = worldArea * 0.1f;

            if (it->second.tex.w > 0) {
                Vec2 uv1 = t.uv[1] - t.uv[0];
                Vec2 uv2 = t.uv[2] - t.uv[0];
                float uvArea = 0.5f * abs(uv1.x * uv2.y - uv1.y * uv2.x);
                float texelFootprint = uvArea * it->second.tex.w * it->second.tex.h;
                rawSamples = min(rawSamples, texelFootprint);
            }

            int numSamples = (int)rawSamples;
            float remainder = rawSamples - (float)numSamples;
            if ((float)rand() / RAND_MAX < remainder) numSamples++;

            if (numSamples > 0) {
                plan.push_back({&t, &it->second, numSamples});
                totalPlanned += numSamples;
            }
        }

        // Scale down proportionally if over budget, then shuffle
        if (totalPlanned > MAX_TOTAL_SAMPLES) {
            float scale = (float)MAX_TOTAL_SAMPLES / totalPlanned;
            cout << "Model is large. Sampling " << (scale * 100.0f) << "% of adjusted footprint..." << endl;
            for (auto& ts : plan) {
                float scaled = ts.numSamples * scale;
                int n = (int)scaled;
                if ((float)rand() / RAND_MAX < (scaled - n)) n++;
                ts.numSamples = n;
            }
        }

        // Shuffle so the MAX_TOTAL_SAMPLES cutoff doesn't bias toward early triangles
        shuffle(plan.begin(), plan.end(), default_random_engine(rand()));

        // Pass 2: collect samples
        for (auto& ts : plan) {
            for (int k = 0; k < ts.numSamples; k++) {
                if (allTextureColors.size() >= MAX_TOTAL_SAMPLES) break;
                if (ts.mat->tex.w > 0) {
                    float r1 = (float)rand() / RAND_MAX, r2 = (float)rand() / RAND_MAX;
                    if (r1 + r2 > 1.0f) { r1 = 1.0f - r1; r2 = 1.0f - r2; }
                    float r3 = 1.0f - r1 - r2;
                    Vec2 uv = ts.tri->uv[0] * r1 + ts.tri->uv[1] * r2 + ts.tri->uv[2] * r3;
                    allTextureColors.push_back(sample(ts.mat->tex, uv).rgb);
                } else {
                    allTextureColors.push_back(ts.mat->diffuseColor);
                }
            }
        }
        
        cout << "Building palette from " << allTextureColors.size() << " samples..." << endl;

        map<tuple<int,int,int>, int> colorCounts;
        for (auto& c : allTextureColors) {
            int r = (int)(c.x * 255.0f) >> 2; // 6-bit quantization
            int g = (int)(c.y * 255.0f) >> 2;
            int b = (int)(c.z * 255.0f) >> 2;
            colorCounts[{r, g, b}]++;
        }

        // Rebuild as weighted list — repeat each color proportional to its count
        // but capped so one dominant color doesn't fully take over
        vector<Vec3> quantizedColors;
        for (auto& [key, count] : colorCounts) {
            auto [r, g, b] = key;
            Vec3 c = { (r << 2) / 255.0f, (g << 2) / 255.0f, (b << 2) / 255.0f };
            int reps = max(1, (int)(logf((float)count + 1) * 100));
            for (int i = 0; i < reps; i++)
                quantizedColors.push_back(c);
        }

        pal.buildFromColors(quantizedColors, colors, colorRanges);
        allTextureColors.clear();
        allTextureColors.shrink_to_fit();
    }

    // --- Compute Material Priorities based on Surface Area ---
    cout << "Calculating material priorities..." << endl;
    map<string, float> matAreas;
    for (const auto& t : vtris) {
        Vec3 e1 = t.p[1] - t.p[0], e2 = t.p[2] - t.p[0];
        float area = 0.5f * sqrt(max(0.0f, dot(cross(e1, e2), cross(e1, e2))));
        matAreas[t.material] += area;
    }

    // Sort material names by area (ascending: smallest area gets rank 0)
    vector<pair<string, float>> sortedMats(matAreas.begin(), matAreas.end());
    sort(sortedMats.begin(), sortedMats.end(), [](const auto& a, const auto& b) {
        return a.second < b.second; 
    });

    map<string, int> matPriorities;
    for (int rank = 0; rank < (int)sortedMats.size(); rank++) {
        matPriorities[sortedMats[rank].first] = rank;
        //cout << "Material Rank [" << rank << "]: " << sortedMats[rank].first 
        //    << " (Area: " << sortedMats[rank].second << ")" << endl;
    }

    /* ================================================================
       SOLID FILL PATH
       ================================================================ */
    if (isSolidFill) {
        cout << "Solid fill mode: allocating " << sx << "x" << sy << "x" << sz << " voxel grid..." << endl;

        size_t cellCount = size_t(sx) * sy * sz;
        size_t estMB = cellCount * (sizeof(uint8_t) + sizeof(VoxelNormal) + sizeof(Vec3) + sizeof(uint16_t) + sizeof(float)) / (1024*1024);
        cout << "  Estimated grid RAM: ~" << estMB << " MB" << endl;

        VoxelGrid grid(sx, sy, sz);

        cout << "Rasterizing surface into grid..." << endl;
        {
            vector<ChunkEntry> flatGrid;
            flatGrid.reserve(vtris.size() * 2);
            float fChunk = (float)CHUNK_SIZE;
            for (int i = 0; i < (int)vtris.size(); i++) {
                const auto& t = vtris[i];
                int min_cx = max(0, (int)floor(min({t.p[0].x, t.p[1].x, t.p[2].x}) / fChunk));
                int max_cx = min((sx - 1) / CHUNK_SIZE, (int)floor(max({t.p[0].x, t.p[1].x, t.p[2].x}) / fChunk));
                int min_cy = max(0, (int)floor(min({t.p[0].y, t.p[1].y, t.p[2].y}) / fChunk));
                int max_cy = min((sy - 1) / CHUNK_SIZE, (int)floor(max({t.p[0].y, t.p[1].y, t.p[2].y}) / fChunk));
                int min_cz = max(0, (int)floor(min({t.p[0].z, t.p[1].z, t.p[2].z}) / fChunk));
                int max_cz = min((sz - 1) / CHUNK_SIZE, (int)floor(max({t.p[0].z, t.p[1].z, t.p[2].z}) / fChunk));
                for (int cz=min_cz;cz<=max_cz;cz++) for (int cy=min_cy;cy<=max_cy;cy++) for (int cx=min_cx;cx<=max_cx;cx++) {
                    uint64_t key=((uint64_t)cx<<42)|((uint64_t)cy<<21)|(uint64_t)cz;
                    flatGrid.push_back({key, i});
                }
                if (i % 50000 == 0) cout << "Mapped " << i << "/" << vtris.size() << " triangles...\r" << flush;
            }
            cout << "\nSorting chunk entries...(very slow for large models, be patient)" << endl;
            sort(flatGrid.begin(), flatGrid.end());

            for (size_t i = 0; i < flatGrid.size(); ) {
                uint64_t currentKey = flatGrid[i].key;
                vector<VTri> chunkTris;
                while (i < flatGrid.size() && flatGrid[i].key == currentKey) {
                    chunkTris.push_back(vtris[flatGrid[i].triIdx]);
                    i++;
                }
                rasterizeIntoGrid(grid, chunkTris, materials, pal, matPriorities);
                cout << "Rasterizing: " << (100*i/flatGrid.size()) << "%\r" << flush;
            }
        }
        vtris.clear(); vtris.shrink_to_fit();
        cout << "\nSurface rasterization complete. Running solid fill..." << endl;

        solidFill(grid, pal);

        cout << "Running opacity pass on filled grid..." << endl;
        applyOpacityPassGrid(grid);

        cout << "Writing filled voxels..." << endl;
        MultiVoxWriter writer(outPath, pal, onFloor, isSolidFill, scale);

        for (int cz = 0; cz * CHUNK_SIZE < sz; cz++)
        for (int cy = 0; cy * CHUNK_SIZE < sy; cy++)
        for (int cx = 0; cx * CHUNK_SIZE < sx; cx++) {
            int ox = cx*CHUNK_SIZE, oy = cy*CHUNK_SIZE, oz = cz*CHUNK_SIZE;
            int csx = min(CHUNK_SIZE, sx-ox);
            int csy = min(CHUNK_SIZE, sy-oy);
            int csz = min(CHUNK_SIZE, sz-oz);

            vector<Voxel> chunkVoxels;
            for (int z = oz; z < oz+csz; z++)
            for (int y = oy; y < oy+csy; y++)
            for (int x = ox; x < ox+csx; x++) {
                VoxelNormal k = grid.getKind(x,y,z);
                if (k == VoxelNormal::SURFACE || k == VoxelNormal::INTERIOR) {
                    uint8_t c = grid.getColor(x,y,z);
                    if (c != 0) {
                        const string& matName = grid.getMaterialName(x, y, z);
                        float op = grid.getOpacity(x, y, z);
                        chunkVoxels.push_back({x, y, z, c, matName, op});
                    }
                }
            }
            if (!chunkVoxels.empty())
                writer.writeChunk(ox, oy, oz, csx, csy, csz, chunkVoxels);
        }

        writer.finalize(onFloor);

    } else {
        /* ================================================================
           ORIGINAL SHELL-ONLY PATH
           ================================================================ */
        vector<ChunkEntry> flatGrid;
        flatGrid.reserve(vtris.size() * 2); 

        float fChunk = (float)CHUNK_SIZE;
        std::cout << "Filtering triangles into chunks..." << endl;
        for (int i = 0; i < (int)vtris.size(); i++) {
            const auto& t = vtris[i];
            int min_cx = max(0, (int)floor(min({t.p[0].x, t.p[1].x, t.p[2].x}) / fChunk));
            int max_cx = min((sx - 1) / CHUNK_SIZE, (int)floor(max({t.p[0].x, t.p[1].x, t.p[2].x}) / fChunk));
            int min_cy = max(0, (int)floor(min({t.p[0].y, t.p[1].y, t.p[2].y}) / fChunk));
            int max_cy = min((sy - 1) / CHUNK_SIZE, (int)floor(max({t.p[0].y, t.p[1].y, t.p[2].y}) / fChunk));
            int min_cz = max(0, (int)floor(min({t.p[0].z, t.p[1].z, t.p[2].z}) / fChunk));
            int max_cz = min((sz - 1) / CHUNK_SIZE, (int)floor(max({t.p[0].z, t.p[1].z, t.p[2].z}) / fChunk));

            long long volume = (long long)(max_cx-min_cx+1)*(max_cy-min_cy+1)*(max_cz-min_cz+1);
            if (volume > 20000) {
                cout << "\n[Warning] Triangle " << i << " is huge! Spans " << volume << " chunks. This will be slow." << endl;
            }

            for (int cz = min_cz; cz <= max_cz; cz++) {
                for (int cy = min_cy; cy <= max_cy; cy++) {
                    for (int cx = min_cx; cx <= max_cx; cx++) {
                        uint64_t key = ((uint64_t)cx << 42) | ((uint64_t)cy << 21) | (uint64_t)cz;
                        flatGrid.push_back({key, i});
                    }
                }
            }
            if (i % 50000 == 0) cout << "Mapped " << i << "/" << vtris.size() << " triangles...\r" << flush;
        }

        cout << "\nSorting " << flatGrid.size() << " chunk entries..." << endl;
        sort(flatGrid.begin(), flatGrid.end());

        MultiVoxWriter writer(outPath, pal, onFloor, isSolidFill, scale);
        cout << "Voxelizing data..." << endl;

        // Accumulate all voxels per chunk, apply opacity pass, then write.
        // We process chunk-by-chunk to keep memory bounded.
        for (size_t i = 0; i < flatGrid.size(); ) {
            uint64_t currentKey = flatGrid[i].key;
            vector<VTri> chunkTris;
            while (i < flatGrid.size() && flatGrid[i].key == currentKey) {
                chunkTris.push_back(vtris[flatGrid[i].triIdx]);
                i++;
            }

            int cx = (currentKey >> 42) & 0x1FFFFF;
            int cy = (currentKey >> 21) & 0x1FFFFF;
            int cz = currentKey & 0x1FFFFF;
            int ox = cx * CHUNK_SIZE, oy = cy * CHUNK_SIZE, oz = cz * CHUNK_SIZE;
            int csx = min(CHUNK_SIZE, sx - ox), csy = min(CHUNK_SIZE, sy - oy), csz = min(CHUNK_SIZE, sz - oz);

            vector<Voxel> chunkVoxels = rasterizeChunk(ox, oy, oz, csx, csy, csz, chunkTris, materials, pal, matPriorities);

            // Apply opacity dithering per chunk.
            applyOpacityPass(chunkVoxels, sx, sy);

            if (!chunkVoxels.empty()) {
                writer.writeChunk(ox, oy, oz, csx, csy, csz, chunkVoxels);
            }

            cout << "\rProgress: " << (100 * i / flatGrid.size()) << "% (" << i << "/" << flatGrid.size() << " entries)" << flush;
        }

        writer.finalize(onFloor);
    }
    string solidIdent = "";
    if (isSolidFill)
    {
        solidIdent = "solid";
    }
    outPath.erase(outPath.size() - 4);
    string outpathComplete = outPath + "_x_" + formatScale(scale) + "_" + solidIdent + ".vox";
    cout << "\nFinished! Saved to " << outpathComplete << endl;
    return 0;
}
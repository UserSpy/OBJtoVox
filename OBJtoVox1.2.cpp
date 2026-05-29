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
    unsigned char* pixels = stbi_load(path.c_str(), &img.w, &img.h, &img.channels, 3);
    if (!pixels) throw runtime_error("Failed to load texture: " + path);
    img.channels = 3;
    img.data.assign(pixels, pixels + img.w * img.h * 3);
    stbi_image_free(pixels);
    return img;
}

Vec3 sample(const Image& img, Vec2 uv) {
    uv.x -= floor(uv.x); uv.y -= floor(uv.y);
    int x = min(img.w - 1, int(uv.x * img.w));
    int y = min(img.h - 1, int((1 - uv.y) * img.h));
    int i = (y * img.w + x) * 3;
    return {img.data[i] / 255.f, img.data[i + 1] / 255.f, img.data[i + 2] / 255.f};
}

/* ----------------------------- OBJ ------------------------------ */

struct Material { 
    Image tex; 
    Vec3 diffuseColor = {1.0f, 1.0f, 1.0f};
    Vec3 fallbackColor = {1.0f, 0.0f, 1.0f};
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
        } else if (t == "Kd" && !matName.empty()) {
            ss >> materials[matName].diffuseColor.x 
               >> materials[matName].diffuseColor.y 
               >> materials[matName].diffuseColor.z;
        } else if (t == "map_Kd" && !matName.empty()) {
            string texpath; getline(ss, texpath);
            texpath.erase(0, texpath.find_first_not_of(" \t"));
            string fullPath = mtllib;
            size_t lastSlash = fullPath.find_last_of("/\\");
            string dir = (lastSlash != string::npos) ? fullPath.substr(0, lastSlash + 1) : "";
            try { materials[matName].tex = loadImage(dir + texpath); } catch (...) {}
        }
    }
}

vector<Tri> loadOBJ(const string& path) {
    ifstream f(path);
    if (!f) throw runtime_error("OBJ open failed");
    vector<Vec3> V; vector<Vec2> VT; vector<Tri> tris;
    string mtllib, line, currentMaterial;

    while (getline(f, line)) {
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
            if (list[i].r == 0 && list[i].g == 0 && list[i].b == 0) continue; 

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
        /*= {
            {9 - 1, 40 -1},
            {57 - 1, 136 -1},
            {153 - 1, 184 -1}
        };*/

        // Parse the range string
        stringstream ss(rangeString);
        string token;
        
        while (getline(ss, token, ',')) {
            // Trim whitespace
            token.erase(token.begin(), find_if(token.begin(), token.end(), 
                [](unsigned char ch) { return !isspace(ch); }));
            token.erase(find_if(token.rbegin(), token.rend(), 
                [](unsigned char ch) { return !isspace(ch); }).base(), token.end());
            
            // Find the dash separator
            size_t dashPos = token.find('-');
            if (dashPos != string::npos) {
                int start = stoi(token.substr(0, dashPos));
                int end = stoi(token.substr(dashPos + 1));
                targetRanges.push_back({start - 1, end - 1}); // Convert to 0-indexed
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
        if (targetCount == 0) return;

        if (uniqueColors.empty()) uniqueColors.push_back({1, 1, 1});
        struct Box { int start, end; Vec3 minV, maxV; };
        vector<Box> boxes; 
        boxes.push_back({0, (int)uniqueColors.size(), {0, 0, 0}, {1, 1, 1}});

        while ((int)boxes.size() < targetCount) {
            int bestBox = -1; float maxSpread = -1;
            for (int i = 0; i < (int)boxes.size(); i++) {
                Vec3 bMin{1, 1, 1}, bMax{0, 0, 0};
                for (int j = boxes[i].start; j < boxes[i].end; j++) {
                    bMin = minv(bMin, uniqueColors[j]); bMax = maxv(bMax, uniqueColors[j]);
                }
                boxes[i].minV = bMin; boxes[i].maxV = bMax;
                float spread = max({bMax.x - bMin.x, bMax.y - bMin.y, bMax.z - bMin.z});
                if (spread > maxSpread && (boxes[i].end - boxes[i].start) > 1) {
                    maxSpread = spread; bestBox = i;
                }
            }
            if (bestBox == -1) break;
            Box b = boxes[bestBox];
            float dx = b.maxV.x - b.minV.x, dy = b.maxV.y - b.minV.y, dz = b.maxV.z - b.minV.z;
            auto sortFn = [&](const Vec3& a, const Vec3& b) {
                if (dx >= dy && dx >= dz) return a.x < b.x;
                if (dy >= dx && dy >= dz) return a.y < b.y;
                return a.z < b.z;
            };
            sort(uniqueColors.begin() + b.start, uniqueColors.begin() + b.end, sortFn);
            int mid = (b.start + b.end) / 2;
            boxes[bestBox] = {b.start, mid, b.minV, b.maxV};
            boxes.push_back({mid, b.end, b.minV, b.maxV});
        }

        numGenerated = activeIndices.back() + 1;

        float tMin = 38.0f / 255.0f;
        float tMax = 225.0f / 255.0f;

        vector<Vec3> avgs;
        Vec3 gMin{1, 1, 1}, gMax{0, 0, 0};
        for (size_t i = 0; i < boxes.size(); i++) {
            Vec3 avg = {0, 0, 0};
            for (int j = boxes[i].start; j < boxes[i].end; j++) avg = avg + uniqueColors[j];
            avg = avg * (1.0f / (boxes[i].end - boxes[i].start));
            avgs.push_back(avg);
            gMin = minv(gMin, avg);
            gMax = maxv(gMax, avg);
        }

        auto getMedian = [](vector<Vec3>& colors, int channel) {
            vector<float> vals;
            for (const auto& v : colors) {
                if (channel == 0) vals.push_back(v.x);
                else if (channel == 1) vals.push_back(v.y);
                else vals.push_back(v.z);
            }
            if (vals.empty()) return 0.5f;
            sort(vals.begin(), vals.end());
            return vals[vals.size() / 2];
        };
        Vec3 medianColor = { getMedian(avgs, 0), getMedian(avgs, 1), getMedian(avgs, 2) };

        for (int i = 0; i < 256; i++) list[i] = {0, 0, 0, 255};

        for (int i = 0; i < (int)activeIndices.size(); i++) {
            int paletteIdx = activeIndices[i];
            if (i < (int)avgs.size()) {
                Vec3 c = avgs[i];
                auto processChannel = [&](float val, float minV, float maxV, float midPivot) {
                    if (val < midPivot && minV < tMin) {
                        if (fabs(midPivot - minV) < 1e-5f) return tMin;
                        return tMin + (val - minV) * (midPivot - tMin) / (midPivot - minV);
                    }
                    if (val >= midPivot && maxV > tMax) {
                        if (fabs(maxV - midPivot) < 1e-5f) return tMax;
                        return midPivot + (val - midPivot) * (tMax - midPivot) / (maxV - midPivot);
                    }
                    return val;
                };

                list[paletteIdx] = {
                    (uint8_t)(max(tMin, min(tMax, processChannel(c.x, gMin.x, gMax.x, medianColor.x))) * 255.0f),
                    (uint8_t)(max(tMin, min(tMax, processChannel(c.y, gMin.y, gMax.y, medianColor.y))) * 255.0f),
                    (uint8_t)(max(tMin, min(tMax, processChannel(c.z, gMin.z, gMax.z, medianColor.z))) * 255.0f),
                    255
                };
            }
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

// Material names are stored in a separate interned table to avoid the cost of
// embedding a std::string in every grid cell (which would balloon RAM for large
// grids).  Each cell stores a uint16_t index into this table; 0 == "no material".
struct MaterialTable {
    vector<string> names;   // names[0] is always "" (no-material sentinel)
    map<string, uint16_t> index;

    MaterialTable() {
        names.push_back("");        // index 0 = no material
        index[""] = 0;
    }

    // Returns the interned index for a name, inserting if necessary.
    // Silently saturates at UINT16_MAX if somehow >65535 materials exist.
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

// Single global material table shared between the grid and solidFill.
static MaterialTable gMatTable;

struct VoxelGrid {
    int sx, sy, sz;
    vector<uint8_t>     color;
    vector<VoxelNormal> kind;
    vector<Vec3>        normal;
    // Material name index per cell (0 = none).  uint16_t keeps the per-cell
    // overhead to 2 bytes regardless of how long material names are.
    vector<uint16_t>    matId;

    VoxelGrid() : sx(0), sy(0), sz(0) {}
    VoxelGrid(int x, int y, int z) : sx(x), sy(y), sz(z),
        color (size_t(x)*y*z, 0),
        kind  (size_t(x)*y*z, VoxelNormal::EMPTY),
        normal(size_t(x)*y*z, {0,0,0}),
        matId (size_t(x)*y*z, 0) {}

    inline bool inBounds(int x, int y, int z) const {
        return x >= 0 && y >= 0 && z >= 0 && x < sx && y < sy && z < sz;
    }
    inline size_t idx(int x, int y, int z) const {
        return size_t(x) + size_t(y)*sx + size_t(z)*sx*sy;
    }

    void set(int x, int y, int z, uint8_t c, VoxelNormal k,
             Vec3 n = {0,0,0}, uint16_t mat = 0) {
        size_t i = idx(x,y,z);
        color [i] = c;
        kind  [i] = k;
        normal[i] = n;
        matId [i] = mat;
    }

    uint8_t     getColor (int x,int y,int z) const { return color [idx(x,y,z)]; }
    VoxelNormal getKind  (int x,int y,int z) const { return kind  [idx(x,y,z)]; }
    Vec3        getNormal(int x,int y,int z) const { return normal[idx(x,y,z)]; }
    uint16_t    getMatId (int x,int y,int z) const { return matId [idx(x,y,z)]; }

    // Convenience: return the interned material name string for a cell.
    const string& getMaterialName(int x, int y, int z) const {
        return gMatTable.lookup(matId[idx(x,y,z)]);
    }
};

/* ------------------------------------------------------------------ */
/*  solidFill                                                           */
/* ------------------------------------------------------------------ */
// Extended to also vote on material names.  For every inside-hit ray we
// record the material id of the hit voxel.  At classification time we pick
// the majority material id (ties broken by a seeded RNG so results are
// deterministic across runs).
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
    size_t printInterval = (initialTodoSize >= 5) ? (initialTodoSize / 5) : 1; 
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

            // Per-ray material vote: record which material id each inside-hit
            // ray sampled.  We only have 6 rays so a small fixed array is fine.
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
                            // Vote: inherit the interior voxel's material
                            matVotes[matVoteCount++] = grid.getMatId(cx, cy, cz);
                            uint8_t ci = grid.getColor(cx, cy, cz);
                            if (ci > 0 && ci <= 255) {
                                const Color& pc = pal.list[ci - 1];
                                colorAccum.x += pc.r / 255.f;
                                colorAccum.y += pc.g / 255.f;
                                colorAccum.z += pc.b / 255.f;
                                colorCount++;
                            }
                        } else {
                            // SURFACE: dot-product inside/outside test
                            Vec3 fn = grid.getNormal(cx, cy, cz);
                            float d_dot = dot(fn, rayDirs[d]);
                            if (d_dot > 0.0f) {
                                insideHits++;
                                // Vote: use the surface voxel's material
                                matVotes[matVoteCount++] = grid.getMatId(cx, cy, cz);
                                uint8_t ci = grid.getColor(cx, cy, cz);
                                if (ci > 0 && ci <= 255) {
                                    const Color& pc = pal.list[ci - 1];
                                    colorAccum.x += pc.r / 255.f;
                                    colorAccum.y += pc.g / 255.f;
                                    colorAccum.z += pc.b / 255.f;
                                    colorCount++;
                                }
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

            // --- classify ---
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

                // --- majority vote for material name ---
                // Count occurrences of each material id seen in inside-hit rays.
                uint16_t bestMat  = 0;
                if (matVoteCount > 0) {
                    // Simple linear scan over at most 6 entries — no need for a map.
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
                    // Find the maximum count; collect all ids that share it.
                    int maxCnt = 0;
                    for (int u = 0; u < unique; u++) if (cnts[u] > maxCnt) maxCnt = cnts[u];
                    // Gather tied winners
                    uint16_t tied[6];
                    int      tieCount = 0;
                    for (int u = 0; u < unique; u++)
                        if (cnts[u] == maxCnt) tied[tieCount++] = ids[u];
                    // Pick one deterministically using the cell's flat index as seed
                    bestMat = tied[flatIdx % tieCount];
                }

                grid.set(x, y, z, ci, VoxelNormal::INTERIOR, {0,0,0}, bestMat);
                resolved++;
                cellResolved = true;
            } else if (outsideHits >= 4) {
                grid.set(x, y, z, 0, VoxelNormal::SPACE);
                resolved++;
                cellResolved = true;
            } else {
                nextTodo.push_back(flatIdx);
            }

            // --- Mid-pass Progress Update ---
            if (cellResolved) {
                totalResolvedSoFar++;
                if (totalResolvedSoFar % printInterval == 0 || totalResolvedSoFar == initialTodoSize) {
                    size_t estimatedRemaining = initialTodoSize - totalResolvedSoFar;
                    cout << "  solidFill pass " << (passNum + 1)
                        << ": resolved " << totalResolvedSoFar << "/" << initialTodoSize
                        << " total cells. (Remaining: " << estimatedRemaining << ")    \r" << endl;
                }
            }
        }

        passNum++;
        cout << "  solidFill pass " << passNum
             << ": resolved " << resolved
             << ", remaining " << nextTodo.size() << "     \r" << flush;

        todo = move(nextTodo);
    }
    cout << "\n  solidFill complete." << endl;
}

/* -------------------- Voxel struct -------------------- */
// Added material name field so downstream writers can access it without
// needing to re-query the grid.
struct Voxel {
    int x, y, z;
    uint8_t color;
    string material;   // empty string if no material
};

/* =================== Multi-VOX writer (unchanged) =================== */

class MultiVoxWriter {
    string baseName;
    bool isSolidFill;
    float scale;
    int fileIdx = 0;
    ofstream f;
    Palette palette;
    
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
        
        // Remove trailing zeros and possible trailing dot
        result.erase(result.find_last_not_of('0') + 1, std::string::npos);
        if (!result.empty() && result.back() == '.') {
            result.pop_back();
        }
        return result;
    }
    void startNewFile() {
        if (f.is_open()) finalize();
        string solidIdent = "";
        if (isSolidFill)
        {
            solidIdent = "_solid";
        }
        
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
    MultiVoxWriter(const string& path, const Palette& pal, const bool isSolidFill, const float scale) : baseName(path), palette(pal), isSolidFill(isSolidFill), scale(scale) {
        if (baseName.find(".vox") != string::npos) baseName = baseName.substr(0, baseName.find(".vox"));
        startNewFile();
    }

    // The VOX format does not carry material names, so this writer ignores
    // Voxel::material.  It is present in the struct for other exporters.
    void writeChunk(int ox, int oy, int oz, int sx, int sy, int sz, const vector<Voxel>& voxels) {
        if (voxels.empty()) return;

        long long currentPos = (long long)f.tellp();
        long long estSize = (long long)voxels.size() * 4 + 1024;

        if (currentPos + estSize > (long long)SIZE_THRESHOLD) {
            startNewFile();
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

    void finalize() {
        if (!f.is_open()) return;
        int32_t nodeId = 0, rootTrnId = nodeId++, grpId = nodeId++;
        auto W = [&](auto v) { f.write((char*)&v, sizeof(v)); };

        { long p = writeChunkHeader("nTRN"); W(rootTrnId); W(0); W(grpId); W(-1); W(0); W(1); writeVoxDict({}); fixChunkHeader(p); }
        { long p = writeChunkHeader("nGRP"); W(grpId); writeVoxDict({}); W((int32_t)chunks.size()); for (size_t i = 0; i < chunks.size(); i++) W((int32_t)(2 + i * 2)); fixChunkHeader(p); }

        for (size_t i = 0; i < chunks.size(); i++) {
            const auto& c = chunks[i];
            int32_t transId = nodeId++, shapeId = nodeId++;
            {
                long p = writeChunkHeader("nTRN");
                W(transId); writeVoxDict({}); W(shapeId); W(-1); W(0); W(1);
                string sOffset = to_string((int)(c.ox + c.sx / 2.0f)) + " " + to_string((int)(c.oy + c.sy / 2.0f)) + " " + to_string((int)(c.oz + c.sz / 2.0f));
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
        return (it->second.tex.w > 0) ? sample(it->second.tex, uv) : it->second.diffuseColor;
    }
    return {1, 0, 1};
}

// Rasterize into a VoxelGrid, now also storing the interned material id.
void rasterizeIntoGrid(VoxelGrid& grid, const vector<VTri>& tris,
                       const map<string, Material>& mats, Palette& pal)
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
                        Vec3 col = triColor(t, u, v, w, mats);
                        grid.set(x, y, iz, pal.get(col), VoxelNormal::SURFACE, n, matId);
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
                                auto it=mats.find(t.material);
                                Vec3 col=(it!=mats.end())
                                    ? ((it->second.tex.w>0)?sample(it->second.tex,uv):it->second.diffuseColor)
                                    : Vec3{1,0,1};
                                if (grid.inBounds(x,y,z))
                                    grid.set(x,y,z,pal.get(col),VoxelNormal::SURFACE,n,matId);
                            }
                        }
                    }
                } else if (poly.size() >= 3) {
                    Vec2 p0={poly[0].p.x,poly[0].p.y},p1={poly[1].p.x,poly[1].p.y},p2={poly[2].p.x,poly[2].p.y};
                    for (int y=miny;y<=maxy;y++) for (int x=minx;x<=maxx;x++) {
                        float u,v,w;
                        if (barycentric({x+0.5f,y+0.5f},p0,p1,p2,u,v,w)) {
                            Vec2 uv={poly[0].uv.x*u+poly[1].uv.x*v+poly[2].uv.x*w, poly[0].uv.y*u+poly[1].uv.y*v+poly[2].uv.y*w};
                            auto it=mats.find(t.material);
                            Vec3 col=(it!=mats.end())
                                ? ((it->second.tex.w>0)?sample(it->second.tex,uv):it->second.diffuseColor)
                                : Vec3{1,0,1};
                            if (grid.inBounds(x,y,z))
                                grid.set(x,y,z,pal.get(col),VoxelNormal::SURFACE,n,matId);
                        }
                    }
                }
            }
        }
    }
}

// Shell-only rasterizer: now populates Voxel::material from the triangle.
vector<Voxel> rasterizeChunk(int ox, int oy, int oz, int sx, int sy, int sz,
                             const vector<VTri>& tris,
                             const map<string, Material>& mats, Palette& pal)
{
    vector<Voxel> localVoxels;
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
                        Vec3 col;
                        auto it = mats.find(t.material);
                        if (it != mats.end()) {
                            if (it->second.tex.w > 0) col = sample(it->second.tex, uv);
                            else col = it->second.diffuseColor;
                        } else col = Vec3{1, 0, 1};
                        localVoxels.push_back({x, y, iz, pal.get(col), t.material});
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
                                Vec3 col; auto it = mats.find(t.material);
                                if (it != mats.end()) col = (it->second.tex.w > 0) ? sample(it->second.tex, uv) : it->second.diffuseColor;
                                else col = Vec3{1, 0, 1};
                                localVoxels.push_back({x, y, z, pal.get(col), t.material});
                            }
                        }
                    }
                } else if (poly.size() >= 3) {
                    Vec2 p0 = {poly[0].p.x, poly[0].p.y}, p1 = {poly[1].p.x, poly[1].p.y}, p2 = {poly[2].p.x, poly[2].p.y};
                    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
                        float u, v, w; if (barycentric({x + 0.5f, y + 0.5f}, p0, p1, p2, u, v, w)) {
                            Vec2 uv = {poly[0].uv.x * u + poly[1].uv.x * v + poly[2].uv.x * w, poly[0].uv.y * u + poly[1].uv.y * v + poly[2].uv.y * w};
                            Vec3 col; auto it = mats.find(t.material);
                            if (it != mats.end()) col = (it->second.tex.w > 0) ? sample(it->second.tex, uv) : it->second.diffuseColor;
                            else col = Vec3{1, 0, 1};
                            localVoxels.push_back({x, y, z, pal.get(col), t.material});
                        }
                    }
                }
            }
        }
    }
    return localVoxels;
}

/* -------------------- ChunkEntry (unchanged) -------------------- */

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

void handleConfig(float &scale, int &maxDim, int &CHUNK_SIZE, int &colors, string &colorRanges, float &voxelsPerUnit, bool &isSolidFill) {
    string configPath = "config.txt";
    
    if (!filesystem::exists(configPath)) {
        ofstream outFile(configPath);
        outFile << "scale=1.0                           # Attempts to multiply the size of the model by this value\n";
        outFile << "chunksize=128                       # Default 128. Anything greater than 256 will cause integer overflow and corrupt the .vox. Smaller chunks use less RAM during conversion but may take more time and hard drive space\n";
        outFile << "maxDim=1000                         # Maximum dimension size, automatically lowers scale if scale is too big. MagicaVoxel squishes/poorly handles .vox files with dimensions greater than 1000 voxels, but other programs (Like Teardown) can handle large dimensions well.\n";
        outFile << "colors=255                          # Default of 255 is standard for .vox\n";
        outFile << "colorRanges=1-184,185-255           # Determines which indexes should be used for colors. Most use cases should just leave at 1-255, but Teardown determines materials based on color index and only uses 1-184. Check Teardown modding wiki for palette details.\n";
        outFile << "voxelsPerUnit=12.0                  # Represents how many voxels is equal to 1 OBJ unit in length (1 OBJ unit = 1 meter in Blender). Is multipled by scale to determine final size.  A value of 12 is roughly 1m in Teardown, 1 is 1m in minecraft, though 0.914 might look better\n";
        outFile << "isSolidFill=False                   # Experimental and very slow for large dimensions, be patient. If false, only creates the surface/shell and leaves the inside empty. If true, fills in most empty voxels that are behind faces and not directly exposed. If inside isn't filled, look out for flipped normals or large openings.\n";
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
            else if (key == "colors") colors = stoi(val);
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
    int colors = 255;
    string colorRanges = "1-255";
    float voxelsPerUnit = 1.0f;
    bool isSolidFill = false;
    
    string inPath = argv[1];
    string outPath = filesystem::path(inPath).replace_extension(".vox").string();

    handleConfig(scale, maxDim, CHUNK_SIZE, colors, colorRanges, voxelsPerUnit, isSolidFill);

    if (argc > 2) scale = atof(argv[2]);
    if (argc > 3) maxDim = atoi(argv[3]);
    if (argc > 4) CHUNK_SIZE = atoi(argv[4]);
    if (argc > 5) isSolidFill = to_bool(argv[5]);
    if (argc > 6) outPath = argv[6]; 

    cout << "Running with: Scale = " << scale << ", Max Dim = " << maxDim 
         << ", Chunk Size = " << CHUNK_SIZE << ", Is Solid Fill = " << isSolidFill 
         << ", Output = " << outPath << endl;

    std::cout << "Loading OBJ..." << endl;
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
    float largestDim = max({size.x, size.y, size.z});
    cout << "Largest Dimension: " << round(scale * largestDim) << endl;
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
        cout << "Generating palette (Reservoir Sampling mode)..." << endl;
        
        vector<Vec3> allTextureColors;
        const int MAX_TOTAL_SAMPLES = 2000000;
        allTextureColors.reserve(MAX_TOTAL_SAMPLES);
        
        long long totalExpectedSamples = 0;
        for (const auto& t : vtris) {
            Vec3 e1 = t.p[1] - t.p[0], e2 = t.p[2] - t.p[0];
            float area = 0.5f * sqrt(max(0.0f, dot(cross(e1, e2), cross(e1, e2))));
            totalExpectedSamples += max(1, (int)(area * 0.1f));
        }

        float sampleProbability = 1.0f;
        if (totalExpectedSamples > MAX_TOTAL_SAMPLES) {
            sampleProbability = (float)MAX_TOTAL_SAMPLES / totalExpectedSamples;
            cout << "Model is large. Sampling " << (sampleProbability * 100.0f) << "% of surface area..." << endl;
        }

        for (const auto& t : vtris) {
            auto it = materials.find(t.material);
            if (it == materials.end()) continue;

            Vec3 e1 = t.p[1] - t.p[0], e2 = t.p[2] - t.p[0];
            float area = 0.5f * sqrt(max(0.0f, dot(cross(e1, e2), cross(e1, e2))));
            int numSamples = max(1, (int)(area * 0.1f * sampleProbability));
            if (numSamples > 100000) numSamples = 100000; 

            for (int k = 0; k < numSamples; k++) {
                if (allTextureColors.size() >= MAX_TOTAL_SAMPLES) break;
                if (it->second.tex.w > 0) {
                    float r1 = (float)rand() / RAND_MAX, r2 = (float)rand() / RAND_MAX;
                    if (r1 + r2 > 1.0f) { r1 = 1.0f - r1; r2 = 1.0f - r2; }
                    float r3 = 1.0f - r1 - r2;
                    Vec2 uv = t.uv[0] * r1 + t.uv[1] * r2 + t.uv[2] * r3;
                    allTextureColors.push_back(sample(it->second.tex, uv));
                } else {
                    allTextureColors.push_back(it->second.diffuseColor);
                }
            }
        }
        
        cout << "Building palette from " << allTextureColors.size() << " samples..." << endl;
        pal.buildFromColors(allTextureColors, colors, colorRanges);
        allTextureColors.clear();
        allTextureColors.shrink_to_fit();
    }

    /* ================================================================
       SOLID FILL PATH
       ================================================================ */
    if (isSolidFill) {
        cout << "Solid fill mode: allocating " << sx << "x" << sy << "x" << sz << " voxel grid..." << endl;

        size_t cellCount = size_t(sx) * sy * sz;
        // +2 bytes per cell for matId (uint16_t)
        size_t estMB = cellCount * (sizeof(uint8_t) + sizeof(VoxelNormal) + sizeof(Vec3) + sizeof(uint16_t)) / (1024*1024);
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
            cout << "\nSorting chunk entries...Could take some time" << endl;
            sort(flatGrid.begin(), flatGrid.end());

            for (size_t i = 0; i < flatGrid.size(); ) {
                uint64_t currentKey = flatGrid[i].key;
                vector<VTri> chunkTris;
                while (i < flatGrid.size() && flatGrid[i].key == currentKey) {
                    chunkTris.push_back(vtris[flatGrid[i].triIdx]);
                    i++;
                }
                rasterizeIntoGrid(grid, chunkTris, materials, pal);
                if (i % 1000 == 0) cout << "Rasterizing: " << (100*i/flatGrid.size()) << "%\r" << flush;
            }
        }
        vtris.clear(); vtris.shrink_to_fit();
        cout << "\nSurface rasterization complete. Running solid fill..." << endl;

        solidFill(grid, pal);

        cout << "Writing filled voxels..." << endl;
        MultiVoxWriter writer(outPath, pal, isSolidFill, scale);

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
                        chunkVoxels.push_back({x, y, z, c, matName});
                    }
                }
            }
            if (!chunkVoxels.empty())
                writer.writeChunk(ox, oy, oz, csx, csy, csz, chunkVoxels);
        }

        writer.finalize();

    } else {
        /* ================================================================
           ORIGINAL SHELL-ONLY PATH
           ================================================================ */
        vector<ChunkEntry> flatGrid;
        flatGrid.reserve(vtris.size() * 2); 

        float fChunk = (float)CHUNK_SIZE;
        std::cout << "Filtering triangles into chunks (Memory-stable mode)..." << endl;
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

        MultiVoxWriter writer(outPath, pal, isSolidFill, scale);
        cout << "Voxelizing data..." << endl;

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

            vector<Voxel> chunkVoxels = rasterizeChunk(ox, oy, oz, csx, csy, csz, chunkTris, materials, pal);
            if (!chunkVoxels.empty()) {
                writer.writeChunk(ox, oy, oz, csx, csy, csz, chunkVoxels);
            }

            if (i % 1000 == 0) cout << "Progress: " << (100 * i / flatGrid.size()) << "% (" << i << "/" << flatGrid.size() << " entries)\r" << flush;
        }

        writer.finalize();
    }

    cout << "\nFinished! Saved to " << outPath << endl;
    return 0;
}
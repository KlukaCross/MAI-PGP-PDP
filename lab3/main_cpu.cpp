#include <iostream>
#include <string>
#include <vector>
#include <limits>
#include <cstdlib>
#include <chrono>
#include <cstring>

#define NC_LIMIT 32

#define BENCHMARK

typedef struct {
    float x, y, z;
} float3;
typedef float3 float3x3[3];
typedef struct {
    unsigned char x, y, z, w;
} uchar4;
typedef struct {
    int x, y;
} int2;

const float FLOAT_MIN_LIMIT = -std::numeric_limits<float>::max();

float3 avg_dev[NC_LIMIT];
float3 cov_inv_dev[3*NC_LIMIT];

float3 make_float3(float x, float y, float z) {
    float3 f;
    f.x = x;
    f.y = y;
    f.z = z;
    return f;
}

unsigned char calculateClassUsingMahalanobisDistanceMethod(int nc, uchar4 p) {
    unsigned char max_class_number = 0;
    float max_value = FLOAT_MIN_LIMIT;
    for (unsigned char j = 0; j < nc; ++j) {
        float p_avg_diff_x = p.x - avg_dev[j].x;
        float p_avg_diff_y = p.y - avg_dev[j].y;
        float p_avg_diff_z = p.z - avg_dev[j].z;

        int i = 3*j;
        float value = -(
            (p_avg_diff_x*cov_inv_dev[i].x + p_avg_diff_y*cov_inv_dev[i+1].x + p_avg_diff_z*cov_inv_dev[i+2].x)*p_avg_diff_x +
            (p_avg_diff_x*cov_inv_dev[i].y + p_avg_diff_y*cov_inv_dev[i+1].y + p_avg_diff_z*cov_inv_dev[i+2].y)*p_avg_diff_y +
            (p_avg_diff_x*cov_inv_dev[i].z + p_avg_diff_y*cov_inv_dev[i+1].z + p_avg_diff_z*cov_inv_dev[i+2].z)*p_avg_diff_z
        );

        if (value > max_value) {
            max_value = value;
            max_class_number = j;
        }
    }
    return max_class_number;
}

void kernel(uchar4 *data, int width, int height, int nc) {
    for (int x = 0; x < width*height; ++x) {
        data[x].w = calculateClassUsingMahalanobisDistanceMethod(nc, data[x]);
    }
}

void readData(std::string& filename, int& w, int& h, uchar4** data) {
    FILE *f = fopen(filename.c_str(), "rb");
    fread(&w, sizeof(int), 1, f);
    fread(&h, sizeof(int), 1, f);
    *data = (uchar4 *)malloc(sizeof(uchar4) * w*h);
    fread(*data, sizeof(uchar4), w*h, f);
    fclose(f);
}

void writeData(std::string& filename, int w, int h, uchar4* data) {
    FILE *f = fopen(filename.c_str(), "wb");
    fwrite(&w, sizeof(int), 1, f);
    fwrite(&h, sizeof(int), 1, f);
    fwrite(data, sizeof(uchar4), w*h, f);
    fclose(f);
}

float3* calculateAvgVector(uchar4 *data, int width, std::vector<std::vector<int2>>& coors) {
    int nc = coors.size();
    float3* avg = (float3 *)malloc(sizeof(float3)*nc);
    for (int j = 0; j < nc; ++j) {
        avg[j] = make_float3(0, 0, 0);
        int np = coors[j].size();
        for (int i = 0; i < np; ++i) {
            uchar4 ps = data[coors[j][i].x + coors[j][i].y*width];
            avg[j].x += ps.x;
            avg[j].y += ps.y;
            avg[j].z += ps.z;
        }
        avg[j].x /= np;
        avg[j].y /= np;
        avg[j].z /= np;
    }
    return avg;
}

float3x3* calculateCovMatrix(uchar4 *data, int width, std::vector<std::vector<int2>>& coors, float3* avg) {
    int nc = coors.size();
    float3x3* cov = (float3x3 *)malloc(sizeof(float3x3)*nc);
    for (int j = 0; j < nc; ++j) {
        for (int k = 0; k < 3; ++k)
            cov[j][k] = make_float3(0, 0, 0);

        int np = coors[j].size();
        for (int i = 0; i < np; ++i) {
            uchar4 ps = data[coors[j][i].x + coors[j][i].y*width];

            float ps_avg_diff_x = ps.x - avg[j].x;
            float ps_avg_diff_y = ps.y - avg[j].y;
            float ps_avg_diff_z = ps.z - avg[j].z;

            cov[j][0].x += ps_avg_diff_x * ps_avg_diff_x;
            cov[j][0].y += ps_avg_diff_x * ps_avg_diff_y;
            cov[j][0].z += ps_avg_diff_x * ps_avg_diff_z;

            cov[j][1].x += ps_avg_diff_y * ps_avg_diff_x;
            cov[j][1].y += ps_avg_diff_y * ps_avg_diff_y;
            cov[j][1].z += ps_avg_diff_y * ps_avg_diff_z;

            cov[j][2].x += ps_avg_diff_z * ps_avg_diff_x;
            cov[j][2].y += ps_avg_diff_z * ps_avg_diff_y;
            cov[j][2].z += ps_avg_diff_z * ps_avg_diff_z;
        }
        for (int k = 0; k < 3; ++k) {
            cov[j][k].x /= np-1;
            cov[j][k].y /= np-1;
            cov[j][k].z /= np-1;
        }
    }
    return cov;
}

float calculateDet(float3x3 m) {
    return m[0].x*m[1].y*m[2].z + m[0].y*m[1].z*m[2].x + m[0].z*m[1].x*m[2].y - m[0].z*m[1].y*m[2].x - m[0].y*m[1].x*m[2].z - m[0].x*m[1].z*m[2].y;
}

float3* calculateCovInvVector(float3x3* cov, int nc) {
    float3* cov_inv = (float3 *)malloc(sizeof(float3)*3*nc);
    for (int j = 0; j < nc; ++j) {
        float det = calculateDet(cov[j]);

        cov_inv[3*j].x = (cov[j][1].y*cov[j][2].z - cov[j][1].z*cov[j][2].y) / det;
        cov_inv[3*j].y = -(cov[j][1].x*cov[j][2].z - cov[j][1].z*cov[j][2].x) / det;
        cov_inv[3*j].z = (cov[j][1].x*cov[j][2].y - cov[j][1].y*cov[j][2].x) / det;

        cov_inv[3*j+1].x = -(cov[j][0].y*cov[j][2].z - cov[j][0].z*cov[j][2].y) / det;
        cov_inv[3*j+1].y = (cov[j][0].x*cov[j][2].z - cov[j][0].z*cov[j][2].x) / det;
        cov_inv[3*j+1].z = -(cov[j][0].x*cov[j][2].y - cov[j][0].y*cov[j][2].x) / det;

        cov_inv[3*j+2].x = (cov[j][0].y*cov[j][1].z - cov[j][0].z*cov[j][1].y) / det;
        cov_inv[3*j+2].y = -(cov[j][0].x*cov[j][1].z - cov[j][0].z*cov[j][1].x) / det;
        cov_inv[3*j+2].z = (cov[j][0].x*cov[j][1].y - cov[j][0].y*cov[j][1].x) / det;
    }
    return cov_inv;
}

int main() {
    int w, h;
    int nc;
    uchar4 *data = nullptr;
    std::string input_filename, output_filename;
    std::cin >> input_filename >> output_filename >> nc;
    std::vector<std::vector<int2>> coors(nc);
    readData(input_filename, w, h, &data);

    for (int j = 0; j < nc; ++j) {
        int np;
        std::cin >> np;
        coors[j].resize(np);
        for (int i = 0; i < np; ++i) {
            std::cin >> coors[j][i].x >> coors[j][i].y;
        }
    }

    float3* avg = calculateAvgVector(data, w, coors);
    float3x3* cov = calculateCovMatrix(data, w, coors, avg);
    float3* cov_inv = calculateCovInvVector(cov, nc);

    memcpy(avg_dev, avg, sizeof(float3) * nc);
    memcpy(cov_inv_dev, cov_inv, sizeof(float3) * 3*nc);

#ifdef BENCHMARK
    std::chrono::time_point<std::chrono::system_clock> start, stop;
    start = std::chrono::system_clock::now();
#endif /* BENCHMARK */

    kernel(data, w, h, nc);

#ifdef BENCHMARK
    stop = std::chrono::system_clock::now();
    uint64_t time = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
    std::cout << time << " us\n";
#endif /* BENCHMARK */

    writeData(output_filename, w, h, data);

    free(data);
    free(avg);
    free(cov);
    free(cov_inv);
    return 0;
}
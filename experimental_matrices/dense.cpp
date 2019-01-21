#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include <time.h>
#include "timer.h++"

#ifndef LOAD 
#define LOAD "0.2"
#endif

#ifndef SIZE
#define SIZE 1600
#endif

using namespace std;

int main() {
    int N = SIZE;
    // Init dense matrix
    vector<vector<bool>> d(N, vector<bool>(N, false));

    // Open input file
    ifstream file;
    string filename = to_string(N) + "_" + LOAD;
    file.open(filename, std::ifstream::in);

    long long pairs;
    file >> pairs;
    for (long long i = 0; i < pairs; i++) {
        int x,y;
        file >> x >> y;
        d[x][y] = true;
    }
    file.close();

    timer clock;
    clock.start();

    //Completely basic dense Floyd Warshall
    for (int k = 0; k < N; k++) {
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                d[i][j] = d[i][j] | (d[i][k] & d[k][j]);
            }
        }
    }
    
    clock.stop();

    double time = clock.getElapsedTimeMicroSec();
    std::cout << LOAD << " " << SIZE << " " << time << " microsecs\n";

    return 0;
}

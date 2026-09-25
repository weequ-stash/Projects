#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

char* fih;
int spawnRate;
int maxRandom;

int _rows, _cols;

void exit_handler(int _) {
    printf("\e[?25h\e[2J\e[H");
    exit(0);
}

void print_help() {
    printf("DESCRIPTION:\n");
    printf("  cmatrix-like terminal fish animation\n\n");

    printf("USAGE:\n");
    printf("  ./fih [options]\n\n");

    printf("FLAGS:\n");
    printf("--help             Shows this message\n");
    printf("-f, --fih          Change fish (input a string)\n");
    printf("-r, --rate         Change fish spawnrate (percentage, 0-100)\n");
    printf("-m, --max-random   Change max random samples\n");
    printf("                   (fish spawn locations)\n");
}

void arg_handler(int argc, char** argv) {
    fih = malloc(sizeof(char) * 7);
    fih = ">-=#)>"; // default

    spawnRate = 50;

    maxRandom = _cols * 2;

    --argc;
    ++argv;
    for(int i = 0; i < argc; ++i) {
        if(strcmp(argv[i], "--help") == 0) {
            print_help();
            exit(0);
        }
        else if(strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fih") == 0) {
            ++i;
            if(i == argc) exit(1);

            char* _tmp = realloc(argv[i], sizeof(char) * (strlen(argv[i]) + 1));
            if(!_tmp) exit(1);

            fih = _tmp;
        }
        else if(strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rate") == 0) {
            ++i;
            if(i == argc) exit(1);

            spawnRate = atoi(argv[i]);
        }
        else if(strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max-random") == 0) {
            ++i;
            if(i == argc) exit(1);

            maxRandom = atoi(argv[i]);
        }
    }
}

int main(int argc, char** argv) {
    FILE* tputout;
    tputout = popen("tput lines cols", "r");
    if(!tputout) return 1;

    char _buffer[12];
    if(!fgets(_buffer, sizeof(_buffer), tputout)) return 2;
    _rows = atoi(_buffer);
    if(!fgets(_buffer, sizeof(_buffer), tputout)) return 2;
    _cols = atoi(_buffer);

    pclose(tputout);

    arg_handler(argc, argv);

    bool _isSpawn;

    int _grid[_rows * _cols];
    int _randomArr[maxRandom];
    int _spawnRow;

    for(int i = 0; i < _rows * _cols; ++i) {
        _grid[i] = -1;
    }

    for(int i = 0; i < maxRandom; ++i) {
        _randomArr[i] = -1;
    }
    for(int i = 0; i < (maxRandom * ((float)spawnRate / 100.0f)) * (maxRandom / _cols) * 2 / strlen(fih); ++i) { // better random choice
        _randomArr[rand() % (sizeof(_randomArr) / sizeof(int))] = rand();
    }

    printf("\e[?25l\e[2J");
    signal(SIGINT, exit_handler);

    int _cycle = 0;
    while(true) {
        _spawnRow = _randomArr[_cycle] % _rows;
        //// _isSpawn = _grid[_spawnRow * _cols] == -1 && spawnRate > 0 && spawnRate <= 100 && !(_randomArr[_cycle] % (int)(100.0f / spawnRate));
        _isSpawn = !(_randomArr[_cycle] == -1);

        // update
        for(int y = 0; y < _rows; ++y) {
            for(int x = _cols - 1; x > 0; --x) {
                _grid[y * _cols + x] = _grid[y * _cols + x - 1];
                _grid[y * _cols + x - 1] = -1;
            }
            if(_grid[y * _cols + 1] == -1) continue;

            _grid[y * _cols] = _grid[y * _cols + 1] - 1;
        }

        if(_isSpawn) { // could be also before the ^ loop
            _grid[_spawnRow * _cols] = strlen(fih) - 1;
        }

        printf("\e[H");

        for(int y = 0; y < _rows; ++y) {
            for(int x = 0; x < _cols; ++x) {
                if(_grid[y * _cols + x] != -1) {
                    printf("%c", fih[_grid[y * _cols + x]]);
                }
                else printf(" ");
            }
            printf("\e[E");
        }

        usleep(400); // TODO: -s for speed

        ++_cycle;
        _cycle %= maxRandom;
    }

    return 0;
}
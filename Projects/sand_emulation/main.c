#include "raylib.h"
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

float gScale = 5;
int gMenuWidth = 40;
int gMenuButtonsGap = 10;
float gMenuAnimTime = 0.1f;
float gMenuButtonAnimTime = 0.1f;

float gAnimSpeed = 1;            // Used to control global animations speed
float _gAnimSpeedMultiplier = 1; // Used internally to match animation update speed for current fps

int gGridWidth = 500;
int gGridHeight = 500;

bool _gIsSafeToCapture = true; // Used to block input

enum InputState {
    PLACE,
    FREELOOK
} gCurrentInputState;

typedef struct {
    Vector2 pos;
    Vector2 size;
    enum InputState action;
    Color color;
} Button;

enum { gTotalMenuButtons = 2 };
Button gMenuButtons[gTotalMenuButtons] = {
    {.size = {20, 20}, .color = DARKGRAY, .action = PLACE},
    {.size = {20, 20}, .color = DARKGRAY, .action = FREELOOK}};

/*---- Misc / Utils ----*/

bool isInside(Vector2 UL, Vector2 DR, Vector2 pos) {
    return (UL.x <= pos.x && pos.x < DR.x) && (UL.y <= pos.y && pos.y <= DR.y);
}

bool isSameColor(Color* x, Color* y) {
    return x->a == y->a && x->r == y->r && x->g == y->g && x->b == y->b;
}

// slightly faster when used in loops where items are mostly the same, functions like !isSameColor()
bool isDifferentColor(Color* x, Color* y) {
    return x->a != y->a || x->r != y->r || x->g != y->g || x->b != y->b;
}

void updateAnimProgress(float* _animProgress, float desired, float duration, bool increase) { // desired is an arbitrary value (used for rendering ui with offset),
    float step;                                                                               // time is the duration of an animation
    if(increase) {
        if(*_animProgress >= desired) {
            *_animProgress = desired;
            _gIsSafeToCapture = 1;
            return;
        }
        step = (desired / (duration * (float)GetMonitorRefreshRate(GetCurrentMonitor()))) * _gAnimSpeedMultiplier;
        *_animProgress += *_animProgress + step > desired ? desired - *_animProgress : step;
    }
    else {
        // desired is now functions as 'start Progress'
        if(*_animProgress <= 0) {
            *_animProgress = 0;
            _gIsSafeToCapture = 1;
            return;
        }
        step = (desired / (duration * (float)GetMonitorRefreshRate(GetCurrentMonitor()))) * _gAnimSpeedMultiplier;
        *_animProgress -= *_animProgress - step < 0 ? *_animProgress : step;
    }
}

/*---- World ----*/

void createSandStream(Color* grid, int width, Vector2 pos, Color color) {
    // DrawPixelV(pos, color);
    grid[(int)pos.y * width + (int)pos.x] = color;
}

void drawWorld(Color* grid, int width, int height, bool* isChanged, RenderTexture2D* frame) {
    BeginTextureMode(*frame);

    for(int y = 0; y < height; ++y) {
        for(int x = 0; x < width; ++x) {
            // do not render pixels on the menu ///// TODO: FIX NOT WORKING
            //* UPDATE: The fix was getting rid of the thing
            // if(!_gIsSafeToCapture && isInside((Vector2){GetScreenWidth() - gMenuWidth, 0},
            //(Vector2){GetScreenWidth(), GetScreenHeight()},
            //(Vector2){x, y})) {
            //    isChanged[y * width + x] = 1; // im too lazy to do something better
            //    continue;
            //}

            if(!isChanged[y * width + x]) {
                continue;
            }

            DrawPixel(x, y, grid[y * width + x]);
        }
    }

    EndTextureMode();
    DrawTexturePro(
        frame->texture,
        (Rectangle){0, 0, width, -height}, // Unflip the texture
        (Rectangle){0, 0, GetScreenWidth(), GetScreenHeight()},
        (Vector2){0, 0},
        0.0f,
        WHITE);
}

void updateWorldData(Color* grid, bool* isChanged, int width, int height) {
    static int dir;
    for(int y = height - 1; y >= 0; --y) {
        for(int x = width - 1; x >= 0; --x) {
            isChanged[y * width + x] = false;
            if(y == height - 1) {
                continue;
            }

            if(!isDifferentColor(&grid[y * width + x], &BLACK)) {
                continue;
            }

            // physics

            dir = GetRandomValue(0, 1);
            dir = dir == 0 ? -1 : 1;

            if(isSameColor(&grid[(y + 1) * width + x], &BLACK)) {
                grid[(y + 1) * width + x] = grid[y * width + x];
                grid[y * width + x] = BLACK;

                isChanged[(y + 1) * width + x] ^= 1;
                isChanged[y * width + x] ^= 1;

                continue;
            }

            if(x + dir < width && x + dir >= 0 && isSameColor(&grid[(y + 1) * width + x + dir], &BLACK)) {
                grid[(y + 1) * width + x + dir] = grid[y * width + x];
                grid[y * width + x] = BLACK;

                isChanged[(y + 1) * width + x + dir] ^= 1;
                isChanged[y * width + x] ^= 1;

                continue;
            }

            dir = dir == 1 ? -1 : 1;

            if(x + dir < width && x + dir >= 0 && isSameColor(&grid[(y + 1) * width + x + dir], &BLACK)) {
                grid[(y + 1) * width + x + dir] = grid[y * width + x];
                grid[y * width + x] = BLACK;

                isChanged[(y + 1) * width + x + dir] ^= 1;
                isChanged[y * width + x] ^= 1;

                continue;
            }
        }
    }
}

/*---- UI ----*/

void handleButton(Button* b, float* _animProgress) {
    Vector2 bEndPos = {b->pos.x + b->size.x, b->pos.y + b->size.y};

    if(isInside(b->pos, bEndPos, GetMousePosition()) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        gCurrentInputState = b->action;
        *_animProgress = 0; // start anim
    }

    if(*_animProgress < 100.0f && *_animProgress != -1) { // anim in progress
        DrawRectangleV(b->pos, b->size, BLUE);
        updateAnimProgress(_animProgress, 100.0f, gMenuButtonAnimTime, true);
        return;
    }

    // anim ended
    *_animProgress = -1;
    DrawRectangleV(b->pos, b->size, DARKGRAY);
}

void drawMenu(float animProgress) {
    int offset = (gMenuWidth - (int)animProgress);

    DrawRectangle(GetScreenWidth() - gMenuWidth + offset, 0, gMenuWidth, GetScreenHeight() + offset, GRAY);

    for(int i = 0; i < gTotalMenuButtons; ++i) {
        DrawRectangle(gMenuButtons[i].pos.x + offset, gMenuButtons[i].pos.y, gMenuButtons[i].size.x, gMenuButtons[i].size.y, gMenuButtons[i].color);
    }
}

void handleMenu() {
    static float _menu_animProgress = -1;
    static float _menuButtons_animProgress[gTotalMenuButtons];

    // check if cursor is inside the menu
    if(isInside((Vector2){GetScreenWidth() - gMenuWidth, 0}, (Vector2){GetScreenWidth(), GetScreenHeight()}, GetMousePosition())) {
        _gIsSafeToCapture = false;
        if(_menu_animProgress < gMenuWidth) {
            drawMenu(_menu_animProgress);
            updateAnimProgress(&_menu_animProgress, (float)gMenuWidth, gMenuAnimTime, true);
            return;
        }
        drawMenu((float)gMenuWidth);

        for(int i = 0; i < gTotalMenuButtons; ++i) {
            handleButton(&gMenuButtons[i], &_menuButtons_animProgress[i]);
        }
        return;
    }

    if(_menu_animProgress > 0) {
        drawMenu(_menu_animProgress);
        updateAnimProgress(&_menu_animProgress, (float)gMenuWidth, gMenuAnimTime, false);
        return;
    }
    _menu_animProgress = -1;
    _gIsSafeToCapture = 1; // If it works don't touch it

    for(int i = 0; i < gTotalMenuButtons; ++i) {
        _menuButtons_animProgress[i] = -1;
    }
}

void handleWorldInput(Color* grid, int width, RenderTexture2D* frame) {
    // Actually works
    if(!isInside((Vector2){0, 0}, (Vector2){GetScreenWidth(), GetScreenHeight()}, GetMousePosition())) {
        return;
    }

    BeginTextureMode(*frame);

    switch(gCurrentInputState) {
    case PLACE:
        if(_gIsSafeToCapture && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            createSandStream(grid, width, GetMousePosition(), YELLOW);
        }

        break;
    case FREELOOK:
        break;

    default:
        break;
    }
    EndTextureMode();
}

void CalculateMenuButtonsPos() {
    int y = 0;
    int x;
    for(int i = 0; i < gTotalMenuButtons; ++i) {
        y += gMenuButtonsGap;

        // center each button
        x = GetScreenWidth() - (gMenuWidth - (gMenuWidth - gMenuButtons[i].size.x) / 2);
        gMenuButtons[i].pos = (Vector2){x, y};

        y += gMenuButtons[i].size.y;
    }
}

void shiftPixelData(Color* grid, Color* temp_grid, Vector2* windowPos) {
    // handle width resize
    // for now we imagine the grid height haven't changed

    // if resized with the left window border

    if(GetWindowPosition().x != windowPos->x) {
        for(int y = 0; y < gGridHeight; ++y) {
            // if to the left
            if(GetScreenWidth() > gGridWidth) {
                for(int x = gGridWidth - 1; x >= 0; --x) {
                    temp_grid[y * GetScreenWidth() + (x + (GetScreenWidth() - gGridWidth))] = grid[y * gGridWidth + x];
                }
            }
            // if to the right
            else {
                for(int x = gGridWidth - GetScreenWidth(); x < gGridWidth; ++x) {
                    temp_grid[y * GetScreenWidth() + (x - (gGridWidth - GetScreenWidth()))] = grid[y * gGridWidth + x];
                }
            }
        }
    }
    // if resized with the right window border
    else {
        for(int y = 0; y < gGridHeight; ++y) {
            // if to the left
            if(GetScreenWidth() < gGridWidth) {
                for(int x = GetScreenWidth() - 1; x >= 0; --x) {
                    temp_grid[y * GetScreenWidth() + x] = grid[y * gGridWidth + x];
                }
            }
            // if to the right
            else {
                for(int x = gGridWidth - 1; x >= 0; --x) {
                    temp_grid[y * GetScreenWidth() + x] = grid[y * gGridWidth + x];
                }
            }
        }
    }

    // after handling width, we use just the new width

    // handle height resize

    // if resized with the top window border
    if(GetWindowPosition().y != windowPos->y) {
        // if to the top
        if(GetScreenHeight() > gGridHeight) {
            for(int y = gGridHeight - 1; y >= 0; --y) {
                for(int x = 0; x < GetScreenWidth(); ++x) {
                    temp_grid[(y + (GetScreenHeight() - gGridHeight)) * GetScreenWidth() + x] = temp_grid[y * GetScreenWidth() + x];
                }
            }
        }
        // if to the bottom
        else {
            for(int y = gGridHeight - GetScreenHeight(); y < gGridHeight; ++y) {
                for(int x = 0; x < GetScreenWidth(); ++x) {
                    temp_grid[(y - (gGridHeight - GetScreenHeight())) * GetScreenWidth() + x] = temp_grid[y * GetScreenWidth() + x];
                }
            }
        }
    }
    // if resized with the bottom window border
    else {
        // nothing to shift
    }
}

bool handleResize(Color** grid, bool** isChanged, Vector2* windowPos, RenderTexture2D* frame) {
    CalculateMenuButtonsPos();

    // At the moment, the old dimentions are stored in a variables gGrid[*], we get new dimentions by calling GetScreen[*]()

    // temp grid to safely move pixel data
    int maxHeight = GetScreenHeight() < gGridHeight ? gGridHeight : GetScreenHeight();
    int maxWidth = GetScreenWidth() < gGridWidth ? gGridWidth : GetScreenWidth();

    Color* temp_grid = malloc((size_t)maxHeight * (size_t)maxWidth * sizeof(Color));
    for(int i = 0; i < maxHeight * maxWidth; ++i) {
        temp_grid[i] = BLACK;
    }

    shiftPixelData(*grid, temp_grid, windowPos);
    printf("SUCCESS: ShiftPixelData()\n\n");

    // resize the texture
    UnloadRenderTexture(*frame);
    *frame = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    printf("SUCCESS: frame resized\n\n");

    // resize the grid and isChanged (allocate new memory with new size)

    size_t newSize = (size_t)GetScreenWidth() * (size_t)GetScreenHeight();

    Color* new_grid = malloc(newSize * sizeof(Color));
    if(new_grid == NULL) {
        return false;
    }

    bool* new_isChanged = malloc(newSize * sizeof(bool));
    if(new_isChanged == NULL) {
        return false;
    }

    free(*grid);
    free(*isChanged);

    *grid = new_grid;
    *isChanged = new_isChanged;

    printf("SUCCESS: allocated memory for grid and isChanged\n\n");

    // write temp_grid to grid
    for(int y = 0; y < GetScreenHeight(); ++y) {
        for(int x = 0; x < GetScreenWidth(); ++x) {
            (*grid)[y * GetScreenWidth() + x] = temp_grid[y * GetScreenWidth() + x];
            (*isChanged)[y * GetScreenWidth() + x] = 1;
        }
    }
    free(temp_grid);
    printf("SUCCESS: grid updated\n\n");

    // update global variables
    gGridWidth = GetScreenWidth();
    gGridHeight = GetScreenHeight();

    // Manually update (render) World
    drawWorld(*grid, gGridWidth, gGridHeight, *isChanged, frame);
    printf("SUCCESS: rendered world\n\n");

    return true;
}

void Init() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(gGridWidth, gGridHeight, "sand");
    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));

    // Camera2D Camera = {
    //     .zoom = gScale};

    // BeginMode2D(Camera);
    EnableCursor();

    CalculateMenuButtonsPos();
}

int main() {
    Init();

    Vector2 windowPos = GetWindowPosition();

    bool* isChanged = malloc(gGridWidth * gGridHeight * sizeof(bool));
    if(isChanged == NULL) return 1;

    RenderTexture2D frame;
    frame = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    // Color grid[gGridWidth * gGridHeight];
    Color* grid = malloc((size_t)gGridWidth * (size_t)gGridHeight * sizeof(Color));
    if(grid == NULL) return 1;

    for(int i = 0; i < gGridWidth * gGridHeight; ++i) {
        grid[i] = BLACK;
    }

    while(!WindowShouldClose()) {
        for(int i = 0; i < gGridWidth * gGridHeight; ++i) {
            isChanged[i] = false;
        }

        BeginDrawing();
        ClearBackground(BLACK);

        if(IsWindowResized()) { // TODO: fix segfaults
            if(!handleResize(&grid, &isChanged, &windowPos, &frame)) {
                free(grid);
                free(isChanged);
                return 1;
            }
        }

        windowPos = GetWindowPosition();

        _gAnimSpeedMultiplier = gAnimSpeed / ((float)GetFPS() / 165);

        handleWorldInput(grid, gGridWidth, &frame);
        updateWorldData(grid, isChanged, gGridWidth, gGridHeight);

        drawWorld(grid, gGridWidth, gGridHeight, isChanged, &frame);

        // draw after world so it doesn't corrupt pixels behind the menu
        handleMenu();

        DrawFPS(0, 0);

        EndDrawing();
    }

    free(grid);
    free(isChanged);

    UnloadRenderTexture(frame);
    // EndMode2D();
    CloseWindow();
    return 0;
}
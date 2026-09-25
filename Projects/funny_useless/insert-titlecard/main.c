#include <raylib.h>
#include <stdio.h>

float gSCALE;
float gAirResistance;

#define MAX_AIR_RESISTANCE (16)
#define SCALE (20000.0f)

typedef struct {
    bool _isGrabbed;

    float x;
    float y;
    float width;
    float height;
    Vector2 vel;
    Vector2 acc;
    Vector2 _grabbedOffset;
    Texture2D texture;
} SquareObj;

bool isInside(Vector2 UL, Vector2 DR, Vector2 pos) {
    return (UL.x <= pos.x && pos.x < DR.x) && (UL.y <= pos.y && pos.y <= DR.y);
}

void doTheThing(SquareObj* ent) {
    Vector2 mousepos = GetMousePosition();

    // handle grabbed state
    if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if(!ent->_isGrabbed && isInside((Vector2){ent->x, ent->y}, (Vector2){ent->x + ent->width, ent->y + ent->height}, GetMousePosition())) {
            ent->_isGrabbed = true;
            ent->_grabbedOffset = (Vector2){mousepos.x - ent->x, mousepos.y - ent->y};
            gAirResistance = 1;
        }
    }
    else {
        if(IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            ent->_isGrabbed = false;
        }
    }

    if(ent->_isGrabbed) {
        gAirResistance *= 1.006; // step
        gAirResistance = gAirResistance >= MAX_AIR_RESISTANCE ? MAX_AIR_RESISTANCE : gAirResistance;

        ent->acc = (Vector2){
            (mousepos.x - (ent->x + ent->_grabbedOffset.x)) / (gSCALE * 2),
            (mousepos.y - (ent->y + ent->_grabbedOffset.y)) / (gSCALE * 2)};
    }
    else {
        gAirResistance = MAX_AIR_RESISTANCE;
        ent->acc = (Vector2){0};
    }

    // update pos
    ent->vel.x += ent->acc.x;
    ent->vel.y += ent->acc.y;

    if(ent->vel.x >= 0) {
        ent->vel.x = ent->vel.x - (gAirResistance / gSCALE) > 0 ? ent->vel.x - (gAirResistance / gSCALE) : 0;
    }
    else {
        ent->vel.x = ent->vel.x + (gAirResistance / gSCALE) < 0 ? ent->vel.x + (gAirResistance / gSCALE) : 0;
    }
    if(ent->vel.y >= 0) {
        ent->vel.y = ent->vel.y - (gAirResistance / gSCALE) > 0 ? ent->vel.y - (gAirResistance / gSCALE) : 0;
    }
    else {
        ent->vel.y = ent->vel.y + (gAirResistance / gSCALE) < 0 ? ent->vel.y + (gAirResistance / gSCALE) : 0;
    }

    ent->x += ent->vel.x;
    ent->y += ent->vel.y;
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1000, 1000, "TITLE CARD");
    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));

    SquareObj mark = (SquareObj){
        false,
        (GetScreenWidth() - 200) / 2,
        (GetScreenHeight() - 200) / 2,
        200,
        200,
        (Vector2){0},
        (Vector2){0},
        (Vector2){0},
        LoadTexture("./invincible.png")};

    while(!WindowShouldClose()) {
        gSCALE = SCALE / GetFPS();

        BeginDrawing();
        ClearBackground(BLACK);
        printf("%f %i\n", gAirResistance, mark._isGrabbed);
        doTheThing(&mark);

        DrawTexturePro(mark.texture,
                       (Rectangle){0, 0, mark.texture.width, mark.texture.height},
                       (Rectangle){mark.x, mark.y, mark.width, mark.height},
                       (Vector2){0, 0},
                       0.0f,
                       WHITE);

        EndDrawing();
    }
    CloseWindow();

    return 0;
}
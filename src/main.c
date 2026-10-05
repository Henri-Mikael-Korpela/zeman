#include "raylib.h"
#include "raygui.h"

#include "calendar.h"

#define PADDING        16
#define HEADER_HEIGHT  40
#define WEEKDAY_HEIGHT 32
#define STATUS_HEIGHT  28
#define GRID_ROWS      6
#define GRID_COLS      7

static Color StyleColor(int property) {
    return GetColor(GuiGetStyle(DEFAULT, property));
}

static void DrawGuiText(const char *text, Vector2 pos, float size, Color color) {
    DrawTextEx(GuiGetFont(), text, pos, size, (float)GuiGetStyle(DEFAULT, TEXT_SPACING), color);
}

static Vector2 MeasureGuiText(const char *text, float size) {
    return MeasureTextEx(GuiGetFont(), text, size, (float)GuiGetStyle(DEFAULT, TEXT_SPACING));
}

// Date shown in the given grid cell. The grid always starts on the Monday of
// the week containing the 1st of the viewed month.
static Date CellDate(Date viewMonth, int cell) {
    Date first = { viewMonth.year, viewMonth.month, 1 };
    return DateAddDays(first, cell - DayOfWeek(first));
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1100, 760, "Calendar");
    SetWindowMinSize(640, 480);
    SetTargetFPS(60);

    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
    const float textSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);

    Date today = DateToday();
    Date selected = today;
    Date view = { today.year, today.month, 1 };

    while (!WindowShouldClose()) {
        today = DateToday();

        // ---- Input -------------------------------------------------------
        if (IsKeyPressed(KEY_LEFT))  selected = DateAddDays(selected, -1);
        if (IsKeyPressed(KEY_RIGHT)) selected = DateAddDays(selected, 1);
        if (IsKeyPressed(KEY_UP))    selected = DateAddDays(selected, -7);
        if (IsKeyPressed(KEY_DOWN))  selected = DateAddDays(selected, 7);
        if (IsKeyPressed(KEY_PAGE_UP))   selected = DateAddMonths(selected, -1);
        if (IsKeyPressed(KEY_PAGE_DOWN)) selected = DateAddMonths(selected, 1);
        if (IsKeyPressed(KEY_HOME))      selected = today;

        float wheel = GetMouseWheelMove();
        if (wheel > 0) selected = DateAddMonths(selected, -1);
        if (wheel < 0) selected = DateAddMonths(selected, 1);

        // Keep the viewed month in sync with keyboard/wheel navigation.
        view = (Date){ selected.year, selected.month, 1 };

        // ---- Layout ------------------------------------------------------
        const float screenW = (float)GetScreenWidth();
        const float screenH = (float)GetScreenHeight();

        Rectangle header = { PADDING, PADDING, screenW - 2*PADDING, HEADER_HEIGHT };
        Rectangle weekdays = { PADDING, header.y + header.height + PADDING/2, header.width, WEEKDAY_HEIGHT };
        Rectangle grid = {
            PADDING, weekdays.y + weekdays.height,
            header.width, screenH - STATUS_HEIGHT - PADDING - (weekdays.y + weekdays.height)
        };
        const float cellW = grid.width / GRID_COLS;
        const float cellH = grid.height / GRID_ROWS;

        // ---- Draw --------------------------------------------------------
        BeginDrawing();
        ClearBackground(StyleColor(BACKGROUND_COLOR));

        // Header: navigation buttons and month title.
        if (GuiButton((Rectangle){ header.x, header.y, 40, header.height }, "<"))
            selected = DateAddMonths(selected, -1);
        if (GuiButton((Rectangle){ header.x + 48, header.y, 40, header.height }, ">"))
            selected = DateAddMonths(selected, 1);
        if (GuiButton((Rectangle){ header.x + 96, header.y, 90, header.height }, "Today"))
            selected = today;

        const char *title = TextFormat("%s %d", MonthName(view.month), view.year);
        const float titleSize = textSize*1.5f;
        Vector2 titleDim = MeasureGuiText(title, titleSize);
        DrawGuiText(title, (Vector2){ header.x + (header.width - titleDim.x)/2,
                                      header.y + (header.height - titleDim.y)/2 },
                    titleSize, StyleColor(TEXT_COLOR_NORMAL));

        // Weekday names.
        for (int col = 0; col < GRID_COLS; col++) {
            const char *name = WeekdayShortName(col);
            Vector2 dim = MeasureGuiText(name, textSize);
            DrawGuiText(name, (Vector2){ weekdays.x + col*cellW + (cellW - dim.x)/2,
                                         weekdays.y + (weekdays.height - dim.y)/2 },
                        textSize, StyleColor(col >= 5 ? TEXT_COLOR_DISABLED : TEXT_COLOR_NORMAL));
        }

        // Day grid.
        Vector2 mouse = GetMousePosition();
        for (int cell = 0; cell < GRID_ROWS*GRID_COLS; cell++) {
            Date date = CellDate(view, cell);
            Rectangle r = { grid.x + (cell % GRID_COLS)*cellW, grid.y + (cell / GRID_COLS)*cellH, cellW, cellH };
            bool inMonth = date.month == view.month;
            bool isToday = DateEqual(date, today);
            bool isSelected = DateEqual(date, selected);
            bool hovered = CheckCollisionPointRec(mouse, r);

            if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) selected = date;

            Color fill = inMonth ? StyleColor(BASE_COLOR_NORMAL) : StyleColor(BACKGROUND_COLOR);
            if (hovered) fill = StyleColor(BASE_COLOR_FOCUSED);
            DrawRectangleRec(r, fill);
            DrawRectangleLinesEx(r, 1, StyleColor(BORDER_COLOR_NORMAL));

            const char *num = TextFormat("%d", date.day);
            Vector2 numDim = MeasureGuiText(num, textSize);
            Vector2 numPos = { r.x + 10, r.y + 8 };
            Color numColor = inMonth ? StyleColor(TEXT_COLOR_NORMAL) : StyleColor(TEXT_COLOR_DISABLED);

            if (isToday) {
                float radius = numDim.y*0.85f;
                DrawCircleV((Vector2){ numPos.x + numDim.x/2, numPos.y + numDim.y/2 }, radius,
                            StyleColor(BASE_COLOR_PRESSED));
                numColor = StyleColor(TEXT_COLOR_PRESSED);
            }
            DrawGuiText(num, numPos, textSize, numColor);

            if (isSelected) {
                Rectangle inner = { r.x + 2, r.y + 2, r.width - 4, r.height - 4 };
                DrawRectangleLinesEx(inner, 2, StyleColor(BORDER_COLOR_PRESSED));
            }
        }

        // Status bar with the selected date.
        GuiStatusBar((Rectangle){ 0, screenH - STATUS_HEIGHT, screenW, STATUS_HEIGHT },
                     TextFormat("%s, %d %s %d", WeekdayName(DayOfWeek(selected)),
                                selected.day, MonthName(selected.month), selected.year));

        EndDrawing();
    }

    CloseWindow();
    return 0;
}

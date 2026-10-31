// snake_game.cpp  -  Windows console Snake
// Build (MSVC):  cl /EHsc /O2 snake_game.cpp
// Build (MinGW): g++ -O2 -std=c++17 snake_game.cpp -o snake_game.exe
//
// Controls: Arrow keys / WASD = move, P = pause, Q = quit, R = restart (after game over)

#include <iostream>
#include <conio.h>
#include <windows.h>
#include <deque>
#include <vector>
#include <fstream>
#include <random>
#include <string>
#include <algorithm>
using namespace std;

const int WIDTH = 20;          // playable cells (horizontal)
const int HEIGHT = 15;         // playable cells (vertical)
const int POINTS_PER_FOOD = 10;
const char* HIGHSCORE_FILE = "highscore.txt";

enum Direction { UP, DOWN, LEFT, RIGHT };

struct Point {
    int x, y;
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

struct Game {
    deque<Point> snake;        // front = head
    Direction dir = RIGHT;     // direction applied on the last tick
    Direction nextDir = RIGHT; // direction requested by the player
    Point food{0, 0};
    int score = 0;
    int highScore = 0;
    bool gameOver = false;
    bool won = false;
    bool paused = false;
    bool quit = false;
    mt19937 rng{random_device{}()};
};

// ---------- console helpers ----------
HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

void hideCursor() {
    CONSOLE_CURSOR_INFO info;
    GetConsoleCursorInfo(hOut, &info);
    info.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &info);
}

void showCursor() {
    CONSOLE_CURSOR_INFO info;
    GetConsoleCursorInfo(hOut, &info);
    info.bVisible = TRUE;
    SetConsoleCursorInfo(hOut, &info);
}

void moveCursorHome() {
    SetConsoleCursorPosition(hOut, COORD{0, 0});
}

void clearScreen() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    DWORD written;
    DWORD size = csbi.dwSize.X * csbi.dwSize.Y;
    FillConsoleOutputCharacterA(hOut, ' ', size, COORD{0, 0}, &written);
    moveCursorHome();
}

// ---------- high score ----------
int loadHighScore() {
    ifstream in(HIGHSCORE_FILE);
    int v = 0;
    if (in >> v) return max(v, 0);
    return 0;
}

void saveHighScore(int v) {
    ofstream out(HIGHSCORE_FILE);
    if (out) out << v;
}

// ---------- game logic ----------
bool onSnake(const Game& g, const Point& p) {
    for (const auto& s : g.snake)
        if (s == p) return true;
    return false;
}

void placeFood(Game& g) {
    vector<Point> freeCells;
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++) {
            Point p{x, y};
            if (!onSnake(g, p)) freeCells.push_back(p);
        }

    if (freeCells.empty()) {          // board full -> player wins
        g.won = true;
        g.gameOver = true;
        return;
    }
    uniform_int_distribution<size_t> pick(0, freeCells.size() - 1);
    g.food = freeCells[pick(g.rng)];
}

void resetGame(Game& g) {
    g.snake.clear();
    int cx = WIDTH / 2, cy = HEIGHT / 2;
    g.snake.push_back({cx, cy});       // head
    g.snake.push_back({cx - 1, cy});
    g.snake.push_back({cx - 2, cy});
    g.dir = g.nextDir = RIGHT;
    g.score = 0;
    g.gameOver = g.won = g.paused = false;
    placeFood(g);
}

int currentDelayMs(const Game& g) {
    int level = g.score / (POINTS_PER_FOOD * 5);   // speed up every 5 foods
    return max(55, 160 - level * 12);
}

const char* speedLabel(int delayMs) {
    if (delayMs > 130) return "Slow";
    if (delayMs > 95)  return "Normal";
    if (delayMs > 65)  return "Fast";
    return "Insane";
}

bool isOpposite(Direction a, Direction b) {
    return (a == UP && b == DOWN) || (a == DOWN && b == UP) ||
           (a == LEFT && b == RIGHT) || (a == RIGHT && b == LEFT);
}

void handleInput(Game& g) {
    while (_kbhit()) {
        int c = _getch();
        Direction want = g.nextDir;
        bool isMove = false;

        if (c == 0 || c == 224) {                 // arrow keys send 2 codes
            switch (_getch()) {
                case 72: want = UP;    isMove = true; break;
                case 80: want = DOWN;  isMove = true; break;
                case 75: want = LEFT;  isMove = true; break;
                case 77: want = RIGHT; isMove = true; break;
            }
        } else {
            switch (tolower(c)) {
                case 'w': want = UP;    isMove = true; break;
                case 's': want = DOWN;  isMove = true; break;
                case 'a': want = LEFT;  isMove = true; break;
                case 'd': want = RIGHT; isMove = true; break;
                case 'p': g.paused = !g.paused; break;
                case 'q': g.quit = true; break;
                case 'r': if (g.gameOver) resetGame(g); break;
            }
        }

        // Compare with the direction actually applied last tick, so that two quick
        // key presses can never make the snake reverse into itself.
        if (isMove && !isOpposite(g.dir, want)) g.nextDir = want;
    }
}

void update(Game& g) {
    g.dir = g.nextDir;

    Point head = g.snake.front();
    switch (g.dir) {
        case UP:    head.y--; break;
        case DOWN:  head.y++; break;
        case LEFT:  head.x--; break;
        case RIGHT: head.x++; break;
    }

    // wall collision
    if (head.x < 0 || head.x >= WIDTH || head.y < 0 || head.y >= HEIGHT) {
        g.gameOver = true;
        return;
    }

    bool eating = (head == g.food);

    // self collision (the tail cell is free this tick unless we are growing)
    size_t checkCount = eating ? g.snake.size() : g.snake.size() - 1;
    for (size_t i = 0; i < checkCount; i++) {
        if (g.snake[i] == head) {
            g.gameOver = true;
            return;
        }
    }

    g.snake.push_front(head);

    if (eating) {
        g.score += POINTS_PER_FOOD;
        if (g.score > g.highScore) g.highScore = g.score;
        placeFood(g);
    } else {
        g.snake.pop_back();
    }

    if (g.gameOver) saveHighScore(g.highScore);
}

// ---------- rendering ----------
void draw(const Game& g) {
    string out;
    out.reserve(2048);

    out += "  ===== SNAKE GAME =====\n";
    out += "  Score: " + to_string(g.score) + "    High Score: " + to_string(g.highScore) + "        \n";

    // top wall
    out += "  ";
    for (int x = 0; x < WIDTH + 2; x++) out += "##";
    out += "\n";

    for (int y = 0; y < HEIGHT; y++) {
        out += "  ##";
        for (int x = 0; x < WIDTH; x++) {
            Point p{x, y};
            if (p == g.snake.front())      out += "@@";
            else if (onSnake(g, p))        out += "oo";
            else if (p == g.food)          out += "<>";
            else                           out += "  ";
        }
        out += "##\n";
    }

    // bottom wall
    out += "  ";
    for (int x = 0; x < WIDTH + 2; x++) out += "##";
    out += "\n";

    string speed = speedLabel(currentDelayMs(g));
    out += "  Speed: " + speed + string(10 - speed.size(), ' ') + "\n";
    out += "  Move: Arrow keys / WASD   Pause: P   Quit: Q\n";

    // status line (padded so old text is fully overwritten)
    string status;
    if (g.won)            status = "  *** YOU WIN! ***   Press R to restart, Q to quit";
    else if (g.gameOver)  status = "  GAME OVER!         Press R to restart, Q to quit";
    else if (g.paused)    status = "  PAUSED             Press P to resume";
    status.resize(60, ' ');
    out += status + "\n";

    moveCursorHome();
    cout << out << flush;
}

// ---------- main ----------
int main() {
    SetConsoleTitleA("Snake Game");
    hideCursor();
    clearScreen();

    Game g;
    g.highScore = loadHighScore();
    resetGame(g);

    while (!g.quit) {
        handleInput(g);

        if (!g.gameOver && !g.paused) update(g);

        draw(g);
        Sleep(g.gameOver || g.paused ? 50 : currentDelayMs(g));
    }

    saveHighScore(g.highScore);
    clearScreen();
    showCursor();
    cout << "Thanks for playing! High score: " << g.highScore << endl;
    return 0;
}

#include "snake.hpp"
#include "../drivers/vga.hpp"
#include "../drivers/keyboard.hpp"

namespace {
    constexpr int W = 40;
    constexpr int H = 12;
    char grid[W * H];
    
    struct Point { int x, y; };
    Point snake[100];
    int snake_len = 3;
    Point food;
    int dir_x = 1, dir_y = 0;

    void draw() {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                char c = grid[y * W + x];
                if (c == '#') VGA::print("#");
                else if (c == '*') VGA::print("*");
                else VGA::print(" ");
            }
            VGA::print("\n");
        }
    }

    void update() {
        Point head = {snake[0].x + dir_x, snake[0].y + dir_y};
        
        if (head.x < 0) head.x = W - 1;
        if (head.x >= W) head.x = 0;
        if (head.y < 0) head.y = H - 1;
        if (head.y >= H) head.y = 0;

        for (int i = 0; i < snake_len; i++) {
            if (snake[i].x == head.x && snake[i].y == head.y) {
                VGA::println("\nGame Over!");
                return;
            }
        }

        for (int i = snake_len; i > 0; i--) snake[i] = snake[i - 1];
        snake[0] = head;

        for (int i = 0; i < W * H; i++) grid[i] = ' ';
        
        for (int i = 0; i < snake_len; i++) grid[snake[i].y * W + snake[i].x] = '#';
        
        if (head.x == food.x && head.y == food.y) {
            snake_len++;
            food.x = (head.x + 7) % (W - 2) + 1;
            food.y = (head.y + 5) % (H - 2) + 1;
        }
        grid[food.y * W + food.x] = '*';
    }
}

namespace Snake {

void run() {
    snake[0] = {W/2, H/2};
    snake[1] = {W/2 - 1, H/2};
    snake[2] = {W/2 - 2, H/2};
    food = {10, 5};
    
    while (true) {
        auto key = Drivers::Keyboard::try_read_scancode();
        if (key.has_value()) {
            uint8_t sc = key.value();
            if (sc == 0x01) return;
            if (sc == 0x11) { dir_x = 0; dir_y = -1; }
            if (sc == 0x1F) { dir_x = 0; dir_y = 1; }
            if (sc == 0x1E) { dir_x = -1; dir_y = 0; }
            if (sc == 0x20) { dir_x = 1; dir_y = 0; }
        }

        update();
        VGA::clear_screen();
        draw();
        
        for (volatile int i = 0; i < 500000; i++);
    }
}

} // namespace Snake
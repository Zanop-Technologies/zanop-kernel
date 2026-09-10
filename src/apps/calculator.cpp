#include "calculator.hpp"
#include "../drivers/vga.hpp"
#include "../drivers/keyboard.hpp"
#include <cstring>

namespace Calculator {

void run() {
    char buf[32];
    size_t pos = 0;
    memset(buf, 0, 32);

    VGA::println("Enter math (e.g., 12+5):");
    VGA::print("> ");

    while (true) {
        auto key = Drivers::Keyboard::read_char();
        if (!key.has_value()) continue;
        char c = key.value();

        if (c == 0x01) return;
        if (c == '\n') {
            VGA::print("\n= ");
            int a = 0, b = 0;
            char op = 0;
            bool parsing_b = false;
            
            for (size_t i = 0; buf[i] != '\0'; i++) {
                if (buf[i] >= '0' && buf[i] <= '9') {
                    int val = buf[i] - '0';
                    if (!parsing_b) a = a * 10 + val;
                    else b = b * 10 + val;
                } else if (buf[i] == '+' || buf[i] == '-' || buf[i] == '*' || buf[i] == '/') {
                    op = buf[i];
                    parsing_b = true;
                }
            }

            int res = 0;
            if (op == '+') res = a + b;
            else if (op == '-') res = a - b;
            else if (op == '*') res = a * b;
            else if (op == '/') res = b != 0 ? a / b : 0;

            char res_buf[16];
            int idx = 15;
            res_buf[idx--] = '\0';
            if (res == 0) res_buf[idx--] = '0';
            bool neg = res < 0;
            if (neg) res = -res;
            while (res > 0) {
                res_buf[idx--] = '0' + (res % 10);
                res /= 10;
            }
            if (neg) res_buf[idx--] = '-';
            
            VGA::println(&res_buf[idx + 1]);
            
            pos = 0;
            memset(buf, 0, 32);
            VGA::print("> ");
        } 
        else if (c == '\b') {
            if (pos > 0) { pos--; buf[pos] = '\0'; VGA::print("\b \b"); }
        }
        else if (pos < 31) {
            buf[pos++] = c;
            char str[2] = {c, '\0'};
            VGA::print(str);
        }
    }
}

} // namespace Calculator
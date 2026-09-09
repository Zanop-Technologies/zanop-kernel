use alloc::string::String;
use alloc::vec::Vec;
use crate::{print, println};
use crate::drivers::keyboard;

/// Very small expression evaluator: supports +, -, *, / on integers,
/// left-to-right (no operator precedence yet — "2 + 3 * 4" == 20, not 14).
/// Good enough for a terminal calculator app; a real precedence-aware
/// parser is a reasonable future upgrade.
fn eval(expr: &str) -> Option<i64> {
    let tokens: Vec<&str> = expr.split_whitespace().collect();
    if tokens.is_empty() {
        return None;
    }

    let mut result: i64 = tokens[0].parse().ok()?;
    let mut i = 1;
    while i + 1 < tokens.len() {
        let op = tokens[i];
        let rhs: i64 = tokens[i + 1].parse().ok()?;
        result = match op {
            "+" => result + rhs,
            "-" => result - rhs,
            "*" => result * rhs,
            "/" => {
                if rhs == 0 {
                    return None;
                }
                result / rhs
            }
            _ => return None,
        };
        i += 2;
    }
    Some(result)
}

pub fn run() {
    println!();
    println!("--- Calculator ---");
    println!("Enter expressions like: 12 + 4    (type 'exit' to quit)");

    loop {
        print!("calc> ");
        let line = read_line();
        let trimmed = line.trim();

        if trimmed == "exit" || trimmed == "quit" {
            println!("Closing Calculator.");
            return;
        }

        match eval(trimmed) {
            Some(result) => println!("= {}", result),
            None => println!("Couldn't parse that. Try: 12 + 4"),
        }
    }
}

/// Blocking line reader shared by the terminal apps — polls the keyboard
/// (interrupt-driven input isn't wired up yet) and echoes to the screen.
pub fn read_line() -> String {
    let mut buf = String::new();
    loop {
        let scancode = keyboard::read_scancode();
        if let Some(ascii) = keyboard::scancode_to_ascii(scancode) {
            match ascii {
                b'\n' => {
                    println!();
                    return buf;
                }
                0x08 => {
                    if !buf.is_empty() {
                        buf.pop();
                        print!("{}", 0x08 as char);
                    }
                }
                c => {
                    buf.push(c as char);
                    print!("{}", c as char);
                }
            }
        }
    }
}
use alloc::collections::VecDeque;
use crate::println;
use crate::drivers::keyboard;
use crate::drivers::vga::WRITER;

const WIDTH: i32 = 40;
const HEIGHT: i32 = 18;

#[derive(Clone, Copy, PartialEq)]
enum Dir { Up, Down, Left, Right }

/// Extremely simple xorshift PRNG seeded from a fixed value — there's no
/// hardware RNG or timer entropy source wired up yet, so food placement
/// is deterministic-but-varied rather than truly random.
struct Rng(u32);
impl Rng {
    fn next(&mut self) -> u32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 17;
        self.0 ^= self.0 << 5;
        self.0
    }
}

pub fn run() {
    println!();
    println!("--- Snake ---");
    println!("W/A/S/D to move. Any other key exits.");

    let mut snake: VecDeque<(i32, i32)> = VecDeque::new();
    snake.push_back((WIDTH / 2, HEIGHT / 2));
    let mut dir = Dir::Right;
    let mut rng = Rng(0x9F3B_17A5);
    let mut food = spawn_food(&mut rng, &snake);
    let mut score: u32 = 0;

    loop {
        // ---- input ----
        // Non-blocking now that read_scancode() genuinely blocks for a
        // real event -- the snake needs to keep moving every tick
        // whether or not a key was pressed this frame.
        if let Some(scancode) = keyboard::try_read_scancode() {
            if let Some(ascii) = keyboard::scancode_to_ascii(scancode) {
                dir = match ascii {
                    b'w' | b'W' => Dir::Up,
                    b's' | b'S' => Dir::Down,
                    b'a' | b'A' => Dir::Left,
                    b'd' | b'D' => Dir::Right,
                    _ => {
                        println!("Closing Snake. Final score: {}", score);
                        return;
                    }
                };
            }
        }

        // ---- move ----
        let (hx, hy) = *snake.front().unwrap();
        let (nx, ny) = match dir {
            Dir::Up => (hx, hy - 1),
            Dir::Down => (hx, hy + 1),
            Dir::Left => (hx - 1, hy),
            Dir::Right => (hx + 1, hy),
        };

        // wall or self collision -> game over
        if nx < 0 || nx >= WIDTH || ny < 0 || ny >= HEIGHT
            || snake.contains(&(nx, ny)) {
            println!("Game over! Final score: {}", score);
            return;
        }

        snake.push_front((nx, ny));
        if (nx, ny) == food {
            score += 1;
            food = spawn_food(&mut rng, &snake);
        } else {
            snake.pop_back();
        }

        draw(&snake, food, score);

        for _ in 0..3_000_000 {
            unsafe { core::arch::asm!("nop") };
        }
    }
}

fn spawn_food(rng: &mut Rng, snake: &VecDeque<(i32, i32)>) -> (i32, i32) {
    loop {
        let x = (rng.next() % WIDTH as u32) as i32;
        let y = (rng.next() % HEIGHT as u32) as i32;
        if !snake.contains(&(x, y)) {
            return (x, y);
        }
    }
}

fn draw(snake: &VecDeque<(i32, i32)>, food: (i32, i32), score: u32) {
    let mut w = WRITER.lock();
    w.clear_screen();
    drop(w);

    crate::println!("Score: {}", score);
    for y in 0..HEIGHT {
        let mut line = alloc::string::String::new();
        for x in 0..WIDTH {
            if (x, y) == *snake.front().unwrap() {
                line.push('@');
            } else if snake.contains(&(x, y)) {
                line.push('o');
            } else if (x, y) == food {
                line.push('*');
            } else {
                line.push(' ');
            }
        }
        crate::println!("{}", line);
    }
}
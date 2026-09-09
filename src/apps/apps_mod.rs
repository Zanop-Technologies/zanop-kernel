pub mod calculator;
pub mod notes;
pub mod clock;
pub mod snake;

pub fn list() -> [&'static str; 4] {
    ["Calculator", "Notes", "Clock", "Snake"]
}

pub fn launch(name: &str) {
    match name {
        "Calculator" | "calculator" => calculator::run(),
        "Notes" | "notes" => notes::run(),
        "Clock" | "clock" => clock::run(),
        "Snake" | "snake" => snake::run(),
        _ => crate::println!("Unknown app: {}", name),
    }
}
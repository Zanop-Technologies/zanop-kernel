use alloc::string::String;
use crate::{print, println};
use crate::fs::VFS;
use super::calculator::read_line;

pub fn run() {
    println!();
    println!("--- Notes ---");
    println!("Commands: new <name>, list, read <name>, exit");

    loop {
        print!("notes> ");
        let line = read_line();
        let trimmed = line.trim();

        if trimmed == "exit" || trimmed == "quit" {
            println!("Closing Notes.");
            return;
        } else if trimmed == "list" {
            let items = VFS.lock().con_here();
            if items.is_empty() {
                println!("(no notes yet)");
            } else {
                for item in items {
                    println!("- {}", item);
                }
            }
        } else if let Some(name) = trimmed.strip_prefix("new ") {
            let name = name.trim();
            match VFS.lock().cre(name, false) {
                Ok(()) => {
                    println!("Type your note. Finish with a line containing only '.'");
                    let mut content = String::new();
                    loop {
                        let l = read_line();
                        if l.trim() == "." {
                            break;
                        }
                        content.push_str(&l);
                        content.push('\n');
                    }
                    let _ = VFS.lock().write_file(name, &content);
                    println!("Saved '{}'.", name);
                }
                Err(_) => println!("Couldn't create '{}' (does it already exist?)", name),
            }
        } else if let Some(name) = trimmed.strip_prefix("read ") {
            let name = name.trim();
            match VFS.lock().con(name) {
                Ok(lines) => {
                    for l in lines {
                        println!("{}", l);
                    }
                }
                Err(_) => println!("No note called '{}'.", name),
            }
        } else {
            println!("Unknown command. Try: new <name>, list, read <name>, exit");
        }
    }
}
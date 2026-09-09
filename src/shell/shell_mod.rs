use alloc::string::String;
use crate::{print, println};
use crate::fs::{VFS, VfsError};
use crate::apps;
use crate::drivers::keyboard;

pub fn run() -> ! {
    println!("Zanop Kernel Shell — type 'app' to see built-in programs.");
    loop {
        let cwd = VFS.lock().cwd_path();
        print!("{} $ ", cwd);

        let line = read_line();
        let mut parts = line.trim().splitn(2, ' ');
        let cmd = parts.next().unwrap_or("");
        let arg = parts.next().unwrap_or("").trim();

        match cmd {
            "" => {}
            "chd" => cmd_chd(arg),
            "cre" => cmd_cre(arg),
            "con" => cmd_con(arg),
            "info" => cmd_info(arg),
            "app" => cmd_app(arg),
            other => println!("Unknown command: '{}'. Try chd, cre, con, info, app.", other),
        }
    }
}

fn cmd_chd(arg: &str) {
    if arg.is_empty() {
        println!("Usage: chd <folder>");
        return;
    }
    match VFS.lock().chd(arg) {
        Ok(()) => {}
        Err(VfsError::NotFound) => println!("No such folder: {}", arg),
        Err(VfsError::NotADirectory) => println!("Not a folder: {}", arg),
        _ => println!("Couldn't change directory."),
    }
}

fn cmd_cre(arg: &str) {
    // Usage: cre file <name>   OR   cre folder <name>
    let mut parts = arg.splitn(2, ' ');
    let kind = parts.next().unwrap_or("");
    let name = parts.next().unwrap_or("").trim();

    if name.is_empty() {
        println!("Usage: cre file <name>  |  cre folder <name>");
        return;
    }

    let is_dir = match kind {
        "folder" | "dir" => true,
        "file" => false,
        _ => {
            println!("Usage: cre file <name>  |  cre folder <name>");
            return;
        }
    };

    match VFS.lock().cre(name, is_dir) {
        Ok(()) => println!("Created {} '{}'.", kind, name),
        Err(VfsError::AlreadyExists) => println!("'{}' already exists.", name),
        _ => println!("Couldn't create '{}'.", name),
    }
}

fn cmd_con(arg: &str) {
    if arg.is_empty() {
        for item in VFS.lock().con_here() {
            println!("{}", item);
        }
        return;
    }
    match VFS.lock().con(arg) {
        Ok(lines) => {
            for l in lines {
                println!("{}", l);
            }
        }
        Err(_) => println!("No such file or folder: {}", arg),
    }
}

fn cmd_info(arg: &str) {
    if arg.is_empty() {
        println!("Usage: info <name>");
        return;
    }
    match VFS.lock().info(arg) {
        Ok((kind, detail)) => println!("{}: {} ({})", arg, kind, detail),
        Err(_) => println!("No such file or folder: {}", arg),
    }
}

fn cmd_app(arg: &str) {
    if arg.is_empty() {
        println!("Built-in apps:");
        for a in apps::list() {
            println!("  - {}", a);
        }
        println!("Run one with: app <name>");
        return;
    }
    apps::launch(arg);
}

/// Blocking line reader for the shell prompt itself.
fn read_line() -> String {
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
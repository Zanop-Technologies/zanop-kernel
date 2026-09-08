use std::process::Command;
use std::env;
use std::path::Path;

fn main() {
    let out_dir = env::var("OUT_DIR").unwrap();

    let asm_files = [
        "src/arch/x86_64/boot.asm",
        "src/arch/x86_64/interrupts.asm",
    ];

    for asm in asm_files.iter() {
        println!("cargo:rerun-if-changed={}", asm);

        let file_stem = Path::new(asm).file_stem().unwrap().to_str().unwrap();
        let obj_path = format!("{}/{}.o", out_dir, file_stem);

        let status = Command::new("nasm")
            .args(["-f", "elf64", asm, "-o", &obj_path])
            .status()
            .expect("failed to run nasm — is it installed?");

        if !status.success() {
            panic!("nasm failed to assemble {}", asm);
        }

        println!("cargo:rustc-link-arg={}", obj_path);
    }

    println!("cargo:rerun-if-changed=linker.ld");
    println!("cargo:rustc-link-arg=-Tlinker.ld");
}
use std::path::PathBuf;
use std::process::Command;

fn main() {
    // Directory for the fontconfig build
    let build_dir = PathBuf::from("build");

    // Configure and build fontconfig using meson
    let mut meson = Command::new("meson");
    meson.current_dir("../");
    meson.arg("setup")
         .arg(build_dir.to_str().unwrap())
         .arg("-Dfontations=enabled");

    let status = meson.status().expect("Failed to execute meson");
    if !status.success() {
        panic!("Meson setup failed");
    }

    let mut ninja = Command::new("ninja");
    ninja.current_dir("../");
    ninja.arg("-C").arg(build_dir.to_str().unwrap());
    let status = ninja.status().expect("Failed to execute ninja");
    if !status.success() {
        panic!("Ninja build failed");
    }

    // Tell cargo to look for fontconfig in the build directory
    println!("cargo:rustc-link-search=native={}", build_dir.join("lib").display());
    println!("cargo:rustc-link-lib=dylib=fontconfig");

    // Rerun this build script if the fontconfig source code changes
    println!("cargo:rerun-if-changed=src");
}

use std::env;

fn main() {
    println!("cargo:rerun-if-env-changed=VEYRA_C_LIB_DIR");
    let dir = env::var("VEYRA_C_LIB_DIR").unwrap_or_else(|_| String::from("../../build"));
    println!("cargo:rustc-link-search=native={dir}");

    for library in ["veyra_c", "veyra_style", "veyra_foundation"] {
        println!("cargo:rustc-link-lib=static={library}");
    }

    let target = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    if target == "linux" {
        println!("cargo:rustc-link-lib=dylib=stdc++");
        println!("cargo:rustc-link-lib=dylib=m");
    } else if target == "macos" {
        println!("cargo:rustc-link-lib=dylib=c++");
    } else if target == "windows" {
        println!("cargo:rustc-link-lib=dylib=stdc++");
    }
}

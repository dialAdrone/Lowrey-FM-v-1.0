# The Lowrey FM simulator

`docs/lowrey.wasm` is `main.c` compiled with `web_fm1.c` (the FM-1's API for the browser):

    pip install ziglang==0.13.0
    python3 -m ziglang cc -target wasm32-wasi -O2 -mexec-model=reactor -I. main.c web_fm1.c -o docs/lowrey.wasm

Rebuild it whenever main.c changes.

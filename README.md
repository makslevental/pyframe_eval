# Demo of [PEP 523 – Adding a frame evaluation API to CPython](https://peps.python.org/pep-0523/)

A minimal example for how to use PEP 523 to intercept stackframe evaluation in Python. Roughly adapted
from [torch/csrc/dynamo/eval_frame.c](https://github.com/pytorch/pytorch/blob/e9d2d74f0abe4b0e0f238e11b537c64041b3f9a7/torch/csrc/dynamo/eval_frame.c).

# Build

```shell
pip install -r requirements.txt
mkdir build && \
  cmake -DPython3_EXECUTABLE=$(which python3) \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_INSTALL_PREFIX=$(pwd) \
    -B build -S $(pwd)
cmake --build build --target install
```

This will deposit `pyframe_eval.cpython-311-x86_64-linux-gnu.so` (or something like that) here.

# Run

```shell
python main.py
```

should print

```shell
# smoke test (i.e., before AST rewrite)
frame func <function foo at 0x7f23adad0220>
frame code obj <code object foo at 0x7f23ae571020, file "/home/mlevental/dev_projects/pyframe_eval/main.py", line 32>
3
# after ast rewrite (scale all constants by 10)
30
```


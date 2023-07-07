from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup, find_namespace_packages

__version__ = "0.0.3.2"

ext_modules = [
    Pybind11Extension(
        "pyframe_eval._pyframe_eval",
        ["cpp_ext/evalFrame.cpp"],
    ),
]


setup(
    name="pyframe_eval",
    version=__version__,
    author="Maksim Levental",
    author_email="maksim.levental@gmail.com",
    url="https://github.com/makslevental/pyframe_eval",
    description="A PEP 523 compatible frame evaluator",
    long_description="A PEP 523 compatible frame evaluator",
    packages=find_namespace_packages(include=["pyframe_eval"]),
    python_requires=">=3.11",
    ext_modules=ext_modules,
    extras_require={"test": "pytest"},
    cmdclass={"build_ext": build_ext},
)

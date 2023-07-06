import os
import re
import subprocess
import sys
from pathlib import Path

from pip._internal.req import parse_requirements
from setuptools import Extension, find_namespace_packages, setup
from setuptools.command.build_ext import build_ext

# Convert distutils Windows platform specifiers to CMake -A arguments
PLAT_TO_CMAKE = {
    "win32": "Win32",
    "win-amd64": "x64",
    "win-arm32": "ARM",
    "win-arm64": "ARM64",
}


# A CMakeExtension needs a sourcedir instead of a file list.
# The name must be the _single_ output extension from the CMake build.
# If you need multiple extensions, see scikit-build.
class CMakeExtension(Extension):
    def __init__(self, name: str, sourcedir: str = "") -> None:
        super().__init__(name, sources=[])
        self.sourcedir = os.fspath(Path(sourcedir).resolve())


class CMakeBuild(build_ext):
    def build_extension(self, ext: CMakeExtension) -> None:
        # Must be in this form due to bug in .resolve() only fixed in Python 3.10+
        ext_fullpath = Path.cwd() / self.get_ext_fullpath(ext.name)
        ext_build_lib_dir = ext_fullpath.parent.resolve()
        debug = int(os.environ.get("DEBUG", 0)) if self.debug is None else self.debug
        # cfg = "Debug" if debug else "Release"
        cfg = "Release"
        cmake_generator = os.environ.get("CMAKE_GENERATOR", "")

        CXX = os.environ.get("CXX", "clang++")
        CC = os.environ.get("CC", "clang")
        cmake_args = [
            f"-DCMAKE_CXX_COMPILER={CXX}",
            # https://bugs.python.org/issue23644
            f"-DCMAKE_C_COMPILER={CC}",
            # this is the correct way to do it for pip install
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={ext_build_lib_dir}{os.sep}pyframe_eval",
            f"-DPython3_EXECUTABLE={sys.executable}",
            f"-DCMAKE_BUILD_TYPE={cfg}",  # not used on MSVC, but no harm
        ]
        build_args = []
        if "CMAKE_ARGS" in os.environ:
            cmake_args += [item for item in os.environ["CMAKE_ARGS"].split(" ") if item]

        # if self.compiler.compiler_type != "msvc":
        # Using Ninja-build since it a) is available as a wheel and b)
        # multithreads automatically. MSVC would require all variables be
        # exported for Ninja to pick it up, which is a little tricky to do.
        # Users can override the generator with CMAKE_GENERATOR in CMake
        # 3.15+.
        if not cmake_generator or cmake_generator == "Ninja":
            try:
                import ninja

                ninja_executable_path = Path(ninja.BIN_DIR) / "ninja"
                cmake_args += [
                    "-GNinja",
                    f"-DCMAKE_MAKE_PROGRAM:FILEPATH={ninja_executable_path}",
                ]
            except ImportError:
                pass

        # else:
        #     # Single config generators are handled "normally"
        #     single_config = any(x in cmake_generator for x in {"NMake", "Ninja"})
        #
        #     # CMake allows an arch-in-generator style for backward compatibility
        #     contains_arch = any(x in cmake_generator for x in {"ARM", "Win64"})
        #
        #     # Specify the arch if using MSVC generator, but only if it doesn't
        #     # contain a backward-compatibility arch spec already in the
        #     # generator name.
        #     if not single_config and not contains_arch:
        #         cmake_args += ["-A", PLAT_TO_CMAKE[self.plat_name]]
        #
        #     # Multi-config generators have a different way to specify configs
        #     if not single_config:
        #         cmake_args += [
        #             f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{cfg.upper()}={ext_build_lib_dir}"
        #         ]
        #         build_args += ["--config", cfg]

        if sys.platform.startswith("darwin"):
            # Cross-compile support for macOS - respect ARCHFLAGS if set
            # NOTE: cibuildwheel inserts these args automatically
            archs = re.findall(r"-arch (\S+)", os.environ.get("ARCHFLAGS", ""))
            if archs:
                cmake_args += ["-DCMAKE_OSX_ARCHITECTURES={}".format(";".join(archs))]

        if "CMAKE_BUILD_PARALLEL_LEVEL" not in os.environ:
            if hasattr(self, "parallel") and self.parallel:
                # CMake 3.12+ only.
                build_args += [f"-j{self.parallel}"]

        build_temp = Path(self.build_temp) / ext.name
        if not build_temp.exists():
            build_temp.mkdir(parents=True)

        subprocess.run(
            ["cmake", ext.sourcedir, *cmake_args], cwd=build_temp, check=True
        )
        subprocess.run(
            ["cmake", "--build", ".", *build_args], cwd=build_temp, check=True
        )


__version__ = "0.0.3"

install_reqs = parse_requirements("requirements.txt", session="hack")

setup(
    name="pyframe_eval",
    version=__version__,
    author="Maksim Levental",
    author_email="maksim.levental@gmail.com",
    url="https://github.com/makslevental/pyframe_eval",
    description="Install a PEP 523 compatible frame evaluator",
    long_description="",
    packages=find_namespace_packages(include=["pyframe_eval"]),
    ext_modules=[CMakeExtension("_pyframe_eval")],
    cmdclass={"build_ext": CMakeBuild},
    zip_safe=False,
    python_requires=">=3.11",
    install_requires=[
        str(ir.requirement) for ir in install_reqs if "git" not in str(ir.requirement)
    ],
)

from setuptools import setup, Extension
from pybind11.setup_helpers import Pybind11Extension, build_ext

# Define the C++ extension module
ext_modules = [
    Pybind11Extension(
        # The name of the module as it will be imported in Python
        "network_module",
        # The source file for the bindings and the C++ implementations
        ["src/network.cpp"],
        # Specify C++11 standard
        extra_compile_args=["-std=c++11"],
        language='c++'
    ),
]


setup(
    name="network_module",
    version="1.0",
    author="Your Name",
    description="Python bindings for pGRAMS Network bindings",
    ext_modules=ext_modules,
    # Use the custom build command from pybind11
    cmdclass={"build_ext": build_ext},
)

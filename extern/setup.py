from setuptools import setup, Extension
from pybind11.setup_helpers import Pybind11Extension, build_ext
import os

# Get the current directory
this_dir = os.path.dirname(os.path.abspath(__file__))

# Define the C++ extension module
ext_modules = [
    Pybind11Extension(
        # The name of the module as it will be imported in Python
        "network_module",
        # The source file for the bindings and the C++ implementations
        [
            os.path.join(this_dir, "src", "network.cpp"),
            os.path.join(this_dir, "..", "tcp_connection.cpp"),
            os.path.join(this_dir, "..", "tcp_protocol.cpp"),
        ],
        include_dirs=["/home/pgrams/asio-1.30.2/include"],
        # Specify C++11 standard
        extra_compile_args=["-std=c++17"],
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

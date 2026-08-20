"""Consumer check for the metl Conan package.

This is not a copy of the test suite -- CI already runs that against the
sources. What it verifies is the thing only packaging can break: that a
consumer gets a usable `metl::metl` target from `find_package(metl)`, that the
headers landed in the include path, and that the C++17 requirement is carried
through rather than assumed.
"""

import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


class MetlTestConan(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            self.run(os.path.join(self.cpp.build.bindir, "test_package"), env="conanrun")

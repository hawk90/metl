"""Conan recipe for metl.

Header-only, so there is nothing to build and nothing to link -- `package_id`
is cleared and the package is a plain header drop. What the recipe still has to
get right is the *contract*: C++17 is a hard requirement (the library is built
out of `if constexpr`, fold expressions and inline variables, so a C++14
consumer fails deep inside a header rather than at configure time), and the
consumer needs the same `metl::metl` target it would get from
`find_package(metl)`, so switching between vcpkg, Conan and a plain install does
not change the CMake a project writes.

Local check:
    conan create . --build=missing
"""

import os

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.files import copy, load
from conan.tools.layout import basic_layout

required_conan_version = ">=2.0"


class MetlConan(ConanFile):
    name = "metl"
    description = "Modern Embedded Template Library: header-only C++17 containers and vocabulary types with no heap, no exceptions and no RTTI"
    license = "MIT"
    url = "https://github.com/hawk90/metl"
    homepage = "https://github.com/hawk90/metl"
    topics = ("embedded", "header-only", "no-exceptions", "no-heap", "cpp17", "deterministic")

    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True

    exports_sources = "include/*", "LICENSE", "CMakeLists.txt"

    def set_version(self):
        # Single source of truth: the version lives in CMakeLists.txt, and
        # release.yml already refuses to publish a tag that disagrees with it.
        # Reading it here means the recipe cannot drift into a third answer.
        if self.version:
            return
        text = load(self, os.path.join(self.recipe_folder, "CMakeLists.txt"))
        for line in text.splitlines():
            stripped = line.strip()
            if stripped.startswith("VERSION "):
                self.version = stripped.split()[1]
                return
        raise RuntimeError("could not find project(VERSION ...) in CMakeLists.txt")

    def layout(self):
        basic_layout(self, src_folder=".")

    def package_id(self):
        self.info.clear()

    def validate(self):
        # Fail here rather than 400 lines into a header.
        check_min_cppstd(self, 17)

    def package(self):
        copy(self, "LICENSE", self.source_folder, os.path.join(self.package_folder, "licenses"))
        copy(
            self,
            "*.hpp",
            os.path.join(self.source_folder, "include"),
            os.path.join(self.package_folder, "include"),
        )

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []

        # Match what `find_package(metl)` provides from the CMake install, so a
        # consumer writes `metl::metl` regardless of how it got the library.
        self.cpp_info.set_property("cmake_file_name", "metl")
        self.cpp_info.set_property("cmake_target_name", "metl::metl")

        # The library uses `if constexpr` and inline variables unguarded.
        self.cpp_info.set_property("cmake_config_version_compat", "SameMajorVersion")

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout


class Mach1Conan(ConanFile):
    name = "mach1"
    version = "0.1.0"
    package_type = "application"

    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "CMakeLists.txt", "src/*"

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generator = "Ninja"
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout


class Mach1Conan(ConanFile):
    name = "mach1"
    version = "0.1.0"
    package_type = "application"

    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "CMakeLists.txt", "src/*"

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        self.requires("cli11/2.6.2")
        self.requires("catch2/3.14.0")
        self.requires("nlohmann_json/3.11.3")
        self.requires("onnxruntime/1.18.1")

    def configure(self):
        # Static: shared ORT can hide OrtGetApiBase under -fvisibility=hidden,
        # and that package ID survives the consumer-only flag split.
        self.options["onnxruntime"].shared = False

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generator = "Ninja"
        tc.cache_variables["CMAKE_EXPORT_COMPILE_COMMANDS"] = "ON"
        # Profile consumer flags (user.mach1:*) — not tools.build:*, so deps keep
        # stable package IDs and are not rebuilt with ASan/coverage/LTO.
        tc.extra_cxxflags = list(self.conf.get("user.mach1:cxxflags", check_type=list, default=[]))
        tc.extra_cflags = list(self.conf.get("user.mach1:cflags", check_type=list, default=[]))
        tc.extra_exelinkflags = list(
            self.conf.get("user.mach1:exelinkflags", check_type=list, default=[])
        )
        tc.extra_sharedlinkflags = list(
            self.conf.get("user.mach1:sharedlinkflags", check_type=list, default=[])
        )
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

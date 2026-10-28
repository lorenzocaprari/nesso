import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout


class Nesso(ConanFile):
    name = "nesso"
    version = "0.2.1"
    license = "Apache-2.0"
    package_type = "application"

    settings = "os", "compiler", "build_type", "arch"
    options = {"bench": [True, False]}
    default_options = {"bench": False}
    exports_sources = "CMakeLists.txt", "src/*"

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        self.requires("cli11/2.6.2")
        self.requires("nlohmann_json/3.11.3")
        self.requires("onnxruntime/1.18.1")

    def build_requirements(self):
        self.test_requires("catch2/3.14.0")
        if self.options.bench:
            self.test_requires("benchmark/1.9.1")

    def configure(self):
        # Static: shared ORT can hide OrtGetApiBase under -fvisibility=hidden,
        # and that package ID survives the consumer-only flag split.
        self.options["onnxruntime"].shared = False

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generator = "Ninja"
        tc.cache_variables["CMAKE_EXPORT_COMPILE_COMMANDS"] = "ON"
        tc.cache_variables["NESSO_VERSION"] = str(self.version)
        tc.cache_variables["NESSO_BUILD_BENCH"] = bool(self.options.bench)
        # Shipped profiles leave this unset. CI passes -c user.nesso:werror=True.
        # conan build runs generators again, so that -c has to be on the build too.
        tc.cache_variables["NESSO_WERROR"] = bool(
            self.conf.get("user.nesso:werror", default=False, check_type=bool)
        )
        # Profile consumer flags (user.nesso:*) — not tools.build:*, so deps keep
        # stable package IDs and are not rebuilt with ASan/coverage/LTO.
        tc.extra_cxxflags = list(self.conf.get("user.nesso:cxxflags", check_type=list, default=[]))
        tc.extra_cflags = list(self.conf.get("user.nesso:cflags", check_type=list, default=[]))
        tc.extra_exelinkflags = list(
            self.conf.get("user.nesso:exelinkflags", check_type=list, default=[])
        )
        tc.extra_sharedlinkflags = list(
            self.conf.get("user.nesso:sharedlinkflags", check_type=list, default=[])
        )
        # GCC 14+ injects -fmodules-ts into every compile while module scanning
        # is on. ccache refuses that flag, and this tree has no C++ modules.
        cxx_launcher = os.environ.get("CMAKE_CXX_COMPILER_LAUNCHER")
        if cxx_launcher:
            tc.cache_variables["CMAKE_C_COMPILER_LAUNCHER"] = os.environ.get(
                "CMAKE_C_COMPILER_LAUNCHER", cxx_launcher
            )
            tc.cache_variables["CMAKE_CXX_COMPILER_LAUNCHER"] = cxx_launcher
            tc.cache_variables["CMAKE_CXX_SCAN_FOR_MODULES"] = False
        # Every host dependency is static, so nesso needs no library search path.
        # Without this, CMakeDeps link directories become a build-tree RUNPATH that
        # the release binary keeps, with a trailing empty entry (the current
        # directory). scripts/check-no-runpath enforces it on the release binary.
        tc.blocks["rpath"].skip_rpath = True
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

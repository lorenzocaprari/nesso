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
        # Direct, so gzip input links zlib and the license gate sees it as nesso's dependency.
        # The revision is the one already locked for onnxruntime's graph.
        self.requires("zlib/1.3.2")
        # In-place tar and zip. xz_utils is pinned so liblzma is the locked 5.8.4, not the range.
        self.requires("libarchive/3.8.7")
        self.requires("xz_utils/5.8.4", override=True)

    def build_requirements(self):
        self.test_requires("catch2/3.14.0")
        if self.options.bench:
            self.test_requires("benchmark/1.9.1")

    def configure(self):
        # Static: shared ORT can hide OrtGetApiBase under -fvisibility=hidden,
        # and that package ID survives the consumer-only flag split.
        self.options["onnxruntime"].shared = False
        # The release binary's NEEDED list has no libz. Keep zlib in the binary.
        self.options["zlib"].shared = False
        # libarchive and liblzma stay inside the binary. No libarchive.so or liblzma.so NEEDED entry.
        # Compression other than zlib and lzma stays off. bzip2 is already in the graph and is not enabled.
        self.options["libarchive"].shared = False
        self.options["libarchive"].with_iconv = False
        self.options["libarchive"].with_acl = False
        self.options["libarchive"].with_xattr = False
        self.options["libarchive"].with_zlib = True
        self.options["libarchive"].with_bzip2 = False
        self.options["libarchive"].with_libxml2 = False
        self.options["libarchive"].with_expat = False
        self.options["libarchive"].with_pcreposix = False
        self.options["libarchive"].with_pcre2 = False
        self.options["libarchive"].with_cng = False
        self.options["libarchive"].with_nettle = False
        self.options["libarchive"].with_openssl = False
        self.options["libarchive"].with_libb2 = False
        self.options["libarchive"].with_lz4 = False
        self.options["libarchive"].with_lzo = False
        self.options["libarchive"].with_lzma = True
        self.options["libarchive"].with_zstd = False
        self.options["libarchive"].with_mbedtls = False
        self.options["xz_utils"].shared = False
        self.options["xz_utils"].with_tools = False

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
        # directory). packaging/scripts/check-no-runpath enforces it on the release binary.
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

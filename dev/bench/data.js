window.BENCHMARK_DATA = {
  "lastUpdate": 1793054897015,
  "repoUrl": "https://github.com/lorenzocaprari/nesso",
  "entries": {
    "nesso": [
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "3a98b7a45c77852a113e4d8d6aac3db155dae1c9",
          "message": "ci: track benchmark history\n\nRecord end-to-end bench results on gh-pages from pushes to main.\nPull requests only append an informational history comparison to the\nbench job summary. The merge gate stays scripts/bench-compare.",
          "timestamp": "2026-10-26T11:11:57+01:00",
          "tree_id": "f6f44c78efee3c7d9267be075880e1614f1634f4",
          "url": "https://github.com/lorenzocaprari/nesso/commit/3a98b7a45c77852a113e4d8d6aac3db155dae1c9"
        },
        "date": 1793009961120,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 3.64,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 137248768,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 25.39,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 137031680,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 242.69,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132583424,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "ae2a1d2aab49051fbe5ac646afe7d3c828f38e12",
          "message": "ci: lint workflows and review dependencies\n\nRun actionlint and zizmor in the lint job. Pull requests also run dependency review there, so the check name stays lint.",
          "timestamp": "2026-10-26T11:54:34+01:00",
          "tree_id": "4ac25c931ff11329ce5fed7d05407a3112e1c25a",
          "url": "https://github.com/lorenzocaprari/nesso/commit/ae2a1d2aab49051fbe5ac646afe7d3c828f38e12"
        },
        "date": 1793012521672,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 3.65,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 135503872,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 25.75,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 134713344,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 243.81,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132419584,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "ed5ae6901c59d1ab19122c3903fbb700e01439ab",
          "message": "ci: harden runners and scan the runtime image\n\nAudit outbound calls on host jobs. Scan the runtime image with a checksum-pinned Trivy binary when Package or a tag release builds it, and upload SARIF without failing the job.",
          "timestamp": "2026-10-26T13:10:31+01:00",
          "tree_id": "6902e551f6b70356496b6ee153439b2b67498421",
          "url": "https://github.com/lorenzocaprari/nesso/commit/ed5ae6901c59d1ab19122c3903fbb700e01439ab"
        },
        "date": 1793017104056,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 3.69,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132317184,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 27,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 131989504,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 261.78,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132075520,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "a94eea417c3ef47e9ab0e5a90c7bff96bcf400ce",
          "message": "fix(ci): grant security-events so Release can call Package\n\nGitHub checks scan-runtime's security-events: write before the job\nif, so verify-package must grant it or a tag release fails at\nstartup. Release passes scan_runtime: false. The image job still\nscans the image it built. Package's own nightly, dispatch, and\npackaging pull requests still scan.",
          "timestamp": "2026-10-26T14:27:14+01:00",
          "tree_id": "6c545893a511496c6b1e1deb153a00a0c42437bd",
          "url": "https://github.com/lorenzocaprari/nesso/commit/a94eea417c3ef47e9ab0e5a90c7bff96bcf400ce"
        },
        "date": 1793021509614,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 0.74,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132091904,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 6.62,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132161536,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 67.55,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132874240,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "1bec1f85e155d7d7fe25e5062cc8a9ddd8948e34",
          "message": "chore(scripts): quote comma words in array literals\n\nshellcheck reads an unquoted comma inside an array literal as a likely\nmistake for an element separator (SC2054). scripts/cppcheck passes\n--enable=warning,performance,portability,unusedFunction and\nscripts/scan-runtime-image passes --pkg-types os,library that way.\nQuote both values. Each array keeps the same elements, so cppcheck and\nTrivy receive the same arguments.",
          "timestamp": "2026-10-26T14:51:28+01:00",
          "tree_id": "b03f46b07793c6dc8815a18713dbbd01ad9ed92c",
          "url": "https://github.com/lorenzocaprari/nesso/commit/1bec1f85e155d7d7fe25e5062cc8a9ddd8948e34"
        },
        "date": 1793023019008,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 1.43,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 137236480,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 13.02,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 137232384,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 129.52,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132620288,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "59aa049f14dcb3925719e3668f6abd9261e837e9",
          "message": "build: ship the release binary without a RUNPATH\n\nCMakeDeps adds every host package's lib directory to the link, and CMake\nrecorded them as the binary's RUNPATH: Conan build-tree paths that do\nnot exist on a user's machine, followed by an empty entry. The dynamic\nloader searches an empty entry as the current directory, so running\nnesso from a directory someone else can write to could load a planted\nlibstdc++.so.6, libm.so.6 or libgcc_s.so.1 (CWE-426).\n\nAll Conan dependencies are linked statically, so the binary needs no\nRUNPATH at all. The toolchain now sets CMAKE_SKIP_RPATH through the\nrpath block. The binary's only other change is the smaller dynamic\nsection.\n\nscripts/check-no-runpath reads readelf -d and fails when an RPATH or\nRUNPATH tag is present, naming empty entries. scripts/build-release-binary\nruns it on the binary it produces, so the release build, the .deb and\nthe runtime image cannot regress. Tests cover a clean binary, a\ntrailing and an only empty entry, RPATH, an unparsable tag line, and\nreadelf failures.",
          "timestamp": "2026-10-26T14:54:22+01:00",
          "tree_id": "2dd21b1c4ab317f784ffdb53bc70afb3196a731b",
          "url": "https://github.com/lorenzocaprari/nesso/commit/59aa049f14dcb3925719e3668f6abd9261e837e9"
        },
        "date": 1793023625215,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 3.57,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132464640,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 25.31,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132493312,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 244.43,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 134955008,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "52a75bd52b02cf095e64fd4b9c38440c9a437740",
          "message": "fix(embed): cap int8 sessions by estimated per-session memory and available RAM\n\nThe default int8 pool was meant to leave half of RAM free, but it\ndivided by the model file size (about 23 MB), so 8 GiB allowed 187\nsessions and the cap never applied. It also used total RAM, not the\nmemory still available.\n\nThe cap now divides half of MemAvailable from /proc/meminfo (total RAM\nwhen the kernel does not report it) by 480 MiB per session, and keeps\nat least one session. The per-session figure (480 MiB) is estimated\nfrom Python ORT measurements, not measured in this build: about 440 MiB\nof activations for one 32x256 batch plus about 40 MiB per session.\n8 GiB available allows 8 sessions, 4 GiB allows 4, and 1 GiB allows 1.\nAn unknown memory figure leaves the pool at the core count, as before.\n\n--threads and NESSO_THREADS still set the session count. When an int8\ncount exceeds the cap, nesso prints a warning on stderr and continues\nwith the requested count. --version --verbose shows the cap.\n\nThe policy moves from onnx_embedder.cpp, which coverage excludes\nbecause it needs the model, into session_policy.cpp. readMachine()\ntakes /proc and /sys roots so tests can use fixture trees. The session\ncount never changes embedding bytes.",
          "timestamp": "2026-10-26T15:11:07+01:00",
          "tree_id": "801e64a76c79e46a92ac650d94d286ac1b22d727",
          "url": "https://github.com/lorenzocaprari/nesso/commit/52a75bd52b02cf095e64fd4b9c38440c9a437740"
        },
        "date": 1793024232499,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 1.39,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 133332992,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 12.65,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132505600,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 125.55,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132579328,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "f5a82d10f0fa5fd5d7dc760c4ff9fcd85d402689",
          "message": "fix(search): reject a corpus built with another model or format version\n\nA corpus whose header carries another format version was reported as\n\"the file is corrupt\", which sent users looking for disk damage instead\nof re-running nesso index. The reader now returns UnsupportedVersion\nfor a valid magic with an unknown version, and search reports \"the\ncorpus format is not supported by this nesso; re-run nesso index\". A\nshort or damaged header is still reported as corrupt.\n\nThe corpus stores the id of the model that embedded it, but nothing\nread it. search now compares it with the embedding model's id before\nthe model loads, and a different id fails with \"the corpus was built\nwith a different model; re-run nesso index\". An empty corpus is still\nreported as empty first. Both failures exit with status 2, as before.\n\nEvery corpus written today stores the same id for the int8 and fp32\nmodels, so this check does not tell those two apart.\n\nTests cover the reader's version handling, the new causes and messages,\nthe facade without a model, and two BDD scenarios with checked-in\nfixtures. other-model.nesso and future-version.nesso are one-chunk.nesso\nwith the model id's last character or the version field changed.",
          "timestamp": "2026-10-26T15:25:41+01:00",
          "tree_id": "2677a244e3f7405a74487fd46a65aea18532ed45",
          "url": "https://github.com/lorenzocaprari/nesso/commit/f5a82d10f0fa5fd5d7dc760c4ff9fcd85d402689"
        },
        "date": 1793025221578,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 3.61,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 131928064,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 25.21,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 136900608,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 242.03,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132136960,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "705d574fe2328058a8a63f288572192feb695c3c",
          "message": "build(release): fail when the release binary links networking code\n\ncurl and OpenSSL are in the Conan graph through date and are not linked\ninto the release binary. scripts/check-network-free fails that binary\nwhen readelf -d reports a denied NEEDED library, when an undefined\ndynamic symbol is a networking libc call that is not in\nscripts/check-network-free.allow, or when defined symbols or the\nversion strings libcurl/ and \"OpenSSL \" show curl or OpenSSL linked\nstatically. A missing tool or a readelf, nm, or strings failure exits 2.\n\nscripts/build-release-binary runs the check on the binary it produces,\nso the release build, the .deb, and the runtime image stay covered.\nTests put fake readelf, nm, and strings on PATH and cover a clean\nbinary, a denied NEEDED entry, a denied import, an allow-listed import,\na static curl_ or SSL_ symbol, version strings, and tool failure.",
          "timestamp": "2026-10-26T15:51:06+01:00",
          "tree_id": "fbbeae7391c4f2877221af839a3b62a66fd0f5f0",
          "url": "https://github.com/lorenzocaprari/nesso/commit/705d574fe2328058a8a63f288572192feb695c3c"
        },
        "date": 1793026616830,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 1.67,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132341760,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 14.88,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132308992,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 149.19,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 133115904,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "ec8977ad5c936cd41d7e9b0816ccf8bfb9842014",
          "message": "fix(embed): keep int8 embeddings off AMX tiles\n\nONNX Runtime 1.18 switches its int8 GEMM to the AMX kernel whenever\nLinux grants the process AMX tile permission. Some virtual machines\nadvertise AMX and grant that permission but do not restore tile\nregisters after a context switch. An int8 GEMM that is preempted then\nreturns wrong numbers, so the same text embedded twice under CPU load\ngave different vectors, from a small drift to a cosine near zero. One\nintra-op thread per session does not prevent it: a single thread is\nstill preempted. fp32 does not use AMX and was not affected.\n\nThe embedder now installs a seccomp filter before the first ONNX\nRuntime environment. It refuses arch_prctl(ARCH_REQ_XCOMP_PERM) with\nEPERM and allows every other call, so ONNX Runtime keeps the AVX-512\nVNNI or AVX2 int8 kernel. Integer GEMM is exact, so on a host that\npreserves tile state those kernels give the same bytes as AMX. The\nfilter sets no_new_privs for the process. If the permission was already\ngranted, or the filter cannot be installed, ONNX Runtime keeps its\ndefault.\n\nThe repeat test now requires byte equality. A new embedder test runs\ntwo batches on two sessions eleven times, requires identical bytes,\nand checks that the tile permission request is refused.",
          "timestamp": "2026-10-26T17:00:11+01:00",
          "tree_id": "242174939c63fac8c3b44eb7473bcd39e2857502",
          "url": "https://github.com/lorenzocaprari/nesso/commit/ec8977ad5c936cd41d7e9b0816ccf8bfb9842014"
        },
        "date": 1793030692287,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 0.81,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132927488,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 7.1,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 127975424,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 69.5,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132628480,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "41ed63aeb74e0ad1028786ef60ccb12da9280753",
          "message": "docs: limit the single-author rule to maintainer and agent commits",
          "timestamp": "2026-10-26T14:00:02-04:00",
          "tree_id": "065ccd0ca7e04601e9b77fec5e8a5e9fd1a1d9ff",
          "url": "https://github.com/lorenzocaprari/nesso/commit/41ed63aeb74e0ad1028786ef60ccb12da9280753"
        },
        "date": 1793038074015,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 1.39,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132481024,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 12.85,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132235264,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 125.32,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 137605120,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "49699333+dependabot[bot]@users.noreply.github.com",
            "name": "dependabot[bot]",
            "username": "dependabot[bot]"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "e666c44043bb5f8d910540b6043693cf43c14f9b",
          "message": "build(deps): bump virtualenv from 21.14.7 to 21.14.13 in /.devcontainer\n\nBumps [virtualenv](https://github.com/pypa/virtualenv) from 21.14.7 to 21.14.13.\n- [Release notes](https://github.com/pypa/virtualenv/releases)\n- [Changelog](https://github.com/pypa/virtualenv/blob/main/docs/changelog.rst)\n- [Commits](https://github.com/pypa/virtualenv/compare/21.14.7...21.14.13)\n\n---\nupdated-dependencies:\n- dependency-name: virtualenv\n  dependency-version: 21.14.13\n  dependency-type: direct:production\n...\n\nSigned-off-by: dependabot[bot] <support@github.com>",
          "timestamp": "2026-10-26T14:00:50-04:00",
          "tree_id": "dc797939011f063dced03d6e33dbc9b09160b822",
          "url": "https://github.com/lorenzocaprari/nesso/commit/e666c44043bb5f8d910540b6043693cf43c14f9b"
        },
        "date": 1793041357464,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 1.21,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132091904,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 11.04,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132599808,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 109.69,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132059136,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "4049ec09f0f5f998415c95e3f6aecc546301a75e",
          "message": "docs(security): describe private reporting as enabled and list 0.2.x\n\nPrivate vulnerability reporting is enabled. Point reporters at the\nSecurity tab form and list 0.2.x as the upcoming supported release.",
          "timestamp": "2026-10-26T14:57:32-04:00",
          "tree_id": "ee7ab14d3f956e29e585b914fcb1b18a56dea9f9",
          "url": "https://github.com/lorenzocaprari/nesso/commit/4049ec09f0f5f998415c95e3f6aecc546301a75e"
        },
        "date": 1793042109795,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 1.4,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 137711616,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 12.63,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132739072,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 132.1,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 131772416,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "882ced5322dfa8223c37eb75f94490fd837ff1e6",
          "message": "ci(release): require approval before publishing\n\nTag pushes wait on the release environment after verify-package. Jobs that\npush, sign, attest, or upload release assets need that approval. A manual\ndry run skips the environment and still does not publish.",
          "timestamp": "2026-10-26T15:27:57-04:00",
          "tree_id": "0c8b22aa453e8d3d38bd9d80dd022234f1181b50",
          "url": "https://github.com/lorenzocaprari/nesso/commit/882ced5322dfa8223c37eb75f94490fd837ff1e6"
        },
        "date": 1793043159366,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 0.88,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 131923968,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 7.8,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132317184,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 74.25,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132456448,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "b2e6099689628961a483bbc0db14d58341936e1f",
          "message": "ci: add a single gate job for required checks",
          "timestamp": "2026-10-26T15:28:14-04:00",
          "tree_id": "a9f6232d280034e29b28c3c22b591f061d08f241",
          "url": "https://github.com/lorenzocaprari/nesso/commit/b2e6099689628961a483bbc0db14d58341936e1f"
        },
        "date": 1793044066294,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 3.57,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 131825664,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 25.04,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132153344,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 239.88,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132501504,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "37952fdbd86cafd3dbf34c829723d5f6c39d81ff",
          "message": "docs(security): drop the enable-reporting note from the issue template\n\nPrivate vulnerability reporting is enabled. The issue template contact\nlink and the code of conduct now describe that form as the private route.",
          "timestamp": "2026-10-26T15:42:38-04:00",
          "tree_id": "6d7bd30960640d4ff2b7f06c0e0926e57b1e7e5f",
          "url": "https://github.com/lorenzocaprari/nesso/commit/37952fdbd86cafd3dbf34c829723d5f6c39d81ff"
        },
        "date": 1793044411123,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 1.21,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132161536,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 11.01,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132427776,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 106.01,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132608000,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "a82123aab35620803bb09703cabea17f0ab78ee7",
          "message": "build(deps): bump devcontainer Python pins to fix known vulnerabilities\n\nPin certifi 2026.10.19 and filelock 4.2.1, the lowest releases of those packages with no known OSV vulnerabilities. virtualenv 21.14.13 is already on main. Hashes come from pip-compile --generate-hashes.",
          "timestamp": "2026-10-26T16:42:05-04:00",
          "tree_id": "3aa2d535bec21c05eb967a899ee75c15d4f74746",
          "url": "https://github.com/lorenzocaprari/nesso/commit/a82123aab35620803bb09703cabea17f0ab78ee7"
        },
        "date": 1793047688840,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 1.4,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 133308416,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 13.11,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 133255168,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 126.51,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 137932800,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "2c9d942a6f7613f34289def1e446b02c71f0e190",
          "message": "fix(platform): read ru_maxrss directly in peakResidentBytes\n\nPlacement-new of rusage into a one-byte-aligned array is undefined.\nRead the glibc field after getrusage and keep the Linux kilobyte-to-byte conversion.",
          "timestamp": "2026-10-26T17:16:54-04:00",
          "tree_id": "d326e085f778eee917e1b0fa9ad9880ab68d1af5",
          "url": "https://github.com/lorenzocaprari/nesso/commit/2c9d942a6f7613f34289def1e446b02c71f0e190"
        },
        "date": 1793049872581,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 3.56,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 136998912,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 25.14,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132407296,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 240.79,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 137187328,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "98c475c466bb87ce191fc8d8563412a3bbf3b9fa",
          "message": "fix(cli): reject an out-of-range NESSO_THREADS with a usage error\n\nParse NESSO_THREADS with std::from_chars into size_t so a digit string\nthat does not fit returns the existing usage error instead of throwing\nstd::out_of_range from std::stoull.",
          "timestamp": "2026-10-26T17:57:32-04:00",
          "tree_id": "297fa91eaf28962037964f0d0b4e8aa1f5f2adc7",
          "url": "https://github.com/lorenzocaprari/nesso/commit/98c475c466bb87ce191fc8d8563412a3bbf3b9fa"
        },
        "date": 1793052134219,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 0.9,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132943872,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 7.66,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132644864,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 76.54,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 132382720,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "f8dad77680d6bb5456f8d7022bd105274de2fe42",
          "message": "docs: show nesso grep in the README and demo",
          "timestamp": "2026-10-26T18:05:50-04:00",
          "tree_id": "a5b1e66c22bf9c964e071498afd44c097eb8b79b",
          "url": "https://github.com/lorenzocaprari/nesso/commit/f8dad77680d6bb5456f8d7022bd105274de2fe42"
        },
        "date": 1793052694394,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 1.36,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132530176,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 12.54,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132157440,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 124.22,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 137527296,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "9d35c9408be61c879edcb57739e8a0e2404a3e06",
          "message": "chore: clean up clang-tidy config, stale scripts, and docs",
          "timestamp": "2026-10-26T18:16:00-04:00",
          "tree_id": "6d56600f11dce5623d899010ea6426d2ec91d443",
          "url": "https://github.com/lorenzocaprari/nesso/commit/9d35c9408be61c879edcb57739e8a0e2404a3e06"
        },
        "date": 1793053451808,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 3.6,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 132300800,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 25.34,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 132911104,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 241.81,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 133210112,
            "unit": "bytes"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "caprarilorenzo5@gmail.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "committer": {
            "email": "85455384+lorenzocaprari@users.noreply.github.com",
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari"
          },
          "distinct": true,
          "id": "dbb724c939a75c5088e9a51d64e0c24c7b1cf249",
          "message": "fix(ci): run the release subjects job on dry runs",
          "timestamp": "2026-10-26T18:40:52-04:00",
          "tree_id": "a737515a2151ce5d51c0db3ca2edb7ca734c6f03",
          "url": "https://github.com/lorenzocaprari/nesso/commit/dbb724c939a75c5088e9a51d64e0c24c7b1cf249"
        },
        "date": 1793054896299,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "grep-1000-wall",
            "value": 3.62,
            "unit": "seconds"
          },
          {
            "name": "grep-1000-rss",
            "value": 133234688,
            "unit": "bytes"
          },
          {
            "name": "grep-10000-wall",
            "value": 25.52,
            "unit": "seconds"
          },
          {
            "name": "grep-10000-rss",
            "value": 137011200,
            "unit": "bytes"
          },
          {
            "name": "grep-100000-wall",
            "value": 246.24,
            "unit": "seconds"
          },
          {
            "name": "grep-100000-rss",
            "value": 137433088,
            "unit": "bytes"
          }
        ]
      }
    ]
  }
}
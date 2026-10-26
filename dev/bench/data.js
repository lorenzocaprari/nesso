window.BENCHMARK_DATA = {
  "lastUpdate": 1793024233330,
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
      }
    ]
  }
}
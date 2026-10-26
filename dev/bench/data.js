window.BENCHMARK_DATA = {
  "lastUpdate": 1793012522040,
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
      }
    ]
  }
}
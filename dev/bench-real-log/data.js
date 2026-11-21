window.BENCHMARK_DATA = {
  "lastUpdate": 1795283768345,
  "repoUrl": "https://github.com/lorenzocaprari/nesso",
  "entries": {
    "nesso real-log": [
      {
        "commit": {
          "author": {
            "name": "Lorenzo Caprari",
            "username": "lorenzocaprari",
            "email": "85455384+lorenzocaprari@users.noreply.github.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "a74a387ef22e4c250b8044814414c74f296aa1be",
          "message": "ci(soak): retire the index-rss workflow (#354)\n\nThe Index RSS workflow gated nesso index peak RSS on the HDFS 2k sample\nrepeated to 1M lines. The nightly real-log benchmark already gates peak\nRSS on real 1M-line HDFS and Thunderbird logs, for index, grep, and\nsearch at 1 and 8 threads, so the repeated sample added a weaker copy of\nthe same check on an input with few distinct messages. Remove the\nworkflow and keep scripts/check-index-rss as a local check.\n\nAGENTS.md, CONTRIBUTING.md, docs/benchmarks.md, docs/building.md, and\nthe changelog no longer list the workflow; the Harden-Runner list drops\nit.",
          "timestamp": "2026-11-21T17:22:15Z",
          "url": "https://github.com/lorenzocaprari/nesso/commit/a74a387ef22e4c250b8044814414c74f296aa1be"
        },
        "date": 1795283767512,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "real-log/hdfs-v1/threads-1/index/wall",
            "value": 15.35,
            "unit": "s"
          },
          {
            "name": "real-log/hdfs-v1/threads-1/grep/wall",
            "value": 8.1,
            "unit": "s"
          },
          {
            "name": "real-log/hdfs-v1/threads-1/search/wall",
            "value": 2.19,
            "unit": "s"
          },
          {
            "name": "real-log/hdfs-v1/threads-1/search-dense/wall",
            "value": 2.08,
            "unit": "s"
          },
          {
            "name": "real-log/hdfs-v1/threads-8/index/wall",
            "value": 15.99,
            "unit": "s"
          },
          {
            "name": "real-log/hdfs-v1/threads-8/grep/wall",
            "value": 8.57,
            "unit": "s"
          },
          {
            "name": "real-log/hdfs-v1/threads-8/search/wall",
            "value": 2.77,
            "unit": "s"
          },
          {
            "name": "real-log/hdfs-v1/threads-8/search-dense/wall",
            "value": 2.69,
            "unit": "s"
          }
        ]
      }
    ]
  }
}
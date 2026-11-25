# ClusterFuzzLite builder. Clang, libc++, and libFuzzer come from base-builder.
# Digest-pinned so Scorecard Pinned-Dependencies accepts the image; Dependabot
# updates the digest with the docker ecosystem.
FROM gcr.io/oss-fuzz-base/base-builder@sha256:ed4aefea5aa72bb8c133576dd13a1417e0fc6eeb27cc0fc7f9880e7e13293b07

# No extra packages in the image. build.sh compiles parser and tokenizer and
# installs zlib headers when they are missing.
COPY . $SRC/nesso
WORKDIR $SRC/nesso
COPY .clusterfuzzlite/build.sh $SRC/build.sh

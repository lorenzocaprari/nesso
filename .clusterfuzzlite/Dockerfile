# ClusterFuzzLite builder. Clang, libc++, and libFuzzer come from base-builder.
# Digest-pinned so Scorecard Pinned-Dependencies accepts the image; Dependabot
# updates the digest with the docker ecosystem.
FROM gcr.io/oss-fuzz-base/base-builder@sha256:1f9cf74bc3d7029b528494542f9d9851b256dceed38d3f420989434dc5ae0760

# No extra packages in the image. build.sh compiles parser and tokenizer and
# installs zlib headers when they are missing.
COPY . $SRC/nesso
WORKDIR $SRC/nesso
COPY .clusterfuzzlite/build.sh $SRC/build.sh

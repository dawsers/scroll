#!/bin/sh -eu

# glslang prints log messages to stdout, remap to stderr
exec "$@" >&2

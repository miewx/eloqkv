#!/usr/bin/env bash
# Wrapper to run the Bun JS implementation
bun "$(dirname "$0")/sync.js" "$@"

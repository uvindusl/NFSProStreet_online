#!/bin/bash
# Exact command used to capture the WebGPU triangle screenshot.
# Requires: web/node_modules/playwright-core (npm install in web/)
set -e
cd "$(dirname "$0")/../../.."
node /home/uvindu/.claude/jobs/af739281/tmp/wp_tri3.js

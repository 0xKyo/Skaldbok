#!/usr/bin/env bash
# Opens Skaldbok. The app starts the players' web server by itself (Web module), so this is all that is needed:
# the players' links are in the app (Web), and their pages work as long as the app is open.
cd "$(dirname "$(readlink -f "$0")")"

APP="./skaldbok"
[ -x "$APP" ] || APP="./build/release/skaldbok"
if [ ! -x "$APP" ]; then
  echo "Could not find skaldbok."
  echo "Build it first:  ./Build.sh        (add --deps the first time to install the compiler and libraries)"
  exit 1
fi

# The players' page (Vue): built once, if it is missing and Node.js is installed.
if [ ! -f web/index.html ] && [ ! -f web/client/dist/index.html ]; then
  if command -v npm >/dev/null; then
    echo "Building the players' web page (first time only)..."
    (cd web && npm install && npm run build)
  else
    echo "Note: the players' page is not built and Node.js is not installed, so the web link will show an empty page."
    echo "Install Node.js (sudo apt install nodejs npm) and run this file again."
  fi
fi

# Detached, like "start" on Windows: closing the terminal does not close the app.
nohup "$APP" "$@" >/dev/null 2>&1 &
disown

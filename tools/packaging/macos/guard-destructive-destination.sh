#!/bin/sh
set -eu

usage() {
    echo "usage: $0 <role> <destination> <expected-basename>" >&2
}

if [ "$#" -ne 3 ]; then
    usage
    exit 2
fi

role=$1
destination=$2
expected_basename=$3

trimmed=$destination
while [ "$trimmed" != "/" ] && [ "${trimmed%/}" != "$trimmed" ]; do
    trimmed=${trimmed%/}
done

case "$trimmed" in
    ""|"/"|"."|"..")
        echo "Refusing destructive $role destination: $destination" >&2
        exit 1
        ;;
    "../"*|*/../*|*/..)
        echo "Refusing destructive $role destination with parent traversal: $destination" >&2
        exit 1
        ;;
esac

basename=${trimmed##*/}
if [ "$basename" != "$expected_basename" ]; then
    echo "Refusing destructive $role destination '$destination': expected basename '$expected_basename', got '$basename'" >&2
    exit 1
fi

exit 0

#!/bin/sh

# This script runs test scripts and artifacts inside pbuilder environment.
# Please use pbuilder-dist to create the environment first.

set -e

DISTRIBUTION="$1"
shift
ARCH="$1"
shift
SCRIPT1="$1"
shift

TDIR=$(mktemp -d)

cp -f "${SCRIPT1}" "${TDIR}"

while [ $# -gt 0 ]; do
    cp "$1" "${TDIR}" 
    shift
done

SCRIPT1NAME=$(basename "${SCRIPT1}")

echo $1

cat <<EOF > "${TDIR}"/start1.sh
#!/bin/sh

set -e

echo $1

cd "${TDIR}"

exec "${TDIR}/${SCRIPT1NAME}"

EOF

chmod a+x "${TDIR}"/start1.sh

pbuilder-dist "${DISTRIBUTION}" "${ARCH}" execute --bindmounts "${TDIR}" "${TDIR}/start1.sh"

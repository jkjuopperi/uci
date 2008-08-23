#!/bin/sh

TESTS_DIR="./tests"
CONFIG_DIR=${TESTS_DIR}"/config"
CHANGES_DIR="/tmp/.uci"
TMP_DIR=${TESTS_DIR}"/tmp"
FULL_SUITE=${TESTS_DIR}"/full_suite.sh"

UCI_STATIC="../uci-static"
[ -x $UCI_STATIC ] || {
	echo "uci-static is not present."
	return 1
}
UCI="${UCI_STATIC} -c ${CONFIG_DIR} -p ${CHANGES_DIR}"

REF_DIR="./references"
SCRIPTS_DIR="./tests.d"
DO_TEST="./shunit2/shunit2"

rm -rf ${TESTS_DIR}
mkdir -p ${TESTS_DIR}

cat << 'EOF' > ${FULL_SUITE}
setUp() {
	mkdir -p ${CONFIG_DIR} ${CHANGES_DIR} ${TMP_DIR}
}
tearDown() {
	rm -rf ${CONFIG_DIR} ${CHANGES_DIR} ${TMP_DIR}
}
assertSameFile() {
	local ref=$1
	local test=$2
	diff -qr $ref $test
	assertTrue $? || {
		echo "REF:"
		cat $ref
		echo "----"
		echo "TEST:"
		cat $test
		echo "----"
	}
}
EOF

for suite in $(ls ${SCRIPTS_DIR}/*)
do
	cat ${suite} >> ${FULL_SUITE}
done

echo ". ${DO_TEST}" >> ${FULL_SUITE}

REF_DIR="${REF_DIR}" \
CONFIG_DIR="${CONFIG_DIR}" \
CHANGES_DIR="${CHANGES_DIR}" \
TMP_DIR="${TMP_DIR}" \
UCI="${UCI}" \
/bin/sh ${FULL_SUITE}

rm -rf ${TESTS_DIR}

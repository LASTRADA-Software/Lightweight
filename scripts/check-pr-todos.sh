#! /bin/bash
count=0
FOUND=$(git grep "TODO(pr)" | grep -v "scripts/check-pr-todos.sh")
if [[ "${FOUND}" != "" ]]; then
    count=$[count + 1]
fi

FOUND2=$(git grep "crispy::todo(" | grep -v "scripts/check-pr-todos.sh")
if [[ "${FOUND2}" != "" ]]; then
    count=$[count + 1]
fi

FOUND3=$(git grep "TODO(PR)" | grep -v "scripts/check-pr-todos.sh")
if [[ "${FOUND3}" != "" ]]; then
    count=$[count + 1]
fi

if [[ "${count}" == "0" ]]; then
    exit 0
fi
echo "This PR still contains PR-related TODO itmes that must be resolved."
echo
echo "${FOUND} ${FOUND2} ${FOUND3}"
exit 1

#!/bin/sh

# Script for generating a list of candidates which have typos in the nomination line
#
# Usage examples:
#
# $ bin/get-typod-pick-list.sh
# $ bin/get-typod-pick-list.sh > picklist
# $ bin/get-typod-pick-list.sh | tee picklist

# NB:
# This script intentionally _never_ checks for specific version tag
# Should we consider folding it with the original get-pick-list.sh

# Use the last branchpoint as our limit for the search
latest_branchpoint=`git merge-base origin/master HEAD`

# Grep for commits with "cherry picked from commit" in the commit message.
git log --reverse --grep="cherry picked from commit" $latest_branchpoint..HEAD |\
	grep "cherry picked from commit" |\
	sed -e 's/^[[:space:]]*(cherry picked from commit[[:space:]]*//' -e 's/)//' > already_picked

# Grep for commits that were marked as a candidate for the stable tree.
git log --reverse --pretty=%H -i --grep='^CC:.*mesa-dev' $latest_branchpoint..origin/master |\
while read sha
do
	# Check to see whether the patch is on the ignore list.
	if [ -f bin/.cherry-ignore ] ; then
		if grep -q ^$sha bin/.cherry-ignore ; then
			continue
		fi
	fi

	# Check to see if it has already been picked over.
	if grep -q ^$sha already_picked ; then
		continue
	fi

	git log -n1 --pretty=oneline $sha | cat
done

rm -f already_picked

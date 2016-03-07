#!/bin/sh

# Script for generating a list of candidates which fix commits that have been
# previously cherry-picked to a stable branch.
#
# Usage examples:
#
# $ bin/get-extra-pick-list.sh
# $ bin/get-extra-pick-list.sh > picklist
# $ bin/get-extra-pick-list.sh | tee picklist

# Use the last branchpoint as our limit for the search
# XXX: there should be a better way for this
latest_branchpoint=`git branch | grep \* | cut -c 3-`-branchpoint

# Grep for commits with "cherry picked from commit" in the commit message.
git log --reverse --grep="cherry picked from commit" $latest_branchpoint..HEAD |\
	grep "cherry picked from commit" |\
	sed -e 's/^[[:space:]]*(cherry picked from commit[[:space:]]*//' -e 's/)//' |\
	cut -c -8 |\
while read sha
do
	# Check if the original commit is referenced in master
	git log -n1 --pretty=oneline --grep=$sha $latest_branchpoint..origin/master |\
		cut -c -8 |\
	while read candidate
	do
		# Check if the potential fix, hasn't landed in branch yet.
		found=`git log -n1 --pretty=oneline --reverse --grep=$candidate $latest_branchpoint..HEAD |wc -l`
		if test $found = 0
		then
			echo Commit $candidate might need to be picked, as it references $sha
		fi
	done
done

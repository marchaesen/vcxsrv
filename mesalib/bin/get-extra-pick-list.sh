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
latest_branchpoint=`git merge-base origin/master HEAD`

# Grep for commits with "cherry picked from commit" in the commit message.
git log --reverse --grep="cherry picked from commit" $latest_branchpoint..HEAD |\
	grep "cherry picked from commit" |\
	sed -e 's/^[[:space:]]*(cherry picked from commit[[:space:]]*//' -e 's/)//'  > already_picked

# For each cherry-picked commit...
cat already_picked | cut -c -8 |\
while read sha
do
	# ... check if it's referenced (fixed by another) patch
	git log -n1 --pretty=oneline --grep=$sha $latest_branchpoint..origin/master |\
		cut -c -8 |\
	while read candidate
	do
		# And flag up if it hasn't landed in branch yet.
		if grep -q ^$candidate already_picked ; then
			continue
		fi
		echo Commit $candidate references $sha
	done
done

rm -f already_picked

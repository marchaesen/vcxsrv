#!/bin/sh

# Script for generating a list of candidates [referenced by a Fixes tag] for
# cherry-picking to a stable branch
#
# Usage examples:
#
# $ bin/get-fixes-pick-list.sh
# $ bin/get-fixes-pick-list.sh > picklist
# $ bin/get-fixes-pick-list.sh | tee picklist

# Use the last branchpoint as our limit for the search
latest_branchpoint=`git merge-base origin/master HEAD`

# List all the commits between day 1 and the branch point...
git log --reverse --pretty=%H $latest_branchpoint > already_landed

# ... and the ones cherry-picked.
git log --reverse --grep="cherry picked from commit" $latest_branchpoint..HEAD |\
	grep "cherry picked from commit" |\
	sed -e 's/^[[:space:]]*(cherry picked from commit[[:space:]]*//' -e 's/)//'  > already_picked

# Grep for commits with Fixes tag
git log --reverse --pretty=%H -i --grep="fixes:" $latest_branchpoint..origin/master |\
while read sha
do
	# Check to see whether the patch is on the ignore list ...
	if [ -f bin/.cherry-ignore ] ; then
		if grep -q ^$sha bin/.cherry-ignore ; then
			continue
		fi
	fi

	# For each one try to extract the tag
	fixes_count=`git show $sha | grep -i "fixes:" | wc -l`
	if [ "x$fixes_count" != x1 ] ; then
		printf "WARNING: Commit \"%s\" has more than one Fixes tag\n" \
		       "`git log -n1 --pretty=oneline $sha`"
	fi
	fixes=`git show $sha | grep -i "fixes:" | head -n 1`
	# The following sed/cut combination is borrowed from GregKH
	id=`echo ${fixes} | sed -e 's/^[ \t]*//' | cut -f 2 -d ':' | sed -e 's/^[ \t]*//' | cut -f 1 -d ' '`

	# Bail out if we cannot find suitable id.
	# Any specific validation the $id is valid and not some junk, is
	# implied with the follow up code
	if [ "x$id" = x ] ; then
		continue
	fi

	# Check if the offending commit is in branch.

	# Be that cherry-picked ...
	# ... or landed before the branchpoint.
	if grep -q ^$id already_picked ||
	   grep -q ^$id already_landed ; then

		# Finally nominate the fix if it hasn't landed yet.
		if grep -q ^$sha already_picked ; then
			continue
		fi

		printf "Commit \"%s\" fixes %s\n" \
		       "`git log -n1 --pretty=oneline $sha`" \
		       "$id"
	fi

done

rm -f already_picked
rm -f already_landed

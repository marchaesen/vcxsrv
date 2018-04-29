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
git log --reverse --pretty=medium --grep="cherry picked from commit" $latest_branchpoint..HEAD |\
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

	# Skip if it has been already cherry-picked.
	if grep -q ^$sha already_picked ; then
		continue
	fi

	# Place every "fixes:" tag on its own line and join with the next word
	# on its line or a later one.
	fixes=`git show --pretty=medium -s $sha | tr -d "\n" | sed -e 's/fixes:[[:space:]]*/\nfixes:/Ig' | grep "fixes:" | sed -e 's/\(fixes:[a-zA-Z0-9]*\).*$/\1/'`

	# For each one try to extract the tag
	fixes_count=`echo "$fixes" | wc -l`
	warn=`(test $fixes_count -gt 1 && echo $fixes_count) || echo 0`
	while [ $fixes_count -gt 0 ] ; do
		# Treat only the current line
		id=`echo "$fixes" | tail -n $fixes_count | head -n 1 | cut -d : -f 2`
		fixes_count=$(($fixes_count-1))

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

			printf "Commit \"%s\" fixes %s\n" \
			       "`git log -n1 --pretty=oneline $sha`" \
			       "$id"
			warn=$(($warn-1))
		fi

	done

	if [ $warn -gt 0 ] ; then
		printf "WARNING: Commit \"%s\" has more than one Fixes tag\n" \
		       "`git log -n1 --pretty=oneline $sha`"
	fi

done

rm -f already_picked
rm -f already_landed

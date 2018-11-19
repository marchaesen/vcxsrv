#!/bin/sh

# Script for generating a list of candidates for cherry-picking to a stable branch
#
# Usage examples:
#
# $ bin/get-pick-list.sh
# $ bin/get-pick-list.sh > picklist
# $ bin/get-pick-list.sh | tee picklist
#
# The output is as follows:
# [nomination_type] commit_sha commit summary

is_stable_nomination()
{
	git show --summary "$1" | grep -q -i -o "CC:.*mesa-stable"
}

is_typod_nomination()
{
	git show --summary "$1" | grep -q -i -o "CC:.*mesa-dev"
}

# Helper to handle various mistypos of the fixes tag.
# The tag string itself is passed as argument and normalised within.
is_sha_nomination()
{
	fixes=`git show --pretty=medium -s $1 | tr -d "\n" | \
		sed -e 's/'"$2"'/\nfixes:/Ig' | \
		grep -Eo 'fixes:[a-f0-9]{8,40}'`

	fixes_count=`echo "$fixes" | wc -l`
	if test $fixes_count -eq 0; then
		return 0
	fi
	while test $fixes_count -gt 0; do
		# Treat only the current line
		id=`echo "$fixes" | tail -n $fixes_count | head -n 1 | cut -d : -f 2`
		fixes_count=$(($fixes_count-1))

		# Bail out if we cannot find suitable id.
		# Any specific validation the $id is valid and not some junk, is
		# implied with the follow up code
		if test "x$id" = x; then
			continue
		fi

		#Check if the offending commit is in branch.

		# Be that cherry-picked ...
		# ... or landed before the branchpoint.
		if grep -q ^$id already_picked ||
		   grep -q ^$id already_landed ; then
			return 0
		fi
	done
	return 1
}

is_fixes_nomination()
{
	is_sha_nomination "$1" "fixes:[[:space:]]*"
	if test $? -eq 0; then
		return 0
	fi
	is_sha_nomination "$1" "fixes[[:space:]]\+"
}

is_brokenby_nomination()
{
	is_sha_nomination "$1" "broken by"
}

is_revert_nomination()
{
	is_sha_nomination "$1" "This reverts commit "
}

# Use the last branchpoint as our limit for the search
latest_branchpoint=`git merge-base origin/master HEAD`

# List all the commits between day 1 and the branch point...
git log --reverse --pretty=%H $latest_branchpoint > already_landed

# ... and the ones cherry-picked.
git log --reverse --pretty=medium --grep="cherry picked from commit" $latest_branchpoint..HEAD |\
	grep "cherry picked from commit" |\
	sed -e 's/^[[:space:]]*(cherry picked from commit[[:space:]]*//' -e 's/)//' > already_picked

# Grep for potential candidates
git log --reverse --pretty=%H -i --grep='^CC:.*mesa-stable\|^CC:.*mesa-dev\|\<fixes\>\|\<broken by\>\|This reverts commit' $latest_branchpoint..origin/master |\
while read sha
do
	# Check to see whether the patch is on the ignore list.
	if test -f bin/.cherry-ignore; then
		if grep -q ^$sha bin/.cherry-ignore ; then
			continue
		fi
	fi

	# Check to see if it has already been picked over.
	if grep -q ^$sha already_picked ; then
		continue
	fi

	if is_stable_nomination "$sha"; then
		tag=stable
	elif is_typod_nomination "$sha"; then
		tag=typod
	elif is_fixes_nomination "$sha"; then
		tag=fixes
	elif is_brokenby_nomination "$sha"; then
		tag=brokenby
	elif is_revert_nomination "$sha"; then
		tag=revert
	else
		continue
	fi

	printf "[ %8s ] " "$tag"
	git --no-pager show --summary --oneline $sha
done

rm -f already_picked
rm -f already_landed

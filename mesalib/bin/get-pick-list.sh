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
	git show --pretty=medium --summary "$1" | grep -q -i -o "CC:.*mesa-stable"
}

is_typod_nomination()
{
	git show --pretty=medium --summary "$1" | grep -q -i -o "CC:.*mesa-dev"
}

fixes=

# Helper to handle various mistypos of the fixes tag.
# The tag string itself is passed as argument and normalised within.
#
# Resulting string in the global variable "fixes" and contains entries
# in the form "fixes:$sha"
is_sha_nomination()
{
	fixes=`git show --pretty=medium -s $1 | tr -d "\n" | \
		sed -e 's/'"$2"'/\nfixes:/Ig' | \
		grep -Eo 'fixes:[a-f0-9]{4,40}'`

	fixes_count=`echo "$fixes" | grep "fixes:" | wc -l`
	if test $fixes_count -eq 0; then
		return 1
	fi

	# Throw a warning for each invalid sha
	while test $fixes_count -gt 0; do
		# Treat only the current line
		id=`echo "$fixes" | tail -n $fixes_count | head -n 1 | cut -d : -f 2`
		fixes_count=$(($fixes_count-1))
		if ! git show $id >/dev/null 2>&1; then
			echo WARNING: Commit $1 lists invalid sha $id
		fi
	done

	return 0
}

# Checks if at least one of offending commits, listed in the global
# "fixes", is in branch.
sha_in_range()
{
	fixes_count=`echo "$fixes" | grep "fixes:" | wc -l`
	while test $fixes_count -gt 0; do
		# Treat only the current line
		id=`echo "$fixes" | tail -n $fixes_count | head -n 1 | cut -d : -f 2`
		fixes_count=$(($fixes_count-1))

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

	if is_fixes_nomination "$sha"; then
		tag=fixes
	elif is_brokenby_nomination "$sha"; then
		tag=brokenby
	elif is_revert_nomination "$sha"; then
		tag=revert
	elif is_stable_nomination "$sha"; then
		tag=stable
	elif is_typod_nomination "$sha"; then
		tag=typod
	else
		continue
	fi

	case "$tag" in
	fixes | brokenby | revert )
		if ! sha_in_range; then
			continue
		fi
		;;
	* )
		;;
	esac

	printf "[ %8s ] " "$tag"
	git --no-pager show --no-patch --pretty=oneline $sha
done

rm -f already_picked
rm -f already_landed

#!/bin/sh

BRANCH=master
NEWER=origin/master

if [ $(git symbolic-ref HEAD) = "refs/heads/$BRANCH" ]
then
    echo "This doesn't make sense if you're already on the branch '$BRANCH'"
    echo "Just run: git merge $NEWER"
    exit 1
fi

BRANCH_HASH=$(git rev-parse $BRANCH)
NEWER_HASH=$(git rev-parse $NEWER)
MERGE_BASE=$(git merge-base $BRANCH_HASH $NEWER_HASH)

if [ "$MERGE_BASE" = "$BRANCH_HASH" ]
then
    git update-ref "refs/heads/$BRANCH" "$NEWER_HASH" "$BRANCH_HASH"
else
    echo "$BRANCH can't be fast-forwarded to $NEWER"
    exit 1
fi

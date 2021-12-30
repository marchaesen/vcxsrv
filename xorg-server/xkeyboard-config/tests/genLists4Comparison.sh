#!/bin/sh

#
# This script compares the group names which "have to be", according to the descriptions in base.xml -
# and actually existing in the symbol files. Some differences are ok (like extra double quotes or 
# extra escaping character) - but all the rest should be in sync.
#

cd $(dirname $0)
ROOT=".."

# temporary files
registry_names=reg_names.lst
group_names=grp_names.lst
registry_names_base=${registry_names}.base
registry_names_extras=${registry_names}.extras

xsltproc reg2ll.xsl $ROOT/rules/base.xml        > $registry_names_base
xsltproc reg2ll.xsl $ROOT/rules/base.extras.xml | grep -v sun_type > $registry_names_extras

cat $registry_names_base $registry_names_extras | \
  sort | \
  uniq | \
  grep -v -e '^$' \
          -e '^custom:' > $registry_names
rm -f $registry_names_base $registry_names_extras

for sym in $ROOT/symbols/*; do
  if [ -f $sym ]; then
    id="`basename $sym`"
    export id
    gawk 'BEGIN{
  FS = "\"";
  id = ENVIRON["id"];
  isDefault = 0;
  isHwSpecificDefault = 0;
  isUnregistered = 0;
}
/#HW-SPECIFIC/{
  isHwSpecificDefault = 1;
}
/#UNREGISTERED/{
  isUnregistered = 1;
}
/^[[:space:]]*\/\//{
  next 
}
/.*default.*/{
  isDefault = 1;
}
/xkb_symbols/{
  variant = $2;
}/^[[:space:]]*name\[Group1\][[:space:]]*=/{
  if (isUnregistered == 1) {
    isUnregistered = 0;
  } else if (isDefault == 1)
  {
    printf "%s:\"%s\"\n",id,$2;
    isDefault=0;
  } else
  {
    name=$2;
    if (isHwSpecificDefault == 1) {
      isHwSpecificDefault = 0;
      printf "%s:\"%s\"\n", id, name;
    } else {
      printf "%s(%s):\"%s\"\n", id, variant, name;
    }
  }
}' $sym
  fi
done | sort | uniq > $group_names

diff -u $registry_names $group_names
rc=$?

if [ $rc != 0 ] ; then
  echo "Legend: '-' is for rules/base.*xml.in, '+' is for symbols/*"
fi

rm -f $registry_names $group_names

exit $rc

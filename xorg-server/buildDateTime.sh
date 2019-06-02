DATE_FMT="%Y%m%d"
TIME_FMT="1%H%M%S"

BUILD_DATE="`date "+$DATE_FMT"`"
BUILD_TIME="`date "+$TIME_FMT"`"
if test "x$SOURCE_DATE_EPOCH" != "x"; then
	BUILD_DATE="`date -u -d "@$SOURCE_DATE_EPOCH" "+$DATE_FMT" 2>/dev/null || date -u -r "$SOURCE_DATE_EPOCH" "+$DATE_FMT" 2>/dev/null || date -u "+$DATE_FMT"`"
	BUILD_TIME="`date -u -d "@$SOURCE_DATE_EPOCH" "+$TIME_FMT" 2>/dev/null || date -u -r "$SOURCE_DATE_EPOCH" "+$TIME_FMT" 2>/dev/null || date -u "+$TIME_FMT"`"
fi

output=$1
echo "#define BUILD_DATE $BUILD_DATE" > $output
echo "#define BUILD_TIME $BUILD_TIME" >> $output

BUILD_DATE=`date +'%Y%m%d'`
BUILD_TIME=`date +'1%H%M%S'`

output=$1
echo "#define BUILD_DATE $BUILD_DATE" > $output
echo "#define BUILD_TIME $BUILD_TIME" > $output

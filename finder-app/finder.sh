#!/bin/sh
filesdir=$1
searchstr=$2

if [ -z "$filesdir" ]; then
    echo "No files directory specified, using current directory"
    exit 1

elif [ ! -d "$filesdir" ]; then
    echo "Files directory $filesdir does not exist"
    exit 1
elif [ ! -d "$filesdir" ]; then
    echo "Files directory $filesdir does not exist"
    exit 1
else
    #echo "Using files directory $filesdir"
    #echo "Searching for files with string $searchstr"
    #echo "Searching for files in $filesdir containing string $searchstr"
    echo "The number of files are `find $filesdir -type f | wc -l` and the number of matching lines are `grep -r "$searchstr" $filesdir | wc -l`"
    #echo "The number of files are `find $filesdir` and the number of matching lines are `grep -r "$searchstr" $filesdir | wc -l`"
fi
#!/bin/sh


sed -ne 's/,/ /g' \
	-e 's/^[ 	]*CMD([ 	]*cmd_\(.*\)).*/\1/p; ' | \
while read cmd right state arg; do
		echo "{ \"$cmd\", cmd_$cmd, $right, $state, $arg },"
done

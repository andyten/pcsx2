#!/bin/sh

IFS="
"

for i in $(find . -iname '*.png' -type f); do
	j=$(echo $i | sed -e 's/ ([0-9]\+)//')
	if [ "$i" = "$j" ]; then
		continue
	fi
	echo $i '->' $j
	mv "$i" "$j"
done

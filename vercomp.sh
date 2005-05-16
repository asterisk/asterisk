#! /bin/bash

### flex just outputs a single line:

## flex version 2.5.4


### but bison is a bit more wordy

## bison (GNU Bison) 1.875c
## Written by Robert Corbett and Richard Stallman.
## 
## Copyright (C) 2003 Free Software Foundation, Inc.
## This is free software; see the source for copying conditions.  There is NO
## warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

### based on this, the version number of the program:
###   a. in the first line of output
###   b. is the last "word" of that line

program=$1
comparefunc=$2
argver=$3

progver1=`$program --version | head -1`

[[ $progver1 =~ '([^ ]+$)'  ]]

progver=$BASH_REMATCH

progver2=$progver
numprogverlist=0

while [[ $progver2 =~ '^([^.]+)\.(.*)' ]]; do
	progver2=${BASH_REMATCH[2]}
	progverlist[$numprogverlist]=${BASH_REMATCH[1]}
	progverlist[$(( ${numprogverlist}+1 ))]=${BASH_REMATCH[2]}

##	echo ${BASH_REMATCH[0]}
##	echo ${BASH_REMATCH[1]}
##	echo ${BASH_REMATCH[2]}
  	(( numprogverlist=$(( $numprogverlist+1 )) ))

done
	(( numprogverlist=$(( $numprogverlist+1 )) ))
  	
##	echo number of elements = $numprogverlist
##	echo element 0 = ${progverlist[0]}
##	echo element 1 = ${progverlist[1]}
##	echo element 2 = ${progverlist[2]}

argver2=$argver
numargverlist=0

while [[ $argver2 =~ '^([^.]+)\.(.*)' ]]; do
	argver2=${BASH_REMATCH[2]}
	argverlist[$numargverlist]=${BASH_REMATCH[1]}
	argverlist[$(( ${numargverlist}+1 ))]=${BASH_REMATCH[2]}

##	echo ${BASH_REMATCH[0]}
##	echo ${BASH_REMATCH[1]}
##	echo ${BASH_REMATCH[2]}
  	(( numargverlist=$(( $numargverlist+1 )) ))

done
	(( numargverlist=$(( $numargverlist+1 )) ))
  	
##	echo number of argver elements = $numargverlist
##	echo element 0 = ${argverlist[0]}
##	echo element 1 = ${argverlist[1]}
##	echo element 2 = ${argverlist[2]}

if (( $numprogverlist < $numargverlist )); then
	for (( i=$numprogverlist ; $i < $numargverlist ; i=$i + 1 )) ; do
##		echo setting progverlist "[" $i "]" to 0
		(( progverlist[$i]='0' ))
		(( numprogverlist=${numprogverlist}+1 ))
	done
elif (( $numargverlist < $numprogverlist )); then
	for (( i=$numargverlist ; $i < $numprogverlist ; i=$i + 1 )) ; do
##		echo setting argverlist "[" $i "]" to 0
		(( argverlist[$i]='0' ))
		(( numargverlist=${numargverlist}+1 ))
	done
fi

## echo numarg=$numargverlist   numprog=$numprogverlist
## echo arg0: ${argverlist[0]}
## echo arg1: ${argverlist[1]}
## echo arg2: ${argverlist[2]}
## echo prog0: ${progverlist[0]}
## echo prog1: ${progverlist[1]}
## echo prog2: ${progverlist[2]}

## the main comparison loop 

for (( i=0 ; $i < $numargverlist ; i=$i + 1 )) ; do
##	echo i= $i

	if [[ ${progverlist[$i]} =~ '^[0-9]+$' &&  ${argverlist[$i]} =~ '^[0-9]+$' ]] ; then  ## nothing but numbers
		if (( ${progverlist[$i]} != ${argverlist[$i]} )); then
			if [[ ${progverlist[$i]}  -lt ${argverlist[$i]} ]]; then
				if [[ $comparefunc == "=" ]]; then
					echo "false"
					exit 0;
				elif [[ $comparefunc == "<" || $comparefunc == "<=" ]]; then
					echo "true"
					exit 0;
				elif [[ $comparefunc == ">" || $comparefunc == ">=" ]]; then
					echo "false"
					exit 0;
				fi
			elif [[ ${progverlist[$i]} -gt ${argverlist[$i]} ]]; then
				if [[ $comparefunc == "=" ]]; then
					echo "false"
					exit 0;
				elif [[ $comparefunc == "<" || $comparefunc == "<=" ]]; then
					echo "false"
					exit 0;
				elif [[ $comparefunc == ">" || $comparefunc == ">=" ]]; then
					echo "true"
					exit 0;
				fi
			fi
		fi
	else  ## something besides just numbers
		if [[ ${progverlist[$i]} != ${argverlist[$i]} ]]; then
			if [[ ${progverlist[$i]} < ${argverlist[$i]} ]]; then
				if [[ $comparefunc == "=" ]]; then
					echo "false"
					exit 0;
				elif [[ $comparefunc == "<" || $comparefunc == "<=" ]]; then
					echo "true"
					exit 0;
				elif [[ $comparefunc == ">" || $comparefunc == ">=" ]]; then
					echo "false"
					exit 0;
				fi
			elif [[ ${progverlist[$i]} > ${argverlist[$i]} ]]; then
				if [[ $comparefunc == "=" ]]; then
					echo "false"
					exit 0;
				elif [[ $comparefunc == "<" || $comparefunc == "<=" ]]; then
					echo "false"
					exit 0;
				elif [[ $comparefunc == ">" || $comparefunc == ">=" ]]; then
					echo "true"
					exit 0;
				fi
			fi
		fi
	fi
done

if [[ $comparefunc == "=" ]]; then
	echo "true"
elif [[ $comparefunc == "<=" || $comparefunc == ">=" ]]; then
	echo "true"
else
	echo "false"
fi

exit 0;

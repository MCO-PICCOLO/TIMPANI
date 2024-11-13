#!/bin/sh


if [ $# -ne 2 ]; then
	echo "Usage: $0 input_file output_file"
	exit 1
fi

GP_INPUT_FILE=$1
GP_OUTPUT_FILE=$2

export GP_INPUT_FILE

exec gnuplot ttsched.gpi > $GP_OUTPUT_FILE
